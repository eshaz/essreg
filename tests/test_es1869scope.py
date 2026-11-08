#!/usr/bin/env python3
# (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
#
# Licensed under GPL Version 3.0
"""es1869scope's analysis on the host. linux/tools/es1869scope's tie.c and
phase.c are built for this machine and called through ctypes, against
tools/clockan.py, numpy, and clocks made up with known noise.

usage: python3 -m unittest tests.test_es1869scope   (needs gcc and numpy)
"""

import ctypes
import os
import shutil
import subprocess
import sys
import tempfile
import unittest

import numpy as np

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SCOPE = os.path.join(ROOT, "linux", "tools", "es1869scope")
sys.path.insert(0, os.path.join(ROOT, "tools"))

import clockan  # noqa: E402

D = ctypes.c_double
DP = ctypes.POINTER(D)
SZ = ctypes.c_size_t


class Loop(ctypes.Structure):
    _fields_ = [("log2n", ctypes.c_uint), ("n", ctypes.c_uint),
                ("win", DP), ("sxx", DP), ("syy", DP), ("sxy", DP),
                ("zx", DP), ("zy", DP), ("segments", ctypes.c_ulong)]


class Fft(ctypes.Structure):
    _fields_ = [("log2n", ctypes.c_uint), ("n", ctypes.c_uint),
                ("tw", ctypes.POINTER(ctypes.c_int16)),
                ("window", ctypes.POINTER(ctypes.c_int16)),
                ("rev", ctypes.POINTER(ctypes.c_uint16)),
                ("mmx", ctypes.c_int)]


class PhEst(ctypes.Structure):
    _fields_ = [("f", D), ("rate", D), ("cycles", D),
                ("at", ctypes.c_ulonglong), ("started", ctypes.c_int)]


def dp(a):
    return a.ctypes.data_as(DP)


class Lib:
    tmp = None
    lib = None

    @classmethod
    def get(cls):
        if cls.lib is None:
            cls.tmp = tempfile.mkdtemp()
            so = os.path.join(cls.tmp, "scope.so")
            subprocess.check_call(
                ["gcc", "-shared", "-fPIC", "-O2", "-Wall", "-Wextra",
                 "-Werror", "-o", so, os.path.join(SCOPE, "tie.c"),
                 os.path.join(SCOPE, "phase.c"), os.path.join(SCOPE, "fft.c"),
                 os.path.join(SCOPE, "loop.c"), "-lm"])
            lib = ctypes.CDLL(so)
            for name in ("tie_adev", "tie_mdev", "tie_tdev", "tie_acov"):
                getattr(lib, name).restype = D
            lib.tie_adev.argtypes = [DP, SZ, D, SZ]
            lib.tie_mdev.argtypes = [DP, SZ, D, SZ]
            lib.tie_tdev.argtypes = [DP, SZ, D, SZ]
            lib.tie_acov.argtypes = [DP, DP, SZ, D, SZ]
            lib.tie_hat.argtypes = [DP, DP, SZ, D, SZ, DP]
            lib.tie_pulses.restype = SZ
            lib.tie_pulses.argtypes = [DP, SZ, D, ctypes.POINTER(SZ), SZ,
                                       DP, DP]
            lib.tie_psd.restype = ctypes.c_int
            lib.tie_psd.argtypes = [DP, SZ, D, ctypes.c_uint, DP]
            lib.tie_fft.argtypes = [DP, ctypes.c_uint]
            lib.ph_init.argtypes = [ctypes.POINTER(PhEst), D, D]
            lib.ph_block.restype = D
            lib.ph_block.argtypes = [ctypes.POINTER(PhEst),
                                     ctypes.POINTER(ctypes.c_short), SZ, SZ,
                                     DP]
            lib.fft_init.argtypes = [ctypes.POINTER(Fft), ctypes.c_uint,
                                     ctypes.c_int]
            lib.fft_run.argtypes = [ctypes.POINTER(Fft),
                                    ctypes.POINTER(ctypes.c_int16),
                                    ctypes.POINTER(ctypes.c_int16)]
            lib.fft_db.argtypes = [ctypes.POINTER(Fft),
                                   ctypes.POINTER(ctypes.c_int16),
                                   ctypes.POINTER(ctypes.c_int16)]
            lib.fft_free.argtypes = [ctypes.POINTER(Fft)]
            lib.loop_init.argtypes = [ctypes.POINTER(Loop), ctypes.c_uint]
            lib.loop_add.argtypes = [ctypes.POINTER(Loop), DP, DP]
            lib.loop_bin.argtypes = [ctypes.POINTER(Loop), ctypes.c_uint,
                                     DP, DP, DP]
            lib.loop_free.argtypes = [ctypes.POINTER(Loop)]
            lib.loop_lag.restype = ctypes.c_long
            lib.loop_lag.argtypes = [DP, DP, ctypes.c_uint, DP]
            cls.lib = lib
        return cls.lib


