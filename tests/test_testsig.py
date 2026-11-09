# (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
#
# Licensed under GPL Version 3.0
"""Test tools/testsig.py and tools/sigan.py: the signals keep what their
table promises and are the same bytes at every run, and the analysis finds
known answers in paths simulated here, with code of their own: delays,
gains, filters, crosstalk, clocks apart, noise, distortion, jitter, a
swapped or summed recorder and a stage that clips. docs/TEST_SIGNALS.md and
docs/TEST_LEVELS.md must be current.

usage: python3 -m unittest tests.test_testsig
"""

import hashlib
import json
import math
import os
import sys
import tempfile
import unittest

import numpy as np

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))

import sigan  # noqa: E402
import testsig as ts  # noqa: E402

RATE = 44100
_MADE = {}


def made(name, rate=RATE, side=None):
    key = (name, rate, side)
    if key not in _MADE:
        _MADE[key] = ts.render(ts.BY_NAME[name], rate, side)
    return _MADE[key]


def db(v):
    return 20 * math.log10(max(abs(v), 1e-15))


# --- paths with known answers -------------------------------------------------

def bh4(t):
    """a 4-term Blackman-Harris window over -1..1"""
    a = (0.35875, 0.48829, 0.14128, 0.01168)
    u = np.pi * (t + 1)
    return a[0] - a[1] * np.cos(u) + a[2] * np.cos(2 * u) - \
        a[3] * np.cos(3 * u)


def at_times(x, t, taps=24):
    """x (frames by channels), band-limited, at fractional frames t"""
    out = np.zeros((len(t), x.shape[1]))
    k = np.arange(-taps + 1, taps + 1)
    for a in range(0, len(t), 8192):
        tt = t[a:a + 8192]
        idx = np.floor(tt).astype(int)[:, None] + k[None, :]
        d = idx - tt[:, None]
        w = np.sinc(d) * bh4(np.clip(d / taps, -1, 1))
        ok = (idx >= 0) & (idx < len(x))
        idx = np.clip(idx, 0, len(x) - 1)
        for c in range(x.shape[1]):
            out[a:a + 8192, c] = np.sum(np.where(ok, x[idx, c], 0) * w, 1)
    return out


def record(x, delay, ppm=0.0, jitter=None, extra=4000):
    """what a recorder hears of x from a player ppm fast, delay frames in,
    with sinusoidal jitter of (peak frames, Hz) on the player"""
    n = int(len(x) / (1 + ppm * 1e-6) + delay + extra)
    m = np.arange(n, dtype=float)
    t = (m - delay) * (1 + ppm * 1e-6)
    if jitter:
        t = t + jitter[0] * np.sin(2 * np.pi * jitter[1] * m / RATE)
    return at_times(x, t)


def shift(x, delay, extra=4000):
    """x later by delay frames, exactly, by the FFT"""
    n = len(x) + int(delay) + extra
    nf = 1 << int(math.ceil(math.log2(n + 1024)))
    s = np.fft.rfft(x, nf, axis=0)
    s *= np.exp(-2j * np.pi * np.arange(len(s)) * delay / nf)[:, None]
    return np.fft.irfft(s, nf, axis=0)[:n]


def filt(x, h, rate=RATE):
    """x through a response given as a function of Hz, by the FFT"""
    n = len(x)
    nf = 1 << int(math.ceil(math.log2(n + rate)))
    f = np.fft.rfftfreq(nf, 1.0 / rate)
    return np.fft.irfft(np.fft.rfft(x, nf, axis=0) * h(f)[:, None], nf,
                        axis=0)[:n]


def lp2(fc, q):
    return lambda f: 1 / ((1j * f / fc) ** 2 + 1j * f / fc / q + 1)


def hp1(fc):
    return lambda f: (1j * f / fc) / (1 + 1j * f / fc)


def path_h(f):
    """the filter of the response checks: two poles at 8 kHz, one zero"""
    return lp2(8000, 0.9)(f) * hp1(20)(f)


def noise(shape, level_db, seed=1):
    rng = np.random.RandomState(seed)
    return rng.standard_normal(shape) * 10 ** (level_db / 20) / math.sqrt(2)


