# (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
#
# Licensed under GPL Version 3.0
"""tools/sb16rec.py, the analysis of a recorded sb16test run: the WAV
formats it reads, its DTMF markers, its measures of a tone, and recorded
runs of the checks on the model of the Sound Blaster 16 and on esssb16
under EMM386. A run's recording is made from what the card's DACs played,
each frame at the time the run's timeline gives, and then broken the ways
a card's sounds break: a flat tone, a 16-bit stream a byte out, a loop, a
dead channel, which the analysis has to find.

usage: python3 -m unittest tests.test_sb16rec   (needs numpy)
"""

import io
import os
import struct
import sys
import tempfile
import unittest
import wave

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(os.path.dirname(HERE), "tools"))

try:
    import numpy as np
    import sb16rec
except ImportError:             # the analysis needs numpy
    np = sb16rec = None

from test_vxdext import HAVE_UNICORN, have_nasm  # noqa: E402

RATE = 48000


def write_wav(path, frames, rate=RATE):
    """frames (floats, frames by channels) as 16-bit PCM"""
    data = np.clip(np.round(frames * 32767), -32768, 32767).astype("<i2")
    with wave.open(path, "wb") as w:
        w.setnchannels(frames.shape[1])
        w.setsampwidth(2)
        w.setframerate(rate)
        w.writeframes(data.tobytes())


def sine(hz, secs, rate=RATE, amp=0.5, phase=0.0):
    t = np.arange(int(secs * rate)) / rate
    return amp * np.sin(2 * np.pi * hz * t + phase)


