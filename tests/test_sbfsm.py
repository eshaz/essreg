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


if __name__ == "__main__":
    unittest.main()
