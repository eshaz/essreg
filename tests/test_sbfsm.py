# (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
#
# Licensed under GPL Version 3.0
"""The Sound Blaster 16 front as a state machine (tests/sbfsm.py, the
reference of docs/SB16_FRONT.md) against both fronts, scene by scene
(sbscenes).

The reference has to reach the chip as esssb16.com does, access for
access (sbops.ports), with the game's 16-bit channel trapped (QPIEMU) and
read back (EMM386), and with Audio 2 on channel 3 and on the 16-bit
channel 5; and the game has to see what the Sound Blaster 16 model shows
it, as test_sb16conf has it for the TSR. ES1869.VXD's front has to send
the chip the reference's operations for its transfers (sbops.transfers):
the accesses of ESS's own code there aren't the front's, and VDMAD
programs the DMA channels where the reference has its trapped channel
carried over. A difference shows the first operation where the two part.

Where test_sb16conf ran the scene on the front first, its run
(sbops.RUNS) serves here; the reference runs once for each configuration.

usage: python3 -m unittest tests.test_sbfsm   (needs nasm and unicorn)
"""

import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import sbops  # noqa: E402
from sbscenes import DUAL, SCENES  # noqa: E402
from test_vxdext import HAVE_UNICORN, have_nasm  # noqa: E402
import test_sb16conf as conf  # noqa: E402

if HAVE_UNICORN:
    from sbfsm import FSMCard  # noqa: E402

_REF = {}


def front(cls, name):
    """the front's (port accesses, operations) on the scene"""
    if (cls.__name__, name) not in sbops.RUNS:
        conf.run(cls, name)
    return sbops.RUNS[cls.__name__, name]


def reference(path, dma2, name):
    """the reference's (port accesses, operations, what the game saw)"""
    if (path, dma2, name) not in _REF:
        card = FSMCard(path, dma2=dma2)
        trace = sbops.record(card)
        got = SCENES[name](card)
        _REF[path, dma2, name] = (sbops.ports(card.chip.log), trace, got)
    return _REF[path, dma2, name]


class Ports:
    """the reference's accesses are those of the TSR's configuration
    `tsr`, a class of test_sb16conf, and the game sees the model"""

    tsr = None
    path, dma2 = "qpi", 3

    def check(self, name):
        if name in DUAL and not self.tsr.audio2:
            self.skipTest("one transfer at a time on Audio 1")
        ports, _trace, got = reference(self.path, self.dma2, name)
        want = front(self.tsr, name)[0]
        diff = sbops.divergence(want, ports)
        if diff:
            self.fail("%s: the TSR's accesses, then the reference's\n%s"
                      % (name, diff))
        diffs = [d for d in conf.differences(conf.model(name), got)
                 if d[0] not in self.tsr.known.get(name, ())]
        self.assertEqual(diffs, [], name)


class Operations:
    """ES1869.VXD's front's operations in the configuration `vxd` are the
    reference's, with Audio 2 on the same channel"""

    vxd = None
    dma2 = 3

    def check(self, name):
        if name in DUAL and not self.vxd.audio2:
            self.skipTest("one transfer at a time on Audio 1")
        want = sbops.transfers(reference("qpi", self.dma2, name)[1])
        got = sbops.transfers(front(self.vxd, name)[1])
        diff = sbops.divergence(want, got)
        if diff:
            self.fail("%s: the reference's operations, then the VxD's\n%s"
                      % (name, diff))


def _scene_test(name):
    def test(self):
        self.check(name)
    test.__doc__ = SCENES[name].__doc__
    return test


for _name in SCENES:
    setattr(Ports, "test_" + _name, _scene_test(_name))
    setattr(Operations, "test_" + _name, _scene_test(_name))


@unittest.skipUnless(HAVE_UNICORN and have_nasm(), "needs nasm and unicorn")
class QPIPortsTest(Ports, unittest.TestCase):
    """the game's 16-bit channel trapped, Audio 2 on channel 3"""

    tsr = conf.QPITest


@unittest.skipUnless(HAVE_UNICORN and have_nasm(), "needs nasm and unicorn")
class QPIAudio1PortsTest(Ports, unittest.TestCase):
    """trapped, Audio 2 on the 16-bit channel 5: every transfer on Audio 1"""

    tsr = conf.QPIAudio1Test
    dma2 = 5


@unittest.skipUnless(HAVE_UNICORN and have_nasm(), "needs nasm and unicorn")
class EMM386PortsTest(Ports, unittest.TestCase):
    """the game's 16-bit channel read back, Audio 2 on channel 3"""

    tsr = conf.EMM386Test
    path = "emm"


@unittest.skipUnless(HAVE_UNICORN and have_nasm(), "needs nasm and unicorn")
class EMM386Audio1PortsTest(Ports, unittest.TestCase):
    """read back, every transfer on Audio 1: the card of the reports"""

    tsr = conf.EMM386Audio1Test
    path, dma2 = "emm", 5


