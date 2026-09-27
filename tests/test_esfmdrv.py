# (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
#
# Licensed under GPL Version 3.0
"""Test ESFM.DRV rebuilt from src/esfm.

RebuildTest: the stock build must match driver/ESFM.DRV byte for byte, and
the fixed build must be a valid driver with the same bank loader, so
esfmpat and essctl keep working with it.

StuckNoteTest runs both builds in a CPU emulator (tests/esfmemu.py) and
reproduces the stuck notes of the ESS driver:
- messages that arrive while the driver is busy are refused, and the lost
  note offs leave notes sounding
- closing with the sustain pedal down leaves notes on
- a message from an interrupt during close splits an FM address/data pair

The fixed build must pass every case the stock build fails.

SustainTest: a sustain pedal a song leaves down holds notes forever on FM.
The fixed build lets it up at a program change and at a GM, GS or XG reset.

BankFileTest: the fixed build plays the bank file named in SYSTEM.INI
[ESFM.DRV] Bank=, read when a program opens the device if its date or time
changed (src/esfm/esfmfile.asm).
"""

import os
import struct
import sys
import tempfile
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, os.path.join(ROOT, "tests"))

import build_esfm  # noqa: E402
from retools.ne import NEFile  # noqa: E402

try:
    import esfmemu
    from esfmemu import (ESFMEmu, Inject, MODM_DATA, MODM_LONGDATA,
                         MODM_RESET, MOM_DONE, MIDIERR_NOTREADY, DRV_POWER,
                         PWR_SUSPENDREQUEST, PWR_SUSPENDRESUME, DRV_ENABLE,
                         DRV_DISABLE)
    HAVE_UNICORN = True
except ImportError:
    HAVE_UNICORN = False

NOTE_A_ON, NOTE_A_OFF = 0x7F3C90, 0x003C80     # middle C, channel 1
NOTE_B_ON, NOTE_B_OFF = 0x7F4090, 0x004090     # E, channel 1 (velocity 0)
SUSTAIN_ON = 0x7F40B0
_builds = {}


def builds():
    if not _builds:
        tmp = tempfile.mkdtemp(prefix="esfmdrv")
        _builds["stock"] = build_esfm.build(False, workdir=tmp)
        _builds["fixed"] = build_esfm.build(True, workdir=tmp)
    return _builds


class RebuildTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        b = builds()
        cls.stock, cls.fixed = b["stock"], b["fixed"]
        with open(build_esfm.ORIGINAL, "rb") as f:
            cls.orig = f.read()

    def test_stock_identical(self):
        self.assertEqual(self.stock, self.orig)

    def test_build_is_current(self):
        with open(os.path.join(ROOT, "build", "ESFM.DRV"), "rb") as f:
            self.assertEqual(f.read(), self.fixed,
                             "build/ESFM.DRV is out of date: run "
                             "tools/build_esfm.py")

    def test_fixed_driver(self):
        ne, orig = NEFile(self.fixed), NEFile(self.orig)
        self.assertEqual(ne.module_name, "ESFM")
        self.assertEqual(ne.entries.keys(), orig.entries.keys())
        self.assertEqual(ne.modules, orig.modules)
        # the square-wave bank of esfm_patch_banks is built in
        with open(build_esfm.FIX_BANK, "rb") as f:
            self.assertEqual(ne.resource_data(256, 1234), f.read())
        # segment 3 (open, close and the bank loader) keeps its layout
        self.assertEqual(ne.segments[2].length, orig.segments[2].length)
        # DGROUP keeps its variables where they were
        self.assertEqual(ne.segment_data(4)[0x3E:0x5B],
                         orig.segment_data(4)[0x3E:0x5B])
        self.assertIn(b"ESFMFIX\0", ne.segment_data(4))


