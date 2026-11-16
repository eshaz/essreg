# (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
#
# Licensed under GPL Version 3.0
"""tools/wchkrec.py, the analysis of a recorded esswchk run, on a report
in esswchk's format and a recording made to it: markers at the report's
times, a tone on the left, a pause, both DACs' tones at once, and the
FM's piano on each side. Then broken as the card can break it: a 16-bit
stream a byte off, the tone on the other side, a dual start three frames
apart, and the FM's sides swapped, which ESS's ESFM.DRV does without
TrueSides; and as a recorder makes it: in mono, on a clock that runs
fast, and after essprobe's run in the same file, whose markers differ.

usage: python3 -m unittest tests.test_wchkrec   (needs numpy)
"""

import io
import os
import re
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(os.path.dirname(HERE), "tools"))

try:
    import numpy as np
    import essprec
    import wchkrec
    from test_sb16rec import dtmf, sine, write_wav
except ImportError:             # the analysis needs numpy
    np = wchkrec = None

RATE = 48000
LEAD = 1.0                      # the recording before the start marker

# (name, what it expects, its spec)
SOUNDS = [("p.start44a", "left", "dev=a1 r=44100 f=16s side=l"),
          ("p.start44b", "left", "dev=a1 r=44100 f=16s side=l"),
          ("p.start22", "left", "dev=a1 r=22050 f=16s side=l"),
          ("p.restart", "lpause", "dev=a1 r=44100 f=16s side=l pause=600"),
          ("p.dual44a", "step", "dev=a1 r=44100 f=16s ch=4"),
          ("p.dual44b", "step", "dev=a1 r=44100 f=16s ch=4"),
          ("p.fm0", "fml", "dev=esfm cc10=0 note=69"),
          ("p.fm127", "fmr", "dev=esfm cc10=127 note=69")]


def piano(secs, rate=RATE):
    """an FM piano's note, 440 Hz with its harmonics, dying away"""
    t = np.arange(int(secs * rate)) / rate
    x = sum(0.3 / k * np.sin(2 * np.pi * 440 * k * t) for k in (1, 2, 3, 5))
    return x * np.exp(-2.0 * t)