def tearDownModule():
    if Lib.tmp:
        shutil.rmtree(Lib.tmp)


def mdev_ref(x, tau0, m):
    """modified Allan deviation by its definition, the slow way"""
    n = len(x)
    d = x[2 * m:] - 2 * x[m:n - m] + x[:n - 2 * m]
    sums = np.array([d[j:j + m].sum() for j in range(n - 3 * m + 1)])
    return np.sqrt(np.mean(sums ** 2) / 2) / (m * m * tau0)


class DeviationTest(unittest.TestCase):
    def setUp(self):
        self.lib = Lib.get()
        rng = np.random.default_rng(1)
        # white frequency noise: a random walk of phase, a second a sample
        self.x = np.cumsum(rng.normal(0, 1e-9, 4000))

    def test_adev_is_clockans(self):
        for m, want in clockan.adev(self.x, (1, 4, 16, 64)):
            got = self.lib.tie_adev(dp(self.x), len(self.x), 1.0, m)
            self.assertAlmostEqual(got / want, 1.0, places=12)

    def test_adev_scales_with_tau0(self):
        a1 = self.lib.tie_adev(dp(self.x), len(self.x), 1.0, 8)
        a4 = self.lib.tie_adev(dp(self.x), len(self.x), 0.25, 8)
        self.assertAlmostEqual(a4 / a1, 4.0, places=12)

    def test_mdev_and_tdev(self):
        for m in (1, 3, 10, 50):
            want = mdev_ref(self.x, 0.5, m)
            got = self.lib.tie_mdev(dp(self.x), len(self.x), 0.5, m)
            self.assertAlmostEqual(got / want, 1.0, places=10)
            tdev = self.lib.tie_tdev(dp(self.x), len(self.x), 0.5, m)
            self.assertAlmostEqual(tdev / (m * 0.5 / np.sqrt(3) * want), 1.0,
                                   places=10)

    def test_too_short_is_nan(self):
        x = np.zeros(10)
        self.assertTrue(np.isnan(self.lib.tie_adev(dp(x), 10, 1.0, 5)))
        self.assertTrue(np.isnan(self.lib.tie_mdev(dp(x), 10, 1.0, 4)))

    def test_white_frequency_slope(self):
        # white FM falls as tau^-1/2: four times the time, half the deviation
        a = [self.lib.tie_adev(dp(self.x), len(self.x), 1.0, m)
             for m in (4, 16)]
        self.assertAlmostEqual(a[1] / a[0], 0.5, delta=0.08)


class HatTest(unittest.TestCase):
    """three clocks with their own white phase noise, measured in two pairs
    that share the middle one"""

    def test_each_clocks_own_noise(self):
        lib = Lib.get()
        rng = np.random.default_rng(7)
        n = 40000
        sig = (1e-9, 3e-9, 2e-9)
        a, b, c = (rng.normal(0, s, n) for s in sig)
        d1, d2 = a - b, b - c
        var = (D * 3)()
        for m in (1, 4):
            lib.tie_hat(dp(d1), dp(d2), n, 1.0, m, var)
            for k in range(3):
                # white PM: Allan variance 3 sigma^2 at one sample, and
                # 3 sigma^2 / m^2 at m
                want = 3 * sig[k] ** 2 / m ** 2
                self.assertAlmostEqual(var[k] / want, 1.0, delta=0.12)

    def test_covariance_of_a_pair_with_itself_is_its_variance(self):
        lib = Lib.get()
        x = np.cumsum(np.random.default_rng(3).normal(0, 1e-9, 3000))
        cov = lib.tie_acov(dp(x), dp(x), len(x), 1.0, 5)
        adev = lib.tie_adev(dp(x), len(x), 1.0, 5)
        self.assertAlmostEqual(cov / adev ** 2, 1.0, places=12)