def dtmf(syms, rate=RATE, on=0.060, off=0.040, amp=0.18):
    """a marker as sb16t.c plays it: each symbol's two tones, then quiet"""
    out = []
    for s in syms:
        k = sb16rec.DTMF_SYMS.index(s)
        tone = sine(sb16rec.DTMF_LO[k // 4], on, rate, amp) + \
            sine(sb16rec.DTMF_HI[k % 4], on, rate, amp)
        out += [tone, np.zeros(int(off * rate))]
    return np.concatenate(out)


@unittest.skipIf(np is None, "needs numpy")
class WavTest(unittest.TestCase):
    """the formats a recorder writes"""

    def test_formats(self):
        x = sine(441, 0.05, amp=0.7)
        cases = []
        cases.append((1, 2, (np.round(x * 32767)).astype("<i2").tobytes()))
        v = np.round(x * 8388607).astype(np.int64)
        b24 = b"".join(struct.pack("<i", int(s))[:3] for s in v)
        cases.append((1, 3, b24))
        cases.append((3, 4, x.astype("<f4").tobytes()))
        cases.append((1, 1, (np.round(x * 127) + 128).astype(np.uint8)
                      .tobytes()))
        for tag, width, body in cases:
            fmt = struct.pack("<HHIIHH", tag, 1, RATE, RATE * width, width,
                              8 * width)
            riff = b"WAVE" + b"fmt " + struct.pack("<I", len(fmt)) + fmt + \
                b"data" + struct.pack("<I", len(body)) + body
            with tempfile.NamedTemporaryFile(suffix=".wav",
                                             delete=False) as f:
                f.write(b"RIFF" + struct.pack("<I", len(riff)) + riff)
            try:
                rate, frames = sb16rec.read_wav(f.name)
            finally:
                os.unlink(f.name)
            self.assertEqual((rate, frames.shape), (RATE, (len(x), 1)))
            self.assertLess(float(np.max(np.abs(frames[:, 0] - x))),
                            2.0 / (1 << (8 * width - 1)) + 1e-6, (tag, width))


@unittest.skipIf(np is None, "needs numpy")
class MarkerTest(unittest.TestCase):
    """the DTMF markers, found where they begin"""

    def test_markers(self):
        rng = np.random.default_rng(1)
        x = np.concatenate([np.zeros(RATE // 4), dtmf("*05#"),
                            sine(441, 0.3, amp=0.3), dtmf("ABCD"),
                            np.zeros(RATE // 4)])
        for level, noise in ((1.0, 0.0), (0.05, 0.002)):
            y = x * level + rng.normal(0, noise, len(x))
            found = sb16rec.markers(sb16rec.band_limit(y, RATE))
            self.assertEqual([m[0] for m in found], ["*05#", "ABCD"])
            self.assertAlmostEqual(found[0][1], 0.25, delta=0.006)
            self.assertAlmostEqual(found[1][1], 0.25 + 0.4 + 0.3,
                                   delta=0.006)

    def test_short_symbols(self):
        """the short run's: 30 ms of tones and 20 ms of quiet"""
        x = np.concatenate([np.zeros(RATE // 10),
                            dtmf("*17#", on=0.030, off=0.020),
                            np.zeros(RATE // 10)])
        found = sb16rec.markers(sb16rec.band_limit(x, RATE))
        self.assertEqual([m[0] for m in found], ["*17#"])
        # its level: two tones of 0.18 each, 11.9 dB under a full-scale sine
        self.assertAlmostEqual(found[0][2], -11.9, delta=1.0)

    def test_clock(self):
        marks = {"ABCD": 0, "*00#": 1000000, "DCBA": 3000000}
        found = [("ABCD", 2.0, -9), ("*00#", 3.0001, -9),
                 ("DCBA", 5.0003, -9)]
        a, b, err, n, late = sb16rec.fit_clock(marks, found)
        self.assertEqual((n, late), (3, []))
        self.assertAlmostEqual(a, 1.0001, delta=2e-5)
        self.assertAlmostEqual(b, 2.0, delta=1e-4)
        self.assertLess(err, 1e-4)

    def test_a_marker_found_late(self):
        """the end marker over the FM note's release, as in the first
        recording: its first symbol hidden, found 32 ms late and left out"""
        marks = {"*%02d#" % i: 1000000 * (i + 1) for i in range(24)}
        marks.update({"ABCD": 0, "DCBA": 26000000})
        found = [(text, 2.0 + 1.00012 * us / 1e6 + 0.002 * (i % 3 - 1), -9)
                 for i, (text, us) in enumerate(sorted(marks.items(),
                                                       key=lambda m: m[1]))]
        found[-1] = ("DCBA", found[-1][1] + 0.032, -9)
        a, b, err, n, late = sb16rec.fit_clock(marks, found)
        self.assertEqual(n, 25)
        self.assertEqual([m for m, _d in late], ["DCBA"])
        self.assertAlmostEqual(late[0][1], 0.032, delta=0.003)
        self.assertAlmostEqual((a - 1) * 1e6, 120, delta=10)
        self.assertLess(err, 0.003)


@unittest.skipIf(np is None, "needs numpy")
class MeasureTest(unittest.TestCase):
    """the pitch and the cleanness of a tone, at the analysis's rate"""

    R = sb16rec.RATE if sb16rec else 8000

    def test_pitch(self):
        for hz in (441.0, 430.66, 882.0, 294.0):
            x = sine(hz, 1.0, self.R)
            self.assertAlmostEqual(sb16rec.pitch(x, hz, rate=self.R), hz,
                                   delta=0.02)
        x = sine(441.0, 0.2, self.R)
        self.assertAlmostEqual(sb16rec.pitch(x, 441, rate=self.R), 441,
                               delta=0.2)

    def test_cleanness(self):
        x = sine(441, 1.0, self.R, amp=0.36)
        med, worst = sb16rec.cleanness(x, 441, self.R)
        self.assertGreater(worst, 80)
        eight = np.round(x * 127) / 127                 # 8-bit steps
        med, worst = sb16rec.cleanness(eight, 441, self.R)
        self.assertGreater(med, sb16rec.CLEAN_DB)
        self.assertGreater(worst, sb16rec.WORST_DB)
        # a 16-bit stream a byte out: each sample's bytes swapped
        v = np.round(x * 32767).astype("<i2")
        swapped = v.byteswap().astype(float) / 32768
        med, worst = sb16rec.cleanness(swapped, 441, self.R)
        self.assertLess(med, 10)
        # a loop back to another phase, once
        jump = np.concatenate([x[:4000], sine(441, 0.5, self.R, 0.36, 2.0)])
        med, worst = sb16rec.cleanness(jump, 441, self.R)
        self.assertGreater(med, 60)
        self.assertLess(worst, sb16rec.WORST_DB)


# --- recorded runs of the checks ---------------------------------------------

def mixer_gains(run, tau):
    """the Sound Blaster 16's master and voice levels (30h-33h) at each
    time, as the run wrote them: left and right gains, 2 dB a step of 31,
    the reset's C0h before the first write"""
    level = {0x30: 0xC0, 0x31: 0xC0, 0x32: 0xC0, 0x33: 0xC0}
    gains = np.ones((len(tau), 2))
    log = [(us, r, v) for us, r, v in run.mix_log if r in level] + \
        [(float("inf"), None, None)]
    t_from = -float("inf")
    for us, reg, v in log:
        sel = (tau >= t_from) & (tau < us)
        for c, (m, w) in enumerate(((0x30, 0x32), (0x31, 0x33))):
            db = 2 * ((level[m] >> 3) - 31) + 2 * ((level[w] >> 3) - 31)
            gains[sel, c] = 10 ** (db / 20)
        if reg is not None:
            level[reg] = v
        t_from = us
    return gains


def synthesize(run, rate=RATE):
    """the recording of what the card's DACs played, stereo at rate: each
    stretch of playing, where the run's timeline saw the counts rise less
    than 1 ms apart, at even times, as a DAC plays, between its frames,
    through the mixer's levels"""
    card = run.card
    lists = ([card.chip.played, card.chip.played2] if run.front
             else [card.played])
    tl = run.timeline
    times = np.array([t for t, _ in tl], dtype=float)
    base = times[0] - 50000
    n = int((times[-1] - base) * rate / 1e6) + rate // 20
    out = np.zeros((n, 2))
    for d, frames in enumerate(lists):
        if not frames:
            continue
        arr = np.array([f if len(f) == 2 else (f[0], f[0]) for f in frames],
                       dtype=float) / 32768
        counts = np.array([c[d] for _, c in tl], dtype=float)
        rose = np.nonzero(np.diff(counts) > 0)[0] + 1
        ev_t, ev_n = times[rose], counts[rose]
        for seg in np.split(np.arange(len(rose)),
                            np.nonzero(np.diff(ev_t) > 1000)[0] + 1):
            if len(seg) < 2:
                continue
            t0, t1 = ev_t[seg[0]], ev_t[seg[-1]]
            n0, n1 = counts[rose[seg[0]] - 1], ev_n[seg[-1]]
            per = (t1 - t0) / (n1 - 1 - n0)         # us a frame
            a = int(np.ceil((t0 - base) * rate / 1e6))
            b = min(int((t1 + per - base) * rate / 1e6), n)
            pos = n0 + ((base + np.arange(a, b) * 1e6 / rate) - t0) / per
            k = np.clip(pos.astype(int), 0, len(arr) - 1)
            k2 = np.clip(k + 1, 0, min(int(n1) - 1, len(arr) - 1))
            frac = (pos - np.floor(pos))[:, None]
            out[a:b] += arr[k] * (1 - frac) + arr[k2] * frac
    tau = base + np.arange(n) * 1e6 / rate
    return out * mixer_gains(run, tau) + fm_notes(run, base, n, rate)[:, None]


def fm_notes(run, base, n, rate):
    """the FM synthesizer's notes, which the chip model doesn't sound: a
    sine at s_fm's pitch while a key is on (B0h bit 5), at a tone's level"""
    out, index, on = np.zeros(n), None, None
    for us, port, v in run.out_log:
        if not port & 1:
            index = v
        elif index == 0xB0 and v & 0x20 and on is None:
            on = us
        elif index == 0xB0 and not v & 0x20 and on is not None:
            a = int((on - base) * rate / 1e6)
            b = min(int((us - base) * rate / 1e6), n)
            out[a:b] += 0.18 * np.sin(2 * np.pi * sb16rec.FM_HZ *
                                      np.arange(b - a) / rate)
            on = None
    return out


class Recorded:
    """a recorded run of the listening sounds on a card, its report and its
    recording, analyzed as they are, and broken"""

    @classmethod
    def setUpClass(cls):
        import test_sb16test as t
        card = cls.card(cls)
        for reg in (0x30, 0x31, 0x32, 0x33):    # master and voice as after
            card.out(0x224, reg)                # a mixer reset, which the
            card.out(0x225, 0xC0)               # synthesis starts from
        cls.run_ = t.Run(card, cls.front,
                         opts=t.O_SHORT | t.O_RECORD | t.O_LISTEN,
                         timeline=True)
        cls.tmp = tempfile.TemporaryDirectory()
        cls.report = os.path.join(cls.tmp.name, "SB16TEST.TXT")
        with open(cls.report, "w") as f:
            f.write("\n".join(cls.run_.lines) + "\n")
        cls.frames = synthesize(cls.run_)
        cls.results = cls.analyze(cls.frames)

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    @classmethod
    def analyze(cls, frames):
        path = os.path.join(cls.tmp.name, "run.wav")
        write_wav(path, frames)
        out = io.StringIO()
        failed = sb16rec.analyze(cls.report, path, out)
        res = {}
        for line in out.getvalue().splitlines():
            parts = line.split(None, 2)
            if len(parts) >= 2 and parts[0].startswith("l.") and \
                    parts[1] in ("PASS", "FAIL", "INFO"):
                res[parts[0]] = (parts[1], parts[2] if len(parts) > 2
                                 else "")
        return failed, res, out.getvalue()

    def window(self, name):
        """a recorded sound's samples in the recording, from the report"""
        checks, sounds, marks = sb16rec.read_report(self.report)
        found = sb16rec.markers(sb16rec.band_limit(self.frames.mean(axis=1),
                                                   RATE))
        a, b = sb16rec.fit_clock(marks, found)[:2]
        spans = sounds[name][2]
        t0, t1 = a * spans[0][0] / 1e6 + b, a * spans[-1][1] / 1e6 + b
        return int(t0 * RATE), int(t1 * RATE)

    def test_every_sound_passes(self):
        failed, res, text = self.results
        self.assertEqual(failed, 0, text)
        self.assertGreaterEqual(len(res), 20, text)
        self.assertIn("markers", text)

    def test_a_flat_tone(self):
        """the right channel's 16-bit tone 40 cents low, as the old fitted
        one was"""
        a, b = self.window("l.right16")
        frames = self.frames.copy()
        n = b - a
        src = np.arange(n) * 2 ** (-40 / 1200)
        frames[a:b, 1] = np.interp(src, np.arange(n), self.frames[a:b, 1])
        failed, res, text = self.analyze(frames)
        self.assertEqual(res["l.right16"][0], "FAIL", text)
        c = float(res["l.right16"][1].split("(")[1].split()[0])
        self.assertLess(c, -35, text)

    def test_a_byte_out(self):
        """a 16-bit stream a byte out: its samples' bytes swapped"""
        a, b = self.window("l.clean.16bit")
        frames = self.frames.copy()
        v = np.round(frames[a:b] * 32767).astype("<i2")
        frames[a:b] = v.byteswap().astype(float) / 32768
        failed, res, text = self.analyze(frames)
        self.assertEqual(res["l.clean.16bit"][0], "FAIL", text)

    def test_a_loop(self):
        """a tone that jumps back a third of a cycle, again and again"""
        a, b = self.window("l.clean.base")
        frames = self.frames.copy()
        part = frames[a:b].copy()
        step = RATE // 20
        out = []
        for i in range(0, len(part), step):
            out.append(part[i:i + step])
            out.append(part[i:i + step // 3])
        frames[a:b] = np.concatenate(out)[:b - a]
        failed, res, text = self.analyze(frames)
        self.assertEqual(res["l.clean.base"][0], "FAIL", text)

    def test_a_dead_channel(self):
        frames = self.frames.copy()
        frames[:, 1] = 0
        failed, res, text = self.analyze(frames)
        for name in ("l.right16", "l.right8", "l.balance"):
            self.assertEqual(res[name][0], "FAIL", (name, text))
        self.assertEqual(res["l.left16"][0], "PASS", text)

    def test_mono(self):
        """a mono recording: no sides to tell, the tones as they are"""
        failed, res, text = self.analyze(self.frames.mean(axis=1,
                                                          keepdims=True))
        self.assertEqual(failed, 0, text)
        self.assertIn("a mono recording", res["l.left16"][1])


@unittest.skipIf(np is None, "needs numpy")
class ModelRecordedTest(Recorded, unittest.TestCase):
    """the model of a Sound Blaster 16"""

    front = False

    def card(self):
        from sbgame import ModelCard
        return ModelCard()


@unittest.skipUnless(np is not None and HAVE_UNICORN and have_nasm(),
                     "needs numpy, nasm and unicorn")
class TSREmmRecordedTest(Recorded, unittest.TestCase):
    """esssb16.com under EMM386 with Audio 2 on DMA 5, the card of the
    reports: everything on Audio 1, the dual test's 16-bit tone alone"""

    front = True

    def card(self):
        from sbfronts import TSRCard
        return TSRCard("emm", dma2=5)

    def test_the_dual_tone(self):
        res = self.results[1]
        self.assertEqual(res["l.dual"][0], "PASS", res["l.dual"])
        self.assertIn("heard h", res["l.dual"][1])


if __name__ == "__main__":
    unittest.main()
