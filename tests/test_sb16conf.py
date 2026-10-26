# (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
#
# Licensed under GPL Version 3.0
"""The Sound Blaster 16 fronts against the Sound Blaster 16 model: every
scene of sbscenes runs on the model (sb16.SB16), on esssb16.com in front
of the ES1869 model (esschip), under QPIEMU, EMM386 and HDPMI32i, and on
ES1869.VXD's front in a DOS box, and what the game saw has to be the same.
Audio 2 is on channel 3, where the fronts play 16-bit sound, and the 8-bit
transfers play on Audio 1; the classes named Audio1 put Audio 2 where the
fronts can't use it, and everything plays on Audio 1, one transfer at a
time, so the scenes of two transfers at once (sbscenes.DUAL) don't run
there. EMM386H0Test is esssb16 /H0 against a Sound Blaster 16 without a
16-bit channel, whose 16-bit transfers go to the 8-bit channel.

Where the chip itself differs, the comparison allows for it. Audio 1's
FIFO (256 bytes) and Audio 2's (64) take the DMA ahead of the DACs, so a
block's interrupt comes when its last byte is in the FIFO, up to 256 bytes
before the DAC has played it: the frames heard at the first interrupt, at
a pause and at the end of a scene can be that many fewer. E2h's byte
doesn't reach memory: the front has no way to write it through the DMA.
A 16-bit block of more than 32768 samples runs in halves of the chip's
counter, so the game's handler also runs at each first half's end, where
82h names neither kind (spurious).
Under EMM386 the TSR doesn't trap the DMA controller, so a game's 16-bit
channel keeps the position it was programmed with, its programming has to
reach the 8237 before the transfer's command, and each command carries it
over anew, from the buffer's start, but for a single-cycle block of the
buffer it carried over last, which goes on from the block before. A game
that mixes up to that channel's count (mix16) plays its first fill over
and over there. EMM386 gives the 8237 the game's programming when the game
unmasks the channel (emm386dma), so one that unmasks it after the command
(resiplay, Pinball Fantasies' driver) plays what the channel held before;
EMM386PinballTest shows both.

The chip model also has to see no undefined use (esschip's violations),
every DMA cycle has to move data (dma8237's bad), and the VxD has to ask
VDMAD nothing it wouldn't do (sbfronts.VDMAD's errors).

usage: python3 -m unittest tests.test_sb16conf   (needs nasm and unicorn)
"""

import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from test_vxdext import HAVE_UNICORN, have_nasm  # noqa: E402
from sbgame import ModelCard  # noqa: E402
from sbscenes import DUAL, SCENES  # noqa: E402

if HAVE_UNICORN:
    from sbfronts import TSRCard, VxDCard  # noqa: E402

FIFO = 256                  # Audio 1's FIFO, in bytes and so at most frames
_MODEL = {}


def model(name, dma16=5):
    """the scene on the Sound Blaster 16 model, once"""
    if (name, dma16) not in _MODEL:
        _MODEL[name, dma16] = SCENES[name](ModelCard(dma16=dma16))
    return _MODEL[name, dma16]


def differences(want, got):
    """what the game saw differently, beyond what Audio 1's FIFO explains"""
    out = []
    for key in sorted(set(want) | set(got)):
        w, g = want.get(key), got.get(key)
        if key.startswith("per_block"):
            ok = w[1:] == g[1:] and len(w) == len(g) and \
                w[0] - FIFO <= g[0] <= w[0] if w and g else w == g
        elif key.startswith("data"):
            ok = g == w[:len(g)] and len(g) >= len(w) - 2 * FIFO
        elif key == "held":
            ok = g <= FIFO // 4         # 16-bit stereo: 4 bytes a frame
        elif key == "held8":
            ok = g <= FIFO              # 8-bit mono: a byte a frame
        elif key == "first":
            ok = w - FIFO <= g <= w
        else:
            ok = w == g
        if not ok:
            out.append((key, w, g))
    return out


class Conformance:
    """the scenes on one front; `known` maps a scene to the keys it may
    differ in, as the module's text says; `audio2` the front plays 16-bit
    sound on Audio 2; `dma16` the model's 16-bit channel"""

    known = {"dma_ident": {"bytes"}, "play16_long_single": {"spurious"},
             "play16_long_auto": {"spurious"},
             "play16_long_exit": {"spurious"}}
    audio2 = True
    dma16 = 5

    def card(self):
        raise NotImplementedError

    def check(self, name):
        if name in DUAL and not self.audio2:
            self.skipTest("one transfer at a time on Audio 1")
        want = model(name, self.dma16)
        card = self.card()
        got = SCENES[name](card)
        self.assertEqual(card.audio2, self.audio2)
        diffs = [d for d in differences(want, got)
                 if d[0] not in self.known.get(name, ())]
        self.assertEqual(diffs, [], name)
        self.assertEqual(card.chip.violations, [], name)
        self.assertEqual(card.chip.dmac.bad[:1], [], name)
        self.assertEqual(getattr(card, "errors", []), [], name)
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
        return TSRCard("qpi", dma2=3)


