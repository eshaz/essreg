# (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
#
# Licensed under GPL Version 3.0
"""The Sound Blaster 16 fronts against the Sound Blaster 16 model: every
scene of sbscenes runs on the model (sb16.SB16) and on esssb16.com in
front of the ES1869 model (esschip), under QPIEMU, EMM386 and HDPMI32i,
and what the game saw has to be the same.

Where the chip itself differs, the comparison allows for it. Audio 1's
FIFO (256 bytes) takes the DMA ahead of the DAC, so a block's interrupt
comes when its last byte is in the FIFO, up to 256 bytes before the DAC
has played it: the frames heard at the first interrupt, at a pause and
at the end of a scene can be that many fewer. E2h's byte doesn't reach
memory: the front has no way to write it through the DMA. Under EMM386
the TSR doesn't trap the DMA controller, so a game's 16-bit channel keeps
the position it was programmed with, its programming has to come before
the transfer's command, and each command carries it over anew, from the
buffer's start.

The chip model also has to see no undefined use (esschip's violations),
and every DMA cycle has to move data (dma8237's bad).

usage: python3 -m unittest tests.test_sb16conf   (needs nasm and unicorn)
"""

import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from test_vxdext import HAVE_UNICORN, have_nasm  # noqa: E402
from sbgame import ModelCard  # noqa: E402
from sbscenes import SCENES  # noqa: E402

if HAVE_UNICORN:
    from sbfronts import TSRCard  # noqa: E402

FIFO = 256                  # Audio 1's FIFO, in bytes and so at most frames
_MODEL = {}


def model(name):
    """the scene on the Sound Blaster 16 model, once"""
    if name not in _MODEL:
        _MODEL[name] = SCENES[name](ModelCard())
    return _MODEL[name]


def differences(want, got):
    """what the game saw differently, beyond what Audio 1's FIFO explains"""
    out = []
    for key in sorted(set(want) | set(got)):
        w, g = want.get(key), got.get(key)
        if key == "per_block":
            ok = w[1:] == g[1:] and len(w) == len(g) and \
                w[0] - FIFO <= g[0] <= w[0] if w and g else w == g
        elif key == "data":
            ok = g == w[:len(g)] and len(g) >= len(w) - 2 * FIFO
        elif key == "held":
            ok = g <= FIFO // 4         # 16-bit stereo: 4 bytes a frame
        elif key == "first":
            ok = w - FIFO <= g <= w
        else:
            ok = w == g
        if not ok:
            out.append((key, w, g))
    return out


class Conformance:
    """the scenes on one front; `known` maps a scene to the keys it may
    differ in, as the module's text says"""

    known = {"dma_ident": {"bytes"}}

    def card(self):
        raise NotImplementedError

    def check(self, name):
        want = model(name)
        card = self.card()
        got = SCENES[name](card)
        diffs = [d for d in differences(want, got)
                 if d[0] not in self.known.get(name, ())]
        self.assertEqual(diffs, [], name)
        self.assertEqual(card.chip.violations, [], name)
        self.assertEqual(card.chip.dmac.bad[:1], [], name)
        return card


def _scene_test(name):
    def test(self):
        self.check(name)
    test.__doc__ = SCENES[name].__doc__
    return test


for _name in SCENES:
    setattr(Conformance, "test_" + _name, _scene_test(_name))


@unittest.skipUnless(HAVE_UNICORN and have_nasm(), "needs nasm and unicorn")
class QPITest(Conformance, unittest.TestCase):
    """esssb16.com through QPIEMU's callback (QEMM's QPI): a real-mode game"""

    def card(self):
        return TSRCard("qpi")


@unittest.skipUnless(HAVE_UNICORN and have_nasm(), "needs nasm and unicorn")
class HDPMITest(Conformance, unittest.TestCase):
    """esssb16.com through HDPMI32i's handler: a protected-mode game"""

    def card(self):
        return TSRCard("pm")


@unittest.skipUnless(HAVE_UNICORN and have_nasm(), "needs nasm and unicorn")
class EMM386Test(Conformance, unittest.TestCase):
    """esssb16.com through EMM386's handler at ring 0: a real-mode game,
    without the DMA controller's traps"""

    known = {**Conformance.known, "position16": {"moving"},
             "play16_dma_after": {"data", "ok", "per_block", "seen"},
             "play16_single_each_block": {"data"}}

    def card(self):
        return TSRCard("emm")


if __name__ == "__main__":
    unittest.main()
