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
import errno
import os
import re
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
                ("zx", DP), ("zy", DP), ("zw", DP),
                ("segments", ctypes.c_ulong)]


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


# eqstate.h's layout, the engine's state that a viewer reads
EQS_POINTS, EQS_BANDS, EQS_ROWS, EQS_BINS = 128, 24, 64, 128
EQS_HIST, EQS_ZBINS, EQS_A1TAPS, EQS_WHY = 64, 33, 34, 160
EQS_LVALS, EQS_LPER, EQS_VERSION = 33, 40, 2
F32, U32, I32 = ctypes.c_float, ctypes.c_uint32, ctypes.c_int32


class EqsBlock(ctypes.Structure):
    _fields_ = [("t", I32), ("counted", I32), ("refitted", I32),
                ("trim", I32), ("moved", I32), ("held", I32),
                ("dev", F32 * EQS_BANDS), ("drift", F32), ("drift_hz", F32),
                ("corr20k", F32), ("chip", F32), ("tie", F32),
                ("applied", F32), ("resid", F32), ("zstep", F32 * 2),
                ("deg", F32 * EQS_BANDS)]


class EqsState(ctypes.Structure):
    _fields_ = [("magic", U32), ("version", U32), ("size", U32),
                ("seq", U32), ("pid", I32), ("now_ms", U32), ("ack", U32),
                ("watched", U32), ("state", I32),
                ("why", ctypes.c_char * EQS_WHY),
                ("stream_rate", U32), ("factor", U32), ("a1_rate", U32),
                ("a1_rest", U32), ("a1c", U32), ("mode", U32),
                ("treble", U32), ("hold", U32),
                ("lagged", U32), ("segments", U32), ("runs", U32),
                ("blocks", U32), ("restarts", U32), ("lag", I32),
                ("delay", F32), ("overlay", F32),
                ("rms", F32 * 4), ("peak", F32 * 4),
                ("hz", F32 * EQS_POINTS), ("mag", (F32 * EQS_POINTS) * 2),
                ("phase", (F32 * EQS_POINTS) * 2),
                ("coh", (F32 * EQS_POINTS) * 2), ("ir", (F32 * 65) * 2),
                ("band_hz", F32 * EQS_BANDS), ("band_dev", F32 * EQS_BANDS),
                ("band_base", F32 * EQS_BANDS),
                ("band_drift", F32 * EQS_BANDS),
                ("band_deg", F32 * EQS_BANDS),
                ("band_base_deg", F32 * EQS_BANDS),
                ("band_drift_deg", F32 * EQS_BANDS),
                ("row_next", U32 * 2), ("row_hz", U32 * 2),
                ("rows", ((ctypes.c_int8 * EQS_BINS) * EQS_ROWS) * 2),
                ("taps", I32 * 48), ("taps0", I32 * 48),
                ("ptaps", I32 * 48), ("ptaps0", I32 * 48),
                ("a1taps", I32 * EQS_A1TAPS), ("a1ptaps", I32 * 32),
                ("zhist", (U32 * EQS_ZBINS) * 2),
                ("hist_next", U32), ("hist_count", U32),
                ("hist", EqsBlock * EQS_HIST),
                ("lin_on", U32), ("lin_fits", U32), ("lin_frames", F32),
                ("lin_centre", I32 * 2),
                ("lin_path", (F32 * EQS_LVALS) * 2),
                ("lin_left", (F32 * EQS_LVALS) * 2),
                ("lin_curve", (I32 * EQS_LVALS) * 2),
                ("lin_gain", (I32 * EQS_LPER) * 2),
                ("lin_turn", (I32 * EQS_LPER) * 2),
                ("lin_db", (F32 * EQS_LPER) * 2),
                ("lin_deg", (F32 * EQS_LPER) * 2),
                ("lin_n", (F32 * EQS_LPER) * 2)]


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
                 os.path.join(SCOPE, "loop.c"), os.path.join(SCOPE, "zstep.c"),
                 "-lm"])
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
        # the ES1869's own loop inverts, so a negative peak counts too
        for lag, sign in ((0, 1), (37, 1), (-120, 1), (900, 1), (311, -1)):
            y = sign * np.roll(x, lag) + rng.normal(0, 0.1, len(x))
            got = lib.loop_lag(dp(x), dp(np.ascontiguousarray(y)), 14,
                               ctypes.byref(frac))
            self.assertEqual(got, lag)
            self.assertLess(abs(frac.value), 0.5)


