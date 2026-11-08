#!/usr/bin/env python3
# (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
#
# Licensed under GPL Version 3.0
"""snd-es1869's filter on Audio 1's recordings, on the host.

capfilt.c is built for this machine and called through ctypes. The tests
hold it to a convolution in numpy, rounded and clipped the same way, frame
for frame, whatever the size of each pass, in mono and stereo, with each
channel's own taps.

usage: python3 -m unittest tests.test_es1869cf   (needs gcc and numpy)
"""

import ctypes
import os
import shutil
import subprocess
import tempfile
import unittest

import numpy as np

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DRV = os.path.join(ROOT, "linux", "es1869")
HALF = 16
TAPS = 2 * HALF + 1
ONE = 16384
CHUNK = 256


class Cf(ctypes.Structure):
    _fields_ = [("taps", (ctypes.c_int16 * (HALF + 1)) * 2),
                ("raw", (ctypes.c_int16 * (TAPS - 1 + CHUNK)) * 2)]


class Lib:
    lib = None
    tmp = None

    @classmethod
    def get(cls):
        if cls.lib is None:
            cls.tmp = tempfile.mkdtemp()
            out = os.path.join(cls.tmp, "capfilt.so")
            subprocess.run(["gcc", "-std=gnu11", "-Wall", "-Wextra", "-Werror",
                            "-O2", "-fPIC", "-shared", "-I" + DRV, "-o", out,
                            os.path.join(DRV, "capfilt.c")], check=True)
            cls.lib = ctypes.CDLL(out)
            for f in ("es1869_cf_reset", "es1869_cf_identity"):
                getattr(cls.lib, f).argtypes = [ctypes.POINTER(Cf)]
            cls.lib.es1869_cf_run.argtypes = [
                ctypes.POINTER(Cf), ctypes.POINTER(ctypes.c_int16),
                ctypes.c_uint32, ctypes.c_uint32]
        return cls.lib


def tearDownModule():
    if Lib.tmp:
        shutil.rmtree(Lib.tmp)