@unittest.skipUnless(HAVE_UNICORN and have_nasm(), "needs nasm and unicorn")
class VxDOperationsTest(Operations, unittest.TestCase):
    """the VxD's 16-bit sound on Audio 2's channel 3"""

    vxd = conf.VxDTest


@unittest.skipUnless(HAVE_UNICORN and have_nasm(), "needs nasm and unicorn")
class VxDAudio1OperationsTest(Operations, unittest.TestCase):
    """the VxD's 16-bit sound on Audio 1"""

    vxd = conf.VxDAudio1Test
    dma2 = 5


class LevelsTest(unittest.TestCase):
    """the Sound Blaster 16's levels on the chip's scales (DS p.55, Table
    21): full volume is the chip's unity, never its gain, each level is the
    nearest the chip has, and both fronts' tables are the reference's"""

    def test_scales(self):
        import sbfsm
        for lvl, db in ((sbfsm.LVL_A1, sbfsm.DB_A1),
                        (sbfsm.LVL_A2, sbfsm.DB_A2),
                        (sbfsm.LVL_MUS, sbfsm.DB_MUS),
                        (sbfsm.LVL_AUX, sbfsm.DB_AUX)):
            self.assertEqual(db[lvl[31]], 0.0)          # full: unity
            self.assertEqual(lvl[:2], (0, 0))           # 0 and 1 mute
            self.assertTrue(all(db[n] <= 0 for n in lvl if n))
            self.assertEqual(list(lvl), sorted(lvl))
            for v in range(2, 32):                      # within half a step
                t = 2 * (v - 31)
                if t >= db[1]:
                    self.assertLessEqual(abs(db[lvl[v]] - t), 1.5, v)
        # the Sound Blaster 16's reset (-14 dB): Audio 1 at -15 dB, the
        # master at -14.25 dB; Audio 1's 15 is +1.5 dB, which nothing gives
        self.assertEqual((sbfsm.LVL_A1[24], sbfsm.LVL_MASTER[24]), (6, 44))
        self.assertEqual((sbfsm.DB_A1[15], sbfsm.DB_A1[14]), (1.5, 0.0))
        self.assertNotIn(15, sbfsm.LVL_A1)
        self.assertEqual((sbfsm.LVL_MASTER[31], sbfsm.LVL_MASTER[0]), (63, 0))
        # back from the chip: each level the nearest, unity at most
        for view, db in ((sbfsm.VIEW_A1, sbfsm.DB_A1),
                         (sbfsm.VIEW_MUS, sbfsm.DB_MUS),
                         (sbfsm.VIEW_AUX, sbfsm.DB_AUX)):
            self.assertEqual(view[0], 0)
            for n in range(1, 16):
                d = min(db[n], 0.0)
                self.assertLessEqual(abs(2 * (view[n] - 31) - d), 1.0, n)
        for m in range(64):
            self.assertLessEqual(abs(2 * (sbfsm.VIEW_MASTER[m] - 31) -
                                     0.75 * (m - 63)), 1.0, m)

    @unittest.skipUnless(have_nasm(), "needs nasm")
    def test_tsr_tables(self):
        import sbfsm
        from sbfronts import tsr_build
        image, syms = tsr_build()
        for name, want in (("lvl_a1", sbfsm.LVL_A1), ("lvl_a2", sbfsm.LVL_A2),
                           ("lvl_mus", sbfsm.LVL_MUS),
                           ("lvl_aux", sbfsm.LVL_AUX),
                           ("lvl_master", sbfsm.LVL_MASTER),
                           ("view_a1", sbfsm.VIEW_A1),
                           ("view_mus", sbfsm.VIEW_MUS),
                           ("view_aux", sbfsm.VIEW_AUX),
                           ("view_master", sbfsm.VIEW_MASTER)):
            at = syms[name] - 0x100
            self.assertEqual(tuple(image[at:at + len(want)]), want, name)

    @unittest.skipUnless(HAVE_UNICORN and have_nasm(),
                         "needs nasm and unicorn")
    def test_vxd_tables(self):
        import sbfsm
        from sbfronts import VxDCard
        m = VxDCard(dma2=3).m
        for name, want in (("Lvl_A1", sbfsm.LVL_A1), ("Lvl_A2", sbfsm.LVL_A2),
                           ("Lvl_Mus", sbfsm.LVL_MUS),
                           ("Lvl_Aux", sbfsm.LVL_AUX),
                           ("Lvl_Master", sbfsm.LVL_MASTER),
                           ("View_A1", sbfsm.VIEW_A1),
                           ("View_Mus", sbfsm.VIEW_MUS),
                           ("View_Aux", sbfsm.VIEW_AUX),
                           ("View_Master", sbfsm.VIEW_MASTER)):
            at = m.syms["ESSREG_S16_" + name]
            got = tuple(m.emu.uc.mem_read(at, len(want)))
            self.assertEqual(got, want, name)


if __name__ == "__main__":
    unittest.main()