class StateTest(unittest.TestCase):
    """eqstate.c: the engine's state in a file, copied whole between its
    writes, its lock telling a viewer that the engine runs, and the FIFO's
    commands"""

    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.mkdtemp()
        cls.so = os.path.join(cls.tmp, "eqstate.so")
        subprocess.check_call(["gcc", "-O2", "-std=gnu99", "-Wall", "-Wextra",
                               "-Werror", "-fPIC", "-shared", "-o", cls.so,
                               os.path.join(SCOPE, "eqstate.c")])
        lib = ctypes.CDLL(cls.so)
        sp = ctypes.POINTER(ctypes.POINTER(EqsState))
        lib.eqs_create.argtypes = [ctypes.c_char_p, ctypes.c_char_p, sp,
                                   ctypes.POINTER(ctypes.c_int)]
        lib.eqs_begin.argtypes = [ctypes.POINTER(EqsState)]
        lib.eqs_end.argtypes = [ctypes.POINTER(EqsState)]
        lib.eqs_command.argtypes = [ctypes.c_int, ctypes.c_char_p,
                                    ctypes.c_uint, ctypes.POINTER(U32)]
        lib.eqs_map.argtypes = [ctypes.c_char_p, sp]
        lib.eqs_alive.argtypes = [ctypes.c_char_p]
        lib.eqs_copy.argtypes = [ctypes.POINTER(EqsState),
                                 ctypes.POINTER(EqsState)]
        lib.eqs_send.argtypes = [ctypes.c_char_p, ctypes.c_char_p, U32]
        cls.lib = lib

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.tmp)

    def create(self, name, cmd=None):
        path = os.path.join(self.tmp, name).encode()
        s = ctypes.POINTER(EqsState)()
        fd = ctypes.c_int(-1)
        r = self.lib.eqs_create(path, cmd and os.path.join(
            self.tmp, cmd).encode(), ctypes.byref(s), ctypes.byref(fd))
        self.assertEqual(r, 0)
        return path, s, fd.value

    def test_the_layout_is_the_same_here_and_on_the_armada(self):
        """the engine's size, which a 32-bit build gives too"""
        path, s, _fd = self.create("layout.state")
        self.assertEqual(ctypes.sizeof(EqsState), 41312)
        self.assertEqual(s.contents.size, ctypes.sizeof(EqsState))
        self.assertEqual(s.contents.magic, 0x31534545)
        self.assertEqual(s.contents.pid, os.getpid())

    def test_a_copy_is_taken_whole_between_writes(self):
        path, s, _fd = self.create("copy.state")
        self.lib.eqs_begin(s)
        s.contents.stream_rate = 44100
        s.contents.mag[1][5] = -0.25
        s.contents.hist[3].trim = 282
        self.lib.eqs_end(s)
        m = ctypes.POINTER(EqsState)()
        self.assertEqual(self.lib.eqs_map(path, ctypes.byref(m)), 0)
        out = EqsState()
        self.assertEqual(self.lib.eqs_copy(m, ctypes.byref(out)), 0)
        self.assertEqual((out.stream_rate, out.hist[3].trim), (44100, 282))
        self.assertAlmostEqual(out.mag[1][5], -0.25)
        # in the middle of a write no copy is whole
        self.lib.eqs_begin(s)
        self.assertEqual(self.lib.eqs_copy(m, ctypes.byref(out)), -1)
        self.lib.eqs_end(s)
        self.assertEqual(self.lib.eqs_copy(m, ctypes.byref(out)), 0)

    def test_the_lock_tells_whether_the_engine_runs(self):
        """an engine in another process holds the file's lock until it
        ends, and a second can't take the file meanwhile"""
        path = os.path.join(self.tmp, "alive.state")
        child = subprocess.Popen(
            [sys.executable, "-c",
             "import ctypes, sys, time\n"
             "lib = ctypes.CDLL(sys.argv[1])\n"
             "s = ctypes.c_void_p(); fd = ctypes.c_int(-1)\n"
             "assert lib.eqs_create(sys.argv[2].encode(), None,"
             " ctypes.byref(s), ctypes.byref(fd)) == 0\n"
             "print('ready', flush=True); time.sleep(30)\n",
             self.so, path], stdout=subprocess.PIPE, text=True)
        try:
            self.assertEqual(child.stdout.readline().strip(), "ready")
            self.assertEqual(self.lib.eqs_alive(path.encode()), child.pid)
            s = ctypes.POINTER(EqsState)()
            fd = ctypes.c_int(-1)
            self.assertLess(self.lib.eqs_create(path.encode(), None,
                                                ctypes.byref(s),
                                                ctypes.byref(fd)), 0)
        finally:
            child.kill()
            child.wait()
            child.stdout.close()
        self.assertEqual(self.lib.eqs_alive(path.encode()), 0)

    def test_commands_come_whole_and_nothing_else_does(self):
        path, s, fd = self.create("cmd.state", "eq.cmd")
        cmd = os.path.join(self.tmp, "eq.cmd").encode()
        word = ctypes.create_string_buffer(16)
        num = U32()
        self.assertEqual(self.lib.eqs_command(fd, word, 16,
                                              ctypes.byref(num)), 0)
        self.assertEqual(self.lib.eqs_send(cmd, b"hold", 7), 0)
        self.assertEqual(self.lib.eqs_send(cmd, b"watch", 8), 0)
        got = []
        for _ in range(2):
            self.assertEqual(self.lib.eqs_command(fd, word, 16,
                                                  ctypes.byref(num)), 1)
            got.append((word.value, num.value))
        self.assertEqual(got, [(b"hold", 7), (b"watch", 8)])
        w = os.open(cmd, os.O_WRONLY | os.O_NONBLOCK)
        os.write(w, b"junk\n")
        os.close(w)
        self.assertEqual(self.lib.eqs_command(fd, word, 16,
                                              ctypes.byref(num)), -1)
        # no engine reading: the viewer is told so at once
        lone = os.path.join(self.tmp, "lone.cmd")
        os.mkfifo(lone)
        self.assertEqual(self.lib.eqs_send(lone.encode(), b"hold", 1),
                         -errno.ENXIO)

    def test_another_layout_is_refused(self):
        path = os.path.join(self.tmp, "other.state")
        with open(path, "wb") as fh:
            fh.write(b"\0" * ctypes.sizeof(EqsState))
        m = ctypes.POINTER(EqsState)()
        self.assertEqual(self.lib.eqs_map(path.encode(), ctypes.byref(m)),
                         -errno.ESTALE)