class Run:
    """a report in esswchk's format and the recording made to it; `sound`
    gives each sound's frames in place of the right ones, `delay` a dual
    sound's right in frames at 44,100 Hz"""

    def __init__(self, true_sides=True, delay=None, sound=None, mono=False,
                 drift=0.0, before=None):
        self.lines = ["esswchk 1.0: the drivers' fixes on the ES1869, in "
                      "Windows", "", "[fm] the FM, recorded digitally",
                      "fm.pan0              %s  left -12.3, right -120.0 "
                      "dBFS, panned left; TrueSides %s; 9000 frames" %
                      ("PASS " if true_sides else "KNOWN",
                       "on" if true_sides else "off"),
                      "", "[rec] a recorded run's sounds",
                      "rec.start            INFO  marker BADC at 0 us; the "
                      "level tone was 441 Hz"]
        delay = delay or {}
        sound = sound or {}
        t, events = 0.6, [(0.0, dtmf("BADC"))]
        for i, (name, expect, spec) in enumerate(SOUNDS):
            syms = "#%03d*" % (i + 1)
            events.append((t, dtmf(syms)))
            on = t + 0.7
            if expect == "lpause":
                parts = [on, on + 0.6, on + 1.0, on + 2.0]
            else:
                parts = [on, on + 1.0]
            for a, b in zip(parts[0::2], parts[1::2]):
                events.append((a, sound.get(name, self.make)(
                    name, expect, b - a, delay.get(name, 0.0))))
            self.lines += ["%-20s INFO  recorded: marker %s at %d us, "
                           "sound%s us, %s expected" % (
                               name, syms, round(t * 1e6), "".join(
                                   " %d-%d" % (round(a * 1e6),
                                               round(b * 1e6))
                                   for a, b in zip(parts[0::2],
                                                   parts[1::2])),
                               expect),
                           "%-20s spec  %s" % (name, spec)]
            t = parts[-1] + 0.5
        events.append((t, dtmf("CDAB")))
        self.lines.append("rec.end              INFO  marker CDAB at %d us"
                          % round(t * 1e6))
        self.lines += ["", "[summary] done", "pass=7 fail=0 known=0 info=12 "
                       "skip=0"]
        # the recorder's clock runs drift fast: the run's second is longer
        scale = 1 + drift
        n = int((LEAD + t + 1.0) * scale * RATE)
        out = np.zeros((n, 2))
        for at, x in events:
            if x.ndim == 1:
                x = np.column_stack((x, x))
            a = int((LEAD + at) * scale * RATE)
            if drift:
                old = np.arange(len(x))
                new = np.arange(int(len(x) * scale)) / scale
                x = np.column_stack([np.interp(new, old, x[:, c])
                                     for c in range(2)])
            out[a:a + len(x)] += x[:n - a]
        if before is not None:
            out = np.concatenate((before, out))
        self.frames = out.mean(axis=1, keepdims=True) if mono else out

    @staticmethod
    def make(name, expect, secs, delay):
        """the sound as the card plays it"""
        tone = sine(441.0, secs, amp=0.3)
        zero = np.zeros(len(tone))
        if expect in ("left", "lpause"):
            return np.column_stack((tone, zero))
        if expect == "step":
            late = sine(441.0, secs, amp=0.3,
                        phase=-2 * np.pi * 441.0 * delay / 44100.0)
            return np.column_stack((tone, late))
        note = piano(secs)
        zero = np.zeros(len(note))
        return np.column_stack((note, zero) if expect == "fml" else
                               (zero, note))

    def analyze(self, tmp, name="run"):
        report = os.path.join(tmp, name + ".txt")
        wav = os.path.join(tmp, name + ".wav")
        with open(report, "w", newline="\r\n") as f:
            f.write("\n".join(self.lines) + "\n")
        write_wav(wav, self.frames)
        out = io.StringIO()
        failed = wchkrec.analyze(report, wav, out)
        res = {}
        for line in out.getvalue().splitlines():
            parts = line.split(None, 2)
            if len(parts) == 3 and parts[0].startswith("p.") and \
                    parts[1] in ("PASS", "FAIL", "KNOWN", "INFO"):
                res[parts[0]] = (parts[1], parts[2])
        return res, failed, out.getvalue()


def recording(report, rate=RATE):
    """a recording made to a report of esswchk's own: each marker at its
    time, each sound as it should hold, LEAD seconds in"""
    _checks, sounds, marks = wchkrec.read_report(report)
    end = max(list(marks.values()) + [b for _m, _u, spans, _w in
                                      sounds.values() for _a, b in spans])
    out = np.zeros((int((LEAD + end / 1e6 + 1.0) * rate), 2))

    def put(us, x):
        if x.ndim == 1:
            x = np.column_stack((x, x))
        a = int((LEAD + us / 1e6) * rate)
        out[a:a + len(x)] += x[:len(out) - a]
    for text, us in marks.items():
        put(us, dtmf(text, rate))
    for name, (_m, _us, spans, want) in sounds.items():
        for a, b in spans:
            put(a, Run.make(name, want, (b - a) / 1e6, 0.0))
    return out