class PulseTest(unittest.TestCase):
    def test_finds_the_pulses_and_nothing_else(self):
        lib = Lib.get()
        rng = np.random.default_rng(11)
        r = rng.normal(5e-6, 1e-6, 5000)
        where = [17, 900, 901, 2500, 4999]
        r[where] += 200e-6
        idx = (SZ * 16)()
        med, mad = D(), D()
        n = lib.tie_pulses(dp(r), len(r), 10.0, idx, 16, ctypes.byref(med),
                           ctypes.byref(mad))
        self.assertEqual(sorted(idx[:n]), where)
        self.assertAlmostEqual(med.value, 5e-6, delta=0.1e-6)
        # a normal distribution's MAD is 0.6745 sigma
        self.assertAlmostEqual(mad.value, 0.6745e-6, delta=0.05e-6)

    def test_more_than_room_still_counts(self):
        lib = Lib.get()
        r = np.zeros(100)
        r[::10] = 1.0
        r[1::10] = 0.001
        idx = (SZ * 2)()
        med, mad = D(), D()
        n = lib.tie_pulses(dp(r), len(r), 3.0, idx, 2, ctypes.byref(med),
                           ctypes.byref(mad))
        self.assertEqual(n, 20)
        self.assertEqual(list(idx), [0, 1])


class SpectrumTest(unittest.TestCase):
    def test_fft_is_numpys(self):
        lib = Lib.get()
        z = np.random.default_rng(5).normal(size=2048)
        c = z[0::2] + 1j * z[1::2]
        lib.tie_fft(dp(z), 10)
        got = z[0::2] + 1j * z[1::2]
        np.testing.assert_allclose(got, np.fft.fft(c), rtol=1e-9, atol=1e-9)

    def test_white_phase_noise_is_flat_at_its_level(self):
        lib = Lib.get()
        s, tau0 = 2e-9, 0.1
        x = np.random.default_rng(9).normal(0, s, 2 ** 16)
        psd = np.zeros(2 ** 9 + 1)
        w = lib.tie_psd(dp(x), len(x), tau0, 10, dp(psd))
        self.assertEqual(w, 2 * 2 ** 6 - 1)
        level = np.mean(psd[8:-8])
        self.assertAlmostEqual(level / (2 * s * s * tau0), 1.0, delta=0.05)

    def test_a_resonance_shows_at_its_frequency(self):
        lib = Lib.get()
        tau0, n = 1.0, 2 ** 14
        t = np.arange(n) * tau0
        f0 = 37 / 1024  # on a bin of the 1024-point windows
        x = 50e-9 * np.sin(2 * np.pi * f0 * t)
        x += np.random.default_rng(2).normal(0, 1e-9, n)
        x += 3e-6 * t  # a rate offset, which each window's line takes out
        psd = np.zeros(513)
        lib.tie_psd(dp(x), n, tau0, 10, dp(psd))
        self.assertEqual(int(np.argmax(psd[1:])) + 1, 37)
        # the tone's power, A^2 / 2, over the window's bins
        df = 1 / (1024 * tau0)
        power = psd[34:41].sum() * df
        self.assertAlmostEqual(power / (50e-9 ** 2 / 2), 1.0, delta=0.05)

    def test_too_short_is_no_window(self):
        lib = Lib.get()
        x = np.zeros(100)
        psd = np.zeros(65)
        self.assertEqual(lib.tie_psd(dp(x), 100, 1.0, 7, dp(psd)), 0)