class EqTest(unittest.TestCase):
    """eq.c: a value's trend over blocks, predicted ahead, and the DAC
    correction's side taps fitted to a response"""

    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.mkdtemp()
        so = os.path.join(cls.tmp, "eq.so")
        subprocess.check_call(["gcc", "-O2", "-std=gnu99", "-Wall", "-Wextra",
                               "-Werror", "-fPIC", "-shared", "-o", so,
                               os.path.join(SCOPE, "eq.c"), "-lm"])
        cls.lib = ctypes.CDLL(so)

        class Series(ctypes.Structure):
            _fields_ = [("t", D * 16), ("v", D * 16), ("n", ctypes.c_int),
                        ("next", ctypes.c_int)]
        cls.Series = Series
        cls.lib.eq_reset.argtypes = [ctypes.POINTER(Series)]
        cls.lib.eq_push.argtypes = [ctypes.POINTER(Series), D, D]
        cls.lib.eq_predict.restype = D
        cls.lib.eq_predict.argtypes = [ctypes.POINTER(Series), D, DP]
        cls.lib.eq_sides_gain.restype = D
        cls.lib.eq_sides_gain.argtypes = [ctypes.POINTER(ctypes.c_long), D, D]
        cls.lib.eq_fit_sides.argtypes = [DP, DP, DP, ctypes.c_int, D,
                                         ctypes.POINTER(ctypes.c_long),
                                         ctypes.POINTER(ctypes.c_long)]
        cls.lib.eq_phase_resp.restype = D
        cls.lib.eq_phase_resp.argtypes = [ctypes.POINTER(ctypes.c_long), D, D]
        cls.lib.eq_fit_phase.argtypes = [DP, DP, DP, ctypes.c_int, D,
                                         ctypes.POINTER(ctypes.c_long),
                                         ctypes.POINTER(ctypes.c_long)]

        class Setup(ctypes.Structure):
            _fields_ = [("rate", ctypes.c_uint), ("factor", ctypes.c_uint),
                        ("a1c", ctypes.c_int), ("mode", ctypes.c_int)]
        cls.Setup = Setup
        cls.lib.eq_policy.argtypes = [ctypes.POINTER(Setup),
                                      ctypes.POINTER(Setup), ctypes.c_char_p,
                                      ctypes.c_uint]

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.tmp)

    def policy(self, now, base=None):
        why = ctypes.create_string_buffer(160)
        f = self.lib.eq_policy(ctypes.byref(self.Setup(*now)),
                               ctypes.byref(self.Setup(*base)) if base
                               else None, why, 160)
        return f, why.value.decode()

    def test_the_policy_rests_audio_1_where_it_must(self):
        """3x stalls the DAC while Audio 1 records, and a stream past
        Audio 1's highest rate can't be recorded"""
        for now in ((44100, 3, 1, 2), (96000, 1, 1, 2), (0, 2, 1, 2)):
            f, why = self.policy(now)
            self.assertEqual(f, 1, now)
            self.assertIn("Audio 1 rests", why)

    def test_the_policy_counts_what_was_measured_like_the_start(self):
        base = (44100, 2, 1, 2)
        f, why = self.policy(base)
        self.assertEqual((f, why), (2 | 4, ""))
        self.assertEqual(self.policy(base, base), (2 | 4, ""))
        # shown, not counted: another rate, 1x, Audio 1's correction
        # switched or another factor than the start's; the trim still moves
        # where the clock is the one the trend follows
        for now, trim in (((48000, 2, 1, 2), 0), ((44100, 1, 1, 2), 4),
                          ((44100, 2, 0, 2), 4)):
            f, why = self.policy(now, base)
            self.assertEqual(f, trim, now)
            self.assertIn("not counted", why)

    def test_the_trim_moves_only_in_the_clock_mode(self):
        for mode in (0, 1):
            f, why = self.policy((44100, 2, 1, mode))
            self.assertEqual(f, 2, mode)
        f, why = self.policy((22050, 2, 1, 2))
        self.assertEqual(f, 4)

    def test_a_drift_is_predicted_a_block_ahead(self):
        s = self.Series()
        self.lib.eq_reset(ctypes.byref(s))
        rng = np.random.default_rng(15)
        # 0.02 dB a minute, with 0.01 dB of noise, over 30 blocks
        for k in range(30):
            self.lib.eq_push(ctypes.byref(s), 60.0 * k,
                             0.02 * k + rng.normal(0, 0.01))
        slope = D()
        got = self.lib.eq_predict(ctypes.byref(s), 60.0 * 30,
                                  ctypes.byref(slope))
        self.assertAlmostEqual(got, 0.6, delta=0.03)
        self.assertAlmostEqual(slope.value * 60, 0.02, delta=0.004)

    def test_few_blocks_give_their_mean(self):
        s = self.Series()
        self.lib.eq_reset(ctypes.byref(s))
        self.lib.eq_push(ctypes.byref(s), 0.0, 1.0)
        self.lib.eq_push(ctypes.byref(s), 60.0, 3.0)
        self.assertAlmostEqual(self.lib.eq_predict(ctypes.byref(s), 600.0,
                                                   None), 2.0)

    def test_the_fit_reaches_a_target_and_holds_0_hz(self):
        rate = 44100.0
        f = np.geomspace(250, 20000, 24)
        f = np.concatenate([f, [50, 100, 150]])
        # a lift of 2 dB at 20 kHz, rising smoothly from 5 kHz
        want = 10 ** (np.clip(np.log2(f / 5000) / 2, 0, 1) * 2 / 20)
        w = np.ones(len(f))
        sides = (ctypes.c_long * 16)()
        self.assertEqual(self.lib.eq_fit_sides(dp(f), dp(want), dp(w),
                                               len(f), rate, None, sides), 0)
        for fk, wk in zip(f, want):
            got = self.lib.eq_sides_gain(sides, fk, rate)
            self.assertLess(abs(20 * np.log10(got / wk)), 0.1, fk)
        self.assertAlmostEqual(self.lib.eq_sides_gain(sides, 0.0, rate), 1.0,
                               places=9)

    def test_a_drift_taken_back_stays_near_the_taps_before(self):
        # the driver's correction at 2x, and a drift of the DAC of -0.3 dB
        # at 20 kHz, as f squared, taken back over es1869scope's bands:
        # sixteen taps over bands that mostly lie low, a system all but
        # singular, which only the ridge keeps in the driver's range
        rate = 44100.0
        src = open(os.path.join(ROOT, "linux", "es1869", "treble.h")).read()
        rows = re.findall(r"\{([^{}]*)\}", src[src.index("treble[3]"):])
        prior = (ctypes.c_long * 16)(
            *[int(x) for x in rows[1].split(",") if x.strip()])
        f = np.geomspace(250, 20000, 24)
        before = np.array([self.lib.eq_sides_gain(prior, fk, rate)
                           for fk in f])
        want = before * 10 ** (0.3 * (f / 20000) ** 2 / 20)
        f = np.concatenate([f, [50, 100, 150]])
        low = [self.lib.eq_sides_gain(prior, fk, rate) for fk in f[-3:]]
        want = np.concatenate([want, low])
        w = np.concatenate([np.ones(24), 0.2 * np.ones(3)])
        sides = (ctypes.c_long * 16)()
        self.assertEqual(self.lib.eq_fit_sides(dp(f), dp(want), dp(w),
                                               len(f), rate, prior, sides),
                         0)
        for fk, wk in zip(f, want):
            got = self.lib.eq_sides_gain(sides, fk, rate)
            self.assertLess(abs(20 * np.log10(got / wk)), 0.01, fk)
        for k in range(16):
            self.assertLess(abs(sides[k] - prior[k]), 1 << 23, k)
        # above the top band it follows, and stays near what it was
        for fk in (21000.0, 22050.0):
            change = 20 * np.log10(self.lib.eq_sides_gain(sides, fk, rate) /
                                   self.lib.eq_sides_gain(prior, fk, rate))
            self.assertLess(abs(change), 0.35, fk)

    def test_phase_taps_fit_a_turn(self):
        """the phase taps' sine series fitted to the turn that treble.h's
        phase taps make at 1x: within 1e-4 of it, and the taps alike"""
        lib = self.lib
        L = ctypes.c_long * 16
        want = L(-40142702, 44711051, -33316449, 25153521, -19642226,
                 15708982, -12749858, 10431719, -8560393, 7016073, -5720952,
                 4622071, -3682292, 2881258, -2254243, 1806345)
        f = np.linspace(100, 21000, 120)
        target = np.array([lib.eq_phase_resp(want, ff, 44100.0)
                           for ff in f])
        w = np.ones(len(f))
        got = L()
        self.assertEqual(lib.eq_fit_phase(
            f.ctypes.data_as(DP), target.ctypes.data_as(DP),
            w.ctypes.data_as(DP), len(f), 44100.0, None, got), 0)
        back = np.array([lib.eq_phase_resp(got, ff, 44100.0) for ff in f])
        self.assertLess(np.abs(back - target).max(), 1e-4)
        # the sine series is odd: no turn at 0 Hz, and the sign as the
        # driver's phase taps have it, a frame after against one before
        self.assertAlmostEqual(lib.eq_phase_resp(want, 0.0, 44100.0), 0.0)
        one = L(1 << 20)
        self.assertGreater(lib.eq_phase_resp(one, 5000.0, 44100.0), 0)


class ZStep(ctypes.Structure):
    _fields_ = [("bins", ctypes.c_uint)] + \
        [(n, DP) for n in ("xl", "xr", "lr")] + \
        [(n, DP * 2) for n in ("qq", "ql", "qr", "yl", "yr", "yq")] + \
        [("segments", ctypes.c_ulong)]