@unittest.skipUnless(np is not None, "needs numpy")
class WchkrecTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)

    def run_of(self, **kw):
        return Run(**kw).analyze(self.tmp.name)

    def test_every_sound_passes(self):
        res, failed, text = self.run_of()
        self.assertEqual(failed, 0, text)
        self.assertEqual({k: v[0] for k, v in res.items()},
                         {name: "PASS" for name, _e, _s in SOUNDS}, text)
        self.assertRegex(res["p.dual44a"][1], r"the right [+-]0\.0\d frames")
        self.assertIn("the clock: 10 markers", text)
        self.assertIn("a spread of 0.0", text)

    def test_a_stream_a_byte_off(self):
        """noise where the tone should be: the line names the fault"""
        def noise(name, expect, secs, delay):
            x = np.random.default_rng(1).uniform(-0.3, 0.3, int(secs * RATE))
            return np.column_stack((x, np.zeros(len(x))))
        res, failed, text = self.run_of(sound={"p.start44b": noise})
        self.assertEqual(res["p.start44b"][0], "FAIL", text)
        self.assertIn("a stream a byte off", res["p.start44b"][1])
        self.assertEqual(failed, 1)

    def test_the_other_side(self):
        def right(name, expect, secs, delay):
            return Run.make(name, expect, secs, delay)[:, ::-1]
        res, _failed, text = self.run_of(sound={"p.start22": right})
        self.assertEqual(res["p.start22"][0], "FAIL", text)

    def test_dual_start(self):
        """three frames apart fails, under half a frame passes"""
        res, _failed, text = self.run_of(delay={"p.dual44a": 3.0,
                                                "p.dual44b": 0.4})
        self.assertEqual(res["p.dual44a"][0], "FAIL", text)
        self.assertIn("the right +3.00 frames after the left",
                      res["p.dual44a"][1])
        self.assertEqual(res["p.dual44b"][0], "PASS", text)
        self.assertIn("+0.40 frames", res["p.dual44b"][1])
        self.assertIn("a spread of 2.60", text)

    def test_true_sides_off(self):
        """ESS's ESFM.DRV plays the left channel on the right: KNOWN where
        the report says TrueSides is off, FAIL where it says on"""
        def swap(name, expect, secs, delay):
            return Run.make(name, expect, secs, delay)[:, ::-1]
        swapped = {"p.fm0": swap, "p.fm127": swap}
        res, _failed, text = self.run_of(true_sides=False, sound=swapped)
        self.assertEqual(res["p.fm0"][0], "KNOWN", text)
        self.assertIn("plays it on the right", res["p.fm0"][1])
        self.assertEqual(res["p.fm127"][0], "KNOWN", text)
        res, _failed, text = self.run_of(sound=swapped)
        self.assertEqual(res["p.fm0"][0], "FAIL", text)

    def test_mono(self):
        """a mono recording has no sides: INFO, and the pause still
        measured"""
        res, failed, text = self.run_of(mono=True)
        for name in ("p.start44a", "p.dual44a", "p.fm0"):
            self.assertEqual(res[name][0], "INFO", name)
        self.assertEqual(failed, 0, text)

    def test_clock_drift(self):
        """a recorder's clock 0.2 % fast: the fit takes it"""
        res, failed, text = self.run_of(drift=0.002)
        self.assertEqual(failed, 0, text)
        ppm = float(re.search(r"the clock: 10 markers, ([\d.]+) ppm",
                              text).group(1))
        self.assertTrue(1500 < ppm < 2500, text)

    def test_after_essprobe(self):
        """essprobe's run first in the same recording: each tool finds
        its own markers and sounds"""
        probe = ["essprobe 2.0: the ES1869 itself, without a front",
                 "rec.start            INFO  marker ABCD at 0 us",
                 "a1.base              INFO  recorded: marker *001# at "
                 "600000 us, sound 1300000-2300000 us, tone expected",
                 "a1.base              spec  S",
                 "rec.end              INFO  marker DCBA at 2800000 us"]
        part = np.zeros((int(5.0 * RATE), 2))
        for at, x in ((0.5, dtmf("ABCD")), (1.1, dtmf("*001#")),
                      (3.3, dtmf("DCBA"))):
            a = int(at * RATE)
            part[a:a + len(x)] += x[:, None]
        tone = sine(441.0, 1.0, amp=0.3)
        part[int(1.8 * RATE):int(1.8 * RATE) + len(tone)] += tone[:, None]
        run = Run(before=part)
        res, failed, text = run.analyze(self.tmp.name)
        self.assertEqual(failed, 0, text)
        self.assertEqual(len(res), len(SOUNDS))
        report = os.path.join(self.tmp.name, "PROBE.TXT")
        with open(report, "w", newline="\r\n") as f:
            f.write("\n".join(probe) + "\n")
        out = io.StringIO()
        self.assertEqual(essprec.analyze(report, os.path.join(
            self.tmp.name, "run.wav"), out), 0, out.getvalue())
        self.assertRegex(out.getvalue(), r"a1\.base +PASS")


if __name__ == "__main__":
    unittest.main()
