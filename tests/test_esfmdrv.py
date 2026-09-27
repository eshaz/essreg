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
import gmcheck  # noqa: E402
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
        # 3 was an earlier build's pedal times
        self.assertEqual(self.emu("fixed").fix_state()["version"], 2)


# General MIDI (esfmgm.asm): device fields, esfmdev.inc
DEV_PROGRAM, DEV_RPN_DATA, DEV_CHAN_FLAGS, DEV_BEND = 0x20, 0x30, 0x40, 0x50
DEV_CHAN_PAN, DEV_CHAN_VOLUME, DEV_CHAN_EXPR = 0x2D2, 0x2E2, 0x2F2
DEV_GM_MOD, DEV_GM_PRESS, DEV_GM_RPN = 0x311, 0x321, 0x341
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
        with open(os.path.join(ROOT, "docs", "TESTING.md")) as f:
            doc = f.read().replace("\n   ", "\n")
        self.assertIn(gmcheck.steps_table(), doc)

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
        for value, side in ((0, 0x10), (127, 0x20)):
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
