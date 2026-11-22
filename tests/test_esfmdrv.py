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

GMTest: what General MIDI asks for that ESS's code doesn't do, in the fixed
build (src/esfm/esfmgm.asm).

BankFileTest: the fixed build plays the bank file named in SYSTEM.INI
[ESFM.DRV] Bank=, read when a program opens the device if its date or time
changed (src/esfm/esfmfile.asm).

SettingsTest: SYSTEM.INI [ESFM.DRV] turns each change off, read once at the
first DRV_ENABLE (src/esfm/esfmini.asm). Each key at 0 does what ESS's
driver does, and with all of them at 0 the fixed build writes the same
ports as ESS's.

Opl3Test: device 1, which plays General MIDI on the chip's OPL3
compatibility mode with Freedoom's GENMIDI instruments
(src/esfm/esfmopl.asm), against the bank and the OPL3's pitch.

Opl3DriverTest: ESFMOPL3.DRV, the OPL3 device as a MIDI driver of its own
for the Multimedia control panel, loaded in the emulator next to ESFM.DRV
(src/esfm/esfmopl3.asm).
"""

import math
import os
import struct
import sys
import tempfile
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, os.path.join(ROOT, "tests"))

import build_esfm  # noqa: E402
import gmcheck  # noqa: E402
from retools.ne import NEFile  # noqa: E402

try:
    import esfmemu
    from esfmemu import (ESFMEmu, Inject, MODM_CLOSE, MODM_DATA,
                         MODM_LONGDATA, MODM_RESET, MOM_CLOSE, MOM_DONE,
                         MIDIERR_NOTREADY, MHDR_DONE, MHDR_PREPARED,
                         DRV_POWER, PWR_SUSPENDREQUEST, PWR_SUSPENDRESUME,
                         DRV_ENABLE, DRV_DISABLE)
    HAVE_UNICORN = True
except ImportError:
    HAVE_UNICORN = False

NOTE_A_ON, NOTE_A_OFF = 0x7F3C90, 0x003C80     # middle C, channel 1
NOTE_B_ON, NOTE_B_OFF = 0x7F4090, 0x004090     # E, channel 1 (velocity 0)
SUSTAIN_ON = 0x7F40B0
DEV_FLAGS = 0x30E                               # bit 2: suspended
_builds = {}


def builds():
    if not _builds:
        tmp = tempfile.mkdtemp(prefix="esfmdrv")
        _builds["stock"] = build_esfm.build(False, workdir=tmp)
        _builds["fixed"] = build_esfm.build(True, workdir=tmp)
        _builds["opl3"] = build_esfm.build_opl3(workdir=tmp)
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

    def test_opl3_driver_is_current(self):
        with open(os.path.join(ROOT, "build", "ESFMOPL3.DRV"), "rb") as f:
            self.assertEqual(f.read(), builds()["opl3"],
                             "build/ESFMOPL3.DRV is out of date: run "
                             "tools/build_esfm.py")

    def test_banks_folder(self):
        # the banks that essinst puts in SYSTEM\\ESFMBANK, where the box of
        # the patch banks lists them: esfm_patch_banks under short names
        names = {"ESS.BIN": "bnk_com.bin", "ESSNT4.BIN": "bnk_NT4.bin",
                 "SQUARE.BIN": "bnk_com_better_square_wave.bin"}
        folder = os.path.join(ROOT, "build", "BANKS")
        self.assertEqual(sorted(os.listdir(folder)), sorted(names))
        for name, orig in names.items():
            with open(os.path.join(folder, name), "rb") as f, \
                    open(os.path.join(ROOT, "esfm_patch_banks", orig),
                         "rb") as g:
                self.assertEqual(f.read(), g.read(), name)

    def test_opl3_driver(self):
        ne = NEFile(builds()["opl3"])
        self.assertEqual(ne.module_name, "ESFMOPL3")
        self.assertEqual(ne.modules, ["KERNEL", "USER"])
        # WEP, DriverProc and modMessage, all in the one fixed code segment
        self.assertEqual(sorted(ne.entries), [1, 2, 3])
        self.assertEqual({seg for seg, _o, _f in ne.entries.values()}, {1})
        code, data = ne.segments
        self.assertFalse(code.flags & 0x0011)          # code, fixed
        self.assertEqual(data.flags & 0x0011, 0x0001)  # data, fixed

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

    def test_running_status_note_offs_in_long_data(self):
        for which, hung in (("stock", [0x40]), ("fixed", [])):
            with self.subTest(which):
                emu = ESFMEmu(builds()[which])
                emu.open()
                emu.data(0x7F3C90)
                emu.data(0x7F4090)
                # ESS's parser turns the second note off into note 7Ch
                emu.longdata(bytes([0x80, 0x3C, 0x00, 0x40, 0x00]))
                self.assertEqual(sorted({n for f, c, n in emu.voices()
                                         if f & 1}), hung)

    def test_real_time_byte_keeps_running_status(self):
        # a timing clock between a note on and its note off in running
        # status: ESS's code took F8h as the running status
        for which, hangs in (("stock", True), ("fixed", False)):
            with self.subTest(which):
                emu = self.emu(which)
                emu.data(NOTE_A_ON)
                emu.data(0xF8)
                emu.data(0x003C)                # 3C 00: note off
                self.assertEqual(bool(emu.keyed_voices()), hangs)
        # a system common byte ends it: 40 7F isn't a note
        emu = self.emu("fixed")
        emu.data(NOTE_A_ON)
        emu.data(0xF6)
        emu.data(0x7F40)
        self.assertEqual(emu.note_voices(0, 0x40), [])

    def test_message_split_between_long_buffers(self):
        for which, plays in (("stock", False), ("fixed", True)):
            with self.subTest(which):
                emu = self.emu(which)
                emu.longdata(bytes([0x90, 0x3C]))
                emu.longdata(bytes([0x7F, 0x40, 0x7F]))  # running status
                self.assertEqual(bool(emu.note_voices(0, 0x3C)), plays)
                self.assertEqual(bool(emu.note_voices(0, 0x40)), plays)
        # the note offs in running status in the next buffers
        emu.longdata(bytes([0x3C]))
        emu.longdata(bytes([0x00, 0x40, 0x00]))
        self.assertSilent(emu)
        # a reset drops what was left
        emu.longdata(bytes([0x90, 0x3C]))
        emu.reset()
        emu.longdata(bytes([0x7F]))
        self.assertEqual(emu.driver_active(), [])
        self.assertUnlocked(emu)

    def test_short_messages_between_long_buffers(self):
        # as if all the bytes came one after the other: a short message's
        # status is the running status of the next long buffer, a
        # real-time byte leaves it alone, a system common one ends it
        emu = self.emu("fixed")
        emu.longdata(bytes([0xC0, 0x05]))       # program 6
        emu.data(0x6407B0)                      # volume 100
        emu.longdata(bytes([0x07, 0x20]))       # B0 07 20
        self.assertEqual(emu.dev8(DEV_CHAN_VOLUME), 0x20)
        emu.data(0xFE)                          # active sensing
        emu.longdata(bytes([0x07, 0x30]))
        self.assertEqual(emu.dev8(DEV_CHAN_VOLUME), 0x30)
        emu.data(0xF6)                          # tune request
        emu.longdata(bytes([0x07, 0x40]))
        self.assertEqual(emu.dev8(DEV_CHAN_VOLUME), 0x30)
        self.assertEqual(emu.dev8(DEV_PROGRAM), 5)
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

    def close_sweep(self, which, step):
        """Send a note on from an interrupt at every step-th instruction
        of a close, and count the trials that leave a voice keyed after
        it."""
        emu = self.emu(which)
        stuck = trials = 0
        for k in range(0, 4000, step):
            emu.data(NOTE_A_ON)
            inj = Inject(MODM_DATA, dw1=0x7F4591, after_insns=k)
            self.assertEqual(emu.close(injects=[inj]), 0)
            if not inj.started:
                break                   # past the close's end
            trials += 1
            if emu.keyed_voices():
                stuck += 1
            emu.open()
        return emu, stuck, trials

    def test_close_sweep(self):
        # from the close's first instruction to its last, the note on of
        # the program's interrupt is never played on the closed device:
        # dropped while the close holds the driver, refused after it
        _emu, stuck, _n = self.close_sweep("stock", 97)
        self.assertGreater(stuck, 0)
        emu, stuck, n = self.close_sweep("fixed", 47)
        self.assertEqual(stuck, 0)
        self.assertGreater(n, 40)
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
        # the second one played: a voice has its note, keyed off by the third
        self.assertIn((2, 1, 0x45), emu.voices())
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
                for i in range(qsize + 4)]
        # a long message refused keeps the flags the program left
        hdr = emu.header(bytes([0x90, 0x43, 0x7F]))
        emu.wr(hdr >> 16, (hdr & 0xFFFF) + 16,
               struct.pack("<I", MHDR_DONE | MHDR_PREPARED))
        injs.append(Inject(MODM_LONGDATA, dw1=hdr, dw2=0x20,
                           after_writes=start + 1 + 3 * len(injs)))
        # a chord in one long message holds the driver long enough
        chord = (0x3C, 0x40, 0x43, 0x48, 0x4C)
        r, _flags = emu.longdata(bytes([0x90]) + bytes(
            b for n in chord for b in (n, 0x7F)), injects=injs)
        self.assertEqual(r, 0)
        results = [i.result for i in injs]
        self.assertEqual(results.count(0), qsize - 1)
        self.assertEqual(results.count(MIDIERR_NOTREADY), 6)
        self.assertEqual(results[-1], MIDIERR_NOTREADY)
        self.assertEqual(emu.header_flags(hdr), MHDR_DONE | MHDR_PREPARED)
        st = emu.fix_state()
        self.assertEqual((st["overflow"], st["maxdepth"]), (6, qsize - 1))
        emu.longdata(bytes([0x80]) + bytes(b for n in chord
                                           for b in (n, 0x00)))
        self.assertSilent(emu)
        self.assertUnlocked(emu)

    def test_close_gives_queued_long_messages_back(self):
        # the program closes the device from its MOM_DONE callback while
        # another long message of it waits: that one comes back first
        emu = self.emu("fixed")
        hdr = emu.header(bytes([0x90, 0x43, 0x7F]))
        injs = [Inject(MODM_LONGDATA, dw1=hdr, dw2=0x20,
                       after_writes=emu.chip.writes + 10),
                Inject(MODM_CLOSE, in_callback=MOM_DONE)]
        r, _flags = emu.longdata(bytes([0x90, 0x3C, 0x7F]), injects=injs)
        self.assertEqual((r, injs[0].result, injs[1].result), (0, 0, 0))
        self.assertEqual(emu.header_flags(hdr) & 0x11, 0x01)  # done
        self.assertEqual([(m, d1) for m, _i, d1, _d2 in emu.callbacks[-3:]],
                         [(MOM_DONE, (esfmemu.STUB_PARA << 16) | 0xD000),
                          (MOM_DONE, hdr), (MOM_CLOSE, 0)])
        self.assertEqual(emu.keyed_voices(), [])        # 43h never played
        self.assertEqual(emu.fix_state()["purged"], 1)
        self.assertUnlocked(emu)

    def test_long_message_during_close(self):
        # queued during ESS's close: marked done, no callback to a client
        # that is gone
        emu = self.emu("fixed")
        emu.data(NOTE_A_ON)
        hdr = emu.header(bytes([0x90, 0x43, 0x7F]))
        inj = Inject(MODM_LONGDATA, dw1=hdr, dw2=0x20,
                     after_writes=emu.chip.writes + 1)
        self.assertEqual(emu.close(injects=[inj]), 0)
        self.assertEqual(inj.result, 0)
        self.assertEqual(emu.header_flags(hdr) & 0x11, 0x01)
        self.assertEqual(emu.callbacks[-1][0], MOM_CLOSE)
        self.assertEqual(emu.keyed_voices(), [])
        self.assertEqual(emu.chip.splits, [])
        self.assertUnlocked(emu)

    def test_long_message_refused_when_its_turn_comes(self):
        # the device is suspended while the message waits: ESS's code
        # refuses it, and the program gets its buffer back all the same
        emu = self.emu("fixed")
        hdr = emu.header(bytes([0x90, 0x43, 0x7F]))
        start = emu.chip.writes
        suspended = bytes([emu.dev8(DEV_FLAGS) | 4])
        injs = [Inject(MODM_LONGDATA, dw1=hdr, dw2=0x20,
                       after_writes=start + 10),
                Inject(poke=(esfmemu.SEG_PARA[4], emu.dev + DEV_FLAGS,
                             suspended), after_writes=start + 20)]
        emu.data(NOTE_A_ON, injects=injs)
        self.assertEqual(injs[0].result, 0)
        self.assertTrue(injs[1].done)
        self.assertEqual(emu.header_flags(hdr) & 0x11, 0x01)
        self.assertEqual((emu.callbacks[-1][0], emu.callbacks[-1][2]),
                         (MOM_DONE, hdr))
        self.assertEqual(emu.note_voices(0, 0x43), [])
        self.assertUnlocked(emu)


# the sustain pedal (esfmped.asm)
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
        emu.data(0x0030C5)
        self.assertEqual(emu.pedals(), [])
        self.assertEqual(emu.keyed_voices(), [])
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
                self.assertEqual(emu.fix_state()["lock"], 0)

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

    # -- ESS's own pedal handling, unchanged --------------------------------

    def test_pedal_up_and_reset_controllers(self):
        for which in ("stock", "fixed"):
            with self.subTest(which):
                emu = self.emu(which)
                emu.data(0x7F3C90)              # a key held down, channel 1
                emu.data(0x7F40B2)              # pedal down, channel 3
                emu.data(0x7F3C92)
                emu.data(0x003C92)
                self.assertEqual(emu.pedals(), [2])
                emu.data(0x0040B2)              # pedal up
                # it let channel 3's note go, channel 1's still sounds
                self.assertEqual({emu.voices()[v][1]
                                  for v in emu.keyed_voices() if v < 16}, {0})
                emu.data(0x7F40B2)
                emu.data(0x7F4092)
                emu.data(0x004092)
                emu.data(0x0079B2)              # reset all controllers
                self.assertEqual(emu.pedals(), [])
                self.assertEqual({emu.voices()[v][1]
                                  for v in emu.keyed_voices() if v < 16}, {0})

    def test_status_block_version(self):
        # 3 was an earlier build's pedal times, 4 added the settings, 5
        # the OPL3 device, 6 its bank file
        self.assertEqual(self.emu("fixed").fix_state()["version"], 6)


# General MIDI (esfmgm.asm): device fields, esfmdev.inc
DEV_PROGRAM, DEV_RPN_DATA, DEV_CHAN_FLAGS, DEV_BEND = 0x20, 0x30, 0x40, 0x50
DEV_CHAN_PAN, DEV_CHAN_VOLUME, DEV_CHAN_EXPR = 0x2D2, 0x2E2, 0x2F2
DEV_GM_MOD, DEV_GM_PRESS, DEV_GM_RPN = 0x311, 0x321, 0x341
DEV_GM_VIB = 0x331
DEV_GM_CENTS, DEV_GM_FINE, DEV_GM_COARSE, DEV_GM_MASTER = (0x361, 0x371,
                                                           0x391, 0x3A1)


def cc(channel, number, value):
    return 0xB0 | channel | number << 8 | value << 16


def rpn(channel, number, msb, lsb=None):
    """Controllers selecting RPN number, then its data entry."""
    out = [cc(channel, 101, number >> 7), cc(channel, 100, number & 0x7F),
           cc(channel, 6, msb)]
    if lsb is not None:
        out.append(cc(channel, 38, lsb))
    return out


def master_volume(value):
    return bytes([0xF0, 0x7F, 0x7F, 0x04, 0x01, value & 0x7F, value >> 7,
                  0xF7])


def pitches(emu, voices):
    """F-number and block of every operator of the voices."""
    return [(r[4] | (r[5] & 3) << 8, (r[5] >> 2) & 7)
            for v in voices for r in emu.op_regs(v)]


@unittest.skipUnless(HAVE_UNICORN, "needs the unicorn module")
class GMTest(unittest.TestCase):
    """What General MIDI asks for beyond ESS's code: modulation, channel
    pressure, RPN 1 and 2, bend range cents, controller 121 as in RP-015,
    GM resets to the defaults and master volume."""

    def emu(self, which="fixed", msgs=()):
        e = ESFMEmu(builds()[which])
        e.open()
        for m in msgs:
            e.data(m)
        return e

    def play(self, emu, channel=0, note=60):
        emu.data(0x7F0090 | channel | note << 8)
        voices = emu.note_voices(channel, note)
        self.assertTrue(voices)
        return voices

    def regs(self, emu, voices, reg):
        return [r[reg] for v in voices for r in emu.op_regs(v)]

    def assertPitch(self, msgs, ref_msgs, channel=0, note=60, ref_note=60,
                    ref_channel=None):
        """The note after msgs sounds at the pitch of ref_note after
        ref_msgs, and not at the pitch it has without msgs."""
        emu = self.emu(msgs=msgs)
        got = pitches(emu, self.play(emu, channel, note))
        ref = self.emu(msgs=ref_msgs)
        ref_ch = channel if ref_channel is None else ref_channel
        want = pitches(ref, self.play(ref, ref_ch, ref_note))
        self.assertEqual(got, want)
        plain = self.emu()
        self.assertNotEqual(got, pitches(plain, self.play(plain, channel,
                                                          note)))
        return emu

    # -- modulation and channel pressure: the chip's vibrato --------------

    def test_modulation_vibrato(self):
        emu = self.emu()
        voices = self.play(emu)
        reg0, reg6 = self.regs(emu, voices, 0), self.regs(emu, voices, 6)
        emu.data(cc(0, 1, 40))                  # shallow
        self.assertEqual(self.regs(emu, voices, 0), [r | 0x40 for r in reg0])
        self.assertEqual(self.regs(emu, voices, 6), reg6)
        emu.data(cc(0, 1, 100))                 # deep
        self.assertEqual(self.regs(emu, voices, 0), [r | 0x40 for r in reg0])
        self.assertEqual(self.regs(emu, voices, 6), [r | 0x40 for r in reg6])
        # a new note gets it too, and modulation 0 takes it away
        voices2 = self.play(emu, note=64)
        self.assertTrue(all(r & 0x40 for r in self.regs(emu, voices2, 0)))
        emu.data(cc(0, 1, 0))
        self.assertEqual(self.regs(emu, voices, 0), reg0)
        self.assertEqual(self.regs(emu, voices, 6), reg6)
        # another channel's notes don't change
        emu.data(cc(1, 1, 127))
        self.assertEqual(self.regs(emu, voices, 0), reg0)
        # ESS's driver ignores controller 1
        stock = self.emu("stock")
        voices = self.play(stock)
        before = [stock.op_regs(v) for v in voices]
        stock.data(cc(0, 1, 127))
        self.assertEqual([stock.op_regs(v) for v in voices], before)

    def test_channel_pressure(self):
        emu = self.emu()
        voices = self.play(emu)
        reg0, reg6 = self.regs(emu, voices, 0), self.regs(emu, voices, 6)
        emu.data(0x0030D0)                      # pressure 48
        self.assertEqual(self.regs(emu, voices, 0), [r | 0x40 for r in reg0])
        self.assertEqual(self.regs(emu, voices, 6), reg6)
        emu.data(cc(0, 1, 10))
        emu.data(0x0070D0)                      # the larger of the two
        self.assertEqual(self.regs(emu, voices, 6), [r | 0x40 for r in reg6])
        emu.data(0x0000D0)
        self.assertEqual(self.regs(emu, voices, 6), reg6)
        emu.data(cc(0, 1, 0))
        self.assertEqual(self.regs(emu, voices, 0), reg0)
        # in a long message, with running status
        self.assertEqual(emu.longdata(bytes([0xD0, 0x10, 0x20]))[0], 0)
        self.assertEqual(emu.dev8(DEV_GM_PRESS), 0x20)
        self.assertEqual(self.regs(emu, voices, 0), [r | 0x40 for r in reg0])

    def test_running_status_in_long_data(self):
        for which, second in (("stock", 0x7C), ("fixed", 0x40)):
            with self.subTest(which):
                emu = self.emu(which)
                emu.longdata(bytes([0x90, 0x3C, 0x7F, 0x40, 0x7F]))
                # ESS's parser ORed 40h into the last note, 3Ch
                self.assertEqual(sorted({n for f, c, n in emu.voices()
                                         if f & 1}), [0x3C, second])

    # -- tuning -----------------------------------------------------------

    def test_fine_tuning(self):
        # 2800h, +25 cents, is a bend of 2400h with the range of 2
        # semitones
        tune = rpn(0, 1, 0x50)
        emu = self.assertPitch(tune, [0x4800E0])
        self.assertEqual(emu.dev16(DEV_GM_FINE), 0x2800)
        # with the LSB, and on a note that already sounds
        emu = self.emu()
        voices = self.play(emu)
        for m in rpn(0, 1, 0x3F, 0x40):         # 1FC0h: -0.8 cents
            emu.data(m)
        ref = self.emu(msgs=[0x3F60E0])         # bend 1FE0h
        self.assertEqual(pitches(emu, voices), pitches(ref, self.play(ref)))

    def test_coarse_tuning(self):
        self.assertPitch(rpn(0, 2, 0x40 + 12), [], ref_note=72)
        self.assertPitch(rpn(3, 2, 0x40 - 5), [], channel=3, ref_note=55)
        # not on channel 10, the drums
        emu = self.emu(msgs=rpn(9, 2, 0x40 + 12))
        plain = self.emu()
        self.assertEqual(pitches(emu, self.play(emu, 9, 38)),
                         pitches(plain, self.play(plain, 9, 38)))
        # ESS's driver: no RPN 2
        stock = self.emu("stock", rpn(0, 2, 0x40 + 12))
        plain = self.emu("stock")
        self.assertEqual(pitches(stock, self.play(stock)),
                         pitches(plain, self.play(plain)))

    def test_bend_range_cents(self):
        # 1 semitone 50 cents at the top equals 3 semitones halfway up
        self.assertPitch(rpn(0, 0, 1, 50) + [0x7F7FE0],
                         rpn(0, 0, 3) + [0x6000E0])
        # the MSB sets the cents to 0
        emu = self.emu(msgs=rpn(0, 0, 1, 50) + [cc(0, 6, 2)])
        self.assertEqual((emu.dev8(DEV_RPN_DATA), emu.dev8(DEV_GM_CENTS)),
                         (2, 0))

    def test_range_change_moves_a_bent_note(self):
        emu = self.emu(msgs=[0x6000E0])         # half way up, 1 semitone
        voices = self.play(emu)
        for m in rpn(0, 0, 12):
            emu.data(m)
        ref = self.emu(msgs=rpn(0, 0, 12) + [0x6000E0])
        self.assertEqual(pitches(emu, voices), pitches(ref, self.play(ref)))

    def test_huge_bend_range(self):
        emu = self.emu(msgs=rpn(0, 0, 127, 127))
        self.play(emu)
        for bend in (0x0000E0, 0x7F7FE0, 0x4000E0, 0x0040E0):
            emu.data(bend)
        self.assertEqual(emu.fix_state()["lock"], 0)

    def test_null_and_nrpn_select_nothing(self):
        for select in ([cc(0, 101, 0x7F), cc(0, 100, 0x7F)],
                       [cc(0, 99, 0), cc(0, 98, 0)]):
            emu = self.emu(msgs=rpn(0, 0, 5) + select + [cc(0, 6, 9),
                                                         cc(0, 38, 9)])
            self.assertEqual((emu.dev8(DEV_RPN_DATA),
                              emu.dev8(DEV_GM_CENTS)), (5, 0))

    # -- controller 121 and the resets -------------------------------------

    def test_reset_all_controllers_rp015(self):
        setup = [cc(0, 7, 50), cc(0, 10, 0), cc(0, 11, 30), cc(0, 1, 100),
                 0x7F40B0] + rpn(0, 0, 12) + rpn(0, 1, 0x50) + [0x7000E0]
        emu = self.emu(msgs=setup)
        voices = self.play(emu)
        emu.data(cc(0, 121, 0))
        # kept: volume, pan, the bend range and the tuning
        self.assertEqual((emu.dev8(DEV_CHAN_VOLUME), emu.dev8(DEV_CHAN_PAN),
                          emu.dev8(DEV_RPN_DATA), emu.dev16(DEV_GM_FINE)),
                         (50, 0x10, 12, 0x2800))
        # reset: expression, modulation, the pedal, the bend and the RPN
        self.assertEqual((emu.dev8(DEV_CHAN_EXPR), emu.dev8(DEV_GM_MOD),
                          emu.dev8(DEV_CHAN_FLAGS) & 1, emu.dev16(DEV_BEND),
                          emu.dev16(DEV_GM_RPN)),
                         (127, 0, 0, 0x2000, 0x7F7F))
        # the note that sounds isn't bent or shaking anymore
        ref = self.emu(msgs=[cc(0, 7, 50), cc(0, 10, 0)] +
                       rpn(0, 1, 0x50))
        rv = self.play(ref)
        self.assertEqual(pitches(emu, voices), pitches(ref, rv))
        self.assertEqual(self.regs(emu, voices, 0), self.regs(ref, rv, 0))
        self.assertEqual(self.regs(emu, voices, 1), self.regs(ref, rv, 1))
        # a data entry now changes nothing
        emu.data(cc(0, 6, 2))
        self.assertEqual(emu.dev8(DEV_RPN_DATA), 12)
        # ESS's code reset the volume, the pan and the range too
        stock = self.emu("stock", setup + [cc(0, 121, 0)])
        self.assertEqual((stock.dev8(DEV_CHAN_VOLUME),
                          stock.dev8(DEV_CHAN_PAN), stock.dev8(DEV_RPN_DATA)),
                         (100, 0x30, 2))

    def test_gm_reset_sets_the_defaults(self):
        setup = [0x0005C2, cc(2, 7, 50), cc(2, 10, 127), cc(2, 1, 100),
                 0x0040D2] + rpn(2, 0, 12, 30) + rpn(2, 1, 0x50) + \
            rpn(2, 2, 0x50)
        for name, sysex in (("GM", GM_ON), ("GM2", GM2_ON), ("GS", GS_RESET),
                            ("XG", XG_ON)):
            with self.subTest(name):
                emu = self.emu(msgs=setup)
                emu.longdata(master_volume(0x1000))
                self.play(emu, 2)
                emu.longdata(sysex)
                self.assertEqual(
                    [emu.dev8(off + 2) for off in (
                        DEV_PROGRAM, DEV_CHAN_VOLUME, DEV_CHAN_PAN,
                        DEV_CHAN_EXPR, DEV_RPN_DATA, DEV_GM_CENTS,
                        DEV_GM_COARSE, DEV_GM_MOD, DEV_GM_PRESS)],
                    [0, 100, 0x30, 127, 2, 0, 0x40, 0, 0])
                self.assertEqual((emu.dev16(DEV_GM_FINE + 4),
                                  emu.dev16(DEV_GM_RPN + 4),
                                  emu.dev8(DEV_GM_MASTER)),
                                 (0x2000, 0x7F7F, 0))
                self.assertEqual(emu.keyed_voices(), [])
                self.assertEqual(emu.fix_state()["lock"], 0)

    def test_midi_reset_sets_the_defaults(self):
        emu = self.emu(msgs=rpn(0, 1, 0x50) + [cc(0, 1, 100)])
        emu.longdata(master_volume(0))
        emu.reset()
        self.assertEqual((emu.dev16(DEV_GM_FINE), emu.dev8(DEV_GM_MOD),
                          emu.dev8(DEV_GM_MASTER), emu.dev16(DEV_GM_RPN)),
                         (0x2000, 0, 0, 0x7F7F))

    # -- master volume and pan ---------------------------------------------

    def test_master_volume(self):
        emu = self.emu()
        voices = self.play(emu)
        tl = [r & 0x3F for r in self.regs(emu, voices, 1)]
        r, flags = emu.longdata(master_volume(0x2000))     # 64: 11.25 dB
        self.assertEqual((r, flags & 1), (0, 1))
        self.assertEqual(emu.dev8(DEV_GM_MASTER), 15)
        down = [r & 0x3F for r in self.regs(emu, voices, 1)]
        self.assertTrue(any(d > t for d, t in zip(down, tl)))
        self.assertTrue(all(d == t or d == min(63, t + 15)
                            for d, t in zip(down, tl)))
        # a new note plays as quietly
        v2 = self.play(emu, 1)
        ref = self.emu()
        self.assertEqual([r & 0x3F for r in self.regs(emu, v2, 1)],
                         [min(63, t + 15) if t != d else t
                          for t, d in zip(self.regs_tl(ref, 1), down)])
        emu.longdata(master_volume(0x3FFF))
        self.assertEqual([r & 0x3F for r in self.regs(emu, voices, 1)], tl)
        # ESS's driver skips it
        stock = self.emu("stock")
        voices = self.play(stock)
        tl = self.regs(stock, voices, 1)
        stock.longdata(master_volume(0))
        self.assertEqual(self.regs(stock, voices, 1), tl)

    # -- build/GMCHECK.MID ---------------------------------------------------

    def test_gmcheck_is_current(self):
        with open(gmcheck.OUT, "rb") as f:
            self.assertEqual(f.read(), gmcheck.smf(gmcheck.events()),
                             "run python3 tools/gmcheck.py")

    def test_gmcheck_steps_in_testing_md(self):
        # word for word: the doc wraps the list for Notepad
        with open(os.path.join(ROOT, "docs", "TESTING.md")) as f:
            doc = " ".join(f.read().split())
        self.assertIn(" ".join(gmcheck.steps_table().split()), doc)

    def test_gmcheck_plays(self):
        emu = self.emu()
        for t, msg in gmcheck.events():
            if msg[0] == 0xF0:
                self.assertEqual(emu.longdata(msg)[0], 0)
            else:
                emu.data(int.from_bytes(msg, "little"))
            if t == 32.5:
                # after controller 121: centered, still, still on the left
                self.assertEqual((emu.dev16(DEV_BEND), emu.dev8(DEV_GM_MOD),
                                  emu.dev8(DEV_CHAN_PAN)), (0x2000, 0, 0x10))
        self.assertEqual(emu.keyed_voices(), [])
        self.assertEqual(emu.fix_state()["lock"], 0)
        self.assertEqual(emu.dev8(DEV_CHAN_PAN), 0x30)

    def regs_tl(self, emu, channel):
        return [r & 0x3F for r in self.regs(emu, self.play(emu, channel), 1)]

    def test_pan_moves_sounding_notes(self):
        emu = self.emu()
        voices = self.play(emu)
        reg6 = self.regs(emu, voices, 6)
        outs = [r & 0x30 for r in reg6]
        self.assertIn(0x30, outs)               # the patch sounds on both
        # the chip's left output is register 6 bit 5 (TrueSides)
        for value, side in ((0, 0x20), (127, 0x10)):
            emu.data(cc(0, 10, value))
            self.assertEqual([r & 0x30 for r in self.regs(emu, voices, 6)],
                             [side if o else 0 for o in outs])
        emu.data(cc(0, 10, 64))                 # the patch's own outputs
        self.assertEqual(self.regs(emu, voices, 6), reg6)
        # ESS's driver pans the next note only
        stock = self.emu("stock")
        voices = self.play(stock)
        reg6 = self.regs(stock, voices, 6)
        stock.data(cc(0, 10, 0))
        self.assertEqual(self.regs(stock, voices, 6), reg6)

    def test_same_pan_position_writes_nothing(self):
        # a pan sweep changes the position three times: the rest of it
        # doesn't rewrite the sounding notes
        emu = self.emu()
        voices = self.play(emu)
        reg6 = self.regs(emu, voices, 6)
        emu.data(cc(0, 10, 0))
        writes = emu.chip.writes
        for value in range(1, 0x30):
            emu.data(cc(0, 10, value))
        self.assertEqual(emu.chip.writes, writes)
        emu.data(cc(0, 10, 0x40))               # moved: written
        self.assertGreater(emu.chip.writes, writes)
        self.assertEqual(self.regs(emu, voices, 6), reg6)

    def test_suspended_device_changes_nothing(self):
        # suspended, FM isn't Windows' and ESS's code refuses the messages:
        # a GM reset or the master volume in one changes nothing either
        emu = self.emu(msgs=[0x0030C0, cc(0, 7, 50)])
        self.play(emu)
        emu.driverproc(DRV_POWER, PWR_SUSPENDREQUEST)
        self.assertTrue(emu.dev8(DEV_FLAGS) & 4)
        writes = emu.chip.writes
        for sysex in (GM_ON, GS_RESET, master_volume(0x1000)):
            self.assertEqual(emu.longdata(sysex)[0], 3)    # not enabled
        self.assertEqual(emu.chip.writes, writes)
        self.assertEqual((emu.dev8(DEV_PROGRAM), emu.dev8(DEV_CHAN_VOLUME),
                          emu.dev8(DEV_GM_MASTER)), (0x30, 50, 0))
        self.assertEqual(emu.fix_state()["lock"], 0)


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
                self.assertEqual([ax for ax, _bx, _dx in emu.open_calls],
                                 [0x716C, 0x6C00])
                self.assertClean(emu)

    def test_drive_not_ready(self):
        # Bank= on a floppy or CD-ROM without a disk: the open fails with
        # no "not ready" box, both calls with BX bit 13, open only
        for lfn in (True, False):
            with self.subTest(lfn=lfn):
                emu = self.emu(files={PATH: marked(self.base(), 0x5A)})
                emu.lfn = lfn
                emu.not_ready = True
                emu.open()
                self.assertEqual(emu.int24, [])
                self.assertTrue(emu.open_calls)
                for ax, bx, dx in emu.open_calls:
                    self.assertEqual((bx & 0x2000, dx), (0x2000, 0x0001))
                self.assertEqual(emu.fix_state()["bstate"], BS_MISSING)
                self.assertEqual(emu.bank(len(self.builtin)), self.builtin)
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
        emu.es_on_block = True
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


# SYSTEM.INI [ESFM.DRV] keys, in the order of their fix_opts bits
SETTINGS = ["QueueWhileBusy", "SilenceOnClose", "PedalRelease", "Vibrato",
            "Tuning", "ResetControllers", "LivePan", "SysEx",
            "RunningStatus", "BetterSquareWave", "TrueSides", "Opl3Device",
            "BankSettings"]
OPT = {k: 1 << i for i, k in enumerate(SETTINGS[:12])}
OPT["BankSettings"] = 0x2000    # after OPT_OPL3_DRV
OPT_DEFAULT, OPT_READ = 0x2FFF, 0x8000
OPT_OPL3_DRV = 0x1000           # Opl3Device=2: device 1 is ESFMOPL3.DRV's
GM_ON = bytes([0xF0, 0x7E, 0x7F, 0x09, 0x01, 0xF7])


@unittest.skipUnless(HAVE_UNICORN, "needs the unicorn module")
class SettingsTest(unittest.TestCase):
    def started(self, which="fixed", ini=None, open_it=True):
        """the driver after DRV_ENABLE with SYSTEM.INI [ESFM.DRV] as in
        ini, {key: value}, and a program's MODM_OPEN"""
        e = ESFMEmu(builds()[which])
        for key, value in (ini or {}).items():
            e.ini[("esfm.drv", key.lower())] = value
        self.assertEqual(e.driverproc(DRV_ENABLE) & 0xFFFF, 1)
        if open_it:
            e.open()
        return e

    def off(self, *keys):
        """keys at 0, and ESS's bank and sides, as ESS's driver plays"""
        ini = {k: "0" for k in keys}
        ini["BetterSquareWave"] = "0"
        ini["TrueSides"] = "0"
        return ini

    # -- reading -----------------------------------------------------------

    def test_read_at_the_first_enable(self):
        e = self.started(open_it=False)
        self.assertEqual(e.fix_state()["opts"], OPT_DEFAULT | OPT_READ)
        self.assertEqual(e.int_reads, [("SYSTEM.INI", "ESFM.DRV", k, 1)
                                       for k in SETTINGS])

    def test_each_key(self):
        for key in SETTINGS:
            for value, on in (("0", False), ("1", True), ("2", True),
                              ("no", False)):
                with self.subTest(key=key, value=value):
                    e = self.started(ini={key: value}, open_it=False)
                    want = OPT_DEFAULT & ~OPT[key] | (OPT[key] if on else 0)
                    if key == "Opl3Device" and value == "2":
                        want |= OPT_OPL3_DRV
                    self.assertEqual(e.fix_state()["opts"], want | OPT_READ)

    def test_read_once(self):
        e = self.started(ini={"Vibrato": "0"})
        e.close()
        e.driverproc(DRV_DISABLE)
        e.ini = {}
        self.assertEqual(e.driverproc(DRV_ENABLE) & 0xFFFF, 1)
        e.open()
        self.assertEqual(len(e.int_reads), len(SETTINGS))
        self.assertEqual(e.fix_state()["opts"],
                         OPT_DEFAULT & ~OPT["Vibrato"] | OPT_READ)

    def test_bank_read_at_every_open(self):
        # Bank= isn't a setting read once: every MODM_OPEN reads it
        e = self.started()
        reads = len(e.ini_reads)
        e.close()
        e.open()
        self.assertEqual(len(e.ini_reads), reads + 1)
        self.assertEqual(e.ini_reads[-1], ("SYSTEM.INI", "ESFM.DRV", "Bank"))

    def test_without_drv_enable_the_defaults(self):
        e = ESFMEmu(builds()["fixed"])
        e.open()
        self.assertEqual(e.fix_state()["opts"], OPT_DEFAULT)

    # -- each key at 0: ESS's driver ------------------------------------------

    def compare(self, key, scenario):
        """scenario(emu) with ESS's driver, the key at 0 and the default;
        the first two alike, the default not"""
        stock = scenario(self.started("stock"))
        off = scenario(self.started(ini=self.off(key)))
        on = scenario(self.started(ini={"BetterSquareWave": "0"}))
        self.assertEqual(off, stock, key)
        self.assertNotEqual(on, stock, key)
        return stock, on

    def test_queue_while_busy(self):
        def note_off_during_note_on(e):
            e.data(NOTE_A_ON)
            inj = Inject(MODM_DATA, dw1=NOTE_A_OFF,
                         after_writes=e.chip.writes + 10)
            e.data(NOTE_B_ON, injects=[inj])
            e.data(NOTE_B_OFF)
            return inj.result, e.keyed_voices()
        stock, _on = self.compare("QueueWhileBusy", note_off_during_note_on)
        self.assertEqual(stock[0], MIDIERR_NOTREADY)

    def test_silence_on_close(self):
        def close_with_pedal(e):
            e.data(SUSTAIN_ON)
            e.data(NOTE_A_ON)
            e.data(NOTE_A_OFF)
            e.close()
            return e.keyed_voices()
        stock, on = self.compare("SilenceOnClose", close_with_pedal)
        self.assertTrue(stock)
        self.assertEqual(on, [])

    def test_pedal_release(self):
        def program_change_with_pedal(e):
            e.data(SUSTAIN_ON)
            e.data(NOTE_A_ON)
            e.data(NOTE_A_OFF)
            e.data(0x05C0)                      # program 6
            return e.pedals(), e.keyed_voices()
        stock, on = self.compare("PedalRelease", program_change_with_pedal)
        self.assertEqual(stock[0], [0])
        self.assertEqual(on, ([], []))

    def regs_after(self, *msgs):
        def scenario(e):
            e.data(NOTE_A_ON)
            for m in msgs:
                if isinstance(m, bytes):
                    e.longdata(m)
                else:
                    e.data(m)
            e.data(0x7F4390)                    # and a new note
            return bytes(e.chip.regs)
        return scenario

    def test_vibrato(self):
        self.compare("Vibrato", self.regs_after(cc(0, 1, 100), 0x60D0))

    def test_tuning(self):
        self.compare("Tuning", self.regs_after(
            *rpn(0, 1, 0x50), *rpn(0, 2, 0x42), *rpn(0, 0, 2, 50), 0x6000E0))

    def test_reset_controllers(self):
        # volume and expression only: pan and the bend are other keys'
        self.compare("ResetControllers", self.regs_after(
            cc(0, 7, 40), cc(0, 11, 60), cc(0, 121, 0)))

    def test_gm_reset_whatever_reset_controllers(self):
        """a GM reset sets the controllers back as RP-015 says with either
        value, though ESS's controller 121 keeps the modulation: the next
        note plays without the vibrato, as after a reset with none"""
        for value in ("1", "0"):
            with self.subTest(ResetControllers=value):
                e = self.started(ini={"ResetControllers": value})
                e.data(cc(0, 1, 100))                  # the deep vibrato
                e.data(0x7F3C90)
                e.longdata(GM_ON)
                self.assertEqual((e.dev8(DEV_GM_MOD), e.dev8(DEV_GM_VIB),
                                  e.dev16(DEV_GM_RPN)), (0, 0, 0x7F7F))
                e.data(0x7F4090)
                ref = self.started(ini={"ResetControllers": value})
                ref.longdata(GM_ON)
                ref.data(0x7F4090)
                got, want = ([r[0] for v in x.note_voices(0, 0x40)
                              for r in x.op_regs(v)] for x in (e, ref))
                self.assertTrue(got)
                self.assertEqual(got, want)

    def test_live_pan(self):
        self.compare("LivePan", self.regs_after(cc(0, 10, 0x7F), cc(0, 8, 0)))

    def test_true_sides(self):
        """a note panned hard left: register 6 bit 5 on the chip, its left
        output, where ESS's driver sets bit 4, the right. A patch's own
        side turns too: the note before the pan keeps the patch's sides,
        one operator on each. LivePan is off on both sides, since it would
        pan the sounding note as well."""
        scenario = self.regs_after(cc(0, 10, 0))
        stock = scenario(self.started("stock"))
        off = scenario(self.started(ini=self.off("TrueSides", "LivePan")))
        on = scenario(self.started(ini={"BetterSquareWave": "0",
                                        "LivePan": "0"}))
        self.assertEqual(off, stock)
        diffs = [(i, a, b) for i, (a, b) in enumerate(zip(stock, on))
                 if a != b]
        for i, a, b in diffs:
            # an operator's register 6, one side swapped for the other
            self.assertEqual((i < 0x240, i & 7, a ^ b), (True, 6, 0x30), i)
            self.assertIn(a & 0x30, (0x10, 0x20), i)
        # the new note's voices: all on the left, where ESS's are right
        left = [(a & 0x30, b & 0x30) for i, a, b in diffs
                if (i >> 5) in (12, 13)]
        self.assertEqual(len(left), 8)
        self.assertEqual(set(left), {(0x10, 0x20)})
        # the note before the pan: the patch's own sides, swapped
        self.assertIn((0x20, 0x10), [(a & 0x30, b & 0x30)
                                     for i, a, b in diffs])

    def test_sysex(self):
        def reset_with_pedal(e):
            e.data(0x05C0)
            e.data(SUSTAIN_ON)
            e.data(NOTE_A_ON)
            e.data(NOTE_A_OFF)
            e.longdata(GM_ON)
            e.longdata(master_volume(0x1000))
            return e.pedals(), e.keyed_voices(), e.dev8(DEV_PROGRAM)
        stock, on = self.compare("SysEx", reset_with_pedal)
        self.assertEqual(stock[2], 5)
        self.assertEqual(on, ([], [], 0))

    def test_running_status(self):
        def long_running_status(e):
            e.data(0x7F3C90)
            e.data(0x7F4090)
            e.longdata(bytes([0x80, 0x3C, 0x00, 0x40, 0x00]))
            e.data(NOTE_A_ON)
            e.data(0xF8)
            e.data(0x003C)
            e.longdata(bytes([0x90, 0x45]))
            e.longdata(bytes([0x7F, 0x47, 0x7F]))
            return sorted({n for f, c, n in e.voices() if f & 1})
        self.compare("RunningStatus", long_running_status)

    def test_better_square_wave(self):
        e = self.started(ini={"BetterSquareWave": "0"}, open_it=False)
        self.assertEqual(e.bank(len(e.ess_bank_res)), e.ess_bank_res)
        e.open()                                # no Bank=: the same bank
        self.assertEqual(e.bank(len(e.ess_bank_res)), e.ess_bank_res)
        self.assertNotEqual(e.ess_bank_res, e.bank_res)
        with open(build_esfm.ESS_BANK, "rb") as f:
            self.assertEqual(e.ess_bank_res, f.read())
        e = self.started(open_it=False)
        self.assertEqual(e.bank(len(e.bank_res)), e.bank_res)
        self.assertEqual(e.freed_locked, [])

    # -- all of them at 0 --------------------------------------------------------

    def test_all_off_is_ess_driver(self):
        """a song's worth of messages: every port write, callback and
        answer as ESS's driver, with every key at 0"""
        def song(e):
            out = []
            for m in (0x05C0, 0x7F3C90, 0x7F4090, SUSTAIN_ON, 0x003C80,
                      cc(0, 1, 90), 0x50D0, *rpn(0, 1, 0x30),
                      *rpn(0, 0, 4, 20), 0x7000E0, cc(0, 10, 0x10),
                      cc(0, 8, 0x70), 0x7F4391, 0x0341C1, 0x7F4891,
                      cc(0, 121, 0), 0xF8, 0x0040, 0x003C90,
                      cc(0, 7, 30), 0x7F4590):
                out.append(e.data(m))
            for data in (bytes([0x90, 0x30, 0x7F, 0x32, 0x7F]),
                         bytes([0x33]), bytes([0x7F, 0xF8, 0x30, 0x00]),
                         GM_ON, master_volume(0x2000),
                         bytes([0x91, 0x3C, 0x7F])):
                out.append(e.longdata(data))
            out.append(e.data(SUSTAIN_ON))
            out.append(e.data(0x7F4C90))
            out.append(e.reset())
            out.append(e.data(SUSTAIN_ON))
            out.append(e.data(0x7F4C90))
            out.append(e.data(0x004C80))
            out.append(e.close())
            return (out, [(off, v) for _d, off, v in e.chip.events],
                    e.callbacks, e.keyed_voices())
        stock = song(self.started("stock"))
        fixed = song(self.started(ini={k: "0" for k in SETTINGS}))
        self.assertEqual(fixed[0], stock[0])
        self.assertEqual(fixed[1], stock[1])
        self.assertEqual(fixed[2], stock[2])
        self.assertEqual(fixed[3], stock[3])
        self.assertTrue(stock[3])               # ESS's close left notes on
        # and the defaults differ
        self.assertNotEqual(song(self.started(ini={"BetterSquareWave": "0"})),
                            stock)


