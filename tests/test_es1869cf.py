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


KNOTS = 17
VALS = 2 * KNOTS - 1
ENV = 10
BANDS = 4
PER = ENV * BANDS


class Cl(ctypes.Structure):
    _fields_ = [("centre", ctypes.c_int16 * 2),
                ("curve", (ctypes.c_int16 * VALS) * 2),
                ("gain", (ctypes.c_int16 * PER) * 2),
                ("turn", (ctypes.c_int16 * PER) * 2)]


class Cf(ctypes.Structure):
    _fields_ = [("taps", (ctypes.c_int16 * (HALF + 1)) * 2),
                ("raw", (ctypes.c_int16 * (TAPS - 1 + CHUNK)) * 2),
                ("cl_on", ctypes.c_bool),
                ("cl", Cl),
                ("cl_pow", ctypes.c_int64 * 2),
                ("cl_adv", ctypes.c_int64 * 2),
                ("cl_re", ctypes.c_int32 * 2),
                ("cl_im", ctypes.c_int32 * 2)]


# the struct's size and a few places, from the C, against the mirror above
SHIM = r"""
#include <stddef.h>
#include "capfilt.h"
size_t cf_layout(int i)
{
    switch (i) {
    case 0: return sizeof(struct es1869_cf);
    case 1: return offsetof(struct es1869_cf, cl_on);
    case 2: return offsetof(struct es1869_cf, cl);
    case 3: return offsetof(struct es1869_cf, cl_pow);
    case 4: return offsetof(struct es1869_cf, cl_im);
    }
    return 0;
}
"""


class Lib:
    lib = None
    tmp = None

    @classmethod
    def get(cls):
        if cls.lib is None:
            cls.tmp = tempfile.mkdtemp()
            out = os.path.join(cls.tmp, "capfilt.so")
            shim = os.path.join(cls.tmp, "shim.c")
            with open(shim, "w") as f:
                f.write(SHIM)
            subprocess.run(["gcc", "-std=gnu11", "-Wall", "-Wextra", "-Werror",
                            "-O2", "-fPIC", "-shared", "-I" + DRV, "-o", out,
                            os.path.join(DRV, "capfilt.c"), shim], check=True)
            cls.lib = ctypes.CDLL(out)
            cls.lib.cf_layout.restype = ctypes.c_size_t
            want = (ctypes.sizeof(Cf), Cf.cl_on.offset, Cf.cl.offset,
                    Cf.cl_pow.offset, Cf.cl_im.offset)
            got = tuple(cls.lib.cf_layout(i) for i in range(5))
            if got != want:
                raise AssertionError("struct es1869_cf: C %s, mirror %s" %
                                     (got, want))
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


# the linearity stage in Python, integer for integer as capfilt.c has it
HILB = (10304, 3113, 1527, 795, 393, 171, 58, 10)
EDGE = (124, 1508, 7214)


