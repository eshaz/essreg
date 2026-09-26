# SPDX-License-Identifier: GPL-3.0-or-later
"""ESFM.DRV rebuilt from src/esfm.

RebuildTest: the stock build must be byte-identical to driver/ESFM.DRV,
and the fixed build a valid driver with the same bank loader (so that
esfmpat and essctl keep working with it).

StuckNoteTest runs both builds in a CPU emulator (tests/esfmemu.py) and
reproduces the stuck notes of the ESS driver: messages that arrive while
the driver is busy are refused, and the lost note offs leave notes
sounding; closing with the sustain pedal down leaves notes on; a message
from an interrupt during close splits an FM address/data pair.  The fixed
build must pass every scenario the stock build fails.
"""

import os
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
                         PWR_SUSPENDREQUEST, PWR_SUSPENDRESUME)
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
        """Note off from an interrupt at every step-th instruction of a
        note on; count the trials that leave a note sounding."""
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


if __name__ == "__main__":
    unittest.main()