# the OPL3 device (esfmopl.asm): device fields, esfmdev.inc
DEV_OPL_CLIENT, DEV_OPL_READY = 0x438, 0x43A
OPL_HAS, OPL_ENGINE = 1, 2      # DEV_OPL_READY: the chip, General MIDI's setup
# an AdLib program's note on channel 0, OPL2 style: C0h without the OPL3's
# output bits
ADLIB_NOTE = ((0x20, 0x01), (0x23, 0x01), (0x40, 0x10), (0x43, 0x00),
              (0x60, 0xF0), (0x63, 0xF0), (0x80, 0x77), (0x83, 0x77),
              (0xA0, 0x98), (0xC0, 0x00), (0xB0, 0x31))
DEV_OPL_CHAN, DEV_OPL_VOICE, OV_SIZE = 0x44F, 0x54F, 16
GENMIDI = os.path.join(ROOT, "third_party", "genmidi", "GENMIDI.lmp")
OPTAB = (0, 1, 2, 8, 9, 10, 16, 17, 18)
FM_HZ = 49612.5                 # the chip's FM rate, the master / 480
MOM_OPEN = 0x3C7
DRVM_INIT, DRVM_ENABLE = 0x64, 0x67


def genmidi(d=None):
    """Freedoom's GENMIDI, or the lump d: 175 instruments, each {flags,
    fine, fixed, voices: [{mod, fb, car, offset}] * 2}"""
    if d is None:
        with open(GENMIDI, "rb") as f:
            d = f.read()
    out = []
    for i in range(175):
        r = d[8 + 36 * i:8 + 36 * (i + 1)]
        flags, fine, fixed = struct.unpack_from("<HBB", r)
        voices = [{"mod": r[o:o + 6], "fb": r[o + 6], "car": r[o + 7:o + 13],
                   "offset": struct.unpack_from("<h", r, o + 14)[0]}
                  for o in (4, 20)]
        out.append({"flags": flags, "fine": fine, "fixed": fixed,
                    "voices": voices})
    return out