class StepTest(unittest.TestCase):
    """zstep.c: the DAC's gap at its sign, from music through the loop"""

    N = 8192

    def gap(self, gap, corr=0.6, seconds=20, noise=0.3):
        lib = Lib.get()
        lib.zstep_gap.restype = D
        lib.zstep_gap.argtypes = [ctypes.c_void_p, ctypes.c_int,
                                  ctypes.c_uint, ctypes.c_uint, DP]
        rate = 44100
        n = rate * seconds
        rng = np.random.RandomState(5)
        f = np.fft.rfftfreq(n, 1.0 / rate)
        shape = 1 / np.sqrt(np.maximum(f, 50.0))

        def music():
            w = np.fft.irfft(np.fft.rfft(rng.standard_normal(n)) * shape, n)
            return w / np.sqrt(np.mean(w * w))

        a = music()
        u = np.column_stack([a, corr * a + np.sqrt(1 - corr ** 2) * music()])
        # codes of about -30 dBFS rms, music's crest
        u = np.rint(u * 1000.0)
        dac = u + gap * (u >= 0)
        # the loop: a lowpass of 31 taps, three frames late, a gain
        t = np.arange(-15, 16)
        h = np.sinc(2 * 18000 / rate * t) * np.hanning(33)[1:-1]
        h *= 0.8 / h.sum()
        y = np.column_stack([np.convolve(dac[:, c], h)[:n] for c in (0, 1)])
        y = np.roll(y, 3, axis=0) + noise * rng.standard_normal((n, 2))
        z = ZStep()
        bins = self.N // 2 + 1
        self.assertEqual(lib.zstep_init(ctypes.byref(z), bins), 0)
        win = 0.5 - 0.5 * np.cos(2 * np.pi * np.arange(self.N) / self.N)
        q = (u >= 0) - 0.5

        def spec(v):
            s = np.fft.rfft(v * win)
            out = np.empty(2 * bins)
            out[0::2], out[1::2] = s.real, s.imag
            return out

        for at in range(0, n - self.N, self.N // 2):
            sl = slice(at, at + self.N)
            sp = [spec(u[sl, 0]), spec(u[sl, 1]), spec(q[sl, 0]),
                  spec(q[sl, 1]), spec(y[sl, 0]), spec(y[sl, 1])]
            lib.zstep_add(ctypes.byref(z), *[dp(v) for v in sp])
        lo, hi = int(1000 / rate * self.N), int(8000 / rate * self.N)
        out = []
        for c in (0, 1):
            spread = D()
            out.append((lib.zstep_gap(ctypes.byref(z), c, lo, hi,
                                      ctypes.byref(spread)), spread.value))
        lib.zstep_free(ctypes.byref(z))
        return out

    def test_gap(self):
        for c, (g, spread) in enumerate(self.gap(7.4)):
            self.assertAlmostEqual(g, 7.4, delta=0.2, msg=c)
            self.assertLess(spread, 0.2)

    def test_no_gap(self):
        for g, _spread in self.gap(0.0):
            self.assertLess(abs(g), 0.2)


class ShotTest(unittest.TestCase):
    """es1869scope draws a log into a picture: two pairs that share the
    Focusrite, interrupts with pulses, and the system's lines"""

    PAIR = (0x5aa9ff, 0xffa64d)
    HAT = (0x41c7b9, 0xff7eb6, 0xe8d04a)
    LATE = 0xff6f6f
    SAY = 0xffcf5a

    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.mkdtemp()
        cls.prog = os.path.join(cls.tmp, "es1869scope")
        src = [os.path.join(SCOPE, f) for f in
               ("es1869scope.c", "tie.c", "draw.c", "fb.c", "plot.c",
                "fft.c", "spectro.c", "loop.c", "loopview.c", "eq.c",
                "zstep.c", "say.c", "eqstate.c", "ctl.c", "chain.c",
                "a1lin.c", os.path.join("..", "..", "es1869", "capfilt.c"),
                os.path.join("..", "..", "test", "settab.c"))]
        subprocess.check_call(["gcc", "-O2", "-std=gnu99", "-Wall",
                               "-I" + os.path.join(ROOT, "linux", "es1869"),
                               "-Wextra", "-Werror", "-o", cls.prog] + src +
                              ["-lasound", "-lz", "-lm", "-lpthread"])

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

    def wav(self, path, frames, rate=44100):
        import wave
        w = wave.open(path, "wb")
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(rate)
        w.writeframes(np.round(np.clip(frames, -1, 1) * 32767)
                      .astype("<i2").tobytes())
        w.close()

    def test_the_loop_view_reads_a_known_path(self):
        """two files, what was played and what came back through a known
        path: the left through [1/4, 1/2, 1/4], the right halved, both
        inverted, 1234 frames late, each with the other at -40 dB"""
        rng = np.random.default_rng(12)
        n = 44100 * 4
        x = rng.uniform(-0.17, 0.17, (n, 2))
        left = np.convolve(x[:, 0], [0.25, 0.5, 0.25])[:n]
        right = 0.5 * x[:, 1]
        y = np.zeros((n + 1234, 2))
        y[1234:, 0] = -(left + 0.01 * x[:, 1])
        y[1234:, 1] = -(right + 0.005 * x[:, 0])
        y += rng.normal(0, 1e-4, y.shape)
        ref = os.path.join(self.tmp, "played.wav")
        rec = os.path.join(self.tmp, "back.wav")
        self.wav(ref, x)
        self.wav(rec, y)
        ppm = os.path.join(self.tmp, "loop.ppm")
        r = subprocess.run([self.prog, "-l", os.devnull, "-v", "3", "-x", ref,
                            "-i", rec, "-s", ppm],
                           capture_output=True, text=True)
        if r.returncode == 2 and "no console font" in r.stderr:
            self.skipTest("no console font on this machine")
        self.assertEqual(r.returncode, 0, r.stderr)
        rep = {}
        for line in r.stdout.splitlines():
            f = line.split()
            key = f[0] if f[0] != "at" else "at %s" % f[1]
            rep[key] = f[1:] if f[0] != "at" else f[2:]
        # the filter's centre is a frame on: the lag and the delay past it
        # add up to 1235 for the left, 1234 for the right
        lag = int(rep["lag"][0])
        dl, dr = map(float, rep["delay"])
        self.assertAlmostEqual(lag + dl, 1235, delta=0.05)
        self.assertAlmostEqual(lag + dr, 1234, delta=0.05)
        self.assertEqual(rep["invert"], ["1", "1"])
        self.assertAlmostEqual(float(rep["gain"][0]),
                               20 * np.log10(0.5 + 0.5 * np.cos(
                                   2 * np.pi * 1000 / 44100)), delta=0.05)
        self.assertAlmostEqual(float(rep["gain"][1]), 20 * np.log10(0.5),
                               delta=0.05)
        for hz in (100, 5000, 10000, 15000, 20000):
            row = rep["at %d" % hz]
            at = dict(zip(row[0::3], zip(row[1::3], row[2::3])))
            h = 0.5 + 0.5 * np.cos(2 * np.pi * hz / 44100)
            h1k = 0.5 + 0.5 * np.cos(2 * np.pi * 1000 / 44100)
            self.assertAlmostEqual(float(at["mag"][0]),
                                   20 * np.log10(h / h1k), delta=0.1, msg=hz)
            self.assertAlmostEqual(float(at["mag"][1]), 0, delta=0.1, msg=hz)
            # the phase past the delay: none, for both
            self.assertLess(abs(float(at["phase"][0])), 1.0, msg=hz)
            self.assertLess(abs(float(at["phase"][1])), 1.0, msg=hz)
            # crosstalk against each side's own path
            self.assertAlmostEqual(float(at["xtalk"][1]), -40, delta=1.5)
            self.assertAlmostEqual(float(at["xtalk"][0]),
                                   20 * np.log10(0.01 / h), delta=1.5)
        seen = self.colours(ppm)
        for rgb in self.PAIR:
            self.assertIn(rgb, seen, "%06x missing" % rgb)

    def test_the_engine_shares_what_it_measured(self):
        """-P: the loop from two files into the state a viewer reads,
        the response at its log-spaced points and the impulse response,
        and the engine ended"""
        rng = np.random.default_rng(13)
        n = 44100 * 4
        x = rng.uniform(-0.17, 0.17, (n, 2))
        y = np.zeros((n + 300, 2))
        y[300:, 0] = np.convolve(x[:, 0], [0.25, 0.5, 0.25])[:n]
        y[300:, 1] = 0.5 * x[:, 1]
        y += rng.normal(0, 1e-4, y.shape)
        ref = os.path.join(self.tmp, "shared-played.wav")
        rec = os.path.join(self.tmp, "shared-back.wav")
        self.wav(ref, x)
        self.wav(rec, y)
        state = os.path.join(self.tmp, "eq.state")
        r = subprocess.run([self.prog, "-l", os.devnull, "-v", "3", "-x", ref,
                            "-i", rec, "-s",
                            os.path.join(self.tmp, "shared.ppm"), "-P",
                            state], capture_output=True, text=True)
        if r.returncode == 2 and "no console font" in r.stderr:
            self.skipTest("no console font on this machine")
        self.assertEqual(r.returncode, 0, r.stderr)
        with open(state, "rb") as fh:
            s = EqsState.from_buffer_copy(fh.read())
        self.assertEqual((s.magic, s.size), (0x31534545, ctypes.sizeof(s)))
        self.assertEqual(s.state, 5)  # EQS_STOPPED
        self.assertTrue(os.path.exists(os.path.join(self.tmp, "eq.cmd")))
        hz = np.array(s.hz)
        self.assertAlmostEqual(hz[0], 20, delta=0.01)
        self.assertAlmostEqual(hz[-1], 20000, delta=1)
        h1k = 0.5 + 0.5 * np.cos(2 * np.pi * 1000 / 44100)
        for i in (40, 80, 110):
            want = 20 * np.log10((0.5 + 0.5 * np.cos(2 * np.pi * hz[i] /
                                                     44100)) / h1k)
            self.assertAlmostEqual(s.mag[0][i], want, delta=0.1, msg=hz[i])
            self.assertAlmostEqual(s.mag[1][i], 0, delta=0.1, msg=hz[i])
            self.assertGreater(s.coh[0][i], 0.99)
        # each side's impulse response peaks where its path does: the
        # left's filter at the lag, which it found, and the right's
        # single tap a frame before it
        self.assertEqual(int(np.argmax(np.abs(np.array(s.ir[0])))), 32)
        self.assertEqual(int(np.argmax(np.abs(np.array(s.ir[1])))), 31)

    def state(self, path):
        """an engine's state as a viewer finds it: measuring a 44.1 kHz
        stream at 2x, six blocks in, with a tone in both spectrograms"""
        s = EqsState()
        s.magic, s.version, s.size = (0x31534545, EQS_VERSION,
                                      ctypes.sizeof(s))
        s.state, s.watched = 2, 1
        s.stream_rate, s.factor, s.a1_rate = 44100, 2, 44100
        s.treble, s.mode, s.a1c = 1, 2, 1
        s.lagged, s.segments, s.blocks = 1, 120, 6
        for k in range(4):
            s.rms[k], s.peak[k] = -23.0, -6.0
        for i in range(EQS_POINTS):
            f = 20 * 1000 ** (i / (EQS_POINTS - 1))
            s.hz[i] = f
            for c in range(2):
                s.mag[c][i] = -0.5 * (f / 20000) ** 2
                s.coh[c][i] = 0.999
        for c in range(2):
            s.ir[c][32] = 1.0
        for b in range(EQS_BANDS):
            s.band_hz[b] = 250 * 80 ** (b / (EQS_BANDS - 1))
            s.band_dev[b], s.band_drift[b] = 0.1, 0.05
        for src in range(2):
            s.row_hz[src] = 44100 if src == 0 else 22050
            s.row_next[src] = 70
            for r in range(EQS_ROWS):
                for j in range(EQS_BINS):
                    s.rows[src][r][j] = -50 if j == 20 else -110
        s.a1taps[0] = s.a1taps[17] = 16384
        # the treble correction's phase taps at 2x, treble.h's first few
        for k, v in enumerate((-4422847, 4817666, -3523667, 2663384)):
            s.ptaps[16 + k] = s.ptaps0[16 + k] = v
        for k in range(3):
            s.a1ptaps[k] = s.a1ptaps[16 + k] = (900, -300, 80)[k]
        for i in range(10):
            s.zhist[0][11 + i] = 100 + i
        s.hist_count = s.hist_next = 6
        for i in range(6):
            b = s.hist[i]
            b.t, b.counted, b.trim = 60 * (i + 1), 1, 10 * i
            b.chip, b.applied, b.resid = 0.49, 0.5, 0.01
            b.corr20k = 0.05 * i
            for k in range(EQS_BANDS):
                b.dev[k] = 0.1
        # Audio 1's linearity, refitted twice: a soft dead zone about a
        # centre 15 LSB under the DC, and what the stage leaves of it
        s.lin_on, s.lin_fits, s.lin_frames = 1, 2, 4e5
        knots = [-512, -384, -256, -192, -128, -96, -64, -48, -32, -24,
                 -16, -12, -8, -6, -4, -2, 0]
        knots += [-k for k in knots[-2::-1]]
        for c in range(2):
            s.lin_centre[c] = -175
            for i, u in enumerate(knots):
                s.lin_path[c][i] = -4 * np.tanh((u + 15) / 50.0)
                s.lin_left[c][i] = 0.05 * np.sin(i)
                s.lin_curve[c][i] = int(1024 * np.tanh((u + 15) / 50.0))
            for i in range(EQS_LPER):
                s.lin_gain[c][i], s.lin_n[c][i] = 16384, 1e4
                s.lin_db[c][i] = -0.3 / (1 + i % 10)
        with open(path, "wb") as fh:
            fh.write(bytes(s))

    def test_the_viewer_draws_every_view_and_stage(self):
        """-A: a state drawn as the console shows it, with no card: each
        view, and the chain with each stage's picture"""
        state = os.path.join(self.tmp, "viewer.state")
        self.state(state)
        runs = [("-v", str(v)) for v in range(1, 6)]
        runs += [("-v", "5", "-S", str(k)) for k in range(11)]
        for args in runs:
            ppm = os.path.join(self.tmp, "viewer.ppm")
            r = subprocess.run([self.prog, "-A", state, "-s", ppm] +
                               list(args), capture_output=True, text=True)
            if r.returncode == 2 and "no console font" in r.stderr:
                self.skipTest("no console font on this machine")
            self.assertEqual(r.returncode, 0, (args, r.stderr))
            seen = self.colours(ppm)
            # the left side's colour wherever a picture draws a series
            if args in (("-v", "2"), ("-v", "3"), ("-v", "5", "-S", "0"),
                        ("-v", "5", "-S", "1"), ("-v", "5", "-S", "3"),
                        ("-v", "5", "-S", "4")):
                self.assertIn(self.PAIR[0], seen, args)

    def test_e_over_files_finds_audio1s_curve(self):
        """-E over files, a monitor at 2x and a recording, as the gap
        study took them: a loop with the DAC's gap, an AC coupling and the
        record path's soft dead zone about a centre 15 LSB under the DC.
        The path's curve comes out at the knots within 0.35 LSB, the
        centre on the DC, and what the stage leaves falls with each pass"""
        rng = np.random.default_rng(21)
        ra, f, secs = 44100, 2, 16
        n = ra * secs * f
        fr = np.fft.rfftfreq(n, 1.0 / (ra * f))
        shape = 1 / np.sqrt(np.maximum(fr, 50.0)) * (fr < 20000)

        def music():
            w = np.fft.irfft(np.fft.rfft(rng.standard_normal(n)) * shape, n)
            return w / np.sqrt(np.mean(w * w))
        a = music()
        codes = np.round(np.column_stack([a, 0.6 * a + 0.8 * music()]) *
                         1100)
        d = codes + 8.0 * (codes >= 0)
        h = -0.81 * (1j * fr / (1j * fr + 4.0)) / (1 + (fr / 30000) ** 4)
        h *= np.exp(-2j * np.pi * fr * 3.4 / ra)
        y = np.column_stack([np.fft.irfft(np.fft.rfft(d[:, c]) * h, n)
                             for c in (0, 1)])[::f]
        dc, cen = -175.0, -190.0
        v = y + dc
        r = v - 4 * np.tanh((v - cen) / 50) + rng.normal(0, 2, v.shape)
        pre = int(1.3 * ra)
        rec = np.concatenate([dc + rng.normal(0, 2, (pre, 2)), r])
        mon_wav = os.path.join(self.tmp, "replay-mon.wav")
        rec_wav = os.path.join(self.tmp, "replay-a1.wav")
        self.wav(mon_wav, codes / 32768, rate=ra * f)
        self.wav(rec_wav, np.round(rec) / 32768, rate=ra)
        log = os.path.join(self.tmp, "replay.log")
        r = subprocess.run([self.prog, "-l", os.devnull, "-E", "-x",
                            mon_wav, "-i", rec_wav, "-O", log, "-s",
                            os.path.join(self.tmp, "replay.ppm")],
                           capture_output=True, text=True)
        if r.returncode == 2 and "no console font" in r.stderr:
            self.skipTest("no console font on this machine")
        self.assertEqual(r.returncode, 0, r.stderr)
        with open(log) as fh:
            lines = [ln for ln in fh if " lin " in ln]
        self.assertEqual(len(lines), 3, lines)
        lefts = []
        for ln in lines:
            for side in "LR":
                m = re.search(side + r" centre (-?\d+) path (.*?) left "
                              r"([\d.]+)", ln)
                self.assertTrue(m, ln)
                # the centre from the first segments' DC, which the
                # music's lowest notes move by a few LSB
                centre = int(m.group(1))
                self.assertLessEqual(abs(centre - dc), 8, ln)
                for at, got in re.findall(r"(-?\d+):([-+][\d.]+)",
                                          m.group(2)):
                    want = -4 * np.tanh((centre + int(at) - cen) / 50)
                    self.assertAlmostEqual(float(got), want, delta=0.35,
                                           msg=(side, at, ln))
                lefts.append(float(m.group(3)))
        # what the stage leaves, each side, pass by pass
        self.assertLess(lefts[4], lefts[0])
        self.assertLess(lefts[5], lefts[1])
        self.assertLess(max(lefts[4:]), 1.2)

    def test_the_viewer_refuses_what_isnt_a_state(self):
        bad = os.path.join(self.tmp, "bad.state")
        with open(bad, "wb") as fh:
            fh.write(b"\0" * 64)
        r = subprocess.run([self.prog, "-A", bad, "-s",
                            os.path.join(self.tmp, "bad.ppm")],
                           capture_output=True, text=True)
        if r.returncode == 2 and "no console font" in r.stderr:
            self.skipTest("no console font on this machine")
        self.assertEqual(r.returncode, 2)
        self.assertIn("bad.state", r.stderr)

    def test_the_calibration_fits_taps_that_flatten_a_path(self):
        """-C on two files: a path that rolls off like the record path, -4
        dB at 20 kHz on the left and -2 on the right; the taps' response
        times the path's is flat to 20 kHz"""
        rng = np.random.default_rng(14)
        n = 44100 * 4
        x = rng.uniform(-0.17, 0.17, (n, 2))
        paths = ([0.1, 0.8, 0.1], [0.05, 0.9, 0.05])
        y = np.zeros((n + 300, 2))
        for c, h in enumerate(paths):
            y[300:, c] = -np.convolve(x[:, c], h)[:n]
        y += rng.normal(0, 1e-4, y.shape)
        ref = os.path.join(self.tmp, "cal-played.wav")
        rec = os.path.join(self.tmp, "cal-back.wav")
        self.wav(ref, x)
        self.wav(rec, y)
        r = subprocess.run([self.prog, "-l", os.devnull, "-C", "-x", ref,
                            "-i", rec, "-s", os.path.join(self.tmp, "c.ppm")],
                           capture_output=True, text=True)
        if r.returncode == 2 and "no console font" in r.stderr:
            self.skipTest("no console font on this machine")
        self.assertEqual(r.returncode, 0, r.stderr)
        taps = {}
        for line in r.stdout.splitlines():
            f = line.split()
            if f[0] == "taps":
                taps[f[1]] = np.array([int(v) for v in f[2:]]) / 16384
        self.assertEqual(sorted(taps), ["L", "R"])
        for c, side in enumerate("LR"):
            t = taps[side]
            self.assertEqual(len(t), 17)
            for hz in range(500, 20001, 500):
                w = 2 * np.pi * hz / 44100
                path = abs(sum(v * np.exp(-1j * w * k)
                               for k, v in enumerate(paths[c])))
                path1k = abs(sum(v * np.exp(-1j * 2 * np.pi * 1000 / 44100
                                            * k)
                                 for k, v in enumerate(paths[c])))
                corr = t[0] + 2 * sum(t[m] * np.cos(w * m)
                                      for m in range(1, 17))
                self.assertLess(abs(20 * np.log10(corr * path / path1k)),
                                0.2, (side, hz))

    def test_the_calibration_takes_back_the_phase_too(self):
        """-C on two files through paths whose phase isn't linear, as the
        record path's anti-alias filter: the symmetric taps and the phase
        taps together leave the path flat within 0.2 dB and its phase past
        the delay within 2.5 degrees from 500 Hz to 18 kHz, as near as 33
        taps come, where the symmetric taps alone leave up to 24 degrees"""
        rng = np.random.default_rng(15)
        n = 44100 * 4
        x = rng.uniform(-0.17, 0.17, (n, 2))
        # 5 and 7.6 dB down at 20 kHz, 28 and -12 degrees past the delay
        paths = ([0.06, 0.74, 0.16, 0.04], [0.2, 0.7, 0.1])
        y = np.zeros((n + 300, 2))
        for c, h in enumerate(paths):
            y[300:, c] = -np.convolve(x[:, c], h)[:n]
        y += rng.normal(0, 1e-4, y.shape)
        ref = os.path.join(self.tmp, "calph-played.wav")
        rec = os.path.join(self.tmp, "calph-back.wav")
        self.wav(ref, x)
        self.wav(rec, y)
        r = subprocess.run([self.prog, "-l", os.devnull, "-C", "-x", ref,
                            "-i", rec, "-s",
                            os.path.join(self.tmp, "cph.ppm")],
                           capture_output=True, text=True)
        if r.returncode == 2 and "no console font" in r.stderr:
            self.skipTest("no console font on this machine")
        self.assertEqual(r.returncode, 0, r.stderr)
        taps, ptaps = {}, {}
        for line in r.stdout.splitlines():
            f = line.split()
            if f and f[0] in ("taps", "ptaps"):
                (taps if f[0] == "taps" else ptaps)[f[1]] = np.array(
                    [int(v) for v in f[2:]]) / 16384
        self.assertEqual(sorted(ptaps), ["L", "R"])
        hz = np.arange(200, 20001, 100.0)
        w = 2 * np.pi * hz / 44100
        m = np.arange(1, 17)
        for c, side in enumerate("LR"):
            path = np.array([sum(v * np.exp(-1j * ww * k)
                                 for k, v in enumerate(paths[c]))
                             for ww in w])
            S = taps[side][0] + 2 * np.cos(np.outer(w, m)) @ taps[side][1:]
            A = 2 * np.sin(np.outer(w, m)) @ ptaps[side]
            for corr, tol in ((S - 1j * A, 2.5), (S, None)):
                total = corr * path
                ph = np.unwrap(np.angle(total))
                fit = (hz >= 200) & (hz <= 3000)
                line = np.polyfit(hz[fit], ph[fit], 1)
                resid = np.degrees(ph - np.polyval(line, hz))
                band = (hz >= 500) & (hz <= 18000)
                if tol is None:
                    # the symmetric taps alone leave the path's phase
                    self.assertGreater(np.abs(resid[band]).max(), 5, side)
                    continue
                mag = 20 * np.log10(np.abs(total) /
                                    np.abs(total[np.argmin(abs(hz - 1000))]))
                self.assertLess(np.abs(mag[band]).max(), 0.2, side)
                self.assertLess(np.abs(resid[band]).max(), tol, side)

    def test_the_loop_view_says_when_there_is_no_loop(self):
        rng = np.random.default_rng(13)
        x = rng.uniform(-0.17, 0.17, (44100 * 3, 2))
        y = rng.uniform(-0.17, 0.17, (44100 * 3, 2))
        ref = os.path.join(self.tmp, "a.wav")
        rec = os.path.join(self.tmp, "b.wav")
        self.wav(ref, x)
        self.wav(rec, y)
        ppm = os.path.join(self.tmp, "none.ppm")
        r = subprocess.run([self.prog, "-l", os.devnull, "-v", "3", "-x", ref,
                            "-i", rec, "-s", ppm],
                           capture_output=True, text=True)
        if r.returncode == 2 and "no console font" in r.stderr:
            self.skipTest("no console font on this machine")
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertTrue(r.stdout.startswith("none no loop"), r.stdout)

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

    def test_a_missing_log_draws_and_says_why(self):
        """with no clock log yet the clock view draws, its status line
        saying why in amber, where es1869scope used to quit"""
        ppm = os.path.join(self.tmp, "missing.ppm")
        r = subprocess.run([self.prog, "-l",
                            os.path.join(self.tmp, "no-such.log"), "-s",
                            ppm], capture_output=True, text=True)
        if r.returncode == 2 and "no console font" in r.stderr:
            self.skipTest("no console font on this machine")
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertIn(self.SAY, self.colours(ppm))


class A1LinTest(unittest.TestCase):
    """a1lin.c: Audio 1's record path against what should have come back,
    and the driver's linearity stage (capfilt.c) loaded from it, in a closed
    loop on a simulated path: a soft dead zone about a centre under the
    recording's DC, and the phase of quiet signals turned back"""

    RATE = 44100
    KNOTS, VALS, ENV, BANDS = 17, 33, 10, 4
    PER = ENV * BANDS
    ONE = 16384
    DC, CENTRE = -175.0, -190.0
    D, K = 4.0, 50.0  # the dead zone's depth and knee, LSB
    Q, A0 = 0.35, 8.0  # the turn back: Q / A radians, A no less than A0

    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.mkdtemp()
        so = os.path.join(cls.tmp, "a1lin.so")
        drv = os.path.join(ROOT, "linux", "es1869")
        shim = os.path.join(cls.tmp, "shim.c")
        with open(shim, "w") as f:
            f.write("#include <stddef.h>\n#include \"a1lin.h\"\n"
                    "#include \"capfilt.h\"\n"
                    "size_t a1l_size(int i) { return i == 0 ? sizeof(struct "
                    "a1lin) : i == 1 ? sizeof(struct a1lin_tables) : i == 2"
                    " ? sizeof(struct a1lin_fit) : sizeof(struct "
                    "es1869_cf); }\n")
        subprocess.check_call(["gcc", "-O2", "-std=gnu99", "-Wall", "-Wextra",
                               "-Werror", "-fPIC", "-shared", "-I" + SCOPE,
                               "-I" + drv, "-o", so,
                               os.path.join(SCOPE, "a1lin.c"),
                               os.path.join(drv, "capfilt.c"), shim, "-lm"])
        lib = cls.lib = ctypes.CDLL(so)
        L = ctypes.c_long

        class Tables(ctypes.Structure):
            _fields_ = [("centre", L * 2), ("curve", (L * cls.VALS) * 2),
                        ("gain", (L * cls.PER) * 2),
                        ("turn", (L * cls.PER) * 2)]

        class Fit(ctypes.Structure):
            _fields_ = [("coh", (D * cls.PER) * 2),
                        ("path", (D * cls.VALS) * 2),
                        ("table", (D * cls.VALS) * 2),
                        ("weight", (D * cls.VALS) * 2),
                        ("curve", (D * cls.VALS) * 2),
                        ("g", (D * (2 * cls.PER)) * 2),
                        ("dg", (D * (2 * cls.PER)) * 2),
                        ("frames", (D * cls.PER) * 2), ("n", D * 2)]
        cls.Tables, cls.Fit = Tables, Fit
        lib.a1l_size.restype = SZ
        assert lib.a1l_size(1) == ctypes.sizeof(Tables)
        assert lib.a1l_size(2) == ctypes.sizeof(Fit)
        VP = ctypes.c_void_p
        I16 = ctypes.POINTER(ctypes.c_int16)
        lib.a1lin_knot.argtypes = [ctypes.c_int]
        lib.a1lin_identity.argtypes = [ctypes.POINTER(Tables),
                                       ctypes.POINTER(L)]
        lib.a1lin_reset.argtypes = [VP, DP]
        lib.a1lin_restart.argtypes = [VP, DP]
        lib.a1lin_add.argtypes = [VP, ctypes.c_int, DP, D, I16, DP, DP, DP,
                                  ctypes.c_uint]
        lib.a1lin_fit.argtypes = [VP, ctypes.POINTER(Fit)]
        lib.a1lin_next.argtypes = [ctypes.POINTER(Fit),
                                   ctypes.POINTER(Tables), D,
                                   ctypes.POINTER(Tables)]
        lib.a1lin_path.restype = D
        lib.a1lin_path.argtypes = [ctypes.POINTER(Fit), ctypes.c_int,
                                   ctypes.c_int]
        lib.es1869_cf_identity.argtypes = [VP]
        lib.es1869_cf_reset.argtypes = [VP]
        lib.es1869_cf_run.argtypes = [VP, I16, ctypes.c_uint32,
                                      ctypes.c_uint32]

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.tmp)

    @staticmethod
    def hilbert(x):
        n = len(x)
        h = np.zeros(n)
        h[1:(n + 1) // 2] = 1
        return np.real(np.fft.ifft(np.fft.fft(x) * -1j * (h - h[::-1])))

    def path(self, p):
        """the simulated record path on what should come back (AC): the
        quiet signal's phase turned back by its envelope, then the soft
        dead zone about the centre, the ADC's noise, the rounding"""
        hp = self.hilbert(p)
        k = int(0.006 * self.RATE)
        env = np.sqrt(np.convolve(p * p + hp * hp, np.ones(k) / k, "same"))
        turn = -self.Q / np.maximum(env, self.A0)
        q = p * np.cos(turn) - hp * np.sin(turn)
        v = q + self.DC
        y = v - self.D * np.tanh((v - self.CENTRE) / self.K)
        return np.round(y + self.rng.normal(0, 1.5, len(y)))

    def stage(self, y, t):
        """the driver's stage on a recording, its FIR only a delay, lined
        up again"""
        lib = self.lib
        cf = ctypes.create_string_buffer(lib.a1l_size(3))
        lib.es1869_cf_identity(cf)
        lib.es1869_cf_reset(cf)
        # struct es1869_cf: taps, raw, then cl_on and the tables
        off_on = 2 * 17 * 2 + 2 * (32 + 256) * 2
        cf[off_on] = 1
        cl = (ctypes.c_int16 * (2 + 2 * self.VALS + 4 * self.PER))()
        cl[0], cl[1] = t.centre[0], t.centre[1]
        at = 2
        for c in range(2):
            for i in range(self.VALS):
                cl[at + c * self.VALS + i] = t.curve[c][i]
        at += 2 * self.VALS
        for c in range(2):
            for i in range(self.PER):
                cl[at + c * self.PER + i] = t.gain[c][i]
                cl[at + 2 * self.PER + c * self.PER + i] = t.turn[c][i]
        ctypes.memmove(ctypes.addressof(cf) + off_on + 2, cl,
                       ctypes.sizeof(cl))
        # as the driver's prepare: the tables, then the reset, which starts
        # the DC under the envelope from the centres
        lib.es1869_cf_reset(cf)
        x = np.ascontiguousarray(np.column_stack([y, y]).astype(np.int16))
        n = len(y)
        lib.es1869_cf_run(cf, x.ctypes.data_as(ctypes.POINTER(
            ctypes.c_int16)), n, 2)
        out = np.empty(n)
        out[:n - 16] = x[16:, 0]
        out[n - 16:] = x[-1, 0]
        return out

    def slow(self, n, hz):
        f = np.fft.rfftfreq(n, 1.0 / self.RATE)
        e = np.fft.irfft(np.fft.rfft(self.rng.standard_normal(n)) *
                         np.exp(-0.5 * (f / hz) ** 2), n)
        return (e - e.min()) / (e.max() - e.min())

    def music(self, secs):
        """four bands of noise under a fade that goes from -85 to -15 dBFS
        and back, each band moving 20 dB on its own, so that each leads at
        every size"""
        n = int(secs * self.RATE)
        f = np.fft.rfftfreq(n, 1.0 / self.RATE)
        out = np.zeros(n)
        fade = -85 + 70 * (0.5 - 0.5 * np.cos(2 * np.pi * 3 *
                                              np.arange(n) / n))
        for lo, hi in ((40, 200), (200, 800), (800, 3200), (3200, 12000)):
            w = np.fft.irfft(np.fft.rfft(self.rng.standard_normal(n)) *
                             ((f >= lo) & (f < hi)), n)
            w /= np.sqrt(np.mean(w * w))
            lev = fade - 20 * self.slow(n, 0.4)
            out += w * 32768 * 10 ** (lev / 20)
        return out

    def measure(self, p, r, y):
        """what should have come (p), what came raw (r) and what the stage
        gave (y), into a1lin, fitted"""
        lib = self.lib
        a = ctypes.create_string_buffer(lib.a1l_size(0))
        cen = (D * 2)(self.DC, self.DC)
        lib.a1lin_reset(a, cen)
        hp, hy = self.hilbert(p), self.hilbert(y - self.DC)
        r16 = np.ascontiguousarray(r.astype(np.int16))
        m = 1 << 16
        cp = lambda x: np.ascontiguousarray(x).ctypes.data_as(DP)  # noqa
        for s in range(64, len(p) - m - 64, m):
            sl = slice(s, s + m)
            for c in range(2):
                lib.a1lin_add(a, c, cp(p[sl]), self.DC,
                              r16[s:].ctypes.data_as(ctypes.POINTER(
                                  ctypes.c_int16)),
                              cp(y[sl]), cp(hp[sl]), cp(hy[sl]), m)
        f = self.Fit()
        self.assertEqual(lib.a1lin_fit(a, ctypes.byref(f)), 0)
        return f

    def tone_gain(self, t, level, hz=997.0):
        """a tone through the path and the stage: its gain and phase
        against what should have come back"""
        n = int(0.6 * self.RATE)
        amp = 32768 * 10 ** (level / 20)
        p = amp * np.cos(2 * np.pi * hz / self.RATE * np.arange(n))
        y = self.stage(self.path(p), t)
        s = slice(4000, n - 4000)
        e = np.exp(-2j * np.pi * hz / self.RATE * np.arange(n))[s]
        g = ((y[s] - y[s].mean()) * e).mean() / (p[s] * e).mean()
        return 20 * np.log10(abs(g)), np.degrees(np.angle(g))

    def test_knots(self):
        want = [-512, -384, -256, -192, -128, -96, -64, -48, -32, -24, -16,
                -12, -8, -6, -4, -2, 0]
        got = [self.lib.a1lin_knot(k) for k in range(self.VALS)]
        self.assertEqual(got[:17], want)
        self.assertEqual(got[16:], [-v for v in want[::-1]])

    def test_the_closed_loop_takes_the_path_back(self):
        """from music alone, a few rounds of measuring what the tables left
        and loading the next bring tones of -60 and -50 dBFS back to the
        path's gain within 0.1 dB and its phase within a third of a degree,
        from 0.6 dB and a degree"""
        self.rng = np.random.default_rng(11)
        lib = self.lib
        t = self.Tables()
        cen = (ctypes.c_long * 2)(int(self.DC), int(self.DC))
        lib.a1lin_identity(ctypes.byref(t), cen)
        g0 = {lev: self.tone_gain(t, lev) for lev in (-60, -50)}
        self.assertLess(g0[-60][0], -0.4)
        self.assertLess(g0[-60][1], -0.5)
        p = self.music(40)
        y0 = self.path(p)
        for _ in range(6):
            f = self.measure(p, y0, self.stage(y0, t))
            nxt = self.Tables()
            lib.a1lin_next(ctypes.byref(f), ctypes.byref(t), 0.7,
                           ctypes.byref(nxt))
            t = nxt
        for lev in (-60, -50):
            db, deg = self.tone_gain(t, lev)
            self.assertLess(abs(db), 0.1, (lev, db, g0[lev]))
            self.assertLess(abs(deg), 0.35, (lev, deg, g0[lev]))
        # what the path itself does, as measured: the dead zone's curve
        f = self.measure(p, y0, self.stage(y0, t))
        for k in range(self.VALS):
            v = self.DC + lib.a1lin_knot(k)
            want = -self.D * np.tanh((v - self.CENTRE) / self.K)
            got = lib.a1lin_path(ctypes.byref(f), 0, k)
            if abs(v - self.DC) <= 256:
                self.assertAlmostEqual(got, want, delta=0.25,
                                       msg=v - self.DC)


if __name__ == "__main__":
    unittest.main()
