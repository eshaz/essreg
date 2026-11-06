# (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
#
# Licensed under GPL Version 3.0
"""Test tools/dualwav.py, the 4-channel WAV files for dual playback: what
each mode puts on the Audio 2 DAC next to the Audio 1 DAC's part."""

import math
import os
import struct
import sys
import tempfile
import unittest
import wave

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))

import dualwav  # noqa: E402

RATE = 22050
TONE = 441.0                    # 50 samples a period


def frames(path):
    """(rate, bits, [4 channels of ints])"""
    with wave.open(path, "rb") as w:
        n, width = w.getnchannels(), w.getsampwidth()
        rate = w.getframerate()
        raw = w.readframes(w.getnframes())
    if width == 2:
        vals = list(struct.unpack("<%dh" % (len(raw) // 2), raw))
    else:
        vals = [v - 128 for v in raw]
    return rate, width * 8, [vals[i::n] for i in range(n)]


class DualWavTest(unittest.TestCase):
    def run_tool(self, *args):
        with tempfile.TemporaryDirectory() as tmp:
            out = os.path.join(tmp, "out.wav")
            dualwav.main(list(args) + [out])
            return frames(out)

    def tone(self, mode, *args):
        return self.run_tool(mode, "--tone", str(TONE), "--rate", str(RATE),
                             "--seconds", "0.2", *args)

    def test_four_channels(self):
        rate, bits, ch = self.tone("same")
        self.assertEqual((rate, bits, len(ch)), (RATE, 16, 4))
        self.assertEqual(len(ch[0]), int(RATE * 0.2))

    def test_same(self):
        _r, _b, ch = self.tone("same")
        self.assertEqual(ch[0], ch[2])
        self.assertEqual(ch[1], ch[3])
        want = [round(16384 * math.sin(2 * math.pi * TONE * i / RATE))
                for i in range(len(ch[0]))]
        self.assertTrue(all(abs(a - b) <= 1 for a, b in zip(ch[0], want)))

    def test_invert(self):
        _r, _b, ch = self.tone("invert")
        self.assertTrue(all(abs(a + b) <= 1 for a, b in zip(ch[0], ch[2])))
        self.assertTrue(any(ch[0]))

    def test_split(self):
        _r, _b, ch = self.tone("split", "--offset", "0.25")
        x = [16384 * math.sin(2 * math.pi * TONE * i / RATE)
             for i in range(len(ch[0]))]
        for a, b, v in zip(ch[0], ch[2], x):
            self.assertLessEqual(abs(a + b - 2 * 0.75 * v), 2)
        self.assertGreater(max(abs(a - b) for a, b in zip(ch[0], ch[2])),
                           8000)

    def test_half(self):
        _r, _b, ch = self.tone("half")
        lag = (dualwav.TAPS - 1) // 2
        for i in range(200, len(ch[0])):
            a = 16384 * math.sin(2 * math.pi * TONE * (i - lag) / RATE)
            b = 16384 * math.sin(2 * math.pi * TONE * (i - lag - 0.5) / RATE)
            self.assertLessEqual(abs(ch[0][i] - a), 2)
            self.assertLessEqual(abs(ch[2][i] - b), 40)

    def test_hilbert(self):
        # 90 degrees behind: the Hilbert transform of a sine is -cosine
        _r, _b, ch = self.tone("hilbert", "--seconds", "0.1")
        lag = (dualwav.HTAPS - 1) // 2
        for i in range(2 * lag, len(ch[0])):
            a = 16384 * math.sin(2 * math.pi * TONE * (i - lag) / RATE)
            b = -16384 * math.cos(2 * math.pi * TONE * (i - lag) / RATE)
            self.assertLessEqual(abs(ch[0][i] - a), 2)
            self.assertLessEqual(abs(ch[2][i] - b), 160)

    def test_whole_delay(self):
        _r, _b, ch = self.tone("same", "--delay2", "3")
        self.assertEqual(ch[2][3:], ch[0][:-3])
        self.assertEqual(ch[2][:3], [0, 0, 0])

    def test_fraction_delay(self):
        _r, _b, ch = self.tone("same", "--delay2", "1.25")
        lag = (dualwav.TAPS - 1) // 2
        for i in range(200, len(ch[0])):
            a = 16384 * math.sin(2 * math.pi * TONE * (i - lag) / RATE)
            b = 16384 * math.sin(2 * math.pi * TONE *
                                 (i - lag - 1.25) / RATE)
            self.assertLessEqual(abs(ch[0][i] - a), 2)
            self.assertLessEqual(abs(ch[2][i] - b), 40)

    def test_delay1(self):
        _r, _b, ch = self.tone("same", "--delay1", "2", "--delay2", "0.5")
        lag = (dualwav.TAPS - 1) // 2
        for i in range(200, len(ch[0])):
            a = 16384 * math.sin(2 * math.pi * TONE * (i - lag - 2) / RATE)
            b = 16384 * math.sin(2 * math.pi * TONE *
                                 (i - lag - 0.5) / RATE)
            self.assertLessEqual(abs(ch[0][i] - a), 2)
            self.assertLessEqual(abs(ch[2][i] - b), 40)

    def test_gains(self):
        _r, _b, ch = self.tone("same", "--gain2", "-6.0206")
        self.assertTrue(all(abs(a / 2 - b) <= 1
                            for a, b in zip(ch[0], ch[2])))

    def test_eight_bit_mono_input(self):
        with tempfile.TemporaryDirectory() as tmp:
            src = os.path.join(tmp, "in.wav")
            with wave.open(src, "wb") as w:
                w.setnchannels(1)
                w.setsampwidth(1)
                w.setframerate(11025)
                w.writeframes(bytes([128, 160, 200, 128, 96, 56] * 10))
            out = os.path.join(tmp, "out.wav")
            dualwav.main(["invert", src, out])
            rate, bits, ch = frames(out)
        self.assertEqual((rate, bits), (11025, 8))
        self.assertEqual(ch[0][:6], [0, 32, 72, 0, -32, -72])
        self.assertEqual(ch[1][:6], ch[0][:6])
        self.assertEqual(ch[2][:6], [0, -32, -72, 0, 32, 72])

    def test_rate_the_driver_plays(self):
        with self.assertRaises(SystemExit):
            self.run_tool("same", "--tone", "100", "--rate", "96000")


if __name__ == "__main__":
    unittest.main()