class PilotTest(unittest.TestCase):
    """a pilot of 19.6 kHz recorded at 48 kHz, as from the ES1869 at the
    Focusrite"""

    RATE = 48000.0
    F = 19600.0
    BLOCK = 4800

    def run_pilot(self, eps, n_blocks, timing=None, amp=1000.0, noise=10.0,
                  seed=4):
        lib = Lib.get()
        n = n_blocks * self.BLOCK
        t = np.arange(n) / self.RATE
        err = eps * t if timing is None else eps * t + timing(t)
        rng = np.random.default_rng(seed)
        s = amp * np.cos(2 * np.pi * self.F * (t + err) + 0.3)
        s += rng.normal(0, noise, n)
        s = np.round(s).astype(np.int16)
        # interleaved with a second channel the estimator must skip
        st = np.zeros(2 * n, dtype=np.int16)
        st[0::2] = s
        st[1::2] = rng.integers(-3000, 3000, n)
        p = PhEst()
        lib.ph_init(ctypes.byref(p), self.F, self.RATE)
        out, a = [], D()
        sp = ctypes.POINTER(ctypes.c_short)
        for k in range(n_blocks):
            chunk = st[2 * k * self.BLOCK:2 * (k + 1) * self.BLOCK]
            out.append(lib.ph_block(ctypes.byref(p), chunk.ctypes.data_as(sp),
                                    2, self.BLOCK, ctypes.byref(a)))
        mid = (np.arange(n_blocks) + 0.5) * self.BLOCK / self.RATE
        return mid, np.array(out), a.value

    def test_rate_offset_and_level(self):
        # 12.5 ppm is 0.245 cycles a second: five turns in 20 s to unwrap
        mid, x, amp = self.run_pilot(12.5e-6, 200)
        slope = np.polyfit(mid, x, 1)[0]
        self.assertAlmostEqual(slope, 12.5e-6, delta=1e-10)
        self.assertAlmostEqual(amp, 1000, delta=2)

    def test_fast_clock_is_positive(self):
        mid, x, _ = self.run_pilot(-80e-6, 50)
        self.assertLess(np.polyfit(mid, x, 1)[0], 0)

    def test_a_step_of_50_ns(self):
        def step(t):
            return np.where(t >= 10.0, 50e-9, 0.0)

        mid, x, _ = self.run_pilot(0.0, 200, timing=step)
        before = x[(mid > 2) & (mid < 9.5)].mean()
        after = x[(mid > 10.5) & (mid < 18)].mean()
        self.assertAlmostEqual((after - before) * 1e9, 50.0, delta=2.0)

    def test_noise_floor_in_nanoseconds(self):
        mid, x, _ = self.run_pilot(3e-6, 300)
        resid = x - np.polyval(np.polyfit(mid, x, 1), mid)
        # a Hann window leaves noise / amplitude * sqrt(3 / N) radians: with
        # 10 against 1000 over 4800 samples, 2.0 ns a block
        floor = 10 / 1000 * np.sqrt(3 / self.BLOCK) / (2 * np.pi * self.F)
        self.assertAlmostEqual(np.std(resid) / floor, 1.0, delta=0.15)

    def test_a_slow_wander_comes_through(self):
        def wander(t):
            return 1e-6 * np.sin(2 * np.pi * 0.05 * t)

        mid, x, _ = self.run_pilot(0.0, 400, timing=wander)
        # against the truth less their mean: what's left is the 2 ns floor
        d = x - wander(mid)
        d -= d.mean()
        self.assertLess(np.std(d), 2.5e-9)
        self.assertLess(np.max(np.abs(d)), 10e-9)