def cdiv2(a):
    """C's division by 2, toward zero"""
    return -((-a) // 2) if a < 0 else a // 2


def place(pw, adv):
    p = max(pw, 1)
    b = 0
    while b < BANDS - 1 and adv * ONE >= EDGE[b] * p:
        b += 1
    top = p.bit_length() - 1
    m = (p >> (top - 8)) if top >= 8 else (p << (8 - top))
    at = cdiv2(((top << 8) | (m & 255)) - (20 << 8))
    return b, min(max(at, 0), (ENV - 1) << 8)


def knot(i):
    return 2 * i if i < 2 else (2 + (i & 1)) << (((i + 2) >> 1) - 1)


def curve_at(v, u):
    mid = KNOTS - 1
    a, d = abs(u), (-1 if u < 0 else 1)
    if a >= 512:
        return v[mid + d * (KNOTS - 1)]
    if a < 4:
        i, sh = a >> 1, 1
    else:
        m = a.bit_length() - 1
        i, sh = 2 * m - 2 + ((a >> (m - 1)) & 1), m - 1
    lo, hi = v[mid + d * i], v[mid + d * (i + 1)]
    return lo + (((hi - lo) * (a - knot(i))) >> sh)


def lerp(t, pos):
    j, f = pos >> 8, pos & 255
    if j >= ENV - 1:
        return t[ENV - 1]
    return t[j] + (((t[j + 1] - t[j]) * f) >> 8)


def reference_cl(x, taps, cl):
    """the FIR and then the stage, frame by frame, for one channel; cl is a
    dict of centre, curve, gain, turn for it"""
    full = np.concatenate([taps[:0:-1], taps]).astype(np.int64)
    pad = np.concatenate([np.zeros(TAPS - 1, np.int64), x.astype(np.int64)])
    accs = np.convolve(pad, full[::-1], "valid")[:len(x)]
    pw = adv = pre = pim = 0
    out = np.empty(len(x), np.int64)
    for i in range(len(x)):
        acc = int(accs[i])
        c0 = i + HALF  # the centre's place in pad
        h = sum(HILB[k] * int(pad[c0 - (2 * k + 1)] - pad[c0 + 2 * k + 1])
                for k in range(8))
        re, im = acc >> 10, h >> 10
        pw += re * re + im * im - (pw >> 8)
        adv += im * pre - re * pim - (adv >> 8)
        pre, pim = re, im
        b, pos = place(pw, adv)
        u = ((acc + ONE // 2) >> 14) - cl["centre"]
        acc += curve_at(cl["curve"], u) * (ONE // 256)
        g = lerp(cl["gain"][b * ENV:(b + 1) * ENV], pos)
        t = lerp(cl["turn"][b * ENV:(b + 1) * ENV], pos)
        acc -= (t * h) >> 14
        acc = acc + ((acc * (g - ONE)) >> 14)
        out[i] = min(max((acc + ONE // 2) >> 14, -32768), 32767)
    return out


def set_cl(cf, sides):
    cf.cl_on = True
    for c, d in enumerate(sides):
        cf.cl.centre[c] = d["centre"]
        for i in range(VALS):
            cf.cl.curve[c][i] = d["curve"][i]
        for i in range(PER):
            cf.cl.gain[c][i] = d["gain"][i]
            cf.cl.turn[c][i] = d["turn"][i]


def identity_cl():
    return dict(centre=0, curve=[0] * VALS, gain=[ONE] * PER, turn=[0] * PER)


def tone(n, f, amp, rate=44100, phase=0.0):
    return amp * np.cos(2 * np.pi * f / rate * np.arange(n) + phase)


def fit_tone(y, f, rate=44100):
    """the complex amplitude of f in y, with its mean"""
    t = np.arange(len(y))
    B = np.column_stack([np.cos(2 * np.pi * f / rate * t),
                         np.sin(2 * np.pi * f / rate * t), np.ones(len(y))])
    c, *_ = np.linalg.lstsq(B, y.astype(float), rcond=None)
    return c[0] - 1j * c[1]


class LinearityTest(unittest.TestCase):
    """the linearity stage after the FIR (capfilt.c)"""

    def fresh(self, sides=None, taps=None):
        lib = Lib.get()
        cf = Cf()
        lib.es1869_cf_identity(ctypes.byref(cf))
        lib.es1869_cf_reset(ctypes.byref(cf))
        if taps is not None:
            set_taps(cf, taps, taps)
        set_cl(cf, sides or (identity_cl(), identity_cl()))
        return cf

    def test_identity_tables_leave_the_filter_as_it_was(self):
        rng = np.random.default_rng(5)
        x = rng.integers(-20000, 20000, (4000, 2))
        taps = np.zeros(HALF + 1)
        taps[0], taps[1], taps[3] = 1.1 * ONE, -0.05 * ONE, 0.01 * ONE
        cf = self.fresh(taps=taps)
        y = run(cf, x, 2, (100, 333))
        self.assertTrue((y[:, 0] == reference(x[:, 0], taps)).all())
        self.assertTrue((y[:, 1] == reference(x[:, 1], taps)).all())

    def test_the_stage_matches_python_integer_for_integer(self):
        rng = np.random.default_rng(6)
        n = 6000
        # music-like: a few tones at many sizes, and noise, about a DC
        x = np.zeros((n, 2))
        for f, a in ((97.0, 900), (733.0, 60), (2900.0, 20), (9100.0, 9)):
            x[:, 0] += tone(n, f, a)
            x[:, 1] += tone(n, f * 1.07, a * 0.7, phase=1.0)
        x += rng.normal(0, 3, (n, 2)) - 170
        x[n // 2:] *= 0.05
        x = np.round(x).astype(np.int64)
        taps = np.zeros(HALF + 1)
        taps[0], taps[1], taps[2] = 1.2 * ONE, -0.1 * ONE, 0.02 * ONE
        sides = []
        for c in range(2):
            d = identity_cl()
            d["centre"] = -190 - 3 * c
            d["curve"] = [int(v) for v in rng.integers(-1500, 1500, VALS)]
            d["gain"] = [int(v) for v in rng.integers(14000, 19000, PER)]
            d["turn"] = [int(v) for v in rng.integers(-800, 800, PER)]
            sides.append(d)
        for sizes in ((1, 2, 3), (256,), (700, 31)):
            cf = self.fresh(sides, taps)
            y = run(cf, x, 2, sizes)
            for c in range(2):
                want = reference_cl(x[:, c], taps, sides[c])
                self.assertTrue((y[:, c] == want).all(),
                                "side %d, passes %s: %d frames differ" % (
                                    c, sizes, (y[:, c] != want).sum()))

    def test_bands_and_envelope_bins(self):
        """a tone lands in its band, and its envelope in its bin"""
        lib = Lib.get()
        lib.es1869_cl_place.argtypes = [
            ctypes.c_int64, ctypes.c_int64, ctypes.POINTER(ctypes.c_uint32),
            ctypes.POINTER(ctypes.c_uint32)]
        for f, band in ((100.0, 0), (450.0, 1), (1500.0, 2), (6000.0, 3)):
            for j in range(ENV):
                amp = 4 << j
                n = 8000
                x = np.zeros((n, 2), np.int64)
                x[:, 0] = np.round(tone(n, f, amp))
                cf = self.fresh()
                run(cf, x, 2, (256,))
                b, pos = ctypes.c_uint32(), ctypes.c_uint32()
                lib.es1869_cl_place(cf.cl_pow[0], cf.cl_adv[0],
                                    ctypes.byref(b), ctypes.byref(pos))
                self.assertEqual(b.value, band, (f, amp))
                # the short Hilbert reads the envelope low under 2 kHz,
                # half a bin at 100 Hz, and the log's linear mantissa
                # up to 11/256 of a bin under it between octaves
                lo = j * 256 - (140 if f < 1000 else 60 if f < 2000 else 30)
                if j == 0:
                    lo = 0
                self.assertGreaterEqual(pos.value, lo, (f, amp))
                self.assertLessEqual(pos.value, j * 256 + 30, (f, amp))

    def test_a_soft_dead_zone_taken_back(self):
        """a path that compresses small samples about a centre: the curve
        that adds back what it took gives a quiet tone's gain back"""
        D, k, c0 = 4.0, 50.0, -190.0
        q = np.arange(-600, 601)
        # the path's output for each input, and the inverse by fixed point
        rec = lambda v: v - D * np.tanh((v - c0) / k)  # noqa: E731
        side = identity_cl()
        side["centre"] = int(c0)
        offs = [-knot(i) for i in range(KNOTS - 1, 0, -1)] + \
            [knot(i) for i in range(KNOTS)]
        vals = []
        for o in offs:
            y = c0 + o
            v = y
            for _ in range(50):
                v = y + D * np.tanh((v - c0) / k)
            vals.append(int(round((v - y) * 256)))
        side["curve"] = vals
        del q
        n = 30000
        rng = np.random.default_rng(7)
        for level, amp in ((-60, 26.5), (-50, 84.0), (-40, 265.0)):
            # with the ADC's own noise, as Audio 1 has it, which keeps the
            # rounding of so small a tone from moving it by 0.05 dB
            x = tone(n, 997.0, amp) - 175
            y = np.round(rec(x) + rng.normal(0, 1.5, n)).astype(np.int64)
            cf = self.fresh((side, side))
            z = run(cf, np.column_stack([y, y]), 2, (256,))
            g_raw = 20 * np.log10(abs(fit_tone(y[1000:], 997.0)) / amp)
            g_out = 20 * np.log10(abs(fit_tone(z[1000 + HALF:, 0], 997.0)) /
                                  amp)
            self.assertLess(g_raw, {-60: -0.5, -50: -0.25, -40: -0.08}[level])
            # what the noise through the curve and the knots leave
            self.assertLess(abs(g_out), 0.05, level)

    def test_gain_and_turn_by_band_and_envelope(self):
        """a gain and a turn in the tone's band and bins move it so much,
        the turn by the Hilbert FIR's own gain at the tone"""
        k = np.arange(-15, 16)
        hq = np.zeros(31)
        for i, t in enumerate(HILB):
            hq[15 + 2 * i + 1] = t / ONE
            hq[15 - 2 * i - 1] = -t / ONE
        for f, band in ((997.0, 2), (5000.0, 3)):
            hg = abs(np.sum(hq * np.exp(-2j * np.pi * f / 44100 * k)))
            side = identity_cl()
            side["gain"] = [ONE] * PER
            side["turn"] = [0] * PER
            for j in range(ENV):
                side["gain"][band * ENV + j] = int(1.1 * ONE)
                side["turn"][band * ENV + j] = int(0.02 * ONE)
            n = 30000
            x = np.round(tone(n, f, 1000.0)).astype(np.int64)
            cf = self.fresh((side, side))
            z = run(cf, np.column_stack([x, x]), 2, (256,))
            a = fit_tone(x[1000:-HALF], f)
            b = fit_tone(z[1000 + HALF:, 0], f)
            r = b / a
            self.assertAlmostEqual(abs(r), 1.1, delta=0.002)
            # ahead by the turn times the transform's gain at f
            self.assertAlmostEqual(np.angle(r), 0.02 * hg, delta=0.0005)


if __name__ == "__main__":
    unittest.main()
