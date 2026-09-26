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

BankFileTest: the fixed build plays the bank file named in SYSTEM.INI
[ESFM.DRV] Bank=, and loads it again when it changes (src/esfm/esfmfile.asm).
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
        self.assertEqual(ne.resource_data(256, 1234),
                         orig.resource_data(256, 1234))
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


# the bank file (esfmfile.asm)
BS_NONE, BS_LOADED, BS_MISSING, BS_BAD, BS_NOMEM, BS_CHANGING = range(6)
WATCH_OFF, WATCH_STARTING, WATCH_RUNNING, WATCH_FAILED = range(4)
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
        e = ESFMEmu(builds()[which])
        self.builtin = e.bank_res
        if setting is not None:
            e.ini[("esfm.drv", "bank")] = setting
        e.files.update(files or {})
        self.assertEqual(e.driverproc(DRV_ENABLE) & 0xFFFF, 1)
        return e

    def playing(self, emu):
        """Marks of the voices a note on of program 0 keys on."""
        emu.data(0x7F3C90)
        out = {emu.chip.regs[v * 32] for v in emu.keyed_voices() if v < 16}
        emu.data(0x003C80)
        return out

    def state(self, emu):
        return {k: v for k, v in emu.fix_state().items()
                if k.startswith("b") or k == "lock"}

    def assertClean(self, emu):
        """Lock free, no file left open, nothing freed while page-locked."""
        self.assertEqual(emu.fix_state()["lock"], 0)
        self.assertEqual(emu.handles, {})
        self.assertEqual(emu.freed_locked, [])

    def watching(self, files, **kw):
        emu = self.emu(files=files, **kw)
        emu.open()
        self.assertTrue(emu.run_task())         # starts, sets its timer
        self.assertEqual(emu.fix_state()["bwatch"], WATCH_RUNNING)
        return emu

    # -- loading ---------------------------------------------------------

    def test_enable_loads_the_file(self):
        bank = marked(self.emu(setting=None).bank_res, 0x5A)
        emu = self.emu(files={PATH: bank})
        self.assertEqual(emu.bank(len(bank)), bank)
        st = self.state(emu)
        self.assertEqual((st["bstate"], st["bsrc"], st["blen"], st["bloads"]),
                         (BS_LOADED, 1, len(bank), 1))
        self.assertEqual(st["bpath"], PATH)
        self.assertIn(("SYSTEM.INI", "ESFM.DRV", "Bank"), emu.ini_reads)
        emu.open()
        self.assertEqual(self.playing(emu), {0x5A})
        self.assertClean(emu)

    def test_riff_file(self):
        bank = marked(self.emu(setting=None).bank_res, 0x33)
        emu = self.emu(files={PATH: riff(bank)})
        self.assertEqual(emu.bank(len(bank)), bank)
        self.assertEqual(emu.fix_state()["blen"], len(bank))
        emu.open()
        self.assertEqual(self.playing(emu), {0x33})

    def test_larger_bank(self):
        # a copy of program 0 at the end, as program 1
        base = bytearray(self.emu(setting=None).bank_res)
        off = struct.unpack_from("<H", base, 0)[0]
        two = (base[off] >> 1) & 3 not in (0, 3)
        patch = base[off:off + (72 if two else 36)]
        struct.pack_into("<H", base, 0, len(base))
        bank = marked(bytes(base + patch + bytes(4096)), 0x71)
        emu = self.emu(files={PATH: bank})
        self.assertEqual(emu.fix_state()["blen"], len(bank))
        emu.open()
        self.assertEqual(self.playing(emu), {0x71})

    def test_long_file_name_call_missing(self):
        # Windows 3.1: INT 21h 716Ch fails with AX=7100h, open with 3Dh,
        # also when the carry comes back clear
        for lfn in (False, "nocarry"):
            with self.subTest(lfn):
                emu = ESFMEmu(builds()["fixed"])
                emu.lfn = lfn
                bank = marked(emu.bank_res, 0x44)
                emu.ini[("esfm.drv", "bank")] = PATH
                emu.files[PATH] = bank
                emu.driverproc(DRV_ENABLE)
                self.assertEqual(emu.bank(len(bank)), bank)
                self.assertEqual(emu.opens, [PATH])
                self.assertEqual(emu.handles, {})

    def test_no_setting(self):
        emu = self.emu(setting=None, files={PATH: b"x" * 600})
        self.assertEqual(emu.bank(len(self.builtin)), self.builtin)
        self.assertEqual(emu.fix_state()["bstate"], BS_NONE)
        self.assertEqual(emu.opens, [])

    def test_missing_file(self):
        emu = self.emu(files={})
        self.assertEqual(emu.bank(len(self.builtin)), self.builtin)
        st = self.state(emu)
        self.assertEqual((st["bstate"], st["bsrc"]), (BS_MISSING, 0))
        self.assertClean(emu)

    def test_not_a_bank(self):
        good = self.emu(setting=None).bank_res
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
                self.assertEqual(emu.fix_state()["bstate"], BS_BAD)
                self.assertEqual(emu.bank(len(self.builtin)), self.builtin)
                self.assertClean(emu)

    def test_no_memory(self):
        emu = ESFMEmu(builds()["fixed"])
        emu.ini[("esfm.drv", "bank")] = PATH
        emu.files[PATH] = marked(emu.bank_res, 0x5A)
        emu.gmem_fail = True
        emu.open()
        self.assertEqual(emu.fix_state()["bstate"], BS_NOMEM)
        self.assertEqual(emu.bank(len(emu.bank_res)), emu.bank_res)
        self.assertClean(emu)
        emu.gmem_fail = False
        emu.close()
        emu.open()                              # checks the file again
        self.assertEqual(self.playing(emu), {0x5A})

    def test_stock_driver_ignores_it(self):
        emu = ESFMEmu(builds()["stock"])
        emu.ini[("esfm.drv", "bank")] = PATH
        emu.files[PATH] = marked(emu.bank_res, 0x5A)
        self.assertEqual(emu.driverproc(DRV_ENABLE) & 0xFFFF, 1)
        emu.open()
        self.assertEqual(emu.bank(len(emu.bank_res)), emu.bank_res)
        self.assertEqual(emu.ini_reads, [])

    # -- watching the file -----------------------------------------------

    def test_changed_file_is_loaded_again(self):
        base = self.emu(setting=None).bank_res
        emu = self.watching({PATH: marked(base, 0x5A)})
        blocks = len(emu.gblocks)
        for mark in (0x61, 0x62, 0x63):
            emu.files[PATH] = marked(base, mark)
            self.assertTrue(emu.tick())
            self.assertEqual(emu.fix_state()["bstate"], BS_CHANGING)
            self.assertTrue(emu.tick())
            st = self.state(emu)
            self.assertEqual(st["bstate"], BS_LOADED)
            self.assertEqual(self.playing(emu), {mark})
        self.assertEqual(emu.fix_state()["bloads"], 4)
        # the new bank is page-locked like the one it replaced, which is
        # freed
        sel = emu.r16(4, 0x14)
        self.assertEqual(emu.plocks.get(sel), 1)
        self.assertEqual(len(emu.gblocks), blocks)
        self.assertClean(emu)

    def test_unchanged_file_is_left_alone(self):
        base = self.emu(setting=None).bank_res
        emu = self.watching({PATH: marked(base, 0x5A)})
        sel, blocks = emu.r16(4, 0x14), dict(emu.gblocks)
        for _ in range(5):
            self.assertTrue(emu.tick())
        st = self.state(emu)
        self.assertEqual((st["bstate"], st["bloads"]), (BS_LOADED, 1))
        self.assertEqual(st["bpolls"], 7)       # enable, open and 5 ticks
        self.assertEqual((emu.r16(4, 0x14), emu.gblocks), (sel, blocks))
        self.assertClean(emu)

    def test_file_being_written(self):
        # a file that differs at every check isn't loaded until it holds
        # still for one more
        base = self.emu(setting=None).bank_res
        emu = self.watching({PATH: marked(base, 0x5A)})
        for mark in range(0x70, 0x78):
            emu.files[PATH] = marked(base, mark)
            emu.tick()
            self.assertEqual(emu.fix_state()["bstate"], BS_CHANGING)
            self.assertEqual(self.playing(emu), {0x5A})
        emu.tick()
        self.assertEqual(self.playing(emu), {0x77})
        # half a bank never plays, and one that is complete again loads
        emu.files[PATH] = marked(base, 0x10)[:4000]
        emu.tick()
        self.assertEqual(emu.fix_state()["bstate"], BS_BAD)
        emu.files[PATH] = marked(base, 0x10)
        emu.tick()
        emu.tick()
        self.assertEqual(self.playing(emu), {0x10})
        self.assertClean(emu)

    def test_file_removed_then_back(self):
        base = self.emu(setting=None).bank_res
        emu = self.watching({PATH: marked(base, 0x5A)})
        del emu.files[PATH]
        emu.tick()
        self.assertEqual(emu.fix_state()["bstate"], BS_MISSING)
        self.assertEqual(self.playing(emu), {0x5A})     # keeps playing it
        emu.files[PATH] = marked(base, 0x22)
        emu.tick()
        emu.tick()
        self.assertEqual(self.playing(emu), {0x22})
        self.assertClean(emu)

    def test_setting_removed(self):
        base = self.emu(setting=None).bank_res
        emu = self.watching({PATH: marked(base, 0x5A)})
        del emu.ini[("esfm.drv", "bank")]
        emu.tick()
        st = self.state(emu)
        self.assertEqual((st["bstate"], st["bsrc"]), (BS_NONE, 0))
        self.assertEqual(emu.bank(len(base)), base)
        self.assertEqual(emu.plocks.get(emu.r16(4, 0x14)), 1)
        # another file
        emu.ini[("esfm.drv", "bank")] = r"C:\OTHER.BNK"
        emu.files[r"C:\OTHER.BNK"] = riff(marked(base, 0x3C))
        emu.tick()
        emu.tick()
        self.assertEqual(self.playing(emu), {0x3C})
        self.assertClean(emu)

    def test_note_on_during_the_swap(self):
        base = self.emu(setting=None).bank_res
        emu = self.watching({PATH: marked(base, 0x5A)})
        emu.files[PATH] = marked(base, 0x61)
        emu.tick()
        inj = Inject(MODM_DATA, dw1=0x7F4090, on_lock=True)
        emu.tick(injects=[inj])
        self.assertTrue(inj.done)
        self.assertEqual(inj.result, 0)
        st = emu.fix_state()
        self.assertEqual((st["queued"], st["bloads"]), (1, 2))
        # it waited for the swap, and played the new bank
        self.assertEqual({emu.chip.regs[v * 32] for v in emu.keyed_voices()
                          if v < 16}, {0x61})
        self.assertEqual(emu.chip.splits, [])
        self.assertClean(emu)

    def test_notes_while_checking(self):
        base = self.emu(setting=None).bank_res
        emu = self.watching({PATH: marked(base, 0x5A)})
        for k in range(0, 60000, 4000):
            emu.files[PATH] = marked(base, 0x40 + k // 4000)
            emu.tick()
            inj = Inject(MODM_DATA, dw1=0x7F3C90, after_insns=k)
            emu.tick(injects=[inj])
            if inj.started:
                self.assertEqual(inj.result, 0)
            emu.data(0x003C80)
            self.assertEqual(emu.keyed_voices(), [])
            self.assertEqual(emu.chip.splits, [])
            self.assertEqual(self.playing(emu), {0x40 + k // 4000})
        self.assertClean(emu)

    def test_checks_only_while_open(self):
        base = self.emu(setting=None).bank_res
        emu = self.watching({PATH: marked(base, 0x5A)})
        emu.close()
        polls = emu.fix_state()["bpolls"]
        emu.files[PATH] = marked(base, 0x66)
        for _ in range(3):
            emu.tick()
        self.assertEqual(emu.fix_state()["bpolls"], polls)
        emu.open()                              # loads it now
        self.assertEqual(self.playing(emu), {0x66})
        self.assertEqual(emu.tasks_created, 1)  # the same task goes on
        self.assertClean(emu)

    def test_no_freed_selector_in_a_register(self):
        # every way a bank block is freed, with a check at each instruction
        # that DS, ES and SS never hold a freed block
        base = self.emu(setting=None).bank_res
        emu = ESFMEmu(builds()["fixed"])
        emu.check_selectors = True
        emu.ini[("esfm.drv", "bank")] = PATH
        emu.files[PATH] = marked(base, 0x5A)
        emu.driverproc(DRV_ENABLE)              # frees bank_load's block
        emu.open()
        emu.run_task()
        emu.files[PATH] = marked(base, 0x61)
        emu.tick()
        emu.tick()                              # frees the old file bank
        emu.tick()                              # frees the unchanged copy
        emu.files[PATH] = b"x" * 600
        emu.tick()                              # frees a bad file
        del emu.ini[("esfm.drv", "bank")]
        emu.tick()                              # the built-in bank back
        emu.close()
        emu.driverproc(DRV_DISABLE)
        self.assertEqual(emu.fix_state()["bloads"], 2)
        self.assertEqual(emu.stale, [])
        self.assertClean(emu)

    # -- the task --------------------------------------------------------

    def test_disable_ends_the_task(self):
        base = self.emu(setting=None).bank_res
        emu = self.watching({PATH: marked(base, 0x5A)})
        emu.close()
        self.assertEqual(emu.driverproc(DRV_DISABLE) & 0xFFFF, 1)
        self.assertEqual(emu.task["state"], "done")
        self.assertEqual(emu.timers, {})
        st = emu.fix_state()
        self.assertEqual(st["bwatch"], WATCH_OFF)
        self.assertEqual(emu.r16(4, 0x14), 0)   # bank_free
        self.assertClean(emu)
        # enabled and opened again: a new task
        self.assertEqual(emu.driverproc(DRV_ENABLE) & 0xFFFF, 1)
        emu.open()
        self.assertEqual(emu.tasks_created, 2)
        self.assertTrue(emu.run_task())
        self.assertEqual(emu.fix_state()["bwatch"], WATCH_RUNNING)

    def test_disable_before_the_task_ran(self):
        emu = self.emu(files={})
        emu.open()
        emu.close()
        self.assertEqual(emu.task["state"], "new")
        emu.driverproc(DRV_DISABLE)
        self.assertEqual(emu.task["state"], "done")     # ran from the Yield
        self.assertEqual(emu.timers, {})
        self.assertNotIn(("MMSYSTEM", 602), emu.calls)
        self.assertEqual(emu.fix_state()["bwatch"], WATCH_OFF)

    def test_without_mmtask(self):
        base = self.emu(setting=None).bank_res
        emu = self.emu(files={PATH: marked(base, 0x5A)})
        emu.mmtask_error = 2
        emu.open()
        st = emu.fix_state()
        self.assertEqual((st["bwatch"], st["bwerr"]), (WATCH_FAILED, 2))
        # the file is still checked at every open
        emu.close()
        emu.files[PATH] = marked(base, 0x66)
        emu.open()
        self.assertEqual(self.playing(emu), {0x66})
        emu.close()
        emu.driverproc(DRV_DISABLE)
        self.assertClean(emu)

    def test_without_timer(self):
        emu = self.emu(files={})
        emu.timer_fail = True
        emu.open()
        emu.run_task()
        st = emu.fix_state()
        self.assertEqual((st["bwatch"], st["bwerr"]), (WATCH_FAILED, 0xFFFF))
        self.assertEqual(emu.task["state"], "done")


if __name__ == "__main__":
    unittest.main()