class PeriodTest(unittest.TestCase):
    """the period pulses keep, as WiFi beacons put them on the Armada's
    interrupts"""

    def period(self, t):
        lib = Lib.get()
        lib.tie_period.restype = D
        lib.tie_period.argtypes = [DP, SZ, D, D, DP]
        t = np.sort(np.asarray(t, dtype=np.float64))
        st = D()
        p = lib.tie_period(dp(t), len(t), 0.005, 2.0, ctypes.byref(st))
        return p, st.value

    def test_beacons_with_jitter_and_strays(self):
        rng = np.random.default_rng(12)
        beats = np.arange(3000) * 0.1024 + rng.normal(0, 0.0003, 3000)
        keep = beats[rng.random(3000) < 0.4]
        strays = rng.uniform(0, 300, 200)
        p, st = self.period(np.r_[keep, strays])
        self.assertAlmostEqual(p, 0.1024, delta=0.0003)
        self.assertGreater(st, 20)

    def test_at_random_there_is_no_period(self):
        t = np.random.default_rng(13).uniform(0, 300, 2000)
        p, st = self.period(t)
        self.assertLess(st, 6)

    def test_too_few(self):
        self.assertEqual(self.period([0.1, 0.2, 0.3])[0], 0)


class FftTest(unittest.TestCase):
    """the spectrogram's FFT: MMX and C alike to the bit, numpy's within
    the rounding of its stages, and the dB scale"""

    I16 = ctypes.POINTER(ctypes.c_int16)

    def run_fft(self, x, mmx, log2n=10):
        lib = Lib.get()
        f = Fft()
        self.assertEqual(lib.fft_init(ctypes.byref(f), log2n, mmx), 0)
        xs = np.ascontiguousarray(x, dtype=np.int16)
        z = np.zeros(2 << log2n, dtype=np.int16)
        db = np.zeros(1 << (log2n - 1), dtype=np.int16)
        lib.fft_run(ctypes.byref(f), xs.ctypes.data_as(self.I16),
                    z.ctypes.data_as(self.I16))
        lib.fft_db(ctypes.byref(f), z.ctypes.data_as(self.I16),
                   db.ctypes.data_as(self.I16))
        used = f.mmx
        lib.fft_free(ctypes.byref(f))
        return z, db, used

    def test_mmx_and_c_agree_to_the_bit(self):
        x = np.random.default_rng(21).integers(-32768, 32767, 1024)
        zc, dc, _ = self.run_fft(x, 0)
        zm, dm, used = self.run_fft(x, 1)
        if not used:
            self.skipTest("no MMX on this machine")
        np.testing.assert_array_equal(zc, zm)
        np.testing.assert_array_equal(dc, dm)

    def test_numpys_spectrum(self):
        n = 1024
        x = np.random.default_rng(22).integers(-20000, 20000, n)
        z, _, _ = self.run_fft(x, 1)
        w = np.round(32767 * (0.5 - 0.5 * np.cos(2 * np.pi *
                                                 np.arange(n) / n)))
        xw = np.floor(x * w / 32768)
        want = np.fft.fft(xw) / n
        got = z[0::2] + 1j * z[1::2]
        # ten stages of rounding, half an LSB each
        self.assertLess(np.max(np.abs(got - want)), 8)

    def test_levels_in_db(self):
        n = 1024
        k = np.arange(n)
        for level, cycles in ((0, 64), (-40, 100.5)):
            amp = 32767 * 10 ** (level / 20)
            x = np.round(amp * np.sin(2 * np.pi * cycles * k / n))
            _, db, _ = self.run_fft(x, 1)
            peak = db.max() / 10
            # half a bin off costs Hann 1.4 dB, which the peak shows
            want = level - (1.42 if cycles % 1 else 0)
            self.assertAlmostEqual(peak, want, delta=0.5)
            # half-way between two bins, either may come out on top
            self.assertIn(int(np.argmax(db)),
                          (int(np.floor(cycles)), int(np.ceil(cycles))))

    def test_a_tone_leaves_a_low_floor(self):
        n = 1024
        x = np.round(30000 * np.sin(2 * np.pi * 64 * np.arange(n) / n))
        _, db, _ = self.run_fft(x, 1)
        far = np.r_[db[:40], db[90:]] / 10
        self.assertLess(np.median(far), -80)