@unittest.skipUnless(HAVE_UNICORN and have_nasm(), "needs nasm and unicorn")
class QPIAudio1Test(Conformance, unittest.TestCase):
    """the same with Audio 2 on the 16-bit channel 5, where the chip takes
    a byte of each word: every transfer on Audio 1"""

    audio2 = False

    def card(self):
        return TSRCard("qpi", dma2=5)


@unittest.skipUnless(HAVE_UNICORN and have_nasm(), "needs nasm and unicorn")
class HDPMITest(Conformance, unittest.TestCase):
    """esssb16.com through HDPMI32i's handler: a protected-mode game"""

    def card(self):
        return TSRCard("pm", dma2=3)


@unittest.skipUnless(HAVE_UNICORN and have_nasm(), "needs nasm and unicorn")
class EMM386Test(Conformance, unittest.TestCase):
    """esssb16.com through EMM386's handler at ring 0: a real-mode game,
    without the DMA controller's traps"""

    known = {**Conformance.known, "position16": {"moving"},
             "mix16": {"back", "mixed"}, "mix16_again": {"back", "mixed"},
             "resiplay": {"back", "mixed", "start"},
             "play16_dma_after": {"data", "ok", "per_block", "seen"}}

    def card(self):
        return TSRCard("emm", dma2=3)


@unittest.skipUnless(HAVE_UNICORN and have_nasm(), "needs nasm and unicorn")
class EMM386Audio1Test(Conformance, unittest.TestCase):
    """the same with Audio 2 on the 16-bit channel 5, the card of the
    reports: every transfer on Audio 1, one at a time"""

    known = EMM386Test.known
    audio2 = False

    def card(self):
        return TSRCard("emm", dma2=5)


@unittest.skipUnless(HAVE_UNICORN and have_nasm(), "needs nasm and unicorn")
class EMM386H0Test(Conformance, unittest.TestCase):
    """/H0 under EMM386: mixer 81h has no 16-bit channel, so a game that
    reads it, as Pinball Fantasies' driver does, programs the 8-bit
    channel itself, and its count moves (resiplay)"""

    audio2 = False
    dma16 = 0

    def card(self):
        return TSRCard("emm", dma2=5, tail="/H0")


@unittest.skipUnless(HAVE_UNICORN and have_nasm(), "needs nasm and unicorn")
class EMM386PinballTest(unittest.TestCase):
    """Pinball Fantasies' Sound Blaster 16 driver under EMM386 (resiplay):
    it unmasks its channel after B6h, and the TSR reads the channel back at
    B6h"""

    def test_at_the_unmask(self):
        """EMM386 gives the 8237 the game's programming at the unmask, so
        the TSR carries over what the channel held before, here its state
        after the reset, and the game's count never moves"""
        got = SCENES["resiplay"](TSRCard("emm", dma2=3))
        self.assertEqual(set(got["start"]), {0})
        self.assertFalse(got["mixed"])

    def test_at_once(self):
        """were each write to reach the 8237 at once, the TSR would carry
        the game's channel over, and its count, which stands still, would
        have the game mix its first fill only: the loop"""
        got = SCENES["resiplay"](TSRCard("emm", dma2=3,
                                         emm386={"at_unmask": False}))
        self.assertEqual(got["start"], list(range(32)))
        self.assertGreater(got["back"], 0)
        self.assertFalse(got["mixed"])


@unittest.skipUnless(HAVE_UNICORN and have_nasm(), "needs nasm and unicorn")
class VxDTest(Conformance, unittest.TestCase):
    """ES1869.VXD's front in a DOS box, with the 16-bit channel the VxD's
    own (VDMAD_Virtualize_Channel)"""

    def card(self):
        return VxDCard(dma2=3)


@unittest.skipUnless(HAVE_UNICORN and have_nasm(), "needs nasm and unicorn")
class VxDAudio1Test(Conformance, unittest.TestCase):
    """the 16-bit channel is Audio 2's, which ESS's handler has, and the
    chip takes a byte of each of its words: every transfer on Audio 1"""

    audio2 = False

    def card(self):
        return VxDCard(dma2=5)


@unittest.skipUnless(HAVE_UNICORN and have_nasm(), "needs nasm and unicorn")
class VxDSingleTest(Conformance, unittest.TestCase):
    """ESS's "Single Mode DMA": the DMA's single transfers as the game
    programs them"""

    def card(self):
        return VxDCard(dma2=3, demand=False)


if __name__ == "__main__":
    unittest.main()