def analyze(name, y, side=None, ref=None, rate=RATE):
    m = made(name, rate, side)
    text, ctx = sigan.analyze(m, sigan.Rec.of(y, rate),
                              None if ref is None else
                              sigan.Rec.of(ref, rate))
    return (ctx.rep.values if ctx else {}), text


def at_f(fh, hz):
    """a response's bin nearest hz: its frequency and value"""
    f, h = fh
    ok = np.flatnonzero(np.isfinite(h))
    i = ok[np.argmin(np.abs(f[ok] - hz))]
    return f[i], h[i]


def at(fh, hz):
    return at_f(fh, hz)[1]


# --- the signals ---------------------------------------------------------------

class SignalsTest(unittest.TestCase):
    def test_table(self):
        names = [s.name for s in ts.SIGNALS]
        self.assertEqual(len(names), len(set(names)))
        groups = {g for g, _t in ts.GROUPS}
        for s in ts.SIGNALS:
            self.assertIn(s.family, ts.FAMILIES, s.name)
            self.assertIn(s.group, groups, s.name)
            self.assertTrue(s.rates and set(s.paths) <= set("LOIDQ"),
                            s.name)
            self.assertTrue(s.what and s.gives and s.good, s.name)
        self.assertTrue(set(ts.SIDES) <= set(names))
        for _key, _v, sigs, _text in ts.SETTINGS:
            for v in sigs.split(","):
                self.assertIn(v.strip(), names)
        self.assertEqual({c for c, _t, _g in ts.PATHS}, set("LOIDQ"))

    def test_same_bytes(self):
        """every run makes the same bytes"""
        for name in ("noise-white", "program", "multitone", "cal",
                     "jtest-hot", "sweep"):
            a = ts.render(ts.BY_NAME[name], RATE)
            b = ts.render(ts.BY_NAME[name], RATE)
            self.assertEqual(ts.wav_bytes(a.data, RATE),
                             ts.wav_bytes(b.data, RATE), name)

    def test_hot_and_length(self):
        """a hot signal's name says so, and no file runs over 60 s"""
        for s in ts.SIGNALS:
            seconds, _level, peak = ts.stats(s)
            self.assertEqual("hot" in s.name, peak >= ts.HOT_DB, s.name)
            self.assertLessEqual(seconds, 60.0, s.name)

    def test_markers(self):
        """a chirp on the left at each end, then one on the right"""
        for name in ("sweep", "noise-pink", "levelcal-hot", "jtest"):
            m = made(name)
            mk = ts.quantize(ts.marker(RATE)).astype(int)
            for at0 in m.info["marks"]:
                left = m.data[at0:at0 + len(mk), 0].astype(int)
                b = at0 + m.info["mark_sep"]
                right = m.data[b:b + len(mk), 1].astype(int)
                self.assertLessEqual(np.abs(left - mk).max(), 1, name)
                self.assertLessEqual(np.abs(right - mk).max(), 1, name)

    def test_levels(self):
        """each body at the table's level"""
        for name in ("sweep", "sweep-low", "sweep-hf", "noise-white",
                     "noise-pink", "program", "multitone", "cal"):
            s = ts.BY_NAME[name]
            m = made(name)
            b, n = m.info["body"]
            x = m.floats()[b:b + n]
            if s.family in ("ess", "linsweep"):
                level = db(np.abs(x).max())
            elif s.family == "multitone":
                p = m.info["period"]
                x = x[p:2 * p]
                level = db(math.sqrt(2 * np.mean(x[:, 0] ** 2)))
            elif s.family == "program":
                level = db(math.sqrt(2 * np.mean(x * x)))
            else:
                level = db(math.sqrt(2 * np.mean(x[:, 0] ** 2)))
            self.assertAlmostEqual(level, s.level, delta=0.05, msg=name)
        m = made("levelcal-hot")
        for st in m.info["steps"]:
            a = st["at"] + st["ramp"]
            amp, _p, _f, _r = sigan.fit_sine(m.floats()[a:a + st["len"], 0],
                                             RATE, st["f"][0])
            self.assertAlmostEqual(db(amp), st["level"], delta=0.02)

    def test_whole_cycles(self):
        for name, rate in (("pilot-19600", 44100), ("pilot-11025", 44100),
                           ("pilot-12000", 48000)):
            m = made(name, rate)
            a, n = m.info["steady"]
            x = m.data[a:a + n, 0]
            p = m.info["table"]
            self.assertTrue(np.array_equal(x[p:], x[:-p]), name)
            self.assertEqual(m.info["cycles"] * rate, ts.BY_NAME[name]
                             .params["f"] * p, name)
        for name in ("multitone", "cal"):
            m = made(name)
            p, k = m.info["period"], m.info["periods"]
            b = m.info["body"][0]
            one = m.data[b + p:b + 2 * p]
            for j in range(2, k - 1):
                self.assertTrue(np.array_equal(
                    one, m.data[b + j * p:b + (j + 1) * p]), name)
            self.assertEqual(p & (p - 1), 0)
        for name in ("imd-smpte", "imd-ccif"):
            m = made(name)
            for f in (m.info["f1"], m.info["f2"]):
                self.assertAlmostEqual(f * 65536 / RATE,
                                       round(f * 65536 / RATE), places=6)

    def test_exact_patterns(self):
        m = made("jtest-hot")
        b, n = m.info["body"]
        i = np.arange(n)
        want = np.where(i % 4 < 2, 16384, -16384) - (i // 96) % 2
        self.assertTrue(np.array_equal(m.data[b:b + n, 0], want))
        m = made("lsb")
        b, n = m.info["body"]
        self.assertEqual(set(np.unique(m.data[b:b + n])),
                         {0, 1, -1, 2, -2, 4, -4, 8, -8})
        m = made("impulses")
        b, n = m.info["body"]
        nz = np.flatnonzero(m.data[b:b + n, 0])
        self.assertEqual(list(nz), list(range(0, n, m.info["period"])))
        m = made("mls")
        b = m.info["body"][0]
        p = m.info["period"]
        one = m.floats()[b + p:b + 2 * p, 0]
        spec = np.abs(np.fft.fft(one))[1:]
        self.assertLess(spec.max() / spec.min(), 1.0001)
        m = made("dither")
        b, n = m.info["body"]
        v = m.data[b:b + n]
        self.assertEqual(set(np.unique(v)), {-1, 0, 1})
        self.assertAlmostEqual(float(np.var(v)), 0.25, delta=0.01)
        m = made("silence")
        b, n = m.info["body"]
        self.assertFalse(np.any(m.data[b:b + n]))

    def test_multitones(self):
        """tones on their bins alone, flat or pink, crest near a sine's"""
        for name in ("multitone", "multitone-pink", "cal"):
            m = made(name)
            p = m.info["period"]
            b = m.info["body"][0]
            one = m.floats()[b + p:b + 2 * p]
            f = np.fft.rfftfreq(p, 1.0 / RATE)
            for c in (0, 1):
                spec = np.abs(np.fft.rfft(one[:, c]))
                bins = np.array(m.info["bins"][c])
                rest = np.delete(spec, bins)
                self.assertLess(db(rest.max() / np.median(spec[bins])), -60)
                cf = db(np.abs(one[:, c]).max() /
                        math.sqrt(np.mean(one[:, c] ** 2)))
                self.assertLess(cf, 3.5, name)
                if m.info["color"] == "white":
                    a = spec[bins]
                    self.assertLess(db(a.max() / a.min()), 0.1, name)
                else:
                    octs = [db(math.sqrt(np.sum(spec[(f >= lo) &
                                                     (f < 2 * lo)] ** 2)))
                            for lo in (125, 250, 500, 1000, 2000, 4000)]
                    self.assertLess(max(octs) - min(octs), 1.0, name)

    def test_noises(self):
        def octaves(x):
            gxx, _a, _b, k = sigan.welch(x[:, None], x[:, None], 8192)
            f = np.fft.rfftfreq(8192, 1.0 / RATE)
            p = gxx[:, 0, 0].real
            return [10 * math.log10(p[(f >= lo) & (f < 2 * lo)].sum())
                    for lo in (62.5, 125, 250, 500, 1000, 2000, 4000, 8000)]
        for name in ("noise-white", "noise-pink", "noise-white-same"):
            m = made(name)
            b, n = m.info["body"]
            x = m.floats()[b:b + n]
            octs = octaves(x[:, 0])
            if "white" in name:
                # 3 dB more an octave, for the same density
                octs = [v - 3.0103 * i for i, v in enumerate(octs)]
            self.assertLess(max(octs) - min(octs), 1.2, name)
            r = np.corrcoef(x[:, 0], x[:, 1])[0, 1]
            if name.endswith("same"):
                self.assertTrue(np.array_equal(x[:, 0], x[:, 1]))
            else:
                self.assertLess(abs(r), 0.02, name)
        m = made("program")
        b, n = m.info["body"]
        x = m.floats()[b:b + n]
        self.assertAlmostEqual(np.corrcoef(x[:, 0], x[:, 1])[0, 1], 0.6,
                               delta=0.06)
        cf = db(np.abs(x[:, 0]).max() / math.sqrt(np.mean(x[:, 0] ** 2)))
        self.assertTrue(13 < cf < 19, cf)

    def test_sweep_band(self):
        m = made("sweep")
        start, n, _c = m.info["segments"][0]
        x = m.floats()[start:start + n, 0]
        f = np.fft.rfftfreq(n, 1.0 / RATE)
        p = np.abs(np.fft.rfft(x)) ** 2
        inside = p[(f >= m.info["f1"]) & (f <= m.info["f2"])].sum()
        self.assertGreater(inside / p.sum(), 0.998)
        near = p[(f >= m.info["f1"] / 2) & (f <= 1.03 * m.info["f2"])].sum()
        self.assertGreater(near / p.sum(), 0.9999)
        self.assertAlmostEqual(m.info["f2"], 0.95 * RATE / 2)

    def test_sides(self):
        base, left = made("sweep"), made("sweep", RATE, "l")
        b, n = base.info["body"]
        self.assertFalse(np.any(left.data[b:b + n, 1]))
        self.assertTrue(np.array_equal(left.data[b:b + n, 0],
                                       base.data[b:b + n, 0]))
        self.assertTrue(np.array_equal(left.data[:b], base.data[:b]))

    def test_find(self):
        self.assertEqual(ts.find("sweep-l-44k"),
                         (ts.BY_NAME["sweep"], 44100, "l"))
        self.assertEqual(ts.find("out/x/levelcal-hot-48k.wav"),
                         (ts.BY_NAME["levelcal-hot"], 48000, None))
        self.assertEqual(ts.find("noise-white-32k"),
                         (ts.BY_NAME["noise-white"], 32000, None))
        self.assertEqual(ts.find("pilot-12000")[1], 48000)
        # a directory after --only is a name that matches nothing
        with open(os.devnull, "w") as null:
            old, sys.stderr = sys.stderr, null
            try:
                with self.assertRaises(SystemExit):
                    ts.main(["--only", "cal-44k", "out/elsewhere"])
            finally:
                sys.stderr = old

    def test_manifest(self):
        pairs = [(ts.BY_NAME["cal"], 44100, None),
                 (ts.BY_NAME["sweep-quick"], 44100, "l")]
        with tempfile.TemporaryDirectory() as tmp:
            ts.write_all(tmp, pairs, quiet=True)
            with open(os.path.join(tmp, "manifest.json")) as f:
                man = json.load(f)["signals"]
            self.assertEqual([e["name"] for e in man],
                             ["sweep-quick-l-44k", "cal-44k"])
            sums = open(os.path.join(tmp, "SHA256SUMS")).read().split("\n")
            for e, line in zip(man, sums):
                data = open(os.path.join(tmp, e["file"]), "rb").read()
                self.assertEqual(hashlib.sha256(data).hexdigest(),
                                 e["sha256"])
                self.assertEqual(line, "%s  %s" % (e["sha256"], e["file"]))
                self.assertEqual(len(data), 44 + 4 * e["frames"])
            cal = man[1]["layout"]
            self.assertEqual(len(cal["phases"][0]), len(cal["bins"][0]))

    def test_docs_current(self):
        """the docs' tables as testsig.py --doc makes them, in this process
        so that the signals' figures are made once"""
        for path in ts.DOCS:
            with open(path, encoding="ascii", newline="") as f:
                old = f.read()
            self.assertEqual(ts.doc_text(path, old), old,
                             "run python3 tools/testsig.py --doc")


# --- the analysis ---------------------------------------------------------------

class AnalysisTest(unittest.TestCase):
    def test_delay_and_gain(self):
        y = shift(made("sweep-quick").floats() * 10 ** (-6.2 / 20),
                  12345.37)
        y += noise(y.shape, -110)
        v, _t = analyze("sweep-quick", y)
        self.assertAlmostEqual(v["lag"], 12345.37, delta=0.005)
        self.assertAlmostEqual(v["ppm"], 0, delta=0.01)
        self.assertAlmostEqual(db(at(v["response:L+R>o"], 1000)), -6.2,
                               delta=0.02)
        # a quiet path with the ADC's offset that the Armada once had,
        # -1005 LSB, over twice the markers' rms
        y = shift(made("sweep-quick").floats() * 10 ** (-12.7 / 20), 777.0)
        y += noise(y.shape, -80) - 1005 / 32768.0
        v, _t = analyze("sweep-quick", y)
        self.assertAlmostEqual(v["lag"], 777.0, delta=0.01)
        self.assertAlmostEqual(db(at(v["response:L+R>o"], 1000)), -12.7,
                               delta=0.05)

    def test_clocks(self):
        """a player 37 ppm fast, by markers, pilot, chirps and periods"""
        for name in ("sweep-quick", "pilot-11025", "chirps", "cal"):
            y = record(made(name).floats(), 9876.25, ppm=37.0) * 0.5
            v, _t = analyze(name, y)
            self.assertAlmostEqual(v["ppm"], 37.0, delta=0.03, msg=name)
            if name == "pilot-11025":
                self.assertAlmostEqual(v["pilot:0"]["ppm"], 37.0, delta=0.005)
            if name == "chirps":
                self.assertAlmostEqual(v["chirps-left"]["ppm"], 37.0,
                                       delta=0.02)
                self.assertLess(v["chirps-left"]["rms_us"], 0.05)
            if name == "sweep-quick":
                self.assertAlmostEqual(db(at(v["response:L+R>o"], 5000)),
                                       -6.02, delta=0.03)
            if name == "cal":
                self.assertAlmostEqual(db(at(v["bins:L>L"], 5000)), -6.02,
                                       delta=0.03)

    def test_filter(self):
        """a known response, in level and phase up to a delay"""
        pts = np.array([200.0, 1000, 3000, 5000, 8000, 10000, 15000])
        for name, key in (("sweep", "response:L+R>o"),
                          ("noise-white", "response:L>L"),
                          ("mls", "response:L+R>o"),
                          ("impulses", "response:L+R>o"),
                          ("multitone", "bins:L>L"), ("cal", "bins:R>R")):
            y = shift(filt(made(name).floats(), path_h), 7777.5)
            v, _t = analyze(name, y)
            fk, got = map(np.array, zip(*[at_f(v[key], p) for p in pts]))
            want = path_h(fk)
            err = 20 * np.log10(np.abs(got) / np.abs(want))
            self.assertLess(np.abs(err).max(), 0.05, (name, err))
            ph = np.unwrap(np.angle(got / want))
            line = np.polyval(np.polyfit(fk, ph, 1), fk)
            self.assertLess(np.degrees(np.abs(ph - line)).max(), 1.0, name)
        v, _t = analyze("steps", shift(filt(made("steps").floats(), path_h),
                                       7777.5))
        for row in v["tones"]:
            if 100 <= row[0] <= 16000:
                self.assertAlmostEqual(row[3], db(path_h(row[0])),
                                       delta=0.01)

    def test_crosstalk(self):
        """right into left at -40 dB a sample later, left into right at -46"""
        for name, key in (("noise-white", "response:%s"),
                          ("multitone", "bins:%s"),
                          ("sweep-lr", "response:%s"),
                          ("separation", None)):
            x = made(name).floats()
            yl = x[:, :1] + 0.01 * shift(x[:, 1:], 1.0, 0)[:len(x)]
            yr = 0.9 * x[:, 1:] + 0.005 * x[:, :1]
            y = shift(np.hstack([yl, yr]), 3333.3)
            y += noise(y.shape, -110)
            v, _t = analyze(name, y)
            if key is None:
                (ll, lr), (rl, rr) = v["separation"]
                self.assertAlmostEqual(lr, -40.0, delta=0.2)
                self.assertAlmostEqual(rl, -46.0, delta=0.2)
                continue
            self.assertAlmostEqual(db(at(v[key % "R>L"], 1000)), -40.0,
                                   delta=0.2, msg=name)
            self.assertAlmostEqual(db(at(v[key % "L>R"], 1000)), -46.02,
                                   delta=0.2, msg=name)
            self.assertAlmostEqual(db(at(v[key % "R>R"], 1000)), -0.92,
                                   delta=0.05, msg=name)

    def test_distortion(self):
        """x + 0.5 x^2 + 2 x^3: harmonics and intermodulation as theory has
        them"""
        def nl(x):
            return x + 0.5 * x ** 2 + 2.0 * x ** 3
        a = 0.1
        fund = a + 0.75 * 2.0 * a ** 3
        h2, h3 = db(0.5 * a * a / 2 / fund), db(2.0 * a ** 3 / 4 / fund)
        v, _t = analyze("steps", shift(nl(made("steps").floats()), 5000.0))
        row = [r for r in v["tones"] if r[0] == 1000 and r[1] == 0][0]
        self.assertAlmostEqual(row[7], h2, delta=0.3)
        self.assertAlmostEqual(row[8], h3, delta=0.3)
        self.assertAlmostEqual(row[3], db(fund / a), delta=0.02)
        v, _t = analyze("sweep", shift(nl(made("sweep").floats()), 5000.0))
        harm = v["harm:L+R>o"][1000]
        self.assertAlmostEqual(harm[0], h2, delta=0.5)
        self.assertAlmostEqual(harm[1], h3, delta=0.5)
        v, _t = analyze("imd-smpte", shift(nl(made("imd-smpte").floats()),
                                           5000.0))
        a1, a2 = 0.8 * 10 ** (-12 / 20), 0.2 * 10 ** (-12 / 20)
        f2 = a2 + 2.0 * (0.75 * a2 ** 3 + 1.5 * a1 * a1 * a2)
        self.assertAlmostEqual(v["imd:0"]["f2-1f1"], db(0.5 * a1 * a2 / f2),
                               delta=0.3)
        self.assertAlmostEqual(v["imd:0"]["f2+2f1"],
                               db(0.75 * 2.0 * a1 * a1 * a2 / f2), delta=0.3)

    def test_jitter(self):
        """sinusoidal jitter of 0.01 frames at the player"""
        peak = 0.01 / RATE
        y = record(made("pilot-11025").floats(), 5000.0,
                   jitter=(0.01, 300.0))
        v, _t = analyze("pilot-11025", y)
        self.assertAlmostEqual(v["jitter_ns"], peak / math.sqrt(2) * 1e9,
                               delta=0.05 * peak / math.sqrt(2) * 1e9)
        y = record(made("jtest-hot").floats(), 5000.0,
                   jitter=(0.01, RATE / 192.0))
        v, _t = analyze("jtest-hot", y)
        self.assertAlmostEqual(v["jtest:0"][1], peak * 1e12,
                               delta=0.1 * peak * 1e12)

    def test_noise_floor(self):
        x = made("silence").floats()
        y = shift(x, 3000.0) + noise((len(x) + 7000, 2), -90)
        t = np.arange(len(y)) / RATE
        y += 10 ** (-105 / 20) * np.sin(2 * np.pi * 1000 * t)[:, None]
        v, _t = analyze("silence", y)
        # white noise keeps 19980 Hz of 22050 in the band
        self.assertAlmostEqual(v["noise:0"][0],
                               -90 + 10 * math.log10(19980 / 22050),
                               delta=0.3)
        hz = [(float(r[1]), float(r[2])) for r in v["lines"]]
        near = [lv for f, lv in hz if abs(f - 1000) < 5]
        self.assertTrue(near and abs(near[0] + 105) < 1.5, hz)

    def test_dc_highpass(self):
        y = shift(filt(made("dc-step").floats(), hp1(3.0)), 3000.0)
        v, _t = analyze("dc-step", y)
        for row in v["dc"]:
            self.assertAlmostEqual(float(row[4]), 3.0, delta=0.06)

    def test_polarity_and_swap(self):
        x = made("polarity").floats()
        v, text = analyze("polarity", shift(np.hstack([-x[:, :1], x[:, 1:]]),
                                            2000.0))
        self.assertEqual(v["polarity"], ["out L carries the left side, "
                                         "inverted",
                                         "out R carries the right side"])
        # a recorder of the other sides is swapped back first
        v, text = analyze("polarity", shift(np.hstack([x[:, 1:], -x[:, :1]]),
                                            2000.0))
        self.assertIn("swapped back", text)
        self.assertEqual(v["polarity"][1], "out R carries the right side")

    def test_dual_skew(self):
        x = made("dual-skew").floats()
        mix = shift(x[:, 0:1], 3.25, 0)[:len(x)] + x[:, 2:3]
        y = record(np.hstack([mix, mix]), 4321.5, ppm=-29.0)
        v, _t = analyze("dual-skew", y)
        self.assertAlmostEqual(v["skew"], 3.25, delta=0.02)

    def test_recorders(self):
        """a recorder of the sides' difference, and one with them swapped"""
        x = made("noise-white").floats()
        d = 0.5 * (x[:, :1] - x[:, 1:])
        y = shift(np.hstack([d, d]), 4000.0) + noise((len(x) + 8000, 2), -100)
        v, _t = analyze("noise-white", y)
        self.assertIn("difference", v["mix"])
        left, right = at(v["response:L>o"], 1000), at(v["response:R>o"],
                                                      1000)
        self.assertAlmostEqual(db(left), -6.02, delta=0.05)
        self.assertAlmostEqual(db(right), -6.02, delta=0.05)
        self.assertAlmostEqual(abs(np.angle(right / left, deg=True)), 180,
                               delta=1)
        xs = made("sweep").floats()
        ds = xs[:, :1] - xs[:, 1:]
        v, text = analyze("sweep", shift(np.hstack([ds, ds]), 4000.0))
        self.assertIn("cancels", text)
        y = shift(x[:, ::-1], 2222.0)
        v, text = analyze("noise-white", y)
        self.assertIn("swapped back", text)
        self.assertAlmostEqual(db(at(v["response:L>L"], 1000)), 0, delta=0.02)

    def test_level_calibration(self):
        """a stage of 4 dB that clips softly at 0.7 of full scale"""
        x = made("levelcal-hot").floats()
        y = shift(0.7 * np.tanh(10 ** (4 / 20) * x / 0.7), 2500.0) * \
            10 ** (-10 / 20)
        v, _t = analyze("levelcal-hot", y)
        lv = v["level:0"]
        self.assertAlmostEqual(lv["g0"], -6.0, delta=0.1)
        self.assertTrue(lv["clip"] is not None and -30 <= lv["clip"] <= -10)
        v, _t = analyze("levelcal-hot", shift(x * 10 ** (-1.8 / 20), 2500.0))
        lv = v["level:0"]
        self.assertIsNone(lv["clip"])
        self.assertAlmostEqual(lv["peak"], -3.1, delta=0.05)

    def test_reference(self):
        """what Audio 2 played as the reference, both at 29 ppm"""
        x = made("noise-white").floats()
        tap = record(x, 1500.0, ppm=29.0)
        rec = record(filt(x, lp2(8000, 0.9)), 9000.0, ppm=29.0)
        v, _t = analyze("noise-white", rec, ref=tap)
        for hz in (1000.0, 5000.0, 10000.0):
            self.assertAlmostEqual(db(at(v["vs-ref:L>L"], hz)),
                                   db(lp2(8000, 0.9)(hz)), delta=0.05)

    def test_residual_apart(self):
        """the player's noise and a line of it, heard through two paths by
        recorders on clocks apart, each with its own noise and one with a
        line of its own: what both heard and what each heard alone"""
        m = made("residual")
        x = m.floats()
        # the player stops for 30 ms 8 s into the program, as Audio 2's DAC
        # does while the VX captures: both recorders hear the stop
        at = m.info["segments"][0]["at"] + 8 * RATE
        x = np.concatenate([x[:at], np.zeros((int(0.03 * RATE), 2)),
                            x[at:]])
        t = np.arange(len(x)) / RATE
        dac = noise(x.shape, -95, seed=7)
        dac += 10 ** (-100 / 20) * np.sin(2 * np.pi * 1000 * t)[:, None]

        def pa(f):
            return lp2(12000, 0.8)(f) * hp1(15)(f) * 0.8

        def px(f):
            return lp2(16000, 0.7)(f) * 0.5

        rec = shift(filt(x + dac, pa), 3000.3)
        rec += noise(rec.shape, -90, seed=8)
        tr = np.arange(len(rec)) / RATE
        rec += 10 ** (-105 / 20) * np.sin(2 * np.pi * 3000 * tr)[:, None]
        # a recorder at 48 kHz, its clock 30 ppm slow against the player
        k = np.arange(int(len(x) * 48000 / RATE) + 9000, dtype=float)
        ref = at_times(filt(x + dac, px),
                       (k - 5000.0) * (RATE / 48000) * (1 + 30e-6))
        ref += noise(ref.shape, -92, seed=9)
        _text, ctx = sigan.analyze(m, sigan.Rec.of(rec, RATE),
                                   sigan.Rec.of(ref, 48000))
        v = ctx.rep.values
        for stops in v["stops"]:
            self.assertEqual(len(stops), 1)
            self.assertAlmostEqual(stops[0][1], 30.0, delta=0.5)
        both = -95 - 10 * math.log10(RATE / 2)
        for side in "LR":
            b = v["both:" + side]
            for hz in (2000, 4000, 8000):
                i = b["hz"].index(hz)
                self.assertAlmostEqual(b["silence"][i], both, delta=0.5)
                self.assertAlmostEqual(b["music"][i], both, delta=0.5)
                self.assertAlmostEqual(
                    b["own_rec"][i], -90 - 10 * math.log10(RATE / 2) -
                    db(abs(pa(hz))), delta=0.3)
                self.assertAlmostEqual(
                    b["own_ref"][i], -92 - 10 * math.log10(24000) -
                    db(abs(px(hz))), delta=0.3)
            # with the music, the residual meets the floor from 250 Hz,
            # past the paths' estimate under the program's loud low end
            hz, music, silence = v["thirds:rec:" + side]
            for f, a, s in zip(hz, music, silence):
                if f >= 250:
                    self.assertAlmostEqual(a, s, delta=1.0, msg=f)
        heard = [(r[0], float(r[2]), r[5]) for r in v["lines"]]
        for who, f0, what in (("rec", 1000, "both"), ("ref", 1000, "both"),
                              ("rec", 3000, "own")):
            self.assertIn(what, [h for w, f, h in heard
                                 if w == who and abs(f - f0) < 3], who)

    def test_boot_method(self):
        y = shift(filt(made("cal").floats(), path_h), 6000.3) * 0.8
        y += noise(y.shape, -100)
        v, _t = analyze("cal", y)
        self.assertLess(v["boot_db"], 0.01)
        self.assertAlmostEqual(v["ppm"], 0, delta=0.01)

    def test_live_estimate(self):
        y = shift(made("track").floats(), 3000.0) * 0.5
        v, _t = analyze("track", y + noise(y.shape, -110))
        for kind, _lv, settle, worst in v["running"]:
            if kind == "silence":
                self.assertLess(float(worst), 0.1)
            else:
                self.assertNotEqual(settle, "never", kind)
                self.assertLess(float(settle), 0.5, kind)
        y = shift(made("program-pilot").floats(), 3000.0)
        v, _t = analyze("program-pilot", y)
        self.assertAlmostEqual(v["pilot:0"]["ppm"], 0, delta=0.01)
        self.assertAlmostEqual(db(at(v["response:L>L"], 1000)), 0, delta=0.02)

    def test_command(self):
        y = shift(made("sweep-quick").floats(), 1000.0) * 0.5
        with tempfile.TemporaryDirectory() as tmp:
            path = os.path.join(tmp, "rec.wav")
            with open(path, "wb") as f:
                f.write(ts.wav_bytes(np.rint(y * 32768).astype(np.int16),
                                     RATE))
            out = os.path.join(tmp, "a")
            with open(os.devnull, "w") as null:
                old, sys.stdout = sys.stdout, null
                try:
                    sigan.main(["sweep-quick-44k", path, "-o", out])
                finally:
                    sys.stdout = old
            text = open(out + ".txt").read()
            self.assertIn("-6.02 dB at 1 kHz", text)
            self.assertTrue(os.path.exists(out + "-response.csv"))


if __name__ == "__main__":
    unittest.main()