def reference(x, taps):
    """the filter in numpy: frame i is the sum over the 33 raw frames up to
    i, the centre 16 back, with silence before the first"""
    full = np.concatenate([taps[:0:-1], taps]).astype(np.int64)
    pad = np.concatenate([np.zeros(TAPS - 1, np.int64), x.astype(np.int64)])
    acc = np.convolve(pad, full[::-1], "valid")[:len(x)]
    # the C rounds by adding half and shifting, which floors
    return np.clip((acc + ONE // 2) >> 14, -32768, 32767)


def run(cf, frames, channels, sizes):
    """frames (n x channels) through the filter in passes of these sizes"""
    lib = Lib.get()
    buf = np.ascontiguousarray(frames.astype(np.int16).ravel())
    p = buf.ctypes.data_as(ctypes.POINTER(ctypes.c_int16))
    at = 0
    n = len(frames)
    k = 0
    while at < n:
        step = min(sizes[k % len(sizes)], n - at)
        lib.es1869_cf_run(ctypes.byref(cf), ctypes.cast(
            ctypes.addressof(p.contents) + 2 * at * channels,
            ctypes.POINTER(ctypes.c_int16)), step, channels)
        at += step
        k += 1
    return buf.reshape(n, channels)


def set_taps(cf, left, right):
    for c, t in enumerate((left, right)):
        for i, v in enumerate(t):
            cf.taps[c][i] = int(v)


class FilterTest(unittest.TestCase):

    def fresh(self):
        lib = Lib.get()
        cf = Cf()
        lib.es1869_cf_identity(ctypes.byref(cf))
        lib.es1869_cf_reset(ctypes.byref(cf))
        return cf

    def test_identity_is_a_delay_of_16_frames(self):
        rng = np.random.default_rng(1)
        x = rng.integers(-32768, 32768, (5000, 2))
        for sizes in ((1,), (7, 300, 13), (256,), (5000,)):
            cf = self.fresh()
            y = run(cf, x, 2, sizes)
            self.assertTrue((y[HALF:] == x[:-HALF]).all(), sizes)
            self.assertTrue((y[:HALF] == 0).all())

    def test_a_filter_matches_numpy_whatever_the_passes(self):
        rng = np.random.default_rng(2)
        x = rng.integers(-20000, 20000, (7000, 2))
        left = np.round(rng.normal(0, 0.05, HALF + 1) * ONE)
        left[0] = 1.3 * ONE
        right = np.round(rng.normal(0, 0.05, HALF + 1) * ONE)
        right[0] = 0.9 * ONE
        for sizes in ((1, 2, 3), (255, 257), (1000,), (31, 7000)):
            cf = self.fresh()
            set_taps(cf, left, right)
            y = run(cf, x, 2, sizes)
            self.assertTrue((y[:, 0] == reference(x[:, 0], left)).all(),
                            sizes)
            self.assertTrue((y[:, 1] == reference(x[:, 1], right)).all(),
                            sizes)

    def test_mono_takes_the_lefts_taps(self):
        rng = np.random.default_rng(3)
        x = rng.integers(-20000, 20000, (3000, 1))
        left = np.zeros(HALF + 1)
        left[0], left[1], left[2] = 1.2 * ONE, -0.1 * ONE, 0.02 * ONE
        right = np.zeros(HALF + 1)
        right[0] = ONE
        cf = self.fresh()
        set_taps(cf, left, right)
        y = run(cf, x, 1, (100,))
        self.assertTrue((y[:, 0] == reference(x[:, 0], left)).all())

    def test_full_scale_clips_and_never_wraps(self):
        x = np.zeros((2000, 2), np.int64)
        x[::2] = 32767
        x[1::2] = -32768
        taps = np.zeros(HALF + 1)
        # a boost at the top of the band: the alternating signal gains 2.4
        taps[0], taps[1] = 1.4 * ONE, -0.5 * ONE
        cf = self.fresh()
        set_taps(cf, taps, taps)
        y = run(cf, x, 2, (64,))
        ref = reference(x[:, 0], taps)
        self.assertTrue((y[:, 0] == ref).all())
        self.assertEqual(y[HALF + 10:, 0].max(), 32767)
        self.assertEqual(y[HALF + 10:, 0].min(), -32768)

    def test_the_response_of_a_correction(self):
        """a filter that lifts the top of the band: the response numpy
        computes from the taps comes through a recording of noise"""
        rng = np.random.default_rng(4)
        x = rng.integers(-8000, 8000, (1 << 16, 2))
        taps = np.zeros(HALF + 1)
        taps[0], taps[1], taps[2] = 1.25 * ONE, -0.15 * ONE, 0.025 * ONE
        cf = self.fresh()
        set_taps(cf, taps, taps)
        y = run(cf, x, 2, (512,))
        n = 4096
        hx = np.zeros(n // 2 + 1, complex)
        hy = np.zeros(n // 2 + 1, complex)
        for s in range(0, len(x) - n - HALF, n // 2):
            w = np.hanning(n)
            fx = np.fft.rfft(w * x[s:s + n, 0])
            fy = np.fft.rfft(w * y[s + HALF:s + HALF + n, 0])
            hx += np.abs(fx) ** 2
            hy += fy * np.conj(fx)
        h = np.abs(hy / hx)
        f = np.fft.rfftfreq(n)
        full = np.concatenate([taps[:0:-1], taps]) / ONE
        want = np.abs(np.fft.rfft(full, n * 8)[::8])
        k = slice(10, n // 2 - 10)
        self.assertLess(np.max(np.abs(20 * np.log10(h[k] / want[k]))), 0.02)
        self.assertGreater(want[np.argmin(abs(f - 0.45))], 1.5)


if __name__ == "__main__":
    unittest.main()