class LoopTest(unittest.TestCase):
    """a path's response from music-like noise through it, as Audio 2 into
    Audio 1 will be measured"""

    def response(self, x, y, log2n=12):
        lib = Lib.get()
        lp = Loop()
        self.assertEqual(lib.loop_init(ctypes.byref(lp), log2n), 0)
        n = 1 << log2n
        for s in range(0, len(x) - n + 1, n // 2):
            xs = np.ascontiguousarray(x[s:s + n])
            ys = np.ascontiguousarray(y[s:s + n])
            lib.loop_add(ctypes.byref(lp), dp(xs), dp(ys))
        out = []
        m, ph, co = D(), D(), D()
        for k in range(n // 2 + 1):
            lib.loop_bin(ctypes.byref(lp), k, ctypes.byref(m),
                         ctypes.byref(ph), ctypes.byref(co))
            out.append((m.value, ph.value, co.value))
        lib.loop_free(ctypes.byref(lp))
        return np.array(out)

    def test_a_known_filter(self):
        rng = np.random.default_rng(31)
        x = rng.normal(0, 1, 1 << 18)
        h = np.array([0.25, 0.5, 0.25])      # a gentle low-pass, 1 sample late
        y = np.convolve(x, h)[:len(x)] + rng.normal(0, 1e-3, len(x))
        r = self.response(x, y)
        n = 1 << 12
        w = 2 * np.pi * np.arange(n // 2 + 1) / n
        want = np.abs(0.25 + 0.5 * np.exp(-1j * w) + 0.25 * np.exp(-2j * w))
        band = slice(10, int(n * 0.4))
        db = 20 * np.log10(r[band, 0] / want[band])
        self.assertLess(np.max(np.abs(db)), 0.05)
        # its phase is a delay of one sample
        np.testing.assert_allclose(np.unwrap(r[band, 1]), -w[band],
                                   atol=0.01)
        self.assertGreater(r[band, 2].min(), 0.99)

    def test_nothing_in_common_has_no_coherence(self):
        rng = np.random.default_rng(32)
        x = rng.normal(0, 1, 1 << 17)
        y = rng.normal(0, 1, 1 << 17)
        r = self.response(x, y)
        self.assertLess(np.median(r[5:-5, 2]), 0.05)

    def test_the_lag_between_two_captures(self):
        lib = Lib.get()
        rng = np.random.default_rng(33)
        x = rng.normal(0, 1, 1 << 14)
        frac = D()
        for lag in (0, 37, -120, 900):
            y = np.roll(x, lag) + rng.normal(0, 0.1, len(x))
            got = lib.loop_lag(dp(x), dp(np.ascontiguousarray(y)), 14,
                               ctypes.byref(frac))
            self.assertEqual(got, lag)
            self.assertLess(abs(frac.value), 0.5)


class ShotTest(unittest.TestCase):
    """es1869scope draws a log into a picture: two pairs that share the
    Focusrite, interrupts with pulses, and the system's lines"""

    PAIR = (0x5aa9ff, 0xffa64d)
    HAT = (0x41c7b9, 0xff7eb6, 0xe8d04a)
    LATE = 0xff6f6f

    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.mkdtemp()
        cls.prog = os.path.join(cls.tmp, "es1869scope")
        src = [os.path.join(SCOPE, f) for f in
               ("es1869scope.c", "tie.c", "draw.c", "fb.c", "plot.c",
                "fft.c", "spectro.c")]
        subprocess.check_call(["gcc", "-O2", "-std=gnu99", "-Wall",
                               "-Wextra", "-Werror", "-o", cls.prog] + src +
                              ["-lasound", "-lz", "-lm"])

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.tmp)

    def log(self, path, secs=300):
        rng = np.random.default_rng(6)
        n = int(secs / 0.1)
        t0 = 4_000_000_000_000
        e = np.cumsum(rng.normal(0, 2e-11, n)) + rng.normal(0, 2e-9, n)
        f = np.cumsum(rng.normal(0, 6e-11, n))
        v = np.cumsum(rng.normal(0, 4e-11, n)) + rng.normal(0, 1e-9, n)
        out = ["# es1869clock 1, raw %d, real 0" % t0]
        for k in range(n):
            raw = t0 + k * 100_000_000
            out.append("p ef %d %.3f 1036.0" % (raw, (e[k] - f[k]) * 1e9))
            out.append("p fv %d %.3f 3277.0" % (raw, (f[k] - v[k]) * 1e9))
            if k % 10 == 0:
                out.append("s %d %d %d 28786.0 10000 8256 1000" %
                           (raw, raw, raw))
                out.append("k 815 28786 -")
        out.append("t 481E2359,72.30.35.89,2,1794160513.4,0.000124,"
                   "-0.00038,0.000335,-28.766,-0.007,0.6,0.04,0.004,519,"
                   "Normal")
        per = 552960 / 23.814e6
        for k in range(int(secs / per)):
            late = abs(rng.normal(3e-6, 1e-6))
            if k % 300 == 7:
                late += 200e-6
            out.append("i %d 1 552960 %d" %
                       (k + 1, t0 + int((k * per + late) * 1e9)))
        with open(path, "w") as fh:
            fh.write("\n".join(out) + "\n")

    def colours(self, ppm):
        with open(ppm, "rb") as fh:
            head = [fh.readline() for _ in range(3)]
            self.assertEqual(head[0].strip(), b"P6")
            w, h = map(int, head[1].split())
            px = np.frombuffer(fh.read(), dtype=np.uint8)
        self.assertEqual((w, h), (800, 600))
        rgb = px.reshape(-1, 3).astype(np.uint32)
        return set(np.unique(rgb[:, 0] << 16 | rgb[:, 1] << 8 | rgb[:, 2]))

    def test_every_panel_draws(self):
        logf = os.path.join(self.tmp, "clock.log")
        ppm = os.path.join(self.tmp, "clock.ppm")
        self.log(logf)
        r = subprocess.run([self.prog, "-l", logf, "-s", ppm],
                           capture_output=True, text=True)
        if r.returncode == 2 and "no console font" in r.stderr:
            self.skipTest("no console font on this machine")
        self.assertEqual(r.returncode, 0, r.stderr)
        seen = self.colours(ppm)
        for rgb in self.PAIR + self.HAT + (self.LATE,):
            self.assertIn(rgb, seen, "%06x missing" % rgb)

    def test_the_spectrogram_puts_a_tone_where_it_belongs(self):
        import wave
        rate, f0 = 48000, 6000.0
        x = 0.5 * np.sin(2 * np.pi * f0 * np.arange(2 * rate) / rate)
        wav = os.path.join(self.tmp, "tone.wav")
        w = wave.open(wav, "wb")
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(rate)
        w.writeframes((x * 32767).astype(np.int16).tobytes())
        w.close()
        ppm = os.path.join(self.tmp, "spec.ppm")
        r = subprocess.run([self.prog, "-i", wav, "-s", ppm],
                           capture_output=True, text=True)
        if r.returncode == 2 and "no console font" in r.stderr:
            self.skipTest("no console font on this machine")
        self.assertEqual(r.returncode, 0, r.stderr)
        with open(ppm, "rb") as fh:
            for _ in range(3):
                fh.readline()
            img = np.frombuffer(fh.read(), dtype=np.uint8).reshape(600, 800,
                                                                   3)
        # the waterfall's rows hold the tone as their brightest column, a
        # quarter of the way from 0 to 24 kHz across the area from x = 48
        rows = img[60:200].astype(int).sum(axis=2)
        cols = rows.argmax(axis=1)
        want = 48 + f0 / 24000 * (800 - 48 - 8)
        self.assertLess(abs(np.median(cols) - want), 4)

    def test_an_empty_log_still_draws(self):
        logf = os.path.join(self.tmp, "empty.log")
        ppm = os.path.join(self.tmp, "empty.ppm")
        with open(logf, "w") as fh:
            fh.write("# es1869clock 1, raw 0, real 0\n")
        r = subprocess.run([self.prog, "-l", logf, "-s", ppm],
                           capture_output=True, text=True)
        if r.returncode == 2 and "no console font" in r.stderr:
            self.skipTest("no console font on this machine")
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertNotIn(self.LATE, self.colours(ppm))


if __name__ == "__main__":
    unittest.main()