@unittest.skipUnless(HAVE_UNICORN, "needs the unicorn module")
class StuckNoteTest(unittest.TestCase):
    def emu(self, which):
        e = ESFMEmu(builds()[which])
        e.open()
        return e

    def assertSilent(self, emu):
        self.assertEqual(emu.keyed_voices(), [])
        self.assertEqual(emu.driver_active(), [])

    def assertUnlocked(self, emu):
        st = emu.fix_state()
        self.assertEqual(st["lock"], 0)
        self.assertEqual(st["head"], st["tail"])
        self.assertEqual(emu.busy(), 0)

    # -- the bugs, and the fix -----------------------------------------------

    def note_off_during_note_on(self, which):
        emu = self.emu(which)
        emu.data(NOTE_A_ON)
        inj = Inject(MODM_DATA, dw1=NOTE_A_OFF,
                     after_writes=emu.chip.writes + 10)
        emu.data(NOTE_B_ON, injects=[inj])
        emu.data(NOTE_B_OFF)
        return emu, inj

    def test_note_off_during_note_on(self):
        emu, inj = self.note_off_during_note_on("stock")
        self.assertEqual(inj.result, MIDIERR_NOTREADY)
        self.assertTrue(emu.keyed_voices())             # note A hangs
        emu, inj = self.note_off_during_note_on("fixed")
        self.assertEqual(inj.result, 0)
        self.assertSilent(emu)
        self.assertUnlocked(emu)
        self.assertEqual(emu.fix_state()["queued"], 1)

    def note_off_from_callback(self, which):
        emu = self.emu(which)
        emu.data(NOTE_A_ON)
        inj = Inject(MODM_DATA, dw1=NOTE_A_OFF, in_callback=MOM_DONE)
        r, flags = emu.longdata(bytes([0x90, 0x40, 0x7F]), injects=[inj])
        self.assertEqual((r, flags & 1), (0, 1))
        emu.data(NOTE_B_OFF)
        return emu, inj

    def test_note_off_from_mom_done(self):
        emu, inj = self.note_off_from_callback("stock")
        self.assertEqual(inj.result, MIDIERR_NOTREADY)
        self.assertTrue(emu.keyed_voices())
        emu, inj = self.note_off_from_callback("fixed")
        self.assertEqual(inj.result, 0)
        self.assertSilent(emu)
        self.assertUnlocked(emu)

    def note_on_during_close(self, which):
        emu = self.emu(which)
        emu.data(NOTE_A_ON)
        inj = Inject(MODM_DATA, dw1=0x7F4591, after_writes=emu.chip.writes + 1)
        self.assertEqual(emu.close(injects=[inj]), 0)
        return emu, inj

    def test_note_on_during_close(self):
        emu, _inj = self.note_on_during_close("stock")
        self.assertTrue(emu.chip.splits)                # register mixed up
        self.assertTrue(emu.keyed_voices())             # sounding after close
        emu, inj = self.note_on_during_close("fixed")
        self.assertEqual(inj.result, 0)
        self.assertEqual(emu.chip.splits, [])
        self.assertEqual(emu.keyed_voices(), [])
        self.assertEqual(emu.fix_state()["purged"], 1)  # dropped: closed
        self.assertUnlocked(emu)

    def close_with_pedal(self, which):
        emu = self.emu(which)
        emu.data(SUSTAIN_ON)
        emu.data(NOTE_A_ON)
        emu.data(NOTE_A_OFF)
        self.assertTrue(emu.keyed_voices())             # held by the pedal
        emu.close()
        return emu

    def test_close_with_sustain_pedal(self):
        self.assertTrue(self.close_with_pedal("stock").keyed_voices())
        self.assertEqual(self.close_with_pedal("fixed").keyed_voices(), [])

    def suspend_with_pedal(self, which):
        emu = self.emu(which)
        emu.data(SUSTAIN_ON)
        emu.data(NOTE_A_ON)
        emu.data(NOTE_A_OFF)
        emu.driverproc(DRV_POWER, PWR_SUSPENDREQUEST)
        return emu

    def test_power_suspend_with_sustain_pedal(self):
        self.assertTrue(self.suspend_with_pedal("stock").keyed_voices())
        emu = self.suspend_with_pedal("fixed")
        self.assertEqual(emu.keyed_voices(), [])
        inj = Inject(MODM_DATA, dw1=0x7F4591,
                     after_writes=emu.chip.writes + 301)
        self.assertEqual(emu.driverproc(DRV_POWER, PWR_SUSPENDRESUME,
                                        injects=[inj]), 1)
        self.assertEqual(emu.chip.splits, [])
        self.assertUnlocked(emu)

    def sweep(self, which, step):
        """Send a note off from an interrupt at every step-th instruction
        of a note on, and count the trials that leave a note sounding."""
        emu = self.emu(which)
        stuck = 0
        for k in range(0, 10300, step):
            emu.data(NOTE_A_ON)
            inj = Inject(MODM_DATA, dw1=NOTE_A_OFF, after_insns=k)
            emu.data(NOTE_B_ON, injects=[inj])
            if not inj.started:
                emu.data(NOTE_A_OFF)
            emu.data(NOTE_B_OFF)
            if emu.keyed_voices() or emu.driver_active():
                stuck += 1
                emu.reset()
        return emu, stuck, len(range(0, 10300, step))

    def test_interrupt_sweep(self):
        _emu, stuck, n = self.sweep("stock", 97)
        self.assertGreater(stuck, n * 3 // 4)
        emu, stuck, n = self.sweep("fixed", 53)
        self.assertEqual(stuck, 0)
        self.assertEqual(emu.chip.splits, [])
        self.assertUnlocked(emu)

    # -- the queue of the fixed driver ---------------------------------------

    def test_long_message_queued(self):
        emu = self.emu("fixed")
        hdr = emu.header(bytes([0x90, 0x43, 0x7F]))
        inj = Inject(MODM_LONGDATA, dw1=hdr, dw2=0x20,
                     after_writes=emu.chip.writes + 10)
        emu.data(NOTE_A_ON, injects=[inj])
        self.assertEqual(inj.result, 0)
        self.assertEqual(emu.header_flags(hdr) & 0x11, 0x01)  # done, dequeued
        self.assertIn(MOM_DONE, [c[0] for c in emu.callbacks])
        self.assertIn(0x43, [n for f, _c, n in emu.voices() if f & 1])
        emu.data(0x004390)
        emu.data(NOTE_A_OFF)
        self.assertSilent(emu)
        self.assertUnlocked(emu)

    def test_reset_queued(self):
        emu = self.emu("fixed")
        inj = Inject(MODM_RESET, after_writes=emu.chip.writes + 10)
        emu.data(NOTE_A_ON, injects=[inj])
        self.assertEqual(inj.result, 0)
        self.assertSilent(emu)                  # the reset came after
        self.assertUnlocked(emu)

    def test_several_in_order(self):
        emu = self.emu("fixed")
        emu.data(NOTE_A_ON)
        start = emu.chip.writes
        injs = [Inject(MODM_DATA, dw1=NOTE_A_OFF, after_writes=start + 5),
                Inject(MODM_DATA, dw1=0x7F4591, after_writes=start + 50),
                Inject(MODM_DATA, dw1=0x004591, after_writes=start + 90)]
        emu.data(NOTE_B_ON, injects=injs)
        self.assertEqual([i.result for i in injs], [0, 0, 0])
        emu.data(NOTE_B_OFF)
        self.assertSilent(emu)
        self.assertEqual(emu.fix_state()["maxdepth"], 3)
        self.assertUnlocked(emu)

    def test_queue_full(self):
        emu = self.emu("fixed")
        start = emu.chip.writes
        qsize = emu.fix_state()["qsize"]
        injs = [Inject(MODM_DATA, dw1=0x0064B1 | (i & 0x3F) << 16,
                       after_writes=start + 1 + 3 * i)
                for i in range(qsize + 5)]
        emu.data(NOTE_A_ON, injects=injs)
        results = [i.result for i in injs]
        self.assertEqual(results.count(0), qsize - 1)
        self.assertEqual(results.count(MIDIERR_NOTREADY), 6)
        st = emu.fix_state()
        self.assertEqual((st["overflow"], st["maxdepth"]), (6, qsize - 1))
        emu.data(NOTE_A_OFF)
        self.assertSilent(emu)
        self.assertUnlocked(emu)


# the sustain pedal (esfmped.asm)
PED_CC64, PED_PROGRAM, PED_SYSEX, PED_CC121 = 1, 2, 3, 4
GM_ON = bytes([0xF0, 0x7E, 0x7F, 0x09, 0x01, 0xF7])
GM2_ON = bytes([0xF0, 0x7E, 0x7F, 0x09, 0x03, 0xF7])
GS_RESET = bytes([0xF0, 0x41, 0x10, 0x42, 0x12, 0x40, 0x00, 0x7F, 0x00,
                  0x41, 0xF7])
XG_ON = bytes([0xF0, 0x43, 0x10, 0x4C, 0x00, 0x00, 0x7E, 0x00, 0xF7])


@unittest.skipUnless(HAVE_UNICORN, "needs the unicorn module")
class SustainTest(unittest.TestCase):
    def emu(self, which):
        e = ESFMEmu(builds()[which])
        e.open()
        return e

    def hold(self, emu, channel, notes=(0x3C, 0x40, 0x43)):
        """Pedal down on channel, a chord played and let go."""
        emu.data(0x7F40B0 | channel)
        for n in notes:
            emu.data(0x7F0090 | channel | n << 8)
            emu.data(0x000080 | channel | n << 8)
        self.assertIn(channel, emu.pedals())
        self.assertTrue(emu.keyed_voices())

    # -- a pedal left down ---------------------------------------------------

    def test_program_change_lets_the_pedal_up(self):
        emu = self.emu("stock")
        self.hold(emu, 5)
        emu.data(0x0030C5)                      # program 48 on channel 6
        self.assertEqual(emu.pedals(), [5])     # ESS: still down, still held
        self.assertTrue(emu.keyed_voices())
        emu = self.emu("fixed")
        self.hold(emu, 5)
        clock = emu.clock()
        emu.data(0x0030C5)
        self.assertEqual(emu.pedals(), [])
        self.assertEqual(emu.keyed_voices(), [])
        st = emu.fix_state()
        self.assertEqual((st["ped_why"][5], st["ped_up"][5], st["ped_prog"][5]),
                         (PED_PROGRAM, clock, clock))
        # the new program plays and lets go as usual
        emu.data(0x7F3C95)
        emu.data(0x003C85)
        self.assertEqual(emu.keyed_voices(), [])

    def test_program_change_keeps_notes_that_are_held_down(self):
        emu = self.emu("fixed")
        emu.data(0x7F40B5)                      # pedal down
        emu.data(0x7F3C95)                      # a key held down
        emu.data(0x0030C5)                      # pedal let up
        self.assertEqual([emu.voices()[v][2] for v in emu.keyed_voices()
                          if v < 16][:1], [0x3C])
        emu.data(0x003C85)                      # the key let go: released
        self.assertEqual(emu.keyed_voices(), [])
        self.assertEqual(emu.driver_active(), [])

    def test_program_change_without_the_pedal(self):
        emu = self.emu("fixed")
        emu.data(0x7F3C95)
        emu.data(0x0030C5)
        self.assertTrue(emu.keyed_voices())     # plays on
        emu.data(0x003C85)
        self.assertEqual(emu.keyed_voices(), [])
        self.assertEqual(emu.fix_state()["ped_why"][5], 0)

    def test_reset_sysex_lets_every_pedal_up(self):
        for name, sysex in (("GM", GM_ON), ("GM2", GM2_ON), ("GS", GS_RESET),
                            ("XG", XG_ON)):
            with self.subTest(name):
                emu = self.emu("stock")
                self.hold(emu, 0)
                self.hold(emu, 5)
                self.assertEqual(emu.longdata(sysex)[0], 0)
                self.assertEqual(emu.pedals(), [0, 5])  # ESS skips SysEx
                emu = self.emu("fixed")
                self.hold(emu, 0)
                self.hold(emu, 5, notes=(0x30, 0x34))
                emu.data(0x7F3C92)              # a key held down, channel 3
                r, flags = emu.longdata(sysex)
                self.assertEqual((r, flags & 1), (0, 1))
                self.assertEqual(emu.pedals(), [])
                self.assertEqual(emu.keyed_voices(), [])        # notes off
                st = emu.fix_state()
                self.assertEqual((st["ped_why"][0], st["ped_why"][5]),
                                 (PED_SYSEX, PED_SYSEX))
                self.assertEqual(st["lock"], 0)

    def test_other_sysex_changes_nothing(self):
        for sysex in (bytes([0xF0, 0x7E, 0x7F, 0x09, 0x02, 0xF7]),  # GM off
                      bytes([0xF0, 0x41, 0x10, 0x42, 0x12, 0x40, 0x01, 0x30,
                             0x00, 0x0F, 0xF7]),                    # GS reverb
                      bytes([0xF0, 0x43, 0x10, 0x4C, 0x02, 0x01, 0x00, 0x01,
                             0xF7]),                                # XG effect
                      GM_ON[:5]):                                   # cut short
            with self.subTest(sysex.hex()):
                emu = self.emu("fixed")
                self.hold(emu, 5)
                emu.longdata(sysex)
                self.assertEqual(emu.pedals(), [5])
                self.assertTrue(emu.keyed_voices())

    def test_reset_sysex_while_busy(self):
        emu = self.emu("fixed")
        self.hold(emu, 5)
        hdr = emu.header(GM_ON)
        inj = Inject(MODM_LONGDATA, dw1=hdr, dw2=0x20,
                     after_writes=emu.chip.writes + 10)
        emu.data(0x7F3C90, injects=[inj])      # a note on, channel 1
        self.assertEqual(inj.result, 0)         # queued, then handled
        self.assertEqual(emu.fix_state()["queued"], 1)
        self.assertEqual(emu.pedals(), [])
        self.assertEqual(emu.keyed_voices(), [])
        self.assertEqual(emu.fix_state()["lock"], 0)

    # -- the times kept for essctl -------------------------------------------

    def test_pedal_times(self):
        emu = self.emu("fixed")
        st = emu.fix_state()
        self.assertEqual(st["ped_down"], [0xFFFF] * 16)
        emu.data(0x7F3C90)
        down = emu.clock()
        emu.data(0x7F40B2)                      # pedal down, channel 3
        emu.data(0x7F3C92)
        emu.data(0x003C92)
        up = emu.clock()
        emu.data(0x0040B2)                      # pedal up
        st = emu.fix_state()
        self.assertEqual((st["ped_down"][2], st["ped_up"][2], st["ped_why"][2]),
                         (down, up, PED_CC64))
        # the pedal let channel 3's note go, channel 1's still sounds
        self.assertEqual({emu.voices()[v][1] for v in emu.keyed_voices()
                          if v < 16}, {0})
        emu.data(0x7F40B2)
        emu.data(0x0079B2)                      # reset all controllers
        st = emu.fix_state()
        self.assertEqual(st["ped_why"][2], PED_CC121)
        self.assertEqual(emu.pedals(), [])
        emu.reset()                             # chip_reset forgets them
        st = emu.fix_state()
        self.assertEqual((st["ped_down"], st["ped_why"]),
                         ([0xFFFF] * 16, [0] * 16))


# the bank file (esfmfile.asm)
BS_NONE, BS_LOADED, BS_MISSING, BS_BAD, BS_NOMEM = range(5)
PATH = r"C:\BANKS\MINE.BIN"


def marked(bank, value):
    """The bank with byte 0 of operator 0 of program 0 set to value, in
    both voices of the patch: the driver writes that byte to the chip as
    it is."""
    b = bytearray(bank)
    off = struct.unpack_from("<H", b, 0)[0]
    b[off + 4] = value
    if (b[off] >> 1) & 3 not in (0, 3):
        b[off + 36 + 4] = value
    return bytes(b)


def riff(bank):
    body = b"Ptch" + b"LIST" + struct.pack("<I", 3) + b"abc\0" + \
        b"fm4 " + struct.pack("<I", len(bank)) + bank
    return b"RIFF" + struct.pack("<I", len(body)) + body


@unittest.skipUnless(HAVE_UNICORN, "needs the unicorn module")
class BankFileTest(unittest.TestCase):
    def emu(self, which="fixed", files=None, setting=PATH):
        """The driver after DRV_ENABLE, with Bank= set to setting."""
        e = ESFMEmu(builds()[which])
        self.builtin = e.bank_res
        if setting is not None:
            e.ini[("esfm.drv", "bank")] = setting
        e.files.update(files or {})
        self.assertEqual(e.driverproc(DRV_ENABLE) & 0xFFFF, 1)
        return e

    def base(self):
        return ESFMEmu(builds()["fixed"]).bank_res

    def playing(self, emu):
        """Marks of the voices a note on of program 0 keys on."""
        emu.data(0x7F3C90)
        out = {emu.chip.regs[v * 32] for v in emu.keyed_voices() if v < 16}
        emu.data(0x003C80)
        return out

    def reopen(self, emu):
        emu.close()
        emu.open()

    def state(self, emu):
        return {k: v for k, v in emu.fix_state().items()
                if k.startswith("b") or k == "lock"}

    def assertClean(self, emu):
        """Lock free, no file left open, nothing freed while page-locked."""
        self.assertEqual(emu.fix_state()["lock"], 0)
        self.assertEqual(emu.handles, {})
        self.assertEqual(emu.freed_locked, [])

    # -- loading at open -----------------------------------------------------

    def test_enable_reads_no_file(self):
        emu = self.emu(files={PATH: marked(self.base(), 0x5A)})
        self.assertEqual(emu.opens, [])
        self.assertEqual(emu.ini_reads, [])
        self.assertEqual(emu.bank(len(self.builtin)), self.builtin)
        self.assertEqual(emu.fix_state()["bstate"], BS_NONE)

    def test_open_loads_the_file(self):
        bank = marked(self.base(), 0x5A)
        emu = self.emu(files={PATH: bank})
        emu.open()
        self.assertEqual(emu.bank(len(bank)), bank)
        st = self.state(emu)
        self.assertEqual((st["bstate"], st["bsrc"], st["blen"], st["bloads"],
                          st["bchecks"]), (BS_LOADED, 1, len(bank), 1, 1))
        self.assertEqual((st["bdate"], st["btime"]),
                         (esfmemu.FILE_DATE, esfmemu.FILE_TIME))
        self.assertEqual(st["bpath"], PATH)
        self.assertIn(("SYSTEM.INI", "ESFM.DRV", "Bank"), emu.ini_reads)
        self.assertEqual(self.playing(emu), {0x5A})
        # the device was closed: nothing was page-locked or queued
        self.assertEqual(emu.fix_state()["queued"], 0)
        self.assertEqual(emu.plocks.get(emu.r16(4, 0x14)), 1)   # bank_lock
        self.assertClean(emu)

    def test_riff_file(self):
        bank = marked(self.base(), 0x33)
        emu = self.emu(files={PATH: riff(bank)})
        emu.open()
        self.assertEqual(emu.bank(len(bank)), bank)
        self.assertEqual(emu.fix_state()["blen"], len(bank))
        self.assertEqual(self.playing(emu), {0x33})

    def test_larger_bank(self):
        # a copy of program 0 at the end, as program 1
        base = bytearray(self.base())
        off = struct.unpack_from("<H", base, 0)[0]
        two = (base[off] >> 1) & 3 not in (0, 3)
        patch = base[off:off + (72 if two else 36)]
        struct.pack_into("<H", base, 0, len(base))
        bank = marked(bytes(base + patch + bytes(4096)), 0x71)
        emu = self.emu(files={PATH: bank})
        emu.open()
        self.assertEqual(emu.fix_state()["blen"], len(bank))
        self.assertEqual(self.playing(emu), {0x71})

    def test_long_file_name_call_missing(self):
        # Windows 3.1: INT 21h 716Ch fails with AX=7100h, open with 3Dh,
        # also when the carry comes back clear
        for lfn in (False, "nocarry"):
            with self.subTest(lfn):
                bank = marked(self.base(), 0x44)
                emu = self.emu(files={PATH: bank})
                emu.lfn = lfn
                emu.open()
                self.assertEqual(emu.bank(len(bank)), bank)
                self.assertEqual(emu.opens, [PATH])
                self.assertClean(emu)

    def test_no_setting(self):
        emu = self.emu(setting=None, files={PATH: b"x" * 600})
        emu.open()
        self.assertEqual(emu.bank(len(self.builtin)), self.builtin)
        self.assertEqual(emu.fix_state()["bstate"], BS_NONE)
        self.assertEqual(emu.opens, [])

    def test_missing_file(self):
        emu = self.emu(files={})
        emu.open()
        self.assertEqual(emu.bank(len(self.builtin)), self.builtin)
        st = self.state(emu)
        self.assertEqual((st["bstate"], st["bsrc"]), (BS_MISSING, 0))
        self.assertClean(emu)

    def test_not_a_bank(self):
        good = self.base()
        off = struct.unpack_from("<H", good, 0)[0]
        cases = {
            "short": good[:300],
            "outside": b"\x00\x01" * 256 + bytes(600),
            "past the end": good[:off + 10],
            "no patches": bytes(1024),
            "riff without fm4": b"RIFF" + struct.pack("<I", 20) + b"Ptch" +
            b"data" + struct.pack("<I", 8) + bytes(8),
            "riff chunk too long": b"RIFF" + struct.pack("<I", 20) +
            b"Ptch" + b"fm4 " + struct.pack("<I", 9000) + good[:600],
            "too big": bytes(0x7FF2),
        }
        for name, data in cases.items():
            with self.subTest(name):
                emu = self.emu(files={PATH: data})
                emu.open()
                self.assertEqual(emu.fix_state()["bstate"], BS_BAD)
                self.assertEqual(emu.bank(len(self.builtin)), self.builtin)
                self.assertClean(emu)

    def test_no_memory(self):
        emu = self.emu(files={PATH: marked(self.base(), 0x5A)})
        emu.gmem_fail = True
        emu.open()
        self.assertEqual(emu.fix_state()["bstate"], BS_NOMEM)
        self.assertEqual(emu.bank(len(self.builtin)), self.builtin)
        self.assertClean(emu)
        emu.gmem_fail = False
        self.reopen(emu)                        # tried again
        self.assertEqual(self.playing(emu), {0x5A})

    def test_stock_driver_ignores_it(self):
        emu = self.emu("stock", files={PATH: marked(self.base(), 0x5A)})
        emu.open()
        self.assertEqual(emu.bank(len(self.builtin)), self.builtin)
        self.assertEqual(emu.ini_reads, [])

    # -- the date and time decide --------------------------------------------

    def test_same_date_is_not_read_again(self):
        base = self.base()
        emu = self.emu(files={PATH: marked(base, 0x5A)})
        emu.open()
        reads = emu.reads
        # new bytes with the date and time of the file read before, like a
        # copy of a file with the same date: not read
        emu.files[PATH] = marked(base, 0x61)
        for _ in range(3):
            self.reopen(emu)
        self.assertEqual(emu.reads, reads)
        st = self.state(emu)
        self.assertEqual((st["bstate"], st["bloads"], st["bchecks"]),
                         (BS_LOADED, 1, 4))
        self.assertEqual(self.playing(emu), {0x5A})
        self.assertClean(emu)

    def test_new_date_is_loaded(self):
        base = self.base()
        emu = self.emu(files={PATH: marked(base, 0x5A)})
        emu.open()
        for mark in (0x61, 0x62, 0x63):
            emu.files[PATH] = marked(base, mark)
            emu.touch(PATH)
            self.reopen(emu)
            self.assertEqual(self.playing(emu), {mark})
            self.assertEqual(emu.fix_state()["btime"],
                             emu.ftimes[PATH][1])
        self.assertEqual(emu.fix_state()["bloads"], 4)
        self.assertClean(emu)

    def test_not_checked_while_open(self):
        base = self.base()
        emu = self.emu(files={PATH: marked(base, 0x5A)})
        emu.open()
        emu.files[PATH] = marked(base, 0x66)
        emu.touch(PATH)
        # another program's open is refused and the bank that plays stays
        user, desc = (esfmemu.STUB_PARA << 16) | (esfmemu.CLIENT_OFF + 0x40), \
            (esfmemu.STUB_PARA << 16) | esfmemu.CLIENT_OFF
        self.assertEqual(emu.modmessage(esfmemu.MODM_OPEN, desc, 0x00030000,
                                        user=user), 4)
        self.assertEqual(emu.fix_state()["bchecks"], 1)
        self.assertEqual(self.playing(emu), {0x5A})
        self.reopen(emu)                        # the next open loads it
        self.assertEqual(self.playing(emu), {0x66})
        self.assertClean(emu)

    def test_bad_file_is_not_read_again(self):
        base = self.base()
        emu = self.emu(files={PATH: marked(base, 0x5A)})
        emu.open()
        emu.files[PATH] = marked(base, 0x10)[:4000]      # half saved
        emu.touch(PATH)
        self.reopen(emu)
        self.assertEqual(emu.fix_state()["bstate"], BS_BAD)
        self.assertEqual(self.playing(emu), {0x5A})     # the bank stays
        reads = emu.reads
        self.reopen(emu)
        self.assertEqual(emu.reads, reads)
        self.assertEqual(emu.fix_state()["bstate"], BS_BAD)
        emu.files[PATH] = marked(base, 0x10)            # saved again
        emu.touch(PATH)
        self.reopen(emu)
        self.assertEqual(self.playing(emu), {0x10})
        self.assertEqual(emu.fix_state()["bstate"], BS_LOADED)
        self.assertClean(emu)

    def test_file_removed_then_back(self):
        base = self.base()
        emu = self.emu(files={PATH: marked(base, 0x5A)})
        emu.open()
        data = emu.files.pop(PATH)
        self.reopen(emu)
        self.assertEqual(emu.fix_state()["bstate"], BS_MISSING)
        self.assertEqual(self.playing(emu), {0x5A})     # keeps playing it
        # back as it was: nothing to read
        emu.files[PATH] = data
        reads = emu.reads
        self.reopen(emu)
        self.assertEqual(emu.reads, reads)
        self.assertEqual(emu.fix_state()["bstate"], BS_LOADED)
        # back with a new date: loaded
        emu.files[PATH] = marked(base, 0x22)
        emu.touch(PATH)
        self.reopen(emu)
        self.assertEqual(self.playing(emu), {0x22})
        self.assertClean(emu)

    def test_setting_removed(self):
        base = self.base()
        emu = self.emu(files={PATH: marked(base, 0x5A)})
        emu.open()
        del emu.ini[("esfm.drv", "bank")]
        self.reopen(emu)
        st = self.state(emu)
        self.assertEqual((st["bstate"], st["bsrc"]), (BS_NONE, 0))
        self.assertEqual(emu.bank(len(base)), base)
        # another file, with the same date and time: a new name loads
        emu.ini[("esfm.drv", "bank")] = r"C:\OTHER.BNK"
        emu.files[r"C:\OTHER.BNK"] = riff(marked(base, 0x3C))
        self.reopen(emu)
        self.assertEqual(self.playing(emu), {0x3C})
        self.assertClean(emu)

    def test_enabled_again_reads_it_again(self):
        base = self.base()
        emu = self.emu(files={PATH: marked(base, 0x5A)})
        emu.open()
        emu.close()
        emu.driverproc(DRV_DISABLE)
        self.assertEqual(emu.driverproc(DRV_ENABLE) & 0xFFFF, 1)
        self.assertEqual(emu.bank(len(base)), base)     # bank_load's
        emu.open()                              # same date, read again
        self.assertEqual(self.playing(emu), {0x5A})
        self.assertEqual(emu.fix_state()["bloads"], 2)
        self.assertClean(emu)

    def test_no_freed_selector_in_a_register(self):
        # every way a bank block is freed, with a check at each instruction
        # that DS, ES and SS never hold a freed block
        base = self.base()
        emu = ESFMEmu(builds()["fixed"])
        emu.check_selectors = True
        emu.ini[("esfm.drv", "bank")] = PATH
        emu.files[PATH] = marked(base, 0x5A)
        emu.driverproc(DRV_ENABLE)
        emu.open()                              # frees bank_load's block
        emu.files[PATH] = marked(base, 0x61)
        emu.touch(PATH)
        self.reopen(emu)                        # frees the old file bank
        emu.files[PATH] = b"x" * 600
        emu.touch(PATH)
        self.reopen(emu)                        # frees a bad file
        del emu.ini[("esfm.drv", "bank")]
        self.reopen(emu)                        # the built-in bank back
        emu.close()
        emu.driverproc(DRV_DISABLE)
        self.assertEqual(emu.fix_state()["bloads"], 2)
        self.assertEqual(emu.stale, [])
        self.assertClean(emu)


if __name__ == "__main__":
    unittest.main()