def att(x):
    """the driver's attenuation for a velocity or controller: 40 log10 in
    0.75 dB steps"""
    if x == 0:
        return 63
    return min(63, round(-40 * math.log10(x / 127) / 0.75))


def opl_hz(chip, v):
    """the frequency OPL3 voice v (bank * 9 + channel) plays"""
    b, c = 0x100 * (v // 9), v % 9
    f = chip.opl[b + 0xA0 + c] | (chip.opl[b + 0xB0 + c] & 3) << 8
    return f * FM_HZ / 2 ** (20 - ((chip.opl[b + 0xB0 + c] >> 2) & 7))


def cents(hz, note):
    return 1200 * math.log2(hz / (440 * 2 ** ((note - 69) / 12)))


@unittest.skipUnless(HAVE_UNICORN, "needs the unicorn module")
class Opl3Test(unittest.TestCase):
    """Device 1, General MIDI on the chip's OPL3 compatibility mode with
    Freedoom's GENMIDI instruments (src/esfm/esfmopl.asm)."""

    @classmethod
    def setUpClass(cls):
        cls.bank = genmidi()
        cls.single = [p for p in range(128) if cls.bank[p]["flags"] == 0]
        cls.double = [p for p in range(128) if cls.bank[p]["flags"] == 4]

    def enabled(self, which="fixed", ini=None, opl3=False):
        """the driver after DRV_ENABLE and the devnode's arrival, and with
        opl3 ESFMOPL3.DRV loaded next to it"""
        e = ESFMEmu(builds()[which])
        for key, value in (ini or {}).items():
            e.ini[("esfm.drv", key.lower())] = value
        if opl3:
            e.load_opl3(builds()["opl3"])
        self.assertEqual(e.driverproc(DRV_ENABLE) & 0xFFFF, 1)
        for msg in (DRVM_INIT, DRVM_ENABLE):
            self.assertEqual(e.modmessage(msg, 0, esfmemu.DEVNODE, user=0,
                                          device=0), 0)
        e.dev = e.r16(4, 0x3C)
        return e

    def emu(self, msgs=(), ini=None):
        """device 1 open, and msgs sent"""
        e = self.enabled(ini=ini)
        self.assertEqual(e.try_open(1), 0)
        for m in msgs:
            self.assertEqual(e.data(m), 0)
        return e

    def table(self, e):
        """the device's voices: (flags, channel, key, instrument voice)"""
        out = []
        for v in range(18):
            b = DEV_OPL_VOICE + OV_SIZE * v
            out.append((e.dev8(b), e.dev8(b + 1), e.dev8(b + 2),
                        e.dev16(b + 4)))
        return out

    def keyed(self, e, channel=None, key=None):
        """OPL3 voices keyed on in the chip, for channel and key in the
        driver's table"""
        t = self.table(e)
        return [v for v in e.chip.opl_keyed()
                if (channel is None or t[v][1] == channel) and
                (key is None or t[v][2] == key)]

    def program(self, e, writes):
        """the program that has the device open writes the chip itself, as
        an OPL3 (register, value), bit 8 the second bank: Windows owns FM
        then, and ES1869.VXD traps none of the FM ports for it"""
        for reg, value in writes:
            port = e.chip.base + (2 if reg & 0x100 else 0)
            e.chip.out(port, reg & 0xFF, 0)
            e.chip.out(port + 1, value, 0)

    def regs(self, e, v):
        """voice v's registers: the modulator's and the carrier's 20h,
        60h, 80h, E0h and 40h, and C0h"""
        b, c = 0x100 * (v // 9), v % 9
        op = OPTAB[c]
        one = [e.chip.opl[b + r + op] for r in (0x20, 0x60, 0x80, 0xE0,
                                                0x40)]
        two = [e.chip.opl[b + r + op + 3] for r in (0x20, 0x60, 0x80,
                                                    0xE0, 0x40)]
        return one, two, e.chip.opl[b + 0xC0 + c]

    # -- the devices -------------------------------------------------------

    def test_two_devices(self):
        self.assertEqual(self.enabled().getnumdevs(), 2)
        self.assertEqual(self.enabled(ini={"Opl3Device": "0"}).getnumdevs(),
                         1)
        self.assertEqual(self.enabled("stock").getnumdevs(), 1)
        # Opl3Device=2 with ESFMOPL3.DRV loaded: it counts device 1, which
        # answers here
        e = self.enabled(ini={"Opl3Device": "2"}, opl3=True)
        self.assertEqual(e.getnumdevs(), 1)
        r, caps = e.devcaps(1)
        self.assertEqual((r, caps[6:38].split(b"\0")[0]),
                         (0, b"OPL3 FM Synthesis (388)"))
        self.assertEqual(e.try_open(1), 0)
        self.assertEqual(e.data(0x7F3C90), 0)
        self.assertTrue(e.chip.opl_keyed())
        # without it at the first count, device 1 stays this driver's for
        # the session, so that it is never missing
        e = self.enabled(ini={"Opl3Device": "2"})
        self.assertEqual(e.getnumdevs(), 2)
        self.assertEqual(e.fix_state()["opts"] & OPT_OPL3_DRV, 0)
        e.load_opl3(builds()["opl3"])
        self.assertEqual(e.getnumdevs(), 2)

    def test_count_reads_the_settings(self):
        # MMSYSTEM may count the devices before DRV_ENABLE
        e = ESFMEmu(builds()["fixed"])
        e.ini[("esfm.drv", "opl3device")] = "2"
        e.load_opl3(builds()["opl3"])
        for msg in (DRVM_INIT, DRVM_ENABLE):
            self.assertEqual(e.modmessage(msg, 0, esfmemu.DEVNODE, user=0,
                                          device=0), 0)
        self.assertEqual(e.getnumdevs(), 1)
        self.assertEqual(e.fix_state()["opts"],
                         OPT_DEFAULT | OPT_OPL3_DRV | OPT_READ)

    def test_caps(self):
        e = self.enabled()
        r, caps = e.devcaps(1)
        self.assertEqual(r, 0)
        self.assertEqual(caps[6:38].split(b"\0")[0], b"OPL3 FM Synthesis (388)")
        tech, voices, notes, mask = struct.unpack_from("<4H", caps, 38)
        self.assertEqual((tech, voices, notes, mask), (4, 18, 18, 0xFFFF))
        r, caps0 = e.devcaps(0)
        self.assertEqual(caps0[6:38].split(b"\0")[0], b"ESFM Synthesis (388)")
        self.assertEqual(caps0[:6] + caps0[38:], caps[:6] + caps[38:])
        # a short structure takes what fits, and nothing past it
        r, caps = e.devcaps(1, size=11)
        self.assertEqual((r, caps[6:11], caps[11:12]), (0, b"OPL3 ", b"\xEE"))
        r, caps = e.devcaps(1, size=4)
        self.assertEqual((r, caps[4:8]), (0, b"\xEE" * 4))
        e = self.enabled(ini={"Opl3Device": "0"})
        self.assertEqual(e.devcaps(1)[0], 2)            # BADDEVICEID
        self.assertEqual(e.try_open(1), 2)

    def test_open_and_close(self):
        e = self.emu()
        # out of native mode, and as an OPL3 after a reset: OPL2 mode, the
        # timers masked, no rhythm, every key off and every operator silent
        self.assertFalse(e.chip.native)
        self.assertEqual([e.chip.opl[r] for r in (0x105, 0x104, 0x004,
                                                  0x0BD, 0x001, 0x008)],
                         [0, 0, 0x60, 0, 0, 0])
        self.assertEqual(e.chip.opl_keyed(), [])
        for v in range(18):
            mod, car, _c0 = self.regs(e, v)
            self.assertEqual((mod[4], car[4]), (0x3F, 0x3F))
        self.assertEqual(e.dev8(DEV_OPL_READY), OPL_HAS)
        self.assertEqual(e.dev16(DEV_OPL_CLIENT), e.client & 0xFFFF)
        self.assertEqual([c[0] for c in e.callbacks], [MOM_OPEN])
        # the first MIDI message sets the OPL3 up for General MIDI
        self.assertEqual(e.data(0x7F3C90), 0)
        self.assertEqual(e.dev8(DEV_OPL_READY), OPL_HAS | OPL_ENGINE)
        self.assertEqual(e.chip.opl[0x105], 1)
        self.assertEqual(e.close(), 0)
        self.assertTrue(e.chip.native)                  # ESS's chip_reset
        self.assertEqual((e.dev8(DEV_OPL_READY), e.dev16(DEV_OPL_CLIENT)),
                         (0, 0))
        self.assertEqual([c[0] for c in e.callbacks], [MOM_OPEN, MOM_CLOSE])
        self.assertEqual(e.chip.opl_splits, [])

    def test_one_device_at_a_time(self):
        e = self.emu()
        self.assertEqual(e.try_open(0), 4)              # MMSYSERR_ALLOCATED
        self.assertEqual(e.close(), 0)
        self.assertEqual(e.try_open(0), 0)
        self.assertEqual(e.try_open(1), 4)
        self.assertTrue(e.chip.native)

    def test_device_0_after_device_1(self):
        """ESFM native mode plays as before once device 1 closed"""
        def play(e):
            for m in (0x05C0, 0x7F3C90, 0x7F4090):
                self.assertEqual(e.data(m), 0)
            return e.keyed_voices(), [e.op_regs(v) for v in range(18)]
        e = self.emu([0x7F3C90])
        e.close()
        e.try_open(0)
        fresh = self.enabled()
        fresh.try_open(0)
        self.assertEqual(play(e), play(fresh))

    # -- notes ---------------------------------------------------------------

    def check_note(self, e, program, key, vel=127, vol=100, pan=0x30,
                   bank=None):
        ins = (bank or self.bank)[program]
        voices = self.keyed(e, 0, key)
        self.assertEqual(len(voices), 2 if ins["flags"] & 4 else 1)
        for v in voices:
            sub = self.table(e)[v][3] & 1
            gv = ins["voices"][sub]
            mod, car, c0 = self.regs(e, v)
            a = min(63, att(vel) + att(vol) + att(127))
            self.assertEqual(mod[:4], list(gv["mod"][:4]))
            self.assertEqual(car[:4], list(gv["car"][:4]))
            self.assertEqual(car[4], gv["car"][4] & 0xC0 | a)
            mod_tl = gv["mod"][5]
            if gv["fb"] & 1 and mod_tl != 0x3F:
                mod_tl = max(mod_tl, a)
            self.assertEqual(mod[4], gv["mod"][4] & 0xC0 | mod_tl)
            self.assertEqual(c0, gv["fb"] & 0x0F | pan)
            note = key + gv["offset"] + 12
            while note < 12:
                note += 12
            while note > 107:
                note -= 12
            want = note + ((ins["fine"] // 2 - 64) / 32 if sub else 0)
            self.assertLess(abs(cents(opl_hz(e.chip, v), want)), 4)
        return voices

    def test_note_on(self):
        for program in (self.single[0], self.double[0], 0, 40, 73):
            with self.subTest(program=program):
                e = self.emu([program << 8 | 0xC0, 0x7F3C90])
                self.check_note(e, program, 60)
                e = self.emu([program << 8 | 0xC0, 0x404590])
                self.check_note(e, program, 69, vel=0x40)

    def test_volume_and_expression(self):
        p = self.single[0]
        e = self.emu([p << 8 | 0xC0, 0x7F3C90, cc(0, 7, 64)])
        self.check_note(e, p, 60, vol=64)
        e.data(cc(0, 11, 32))
        v = self.keyed(e, 0, 60)[0]
        car = self.regs(e, v)[1]
        self.assertEqual(car[4] & 0x3F, min(63, att(127) + att(64) + att(32)))

    def test_pan(self):
        p = self.single[0]
        for value, sides in ((0, 0x20), (47, 0x20), (48, 0x30), (80, 0x30),
                             (81, 0x10), (127, 0x10)):
            with self.subTest(value=value):
                e = self.emu([p << 8 | 0xC0, cc(0, 10, value), 0x7F3C90])
                self.check_note(e, p, 60, pan=sides)
        # ESS's sides with TrueSides=0, as on an OPL3; and sounding notes move
        e = self.emu([p << 8 | 0xC0, 0x7F3C90, cc(0, 10, 0)],
                     ini={"TrueSides": "0"})
        self.check_note(e, p, 60, pan=0x10)

    def test_note_off_and_pedal(self):
        e = self.emu([0x7F3C90, 0x003C80])
        self.assertEqual(self.keyed(e), [])
        e = self.emu([SUSTAIN_ON, 0x7F3C90, 0x003C80])
        self.assertTrue(self.keyed(e, 0, 60))
        e.data(0x0040B0)
        self.assertEqual(self.keyed(e), [])

    def test_program_change_lets_the_pedal_up(self):
        # a pedal left down from an earlier part: the next program change
        # lets it up, and the notes it held end, as on device 0
        # (PedalRelease); keys still down keep sounding
        p = self.single[0]
        e = self.emu([SUSTAIN_ON, 0x7F3C90, 0x003C80, 0x7F4090,
                      cc(1, 64, 127), 0x7F3C91, 0x003C81])
        self.assertTrue(self.keyed(e, 0, 60))
        e.data(p << 8 | 0xC0)
        self.assertEqual(self.keyed(e, 0, 60), [])
        self.assertTrue(self.keyed(e, 0, 64))           # its key is down
        self.assertTrue(self.keyed(e, 1, 60))           # another channel's
        self.assertEqual(e.dev8(DEV_OPL_CHAN + 4), 0)   # the pedal up
        e.data(0x7F4290)                                # a new note
        e.data(0x004280)
        self.assertEqual(self.keyed(e, 0, 0x42), [])    # not held
        # without a pedal, a program change keeps every note
        e = self.emu([0x7F3C90, p << 8 | 0xC0])
        self.assertTrue(self.keyed(e, 0, 60))
        # PedalRelease=0: the pedal stays down, as on an OPL3 card
        e = self.emu([SUSTAIN_ON, 0x7F3C90, 0x003C80, p << 8 | 0xC0],
                     ini={"PedalRelease": "0"})
        self.assertTrue(self.keyed(e, 0, 60))
        self.assertEqual(e.dev8(DEV_OPL_CHAN + 4), 1)

    # -- the bank file, Opl3Bank= (esfmfile.asm) -----------------------------

    LUMP = r"C:\BANKS\DOOM.OP2"

    def lump(self, mark, data=None):
        """Freedoom's GENMIDI with each instrument's first modulator 20h
        set to mark, and its parse"""
        if data is None:
            with open(GENMIDI, "rb") as f:
                data = f.read()
        d = bytearray(data)
        for i in range(175):
            d[8 + 36 * i + 4] = mark
        return bytes(d), genmidi(bytes(d))

    def bank_emu(self, files, key=LUMP, ini=None):
        """the driver enabled with Opl3Bank=key (None: no key) and files"""
        e = self.enabled(ini=ini)
        e.files.update({k.upper(): v for k, v in files.items()})
        if key is not None:
            e.ini[("esfm.drv", "opl3bank")] = key
        return e

    def blk(self, e):
        """the block of the OPL3 bank that plays, 0 for the built-in one"""
        return e.fix_state()["oblk"]

    def test_bank_file(self):
        data, bank = self.lump(0x21)
        e = self.bank_emu({self.LUMP: data})
        self.assertEqual(e.opens, [])                   # not at enable
        self.assertEqual(e.try_open(1), 0)
        st = e.fix_state()
        self.assertEqual((st["ostate"], st["osrc"], st["oloads"],
                          st["ochecks"], st["opath"]),
                         (BS_LOADED, 1, 1, 1, self.LUMP))
        self.assertIn(("SYSTEM.INI", "ESFM.DRV", "Opl3Bank"), e.ini_reads)
        blk = self.blk(e)
        self.assertEqual(e.plocks.get(blk), 1)          # read at interrupts
        p = self.double[0]
        for m in (p << 8 | 0xC0, 0x7F3C90):
            self.assertEqual(e.data(m), 0)
        self.check_note(e, p, 60, bank=bank)
        self.assertEqual(e.close(), 0)
        # the same file again: checked, not read
        self.assertEqual(e.try_open(1), 0)
        st = e.fix_state()
        self.assertEqual((st["oloads"], st["ochecks"]), (1, 2))
        self.assertEqual(self.blk(e), blk)
        self.assertEqual(e.close(), 0)
        # saved again: read, and the old block freed, unlocked first
        data2, bank2 = self.lump(0x22)
        e.files[self.LUMP.upper()] = data2
        e.touch(self.LUMP.upper())
        self.assertEqual(e.try_open(1), 0)
        self.assertEqual(e.fix_state()["oloads"], 2)
        self.assertNotIn(blk, e.gblocks)
        for m in (p << 8 | 0xC0, 0x7F3C90):
            self.assertEqual(e.data(m), 0)
        self.check_note(e, p, 60, bank=bank2)
        self.assertEqual(e.close(), 0)
        # the key gone: Freedoom's lump, and the file's block freed
        blk = self.blk(e)
        del e.ini[("esfm.drv", "opl3bank")]
        self.assertEqual(e.try_open(1), 0)
        st = e.fix_state()
        self.assertEqual((st["ostate"], st["osrc"], self.blk(e)),
                         (BS_NONE, 0, 0))
        self.assertNotIn(blk, e.gblocks)
        for m in (p << 8 | 0xC0, 0x7F3C90):
            self.assertEqual(e.data(m), 0)
        self.check_note(e, p, 60)
        self.assertEqual(e.close(), 0)
        self.assertEqual((e.freed_locked, e.stale, e.handles), ([], [], {}))

    def test_bank_file_names(self):
        # the lump of a WAD keeps its names after the instruments, and a
        # file without them plays all the same
        data, bank = self.lump(0x23)
        for size in (len(data), 8 + 175 * 36):
            with self.subTest(size=size):
                e = self.bank_emu({self.LUMP: data[:size]})
                self.assertEqual(e.try_open(1), 0)
                self.assertEqual(e.fix_state()["ostate"], BS_LOADED)
                p = self.single[0]
                for m in (p << 8 | 0xC0, 0x7F3C90):
                    self.assertEqual(e.data(m), 0)
                self.check_note(e, p, 60, bank=bank)

    def test_bank_file_refused(self):
        # a file that isn't a GENMIDI lump, or can't be read: Freedoom's
        # lump plays, and a bad file isn't read again until it changes
        data, _bank = self.lump(0x24)
        for name, files, state in (
                ("missing", {}, BS_MISSING),
                ("header", {self.LUMP: b"#OPL_IX#" + data[8:]}, BS_BAD),
                ("short", {self.LUMP: data[:8 + 174 * 36]}, BS_BAD),
                ("tiny", {self.LUMP: data[:100]}, BS_BAD)):
            with self.subTest(name):
                e = self.bank_emu(files)
                self.assertEqual(e.try_open(1), 0)
                st = e.fix_state()
                self.assertEqual((st["ostate"], st["osrc"], self.blk(e)),
                                 (state, 0, 0))
                p = self.single[0]
                for m in (p << 8 | 0xC0, 0x7F3C90):
                    self.assertEqual(e.data(m), 0)
                self.check_note(e, p, 60)
                self.assertEqual(e.close(), 0)
                self.assertEqual(e.try_open(1), 0)
                st = e.fix_state()
                self.assertEqual((st["ostate"], st["oloads"], st["ochecks"]),
                                 (state, 0, 2))
                self.assertEqual((e.freed_locked, e.stale, e.handles),
                                 ([], [], {}))

    def test_bank_file_only_for_device_1(self):
        # device 0's open reads Bank= only, and Opl3Device=0 reads neither
        # the key nor the file
        data, _bank = self.lump(0x25)
        e = self.bank_emu({self.LUMP: data})
        self.assertEqual(e.try_open(0), 0)
        self.assertNotIn(("SYSTEM.INI", "ESFM.DRV", "Opl3Bank"), e.ini_reads)
        self.assertEqual(e.fix_state()["ochecks"], 0)
        e = self.bank_emu({self.LUMP: data}, ini={"Opl3Device": "0"})
        self.assertEqual(e.try_open(1), 2)              # BADDEVICEID
        self.assertEqual(e.fix_state()["ochecks"], 0)

    def test_bank_file_and_enable(self):
        # DRV_ENABLE puts Freedoom's lump back, and the next open reads
        # the file again
        data, bank = self.lump(0x26)
        e = self.bank_emu({self.LUMP: data})
        self.assertEqual(e.try_open(1), 0)
        blk = self.blk(e)
        self.assertEqual(e.close(), 0)
        self.assertEqual(e.driverproc(DRV_ENABLE) & 0xFFFF, 1)
        st = e.fix_state()
        self.assertEqual((st["osrc"], self.blk(e)), (0, 0))
        self.assertNotIn(blk, e.gblocks)
        self.assertEqual(e.try_open(1), 0)
        self.assertEqual(e.fix_state()["oloads"], 2)
        p = self.single[0]
        for m in (p << 8 | 0xC0, 0x7F3C90):
            self.assertEqual(e.data(m), 0)
        self.check_note(e, p, 60, bank=bank)
        self.assertEqual((e.freed_locked, e.stale), ([], []))

    def test_bend(self):
        p = self.single[0]
        off = self.bank[p]["voices"][0]["offset"] + 12
        e = self.emu([p << 8 | 0xC0, 0x7F3C90, 0x7F7FE0])
        v = self.keyed(e)[0]
        self.assertLess(abs(cents(opl_hz(e.chip, v),
                                  60 + off + 2 * 8191 / 8192)), 4)
        for m in rpn(0, 0, 12):
            e.data(m)
        self.assertLess(abs(cents(opl_hz(e.chip, v),
                                  60 + off + 12 * 8191 / 8192)), 4)
        e.data(0x0000E0)
        self.assertLess(abs(cents(opl_hz(e.chip, v), 60 + off - 12)), 4)

    def test_drums(self):
        e = self.emu([0x7F2499])                        # 36 on channel 10
        voices = self.keyed(e, 9, 36)
        ins = self.bank[128 + 36 - 35]
        self.assertTrue(ins["flags"] & 1)               # a fixed pitch
        self.assertEqual(len(voices), 2 if ins["flags"] & 4 else 1)
        self.assertEqual(self.table(e)[voices[0]][3] >> 1, 129)
        self.assertLess(abs(cents(opl_hz(e.chip, voices[0]),
                                  ins["fixed"] + 12)), 4)
        e = self.emu([0x7F2299, 0x7F5299])              # 34 and 82: none
        self.assertEqual(self.keyed(e), [])

    def test_eighteen_voices(self):
        p = self.single[0]
        e = self.emu([p << 8 | 0xC0] +
                     [0x7F0090 | (40 + k) << 8 for k in range(20)])
        keys = sorted(self.table(e)[v][2] for v in self.keyed(e))
        self.assertEqual(keys, list(range(42, 60)))     # the oldest went

    def test_running_status(self):
        e = self.emu([0x7F3C90, 0x7F40, 0xF8, 0x7F43])
        self.assertEqual(sorted({self.table(e)[v][2] for v in self.keyed(e)}),
                         [60, 64, 67])

    def test_long_messages(self):
        e = self.emu([0x0028C1])                        # channel 2: 40
        r, flags = e.longdata(bytes([0x90, 0x3C, 0x7F, 0x40, 0x7F]))
        self.assertEqual((r, flags & MHDR_DONE), (0, MHDR_DONE))
        self.assertEqual(e.callbacks[-1][0], MOM_DONE)
        self.assertEqual(len({self.table(e)[v][2] for v in self.keyed(e)}),
                         2)
        # a GM reset split between buffers sets the channels back
        e.longdata(bytes([0xF0, 0x7E, 0x7F, 0x09]))
        self.assertTrue(self.keyed(e))
        e.longdata(bytes([0x01, 0xF7]))
        self.assertEqual(self.keyed(e), [])
        self.assertEqual(e.dev8(DEV_OPL_CHAN + 16), 0)  # channel 2's program
        r, flags = e.longdata(bytes([0x91, 0x3C, 0x7F]))
        self.assertEqual(r, 0)

    def test_message_from_an_interrupt(self):
        """a note off from an interrupt in the middle of a note on waits in
        the queue, comes back as device 0's and goes to device 1"""
        p = self.single[0]
        e = self.emu([p << 8 | 0xC0, 0x7F3C90])
        inj = Inject(MODM_DATA, dw1=0x003C80, after_writes=e.chip.writes + 5)
        self.assertEqual(e.data(0x7F4090, injects=[inj]), 0)
        self.assertEqual(inj.result, 0)
        self.assertEqual(sorted(self.table(e)[v][2] for v in self.keyed(e)),
                         [64])
        self.assertEqual(e.chip.opl_splits, [])
        self.assertEqual(e.fix_state()["lock"], 0)

    def test_reset(self):
        e = self.emu([SUSTAIN_ON, 0x7F3C90, 0x7F4091])
        self.assertEqual(e.reset(), 0)
        self.assertEqual(self.keyed(e), [])
        self.assertEqual(e.dev8(DEV_OPL_CHAN + 4), 0)  # the pedal up

    def test_suspend_and_resume(self):
        e = self.emu([SUSTAIN_ON, 0x7F3C90, 0x003C80])
        e.driverproc(DRV_POWER, PWR_SUSPENDREQUEST)
        self.assertEqual(self.keyed(e), [])
        self.assertFalse(e.chip.native)                 # nothing of ESS's
        writes = e.chip.writes
        self.assertEqual(e.data(0x7F3C90), 3)           # not enabled
        self.assertEqual(e.chip.writes, writes)
        self.assertEqual(e.driverproc(DRV_POWER, PWR_SUSPENDRESUME), 1)
        self.assertTrue(e.chip.native)                  # ESS's chip_reset
        self.assertEqual(e.data(0x7F3C90), 0)           # OPL3 mode again
        self.assertFalse(e.chip.native)
        self.assertTrue(self.keyed(e, 0, 60))
        e.close()
        self.assertTrue(e.chip.native)

    # -- a program that writes the chip itself -------------------------------

    def test_direct_access(self):
        """the program that opened the device writes the chip itself, as an
        OPL3: its registers stay as it wrote them, the device writes nothing
        until a MIDI message comes, and the close cuts its note"""
        e = self.emu()
        writes = e.chip.writes
        self.program(e, ADLIB_NOTE)
        self.assertEqual(e.chip.writes, writes + 2 * len(ADLIB_NOTE))
        for reg, value in ADLIB_NOTE:
            self.assertEqual(e.chip.opl[reg], value, hex(reg))
        self.assertEqual(e.chip.opl_keyed(), [0])
        self.assertEqual(e.chip.opl[0x105], 0)          # OPL2 mode still
        # messages that aren't MIDI write nothing, and ESS's code neither
        writes = e.chip.writes
        self.assertEqual(e.devcaps(1)[0], 0)
        e.call((esfmemu.SEG_PARA[1], 0x10), [e.dev, 0x240, 1])
        self.assertEqual(e.chip.writes, writes)
        self.assertEqual(e.chip.opl_keyed(), [0])
        self.assertEqual(e.close(), 0)
        self.assertEqual(e.chip.opl_keyed(), [])
        self.assertEqual(e.chip.opl[0x43], 0x3F)
        self.assertTrue(e.chip.native)                  # ESS's chip again
        self.assertEqual(e.chip.opl_splits, [])

    def test_direct_rhythm_and_suspend(self):
        """a power suspend keys off the program's notes in both banks and
        its rhythm, and writes nothing of ESS's"""
        e = self.emu()
        self.program(e, ADLIB_NOTE + ((0x105, 1), (0x1B0, 0x31),
                                      (0xBD, 0x30)))
        self.assertEqual(e.chip.opl_keyed(), [0, 9])
        e.driverproc(DRV_POWER, PWR_SUSPENDREQUEST)
        self.assertEqual(e.chip.opl_keyed(), [])
        self.assertEqual(e.chip.opl[0xBD], 0)
        self.assertFalse(e.chip.native)
        self.assertEqual(e.driverproc(DRV_POWER, PWR_SUSPENDRESUME), 1)
        self.assertEqual(e.close(), 0)
        self.assertTrue(e.chip.native)

    def test_midi_after_direct_access(self):
        """a MIDI message after the program's own writes sets the OPL3 up
        for General MIDI, which keys the program's note off and plays"""
        e = self.emu()
        self.program(e, ADLIB_NOTE)
        self.assertEqual(e.data(0x7F3C90), 0)
        self.assertEqual(e.chip.opl[0x105], 1)
        self.check_note(e, 0, 60)
        self.assertEqual(sorted(e.chip.opl_keyed()),
                         sorted(self.keyed(e, 0, 60)))

    def test_ess_writes_dropped(self):
        """fm_write writes nothing while device 1 has the chip, from its
        open on"""
        e = self.emu()
        writes = e.chip.writes
        e.call((esfmemu.SEG_PARA[1], 0x10), [e.dev, 0x240, 1])
        self.assertEqual(e.chip.writes, writes)
        self.assertEqual(e.data(0x7F3C90), 0)
        writes = e.chip.writes
        e.call((esfmemu.SEG_PARA[1], 0x10), [e.dev, 0x240, 1])
        self.assertEqual(e.chip.writes, writes)
        e.close()
        writes = e.chip.writes
        e.call((esfmemu.SEG_PARA[1], 0x10), [e.dev, 0x240, 1])
        self.assertEqual(e.chip.writes, writes + 3)
        self.assertEqual(e.chip.regs[0x240], 1)

    def test_tables(self):
        """the F-number table is the formula's at 49,612.5 Hz"""
        with open(os.path.join(ROOT, "src", "esfm", "esfmopl.asm")) as f:
            src = f.read()
        line = src[src.index("opl_fnum:"):].split("\n")[0]
        got = [int(x) for x in line.split("dw")[1].split(",")]
        want = [round(440 * 2 ** ((19 + i - 69) / 12) * 2 ** 20 / FM_HZ)
                for i in range(13)]
        self.assertEqual(got, want)
        body = src[src.index("opl_att:"):src.index("opl_name:")]
        got = [int(x) for x in body.replace("opl_att:", "").replace(
            "db", ",").replace("\n", ",").split(",") if x.strip() and
            not x.strip().startswith(";")]
        self.assertEqual(got[:128], [att(x) for x in range(128)])


DRV_LOAD, DRV_OPEN, DRV_CLOSE, DRV_FREE = 1, 3, 4, 6
DRV_CONFIGURE, DRV_QUERYCONFIGURE, DRV_INSTALL, DRV_REMOVE = 7, 8, 9, 10
MODM_GETNUMDEVS = 1


@unittest.skipUnless(HAVE_UNICORN, "needs the unicorn module")
class ConfigureTest(unittest.TestCase):
    """The Multimedia control panel's Settings button of the driver:
    DRV_QUERYCONFIGURE and DRV_CONFIGURE open ESFMCFG.DLL's box of the
    patch banks for device 0 (src/esfm/esfmcfg.asm)"""

    def enabled(self, which="fixed", ini=None):
        e = ESFMEmu(builds()[which])
        for key, value in (ini or {}).items():
            e.ini[("esfm.drv", key.lower())] = value
        self.assertEqual(e.driverproc(DRV_ENABLE) & 0xFFFF, 1)
        return e

    def test_query(self):
        # the button is there when the DLL can be found
        e = self.enabled()
        self.assertEqual(e.driverproc(DRV_QUERYCONFIGURE), 1)
        self.assertEqual(e.file_lookups, [("ESFMCFG.DLL", 0x4000)])
        e.cfg_dll = False
        self.assertEqual(e.driverproc(DRV_QUERYCONFIGURE), 0)
        self.assertEqual(e.libraries, {})

    def test_configure(self):
        # the box gets the panel's window and device 0, and the driver
        # answers DRVCNF_OK or DRVCNF_CANCEL as the box ended (ESS's
        # DRV_ENABLE code, which they go through, answers 1 or 0); the
        # library is freed after it
        for result, want in ((1, 1), (0, 0), (2, 1)):
            with self.subTest(result=result):
                e = self.enabled()
                e.cfg_result = result
                self.assertEqual(e.driverproc(DRV_CONFIGURE, 0x1234,
                                              0x56789ABC), want)
                self.assertEqual(e.cfg_calls, [(0x1234, 0)])
                self.assertEqual(e.libraries, {})
        # no DLL: DRVCNF_CANCEL, and no box
        e = self.enabled()
        e.cfg_dll = False
        self.assertEqual(e.driverproc(DRV_CONFIGURE, 0x1234), 0)
        self.assertEqual(e.cfg_calls, [])

    def test_bank_settings_0(self):
        # BankSettings=0: ESS's answers, 0 to both, and nothing looked for
        e = self.enabled(ini={"BankSettings": "0"})
        for msg in (DRV_QUERYCONFIGURE, DRV_CONFIGURE):
            self.assertEqual(e.driverproc(msg, 0x1234), 0)
        self.assertEqual((e.file_lookups, e.cfg_calls, e.libraries),
                         ([], [], {}))
        e = self.enabled("stock")
        for msg in (DRV_QUERYCONFIGURE, DRV_CONFIGURE):
            self.assertEqual(e.driverproc(msg, 0x1234), 0)
        self.assertEqual((e.file_lookups, e.cfg_calls), ([], []))

    def test_other_messages_stay(self):
        # the messages around them in ESS's table answer as before
        for which in ("fixed", "stock"):
            with self.subTest(which):
                e = self.enabled(which)
                for msg, want in ((DRV_OPEN, 1), (DRV_CLOSE, 1),
                                  (DRV_INSTALL, 1), (DRV_REMOVE, 0)):
                    self.assertEqual(e.driverproc(msg) & 0xFFFF, want, msg)


@unittest.skipUnless(HAVE_UNICORN, "needs the unicorn module")
class Opl3DriverTest(unittest.TestCase):
    """ESFMOPL3.DRV in front of ESFM.DRV's device 1: one device, whose
    messages go on to ESFM.DRV's modMessage as device 1's"""

    def pair(self, value="2", which="fixed", esfm=True, ini=None):
        """both drivers loaded and enabled, with Opl3Device=value (None:
        no key) and [ESFM.DRV] as in ini, and ESFM.DRV found by name if
        esfm"""
        e = ESFMEmu(builds()[which])
        if value is not None:
            e.ini[("esfm.drv", "opl3device")] = value
        for key, v in (ini or {}).items():
            e.ini[("esfm.drv", key.lower())] = v
        e.load_opl3(builds()["opl3"])
        e.esfm_loaded = esfm
        self.assertEqual(e.driverproc(DRV_ENABLE) & 0xFFFF, 1)
        for msg in (DRV_LOAD, DRV_ENABLE, DRV_OPEN):
            self.assertEqual(e.opl3_driverproc(msg), 1)
        # the devnode arrives: MMSYSTEM tells both drivers
        for msg in (DRVM_INIT, DRVM_ENABLE):
            self.assertEqual(e.modmessage(msg, 0, esfmemu.DEVNODE, user=0,
                                          device=0), 0)
            self.assertEqual(e.opl3_modmessage(msg, 0, esfmemu.DEVNODE), 0)
        e.dev = e.r16(4, 0x3C)
        return e

    def count(self, e, devnode=esfmemu.DEVNODE):
        return e.opl3_modmessage(MODM_GETNUMDEVS, devnode)

    def test_counts(self):
        # a program sees the OPL3 device once, ESFM.DRV's or this driver's
        for value, esfm, mine in (("2", 1, 1), ("1", 2, 0), (None, 2, 0),
                                  ("0", 1, 0)):
            with self.subTest(value=value):
                e = self.pair(value)
                self.assertEqual((e.getnumdevs(), self.count(e)),
                                 (esfm, mine))
        # another card's devnode, which ESFM.DRV doesn't have: none
        self.assertEqual(self.count(self.pair(), 0x5555), 0)
        # ESFM.DRV not loaded yet: the count all the same, and the open
        # finds no driver
        e = self.pair(esfm=False)
        self.assertEqual(self.count(e), 1)
        self.assertEqual(e.opl3_open(), 6)              # NODRIVER
        self.assertEqual(e.opl3_modmessage(MODM_DATA, 0x7F3C90), 6)

    def test_caps_and_play(self):
        e = self.pair()
        r, caps = e.opl3_devcaps()
        self.assertEqual((r, caps[6:38].split(b"\0")[0]),
                         (0, b"OPL3 FM Synthesis (388)"))
        self.assertEqual(e.opl3_devcaps(device=1)[0], 2)   # BADDEVICEID
        self.assertEqual(e.opl3_open(), 0)
        self.assertEqual(e.dev8(DEV_OPL_READY), OPL_HAS)
        self.assertEqual(e.dev16(DEV_OPL_CLIENT), e.client & 0xFFFF)
        self.assertEqual(e.opl3_data(0x7F3C90), 0)
        self.assertEqual(e.dev8(DEV_OPL_READY), OPL_HAS | OPL_ENGINE)
        self.assertTrue(e.chip.opl_keyed())
        self.assertEqual(e.try_open(0), 4)              # ALLOCATED
        self.assertEqual(e.opl3_close(), 0)
        self.assertTrue(e.chip.native)
        self.assertEqual(e.chip.opl_keyed(), [])
        self.assertEqual([c[0] for c in e.callbacks], [MOM_OPEN, MOM_CLOSE])
        # ESFM.DRV was looked up once, at the caps
        self.assertEqual(e.module_lookups, ["ESFM"])

    def test_loaded_after_the_count(self):
        # Windows counted ESFM.DRV's devices before it loaded this driver.
        # ES1869.VXD told Windows at the card's start that this driver is
        # there (040E answers 1): ESFM.DRV leaves device 1 to it all the
        # same. Otherwise, ESS's VxD (CF set) or 0: ESFM.DRV keeps device 1
        # and this driver has none, so a program sees the OPL3 device once
        for said, esfm, mine in ((None, 2, 0), (0, 2, 0), (1, 1, 1)):
            with self.subTest(said=said):
                e = ESFMEmu(builds()["fixed"])
                e.ini[("esfm.drv", "opl3device")] = "2"
                e.vxd_opl3 = said
                self.assertEqual(e.driverproc(DRV_ENABLE) & 0xFFFF, 1)
                for msg in (DRVM_INIT, DRVM_ENABLE):
                    self.assertEqual(e.modmessage(
                        msg, 0, esfmemu.DEVNODE, user=0, device=0), 0)
                self.assertEqual(e.getnumdevs(), esfm)
                self.assertEqual(e.vxd_calls.count(0x040E), 1)
                e.load_opl3(builds()["opl3"])
                e.esfm_loaded = True
                for msg in (DRV_LOAD, DRV_ENABLE, DRV_OPEN):
                    self.assertEqual(e.opl3_driverproc(msg), 1)
                self.assertEqual(self.count(e), mine)
                self.assertEqual(e.getnumdevs(), esfm)
                self.assertEqual(bool(e.fix_state()["opts"] & OPT_OPL3_DRV),
                                 bool(mine))
                self.assertEqual(e.vxd_calls.count(0x040E), 1)
                if mine:
                    e.dev = e.r16(4, 0x3C)
                    self.assertEqual(e.opl3_open(), 0)
                    self.assertEqual(e.opl3_data(0x7F3C90), 0)
                    self.assertTrue(e.chip.opl_keyed())
                    self.assertEqual(e.opl3_close(), 0)
        # and the other way round: this driver's count is ESFM.DRV's first,
        # and the VxD isn't asked
        e = self.pair()
        self.assertEqual((self.count(e), e.getnumdevs()), (1, 1))
        self.assertEqual(e.vxd_calls.count(0x040E), 0)

    def test_count_without_the_card(self):
        # a count for a devnode that ESFM.DRV doesn't have, or before the
        # card is enabled, is 0 and decides nothing: the card's first count
        # does
        e = ESFMEmu(builds()["fixed"])
        e.ini[("esfm.drv", "opl3device")] = "2"
        e.vxd_opl3 = 1
        self.assertEqual(e.driverproc(DRV_ENABLE) & 0xFFFF, 1)
        self.assertEqual(e.getnumdevs(), 0)
        self.assertEqual(e.fix_state()["opts"] & OPT_OPL3_DRV, OPT_OPL3_DRV)
        self.assertEqual(e.vxd_calls.count(0x040E), 0)
        for msg in (DRVM_INIT, DRVM_ENABLE):
            self.assertEqual(e.modmessage(msg, 0, esfmemu.DEVNODE, user=0,
                                          device=0), 0)
        self.assertEqual(e.getnumdevs(), 1)
        self.assertEqual(e.vxd_calls.count(0x040E), 1)

    def test_devnode_news_stay_here(self):
        # ESFM.DRV hears of its devnode itself: this driver's DRVM_DISABLE
        # and DRVM_EXIT don't reach it, and other messages aren't passed on
        e = self.pair()
        lookups = len(e.module_lookups)
        for msg in (0x66, 0x65):
            self.assertEqual(e.opl3_modmessage(msg, 0, esfmemu.DEVNODE), 0)
        self.assertEqual(e.opl3_modmessage(0x4000, 0, 0), 8)  # NOTSUPPORTED
        self.assertEqual(len(e.module_lookups), lookups)
        self.assertEqual(e.opl3_open(), 0)
        self.assertEqual(e.opl3_data(0x7F3C90), 0)
        self.assertTrue(e.chip.opl_keyed())

    def test_no_import_at_interrupt_time(self):
        # the data only follows the pointer that the open found
        e = self.pair()
        self.assertEqual(e.opl3_open(), 0)
        calls = len(e.calls)
        for m in (0x7F3C90, 0x003C80, 0x7F4090):
            self.assertEqual(e.opl3_data(m), 0)
        self.assertEqual(e.calls[calls:], [])

    def test_ess_driver(self):
        # ESS's ESFM.DRV has no device 1, and refuses its open
        e = self.pair(which="stock")
        self.assertEqual(e.opl3_open(), 2)              # BADDEVICEID

    def test_driverproc(self):
        e = self.pair()
        for msg, want in ((DRV_CLOSE, 1), (DRV_DISABLE, 1), (DRV_FREE, 1),
                          (DRV_INSTALL, 1), (DRV_REMOVE, 1), (DRV_POWER, 1)):
            self.assertEqual(e.opl3_driverproc(msg), want, msg)
        self.assertEqual(e.defdriverprocs, [])
        e.opl3_driverproc(0x4000)
        self.assertEqual(e.defdriverprocs, [0x4000])
        # the settings, read once
        for key in ("Opl3Device", "BankSettings"):
            self.assertEqual(len([r for r in e.int_reads
                                  if r[2] == key]), 2, key)

    def test_settings_box(self):
        # the Multimedia control panel's Settings button: ESFMCFG.DLL's
        # box for device 1, as ESFM.DRV opens it for device 0
        e = self.pair()
        self.assertEqual(e.opl3_driverproc(DRV_QUERYCONFIGURE), 1)
        self.assertEqual(e.file_lookups, [("ESFMCFG.DLL", 0x4000)])
        for result in (1, 0):
            e.cfg_result = result
            self.assertEqual(e.opl3_driverproc(DRV_CONFIGURE, 0x2345,
                                               0x11112222), result)
        self.assertEqual(e.cfg_calls, [(0x2345, 1), (0x2345, 1)])
        self.assertEqual(e.libraries, {})
        self.assertEqual(e.defdriverprocs, [])
        # without the DLL: no button, and no box
        e.cfg_dll = False
        self.assertEqual(e.opl3_driverproc(DRV_QUERYCONFIGURE), 0)
        self.assertEqual(e.opl3_driverproc(DRV_CONFIGURE, 0x2345), 0)
        self.assertEqual(len(e.cfg_calls), 2)
        # BankSettings=0: neither, and nothing looked for
        e = self.pair(ini={"BankSettings": "0"})
        for msg in (DRV_QUERYCONFIGURE, DRV_CONFIGURE):
            self.assertEqual(e.opl3_driverproc(msg, 0x2345), 0)
        self.assertEqual((e.file_lookups, e.cfg_calls), ([], []))


if __name__ == "__main__":
    unittest.main()
