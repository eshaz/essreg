#!/usr/bin/env python3
# (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
#
# Licensed under GPL Version 3.0
"""snd-es1869's rate change and clock fit, on the host.

resample.c is built for this machine and called through ctypes. The tests
hold its integer table to a Kaiser-windowed sinc from numpy, its C and MMX
runs to the same bytes, on the worst signals for the 32-bit sums too, its
noise on tones to what docs/LINUX.md says of it, and its clock fit, on
interrupts that come late, to the rate they were made with.

usage: python3 -m unittest tests.test_es1869rs   (needs gcc and numpy)
"""

import ctypes
import os
import platform
import re
import shutil
import subprocess
import tempfile
import unittest

try:
    import numpy as np
except ImportError:
    np = None

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DRV = os.path.join(ROOT, "linux", "es1869")

TAPS = 68
BEHIND = 33
ROWS = 128
STRIDE = 2 * TAPS
TABLE = ROWS * STRIDE
WORDS = [24, 24, 23, 21, 20, 19, 18, 17, 16, 17, 18, 19, 20, 21, 23, 24, 24]
SCALE = [WORDS[n // 4] for n in range(TAPS)]
BETA = 9.0
FS = 44100.0
MASTER = 23814000.0
X86 = platform.machine() in ("x86_64", "i386", "i686")

s16p = ctypes.POINTER(ctypes.c_int16)


class Job(ctypes.Structure):
    _fields_ = [("tab", s16p), ("centre", ctypes.c_uint32),
                ("ch", s16p * 2), ("odd", (s16p * 3) * 2),
                ("dst", ctypes.POINTER(ctypes.c_uint8)),
                ("frames", ctypes.c_uint32), ("room", ctypes.c_uint32),
                ("used", ctypes.c_uint32), ("frac", ctypes.c_uint32),
                ("step", ctypes.c_int32), ("stereo", ctypes.c_bool),
                ("dither", ctypes.c_bool), ("seed", ctypes.c_uint32),
                ("last", ctypes.c_int32 * 2)]


class Fit(ctypes.Structure):
    _fields_ = [("x", ctypes.c_int64), ("line", ctypes.c_int64),
                ("rate", ctypes.c_int64), ("k", ctypes.c_uint32),
                ("kmax", ctypes.c_uint32), ("odd", ctypes.c_uint32),
                ("seeded", ctypes.c_bool)]


class Clock(ctypes.Structure):
    _fields_ = [("chip", Fit), ("system", Fit), ("ticks", ctypes.c_uint64),
                ("g_ticks", ctypes.c_uint64), ("g_raw", ctypes.c_uint64),
                ("g_best", ctypes.c_int64), ("b_ticks", ctypes.c_uint64),
                ("b_raw", ctypes.c_uint64), ("l_ticks", ctypes.c_uint64),
                ("l_raw", ctypes.c_uint64), ("u_raw", ctypes.c_uint64),
                ("u_mono", ctypes.c_uint64), ("ns_q16", ctypes.c_uint32),
                ("samples", ctypes.c_uint32), ("have", ctypes.c_bool),
                ("fed", ctypes.c_bool), ("timed", ctypes.c_bool)]


class RS(ctypes.Structure):
    _fields_ = [("tab", s16p), ("flat", s16p), ("treble", ctypes.c_bool),
                ("on", ctypes.c_bool), ("mmx", ctypes.c_bool),
                ("clock", ctypes.c_bool), ("dither", ctypes.c_bool),
                ("lined", ctypes.c_bool),
                ("frac", ctypes.c_uint32), ("trim", ctypes.c_int32),
                ("base", ctypes.c_int32), ("step", ctypes.c_int32),
                ("w", ctypes.c_ulong), ("pos", ctypes.c_ulong),
                ("since", ctypes.c_uint32), ("seed", ctypes.c_uint32),
                ("last", ctypes.c_int32 * 2),
                ("ch", (ctypes.c_int16 * (256 + TAPS + 2)) * 2),
                ("odd", ((ctypes.c_int16 * (256 + TAPS + 2)) * 3) * 2)]


class Stream(ctypes.Structure):
    _fields_ = [("src", ctypes.c_void_p), ("ring", ctypes.c_void_p),
                ("buffer", ctypes.c_ulong), ("boundary", ctypes.c_ulong),
                ("appl", ctypes.c_ulong),
                ("rot", ctypes.POINTER(ctypes.c_ulong)),
                ("stereo", ctypes.c_bool), ("mmx", ctypes.c_bool)]


class Fill(ctypes.Structure):
    _fields_ = [("buffer", ctypes.c_ulong), ("boundary", ctypes.c_ulong),
                ("guard", ctypes.c_ulong), ("lead", ctypes.c_ulong),
                ("slack", ctypes.c_ulong), ("filled", ctypes.c_ulong),
                ("seen", ctypes.c_ulong), ("standing", ctypes.c_bool),
                ("backlog", ctypes.c_bool), ("late", ctypes.c_uint32)]


_LIB = {}


def library():
    """resample.c, built once"""
    if "lib" not in _LIB:
        tmp = _LIB["tmp"] = tempfile.TemporaryDirectory()
        out = os.path.join(tmp.name, "libes1869rs.so")
        subprocess.run(["gcc", "-std=gnu11", "-Wall", "-Wextra", "-Werror",
                        "-O2", "-fPIC", "-shared", "-I" + DRV, "-o", out,
                        os.path.join(DRV, "resample.c"),
                        os.path.join(DRV, "hw.c")],
                       check=True, capture_output=True)
        lib = ctypes.CDLL(out)
        lib.es1869_rs_table.argtypes = [s16p, ctypes.c_bool]
        lib.es1869_rs_table.restype = None
        lib.es1869_rs_run_c.argtypes = [ctypes.POINTER(Job)]
        lib.es1869_rs_run_c.restype = ctypes.c_uint32
        if X86:
            lib.es1869_rs_run_mmx.argtypes = [ctypes.POINTER(Job)]
            lib.es1869_rs_run_mmx.restype = ctypes.c_uint32
        lib.es1869_rs_split.argtypes = [s16p, s16p, ctypes.c_void_p,
                                        ctypes.c_uint32, ctypes.c_bool]
        lib.es1869_rs_split.restype = None
        ul = ctypes.c_ulong
        lib.es1869_fill_plan.argtypes = [ctypes.POINTER(Fill), ul, ul, ul,
                                         ctypes.POINTER(ul)]
        lib.es1869_fill_plan.restype = ul
        lib.es1869_rs_limit.argtypes = [ul, ul, ul, ul]
        lib.es1869_rs_limit.restype = ul
        lib.es1869_rs_fill.argtypes = [ctypes.POINTER(RS),
                                       ctypes.POINTER(Stream), ul, ul]
        lib.es1869_rs_fill.restype = None
        lib.es1869_rs_seek.argtypes = [ctypes.POINTER(RS),
                                       ctypes.POINTER(Stream), ctypes.c_long]
        lib.es1869_rs_seek.restype = None
        lib.es1869_clock_init.argtypes = [ctypes.POINTER(Clock)]
        lib.es1869_clock_init.restype = None
        lib.es1869_clock_stretch.argtypes = [ctypes.POINTER(Clock)]
        lib.es1869_clock_stretch.restype = None
        lib.es1869_clock_irq.argtypes = [ctypes.POINTER(Clock), ctypes.c_uint32,
                                         ctypes.c_uint64]
        lib.es1869_clock_irq.restype = ctypes.c_bool
        lib.es1869_clock_system.argtypes = [ctypes.POINTER(Clock),
                                            ctypes.c_uint64, ctypes.c_uint64]
        lib.es1869_clock_system.restype = None
        lib.es1869_clock_offset.argtypes = [ctypes.POINTER(Clock)]
        lib.es1869_clock_offset.restype = ctypes.c_int32
        lib.es1869_clock_ppb.argtypes = [ctypes.c_int64]
        lib.es1869_clock_ppb.restype = ctypes.c_int64
        lib.es1869_clock_base.argtypes = [ctypes.c_uint32]
        lib.es1869_clock_base.restype = ctypes.c_int32
        lib.es1869_clock_q32.argtypes = [ctypes.c_int32]
        lib.es1869_clock_q32.restype = ctypes.c_int32
        lib.es1869_trim_ppb.argtypes = [ctypes.c_uint]
        lib.es1869_trim_ppb.restype = ctypes.c_int32
        lib.es1869_clock_tpf.argtypes = [ctypes.c_uint32, ctypes.c_uint32]
        lib.es1869_clock_tpf.restype = ctypes.c_uint32
        _LIB["lib"] = lib
    return _LIB["lib"]


def table(flat=False):
    if ("tab", flat) not in _LIB:
        tab = (ctypes.c_int16 * TABLE)()
        library().es1869_rs_table(tab, flat)
        _LIB["tab", flat] = tab
    return _LIB["tab", flat]


def ideal_row(i):
    """the filter at the place i of ROWS, by numpy"""
    t = np.arange(TAPS) - BEHIND - i / ROWS
    w = np.i0(BETA * np.sqrt(np.clip(1 - (t / (TAPS / 2)) ** 2, 0, 1)))
    return np.sinc(t) * w / np.i0(BETA)


def row_taps(rows, i):
    """a row of the table as the taps' values, with the 1/2 of the two taps
    around the place, which rs_half adds"""
    v = rows[i, :TAPS] / 2.0 ** np.array(SCALE)
    v[BEHIND] += 0.5
    v[BEHIND + 1] += 0.5
    return v


def shifted(j, chans):
    """the channels' copies a sample, two and three on, which MMX reads; what
    has to stay alive as long as the job"""
    keep = []
    for c, a in enumerate(chans):
        for k in range(3):
            o = np.concatenate([a[k + 1:], np.zeros(8, dtype=np.int16)])
            j.odd[c][k] = o.ctypes.data_as(s16p)
            keep.append(o)
    return keep


def run(x, y, frames, frac, step, stereo=True, mmx=False, dither=False,
        seed=12345, flat=False):
    """frames of the stream through one call; returns bytes, used, frac"""
    lib = library()
    room = frames + 8
    out = (ctypes.c_uint8 * (room * 8))()
    a = np.array(x, dtype=np.int16)
    b = np.array(y, dtype=np.int16)
    j = Job()
    j.tab = ctypes.cast(table(flat), s16p)
    j.centre = 1 if flat else 2
    j.ch[0] = a.ctypes.data_as(s16p)
    j.ch[1] = b.ctypes.data_as(s16p)
    alive = shifted(j, (a, b))
    j.dst = ctypes.cast(out, ctypes.POINTER(ctypes.c_uint8))
    j.frames = frames
    j.room = room
    j.frac = frac & 0xFFFFFFFF
    j.step = step
    j.stereo = stereo
    j.dither = dither
    j.seed = seed
    n = (lib.es1869_rs_run_mmx if mmx else lib.es1869_rs_run_c)(ctypes.byref(j))
    del alive
    return bytes(out[:n * (8 if stereo else 4)]), j.used, j.frac


def through(x, step, y=None, frac=0, mmx=False, dither=False, seed=12345,
            flat=False):
    """arrays through one long run, all but the last TAPS + 2 frames; the
    ring frames as arrays, the first at x[BEHIND] and frac past it"""
    lib = library()
    n = len(x) - TAPS - 2
    room = n + n // 256 + 64
    stereo = y is not None
    out = np.zeros(room * (8 if stereo else 4), dtype=np.uint8)
    a = np.asarray(x, dtype=np.int16)
    b = np.asarray(y if stereo else x, dtype=np.int16)
    j = Job()
    j.tab = ctypes.cast(table(flat), s16p)
    j.centre = 1 if flat else 2
    j.ch[0] = a.ctypes.data_as(s16p)
    j.ch[1] = b.ctypes.data_as(s16p)
    alive = shifted(j, (a, b)) if mmx else None
    j.dst = out.ctypes.data_as(ctypes.POINTER(ctypes.c_uint8))
    j.frames = n
    j.room = room
    j.frac = frac & 0xFFFFFFFF
    j.step = step
    j.stereo = stereo
    j.dither = dither
    j.seed = seed
    run_it = lib.es1869_rs_run_mmx if mmx and X86 else lib.es1869_rs_run_c
    m = run_it(ctypes.byref(j))
    del alive
    v = samples(out[:m * (8 if stereo else 4)].tobytes(), stereo)
    return v if stereo else v[0]


def places(n, step, frac=0):
    """where the n ring frames of through() fall, in frames of the stream"""
    return BEHIND + (frac / 2 ** 32) + np.arange(n) * (1 + step / 2 ** 32)


def samples(raw, stereo=True):
    """the ring's bytes back to the 16-bit samples"""
    d = np.frombuffer(raw, dtype=np.uint8)
    v = d[0::4].astype(np.int32) | (d[2::4].astype(np.int32) << 8)
    v = v.astype(np.uint16).astype(np.int16).astype(np.int64)
    return (v[0::2], v[1::2]) if stereo else (v, v)


def analyse(y, f):
    """noise and distortion, and the worst spur, against the tone, in dB"""
    n = len(y)
    t = np.arange(n)
    b = np.stack([np.cos(2 * np.pi * f * t / FS), np.sin(2 * np.pi * f * t / FS),
                  np.ones(n)], 1)
    co = np.linalg.lstsq(b, y.astype(float), rcond=None)[0]
    r = y - b @ co
    a = np.hypot(co[0], co[1])
    w = np.abs(np.fft.rfft(r * np.blackman(n))) / (n * 0.42) * 2
    return (20 * np.log10(np.sqrt(np.mean(r ** 2)) / (a / np.sqrt(2))),
            20 * np.log10(np.max(w[3:]) / a))


def q32(eps):
    """a step as a fraction, as the job takes it"""
    return int(round(eps * 2 ** 32))


def exact(eps):
    """the step nearest eps, as what it really takes"""
    return round(eps * 2 ** 32) / 2 ** 32


def tone_through(f, eps, level=-1.0, n=40000, mmx=False):
    """a tone through the filter in runs, as the driver feeds it"""
    amp = 32767 * 10 ** (level / 20)
    x = np.round(amp * np.sin(2 * np.pi * f * np.arange(n + 600) / FS))
    x = x.astype(np.int64)
    step = int(round(eps * 2 ** 32))
    out = []
    pos, frac = 0, 0x12345678
    while pos < n:
        raw, used, frac = run(x[pos:pos + 330], x[pos:pos + 330], 256, frac,
                              step, mmx=mmx)
        out.append(samples(raw)[0])
        pos += used
    return np.concatenate(out)


@unittest.skipUnless(np is not None and shutil.which("gcc"), "needs numpy, gcc")
class TableTest(unittest.TestCase):
    def rows(self):
        return np.ctypeslib.as_array(table()).reshape(ROWS, STRIDE)

    def test_row_is_the_ideal_filter(self):
        """every coefficient within what the rounding and the sum's fix leave"""
        rows = self.rows()
        for i in range(ROWS):
            err = np.abs(row_taps(rows, i) - ideal_row(i))
            self.assertLess(np.max(np.delete(err, [BEHIND, BEHIND + 1])),
                            2.4e-5, f"row {i}")
            self.assertLess(np.max(err), 5e-5, f"row {i}")

    def test_steps_reach_the_next_row(self):
        rows = self.rows()
        for i in range(ROWS - 1):
            np.testing.assert_array_equal(
                rows[i, TAPS:], 2 * (rows[i + 1, :TAPS] - rows[i, :TAPS]))

    def test_mirrored(self):
        """but for the unit on every other tap, which stays where it is"""
        rows = self.rows()
        up = np.zeros(TAPS, dtype=np.int64)
        up[::2] = 1
        for i in range(1, ROWS // 2):
            np.testing.assert_array_equal(
                rows[i, :TAPS] - up, (rows[ROWS - i, :TAPS] - up)[::-1])

    def test_gain_is_one(self):
        """with the half of the two taps around the place, and a unit more
        on every other tap, which the blend's rounding down takes off"""
        rows = self.rows()
        fine = 2 ** (24 - np.array(SCALE))
        down = int(np.sum(fine[::2]))
        for i in range(1, ROWS):
            total = int(np.sum(rows[i, :TAPS].astype(np.int64) * fine))
            self.assertLessEqual(abs(total - down), 1 << 7, f"row {i}")

    def test_first_row(self):
        """the frame itself, but for a 1/2 that takes 32767 of 32768"""
        rows = self.rows()
        want = np.zeros(TAPS)
        want[::2] = 1
        want[BEHIND] = 32767
        want[BEHIND + 1] = -32767
        np.testing.assert_array_equal(rows[0, :TAPS], want)

    def test_sums_stay_in_32_bits(self):
        """on the worst signal, lane by lane, each sum as the shifts leave it"""
        rows = self.rows()
        limit = 2 ** 31
        blends = [rows[i, :TAPS].astype(np.int64) +
                  (rows[i, TAPS:].astype(np.int64) * f >> 16)
                  for i in range(ROWS) for f in (0, 16384, 32767)]

        def lane(c, word, n):
            a = 4 * word + 2 * n
            return 32768 * int(np.abs(c[a:a + 2]).sum())

        for c in blends:
            self.assertLess(int(np.abs(c).max()), 32769)
            for n in (0, 1):
                acc = 0
                for words, shift in (((0, 16, 1, 15), 1), ((2, 14), 2),
                                     ((3, 13), 1), ((4, 12), 1), ((5, 11), 1),
                                     ((6, 10), 2)):
                    for w in words:
                        self.assertLess(lane(c, w, n), limit)
                        acc += lane(c, w, n)
                        self.assertLess(acc, limit)
                    acc >>= shift
                near = lane(c, 7, n) + lane(c, 9, n)
                self.assertLess(near, limit)
                acc += near >> 1
                self.assertLess(acc, limit)
                acc >>= 2
                centre = lane(c, 8, n)
                self.assertLess(centre, limit)
                acc += (centre >> 2) + 32768 * (1 << 13) + (1 << 14)
                self.assertLess(acc, limit)


@unittest.skipUnless(np is not None and shutil.which("gcc"), "needs numpy, gcc")
class RunTest(unittest.TestCase):
    def signal(self, seed, n=400, kind="noise"):
        rng = np.random.default_rng(seed)
        if kind == "noise":
            return rng.integers(-32768, 32768, n)
        if kind == "edges":
            return rng.choice([-32768, 32767], n)
        # the sign of the tails at the middle place: the largest output
        c = np.tile(np.sign(ideal_row(ROWS // 2)), n // TAPS + 1)[:n]
        return (c * 32767).astype(np.int64)

    @unittest.skipUnless(X86, "x86 only")
    def test_mmx_is_the_c_run(self):
        for kind in ("noise", "edges", "worst"):
            for seed in range(3):
                x = self.signal(seed, kind=kind)
                y = self.signal(seed + 100, kind=kind)
                for step in (0, 1 << 16, -(1 << 16), 1082411, -1082411,
                             1 << 24):
                    for stereo in (True, False):
                        for dither in (False, True):
                            frac = 0x9E3779B9 * (seed + 1)
                            a = run(x, y, 256, frac, step, stereo,
                                    False, dither)
                            b = run(x, y, 256, frac, step, stereo,
                                    True, dither)
                            self.assertEqual(a, b, (kind, seed, step, stereo,
                                                    dither))

    def test_position(self):
        """a run goes through the frames at the step's rate"""
        x = self.signal(1, kind="noise")
        eps = 1000e-6
        step = int(round(eps * 2 ** 32))
        raw, used, frac = run(x, x, 256, 0, step)
        out = len(raw) // 8
        self.assertEqual(out, 256 - round(256 * eps) if used == 256 else out)
        self.assertIn(used, (256, 257))
        total = out * (2 ** 32 + step)
        self.assertEqual(total, (used << 32) + frac)

    def test_negative_step_holds_a_frame_back(self):
        x = self.signal(2, kind="noise")
        step = -(1 << 24)
        raw, used, frac = run(x, x, 256, 0, step)
        out = len(raw) // 8
        self.assertEqual(out * ((1 << 32) + step), (used << 32) + frac)
        self.assertGreater(out, 256)

    def test_room_stops_a_run(self):
        x = self.signal(3, kind="noise")
        lib = library()
        out = (ctypes.c_uint8 * 800)()
        a = (ctypes.c_int16 * len(x))(*[int(v) for v in x])
        j = Job()
        j.tab = ctypes.cast(table(), s16p)
        j.centre = 2
        j.ch[0] = j.ch[1] = ctypes.cast(a, s16p)
        j.dst = ctypes.cast(out, ctypes.POINTER(ctypes.c_uint8))
        j.frames = 256
        j.room = 100
        j.stereo = True
        self.assertEqual(lib.es1869_rs_run_c(ctypes.byref(j)), 100)
        self.assertEqual(j.used, 100)

    def test_clips_and_never_wraps(self):
        """a signal that peaks above full scale clips at the rails"""
        x = self.signal(4, kind="worst")
        y = x
        raw, _, _ = run(x, y, 256, 1 << 31, 0)
        got = samples(raw)[0]
        self.assertEqual(got.max(), 32767)
        self.assertEqual(got.min(), -32768)
        rows = np.ctypeslib.as_array(table()).reshape(ROWS, STRIDE)
        c = row_taps(rows, ROWS // 2)
        for i in range(0, 200, 7):
            want = float(np.sum(c * x[i:i + TAPS]))
            self.assertLess(abs(got[i] - np.clip(want, -32768, 32767)), 40)

    def test_split(self):
        lib = library()
        src = (ctypes.c_int16 * 8)(1, 2, 3, 4, 5, 6, 7, 8)
        lo = (ctypes.c_int16 * 4)()
        hi = (ctypes.c_int16 * 4)()
        lib.es1869_rs_split(lo, hi, src, 4, True)
        self.assertEqual(list(lo), [1, 3, 5, 7])
        self.assertEqual(list(hi), [2, 4, 6, 8])
        lib.es1869_rs_split(lo, hi, src, 4, False)
        self.assertEqual(list(lo), [1, 2, 3, 4])

    def test_dither_keeps_its_size(self):
        """a triangle of 1 LSB each way, its mean none"""
        x = np.zeros(400, dtype=np.int64)
        got = samples(run(x, x, 256, 1 << 31, 0, dither=True)[0])[0]
        self.assertLessEqual(np.abs(got).max(), 1)


@unittest.skipUnless(np is not None and shutil.which("gcc"), "needs numpy, gcc")
class QualityTest(unittest.TestCase):
    """what docs/LINUX.md says of the filter. A step of 252 ppm is what 48 kHz
    takes, 30 ppm what the Armada's crystal takes against real time"""

    def test_to_20_khz(self):
        for eps in (252e-6, -252e-6, 30e-6):
            for f in (1000.3, 5000.7, 10000.9, 16000.1, 18000.3, 19000.1,
                      20000.3):
                y = tone_through(f, eps, n=1 << 18)
                thdn, spur = analyse(y, f * (1 + exact(eps)))
                self.assertLess(thdn, -88.0, f"{f} Hz at {eps}")
                self.assertLess(spur, -94.0, f"{f} Hz at {eps}")

    def test_small_offset(self):
        """long enough for the place to go round: 1 ppm takes 1000000 frames"""
        for eps in (1e-6, -1e-6, 10e-6):
            for f in (1000.3, 19000.1):
                y = tone_through(f, eps, n=1 << 21)
                thdn, spur = analyse(y, f * (1 + exact(eps)))
                self.assertLess(thdn, -88.0, f"{f} Hz at {eps}")
                self.assertLess(spur, -94.0, f"{f} Hz at {eps}")

    def test_mmx_measures_the_same(self):
        if not X86:
            self.skipTest("x86 only")
        y = tone_through(10000.9, 30e-6, mmx=True)
        thdn, _ = analyse(y, 10000.9 * (1 + exact(30e-6)))
        self.assertLess(thdn, -88.0)

    def test_flat_to_20_khz(self):
        """the level, by a fit, within 0.002 dB, and a little less at 21 kHz"""
        for f, least in ((20.1, -0.002), (100.1, -0.002), (1000.3, -0.002),
                         (10000.9, -0.002), (16000.1, -0.002),
                         (20000.3, -0.002), (21000.7, -0.6)):
            eps = 30e-6
            y = tone_through(f, eps, n=1 << 18)
            t = np.arange(len(y)) * (1 + exact(eps)) * 2 * np.pi * f / FS
            b = np.stack([np.cos(t), np.sin(t), np.ones(len(y))], 1)
            co = np.linalg.lstsq(b, y.astype(float), rcond=None)[0]
            db = 20 * np.log10(np.hypot(co[0], co[1]) /
                               (32767 * 10 ** (-1 / 20)))
            self.assertGreater(db, least, f"{f} Hz")
            self.assertLess(db, 0.002, f"{f} Hz")

    def test_every_place_on_time(self):
        """a tone against where the frames fall, not against a fit: the
        filter delays nothing at any frequency"""
        for f in (997.0, 10000.9, 20000.3):
            n = 1 << 18
            for step in (1 << 12, -(1 << 12), 1 << 16):
                frac = 0x4000_0000
                w = 2 * np.pi * f / FS
                x = np.rint(29000 * np.sin(w * np.arange(n)))
                y = through(x, step, frac=frac)
                at = places(len(y), step, frac)
                keep = at < n - TAPS - 2
                e = y[keep] - 29000 * np.sin(w * at[keep])
                self.assertLess(20 * np.log10(np.std(e) / 29000 * 2 ** .5),
                                -80.0, f"{f} Hz, step {step}")

    def test_quiet_tones(self):
        """the noise stays that of two roundings, however low the tone, and
        the tone's level holds"""
        eps = 30e-6
        for db, off in ((-20, 0.02), (-40, 0.02), (-60, 0.02), (-80, 0.25)):
            y = tone_through(997.0, eps, level=db, n=1 << 18)
            t = np.arange(len(y)) * (1 + exact(eps)) * 2 * np.pi * 997.0 / FS
            b = np.stack([np.cos(t), np.sin(t), np.ones(len(y))], 1)
            co = np.linalg.lstsq(b, y.astype(float), rcond=None)[0]
            noise = np.std(y - b @ co)
            self.assertLess(noise, 0.5, f"{db} dBFS")
            self.assertAlmostEqual(
                20 * np.log10(np.hypot(co[0], co[1]) / 32767), db, delta=off)

    def test_two_tones(self):
        """19 and 20 kHz, where a filter's edge would make 1 kHz, and 60 Hz
        under 7 kHz, each with what falls between them at -90 dB or below"""
        for f1, a1, f2, a2 in ((19000.3, 14000, 20000.3, 14000),
                               (60.1, 21000, 7000.7, 5250)):
            n, eps = 1 << 18, 30e-6
            at = np.arange(n)
            x = np.rint(a1 * np.sin(2 * np.pi * f1 / FS * at) +
                        a2 * np.sin(2 * np.pi * f2 / FS * at))
            y = through(x, int(eps * 2 ** 32))
            t = np.arange(len(y)) * (1 + exact(eps)) * 2 * np.pi / FS
            b = np.stack([np.cos(f1 * t), np.sin(f1 * t), np.cos(f2 * t),
                          np.sin(f2 * t), np.ones(len(y))], 1)
            r = y - b @ np.linalg.lstsq(b, y.astype(float), rcond=None)[0]
            w = np.abs(np.fft.rfft(r * np.blackman(len(r)))) / (len(r) * 0.42)
            top = 20 * np.log10(2 * np.max(w[3:]) / 32767)
            self.assertLess(top, -100.0, f"{f1}, {f2}")

    def test_many_tones_null(self):
        """500 tones falling at 3 dB an octave to 20 kHz, as music's spectrum
        does, against the same tones at the places: what the filter leaves
        is the noise of two 16-bit roundings, its input's and its output's,
        within 0.75 dB"""
        rng = np.random.default_rng(7)
        n, eps = 1 << 16, 30e-6
        step = int(eps * 2 ** 32)
        hz = np.exp(rng.uniform(np.log(30), np.log(20000), 500))
        amp = 1 / np.sqrt(hz)
        ph = rng.uniform(0, 2 * np.pi, 500)

        def sines(at):
            v = np.zeros(len(at))
            for i in range(0, 500, 50):
                v += (amp[i:i + 50] * np.sin(
                    np.outer(at, 2 * np.pi * hz[i:i + 50] / FS) +
                    ph[i:i + 50])).sum(1)
            return v

        pure = sines(np.arange(n, dtype=float))
        k = 32000 / np.abs(pure).max()
        y = through(np.rint(k * pure), step)
        at = places(len(y), step)
        keep = at < n - TAPS - 2
        ideal = k * sines(at[keep])
        err = np.std(y[keep] - ideal)
        floor = np.std(np.rint(ideal) - ideal)
        self.assertLess(20 * np.log10(err / floor), 0.75 + 3.01)
        self.assertLess(20 * np.log10(err / 32767), -97.0)

    def test_digital_silence_stays_silent(self):
        x = np.zeros(4000, dtype=np.int64)
        for step in (1 << 12, -(1 << 12), 1082411):
            self.assertEqual(np.count_nonzero(through(x, step)), 0)

    def test_direct_current(self):
        """to half a unit on average, and 3 at any place, at any level"""
        for level in (-32768, -30000, -1000, -1, 1, 1000, 30000, 32767):
            x = np.full(1 << 17, level)
            y = through(x, 1 << 16)
            self.assertLessEqual(np.abs(y - level).max(), 3, level)
            self.assertLessEqual(abs(np.mean(y) - level), 0.5, level)

    def test_sides_stay_apart(self):
        """nothing of a side reaches the other, and the blend is the same
        for both: a tone on both sides comes out the same on both"""
        n = 1 << 14
        x = np.rint(29000 * np.sin(2 * np.pi * 997.0 / FS * np.arange(n)))
        left, right = through(x, 1 << 16, y=np.zeros(n))
        self.assertEqual(np.count_nonzero(right), 0)
        self.assertGreater(np.abs(left).max(), 28000)
        left, right = through(x, 1 << 16, y=x)
        np.testing.assert_array_equal(left, right)

    def test_clips_cleanly_above_full_scale(self):
        """the frames 1, 1, -1, -1 at full scale are a tone at a quarter of
        the rate that peaks 3 dB over it, between them. It clips there, and
        doesn't wrap"""
        x = np.tile([32767, 32767, -32767, -32767], 1 << 12)
        step, frac = 1 << 18, 1 << 31
        y = through(x, step, frac=frac)
        at = places(len(y), step, frac)
        want = 32767 * 2 ** .5 * np.sin(2 * np.pi * (at + .5) / 4)
        keep = (at > 2 * TAPS) & (at < len(x) - 2 * TAPS)
        self.assertLess(np.abs(y - np.clip(want, -32768, 32767))[keep].max(),
                        64)
        self.assertEqual(y[keep].max(), 32767)
        self.assertEqual(y[keep].min(), -32768)

    def test_dither_costs_what_it_should(self):
        """a triangle of 1 LSB each way, high-passed: under 6 dB of noise"""
        n, eps = 1 << 17, 30e-6
        x = np.rint(29000 * np.sin(2 * np.pi * 997.0 / FS * np.arange(n)))
        step = int(eps * 2 ** 32)

        def noise(d):
            y = through(x, step, dither=d)
            at = places(len(y), step)
            keep = at < n - TAPS - 2
            return np.std(y[keep] - 29000 * np.sin(2 * np.pi * 997.0 / FS *
                                                   at[keep]))

        self.assertLess(20 * np.log10(noise(True) / noise(False)), 6.0)
        self.assertGreater(noise(True), noise(False))


@unittest.skipUnless(np is not None and shutil.which("gcc"), "needs numpy, gcc")
class TrebleTest(unittest.TestCase):
    """the table with the DAC's hold taken back: flat with the hold, at the
    same cost, and no less sound than the plain one for what it is"""

    def level(self, f, eps=30e-6, mmx=False):
        n = 1 << 17
        t = np.arange(n + 200)
        x = np.round(20000 * np.sin(2 * np.pi * f * t / FS))
        y = through(x, q32(eps), flat=True, mmx=mmx)
        y = y[1024:-1024]
        k = np.arange(len(y)) * (1 + exact(eps)) * 2 * np.pi * f / FS
        b = np.stack([np.cos(k), np.sin(k), np.ones(len(y))], 1)
        co = np.linalg.lstsq(b, y.astype(float), rcond=None)[0]
        r = y - b @ co
        db = 20 * np.log10(np.hypot(co[0], co[1]) / 20000)
        hold = 20 * np.log10(np.sinc(f / FS))
        return db + hold, 20 * np.log10(r.std() / (np.hypot(co[0], co[1]) / 2 ** 0.5))

    def test_flat_with_the_hold(self):
        for f in (100.1, 997.3, 5000.7, 10000.9, 15000.1, 18000.3, 20000.3):
            db, _ = self.level(f)
            self.assertLess(abs(db), 0.16, f"{f} Hz")

    def test_clean(self):
        for f in (997.3, 10000.9, 20000.3):
            _, thdn = self.level(f)
            self.assertLess(thdn, -84.0, f"{f} Hz")

    def test_gain_is_one(self):
        rows = np.array(table(True), dtype=np.int64).reshape(ROWS, STRIDE)
        scale = list(SCALE)
        for n in range(32, 36):
            scale[n] = 15
        scale = 2.0 ** np.array(scale)
        for i in range(0, ROWS, 7):
            total = float((rows[i, :TAPS] / scale).sum()) + 1
            self.assertAlmostEqual(total, 1.0, places=3)

    def test_fits_its_words(self):
        """no tap met the 16 bits, which would be a clamp and a click"""
        rows = np.array(table(True), dtype=np.int16).reshape(ROWS, STRIDE)
        self.assertLess(int(np.abs(rows.astype(int)[:, :TAPS]).max()), 32760)

    def test_mmx_is_the_c_run(self):
        if not X86:
            self.skipTest("x86 only")
        rng = np.random.default_rng(5)
        x = rng.integers(-20000, 20000, 256 + TAPS + 8)
        y = rng.integers(-20000, 20000, 256 + TAPS + 8)
        for eps in (0, 30e-6, -252e-6, 1e-3):
            for stereo in (True, False):
                c = run(x, y, 256, 12345, q32(eps), stereo, flat=True)
                m = run(x, y, 256, 12345, q32(eps), stereo, mmx=True, flat=True)
                self.assertEqual(c, m)


@unittest.skipUnless(np is not None and shutil.which("gcc"), "needs numpy, gcc")
class FillTest(unittest.TestCase):
    """the driver's fills, with a client that keeps its buffer full, as MPD
    and es1869test do: what the DMA reads, against where the line has it"""

    BUFFER = 8192
    PERIOD = 220
    AHEAD = 34

    def stream(self, eps, limit=True, frames=40000, exact=False, first=0,
               rewind=0, budget=256):
        """frames read off the line by more than 8 of 16 bits, or with exact
        those that differ at all from one long run through the filter. The
        stream's first frame is first, across the boundary if near it, and
        with rewind the client takes that many frames back at each tenth
        period, and writes them again. A fill takes budget frames at a
        time, and the plan goes on from the line, as the driver's does"""
        lib = library()
        b, period = self.BUFFER, self.PERIOD
        bound = b << 16
        src = (ctypes.c_int16 * (2 * b))()
        ring = (ctypes.c_uint8 * (8 * b))()
        rot = ctypes.c_ulong(0)
        rs = RS()
        rs.tab = ctypes.cast(table(), s16p)
        rs.step = int(round(eps * 2 ** 32))
        rs.pos = first % bound
        plan = Fill(buffer=b, boundary=bound, guard=4, lead=1764, slack=16,
                    filled=first % bound, seen=first % bound)
        st = Stream(src=ctypes.addressof(src), ring=ctypes.addressof(ring),
                    buffer=b, boundary=bound, rot=ctypes.pointer(rot),
                    stereo=True)
        tone = 20000 * np.sin(2 * np.pi * 997.0 / 48000 *
                              np.arange(frames + 3 * b))
        state = {"appl": first, "hw": first}

        def write(n):
            for i in range(state["appl"], state["appl"] + n):
                v = round(tone[i - first])
                src[2 * (i % b)] = src[2 * (i % b) + 1] = v
            state["appl"] += n
            st.appl = state["appl"] % bound

        def fill():
            while True:
                to = state["appl"] % bound
                if limit:
                    to = lib.es1869_rs_limit(to, state["hw"] % bound, b, bound)
                start = ctypes.c_ulong(0)
                n = lib.es1869_fill_plan(ctypes.byref(plan), to,
                                         state["hw"] % bound, budget,
                                         ctypes.byref(start))
                if not n:
                    return
                lib.es1869_rs_fill(ctypes.byref(rs), ctypes.byref(st),
                                   start.value, n)
                last = not plan.backlog
                if last:
                    lib.es1869_rs_seek(ctypes.byref(rs), ctypes.byref(st),
                                       -self.AHEAD - 1)
                plan.filled = rs.pos
                if last:
                    return

        write(b)
        fill()
        got = np.ctypeslib.as_array(ring)
        done, bad = 0, 0
        pos = np.arange(frames * 2) * (1 + eps)
        want = 20000 * np.sin(2 * np.pi * 997.0 / 48000 * pos)
        if exact:
            whole = np.concatenate([np.zeros(BEHIND), np.rint(tone)])
            want = through(whole, rs.step)
        while done + period < frames:
            at = np.arange(done, done + period) % b
            v = (got[8 * at].astype(np.int32) |
                 got[8 * at + 2].astype(np.int32) << 8)
            v = np.where(v >= 32768, v - 65536, v)
            off = np.abs(v - want[done:done + period])
            bad += int(np.sum(off > (0 if exact else 8)))
            done += period
            state["hw"] = first + int(done * (1 + eps))
            write(b - (state["appl"] - state["hw"]))
            fill()
            if rewind and done % (10 * period) == 0:
                # no further than what the ring has been made up to, less
                # what the DMA may reach before the next fill
                state["appl"] -= rewind
                st.appl = state["appl"] % bound
                fill()
                write(rewind)
                fill()
        self.assertEqual(plan.late, 0)
        return bad

    def test_every_frame_on_the_line(self):
        for eps in (252e-6, -252e-6, 1000e-6, -1000e-6, -2000e-6):
            self.assertEqual(self.stream(eps), 0, eps)

    def test_fills_make_what_one_run_makes(self):
        """chunks, seams and the frames made again leave no trace: the DMA
        reads, byte for byte, what one long run would have made"""
        for eps in (252e-6, -252e-6, 30e-6, 1000e-6):
            self.assertEqual(self.stream(eps, exact=True), 0, eps)

    def test_fills_in_short_pieces(self):
        """a fill that takes several pieces, as a client's period does in the
        driver, makes the same bytes, with the plan following the line"""
        for eps in (1000e-6, 252e-6, -1000e-6):
            self.assertEqual(
                self.stream(eps, exact=True, budget=16, frames=80000), 0, eps)

    def test_across_the_boundary(self):
        """the stream's frame count wraps, and the line goes on"""
        bound = self.BUFFER << 16
        for eps in (252e-6, -252e-6):
            self.assertEqual(self.stream(eps, exact=True, first=bound - 20000),
                             0, eps)

    def test_a_rewind_goes_back_on_the_line(self):
        """what the client writes again comes out as it did the first time"""
        for eps in (252e-6, -252e-6):
            self.assertEqual(self.stream(eps, exact=True, rewind=1024), 0, eps)

    def test_a_step_down_needs_the_limit(self):
        """the bug it mends: with no limit a full buffer's last frames land
        on what the DMA has yet to play"""
        self.assertGreater(self.stream(-1000e-6, limit=False), 100)
        self.assertEqual(self.stream(1000e-6, limit=False), 0)

    def test_limit(self):
        lib = library()
        b, bound = 16384, 16384 << 16
        most = b - 64 - b // 256
        self.assertEqual(lib.es1869_rs_limit(1000, 0, b, bound), 1000)
        self.assertEqual(lib.es1869_rs_limit(b, 0, b, bound), most)
        # across the boundary, and with the DMA past the frame
        self.assertEqual(lib.es1869_rs_limit(b - 100, bound - 100, b, bound),
                         (bound - 100 + most) % bound)
        self.assertEqual(lib.es1869_rs_limit(100, 200, b, bound), 100)


@unittest.skipUnless(np is not None and shutil.which("gcc"), "needs numpy, gcc")
class ClockTest(unittest.TestCase):
    """the fit on interrupts as the Armada's come: mostly near on time, late
    by 15 us rms, now and then by hundreds"""

    PERIOD = 1024 * 540  # master ticks: 1024 frames at 44100 Hz

    def feed(self, c, seconds, ppb, rng, start=1_000_000_000, sys_ppb=0,
             jitter=True):
        lib = library()
        rate = MASTER * (1 + ppb * 1e-9)
        t = self.PERIOD / rate
        n = int(seconds / t)
        for k in range(n):
            raw = start + (k + 1) * t * 1e9
            if jitter:
                late = abs(rng.normal(0, 15e3)) + 2e3
                if rng.random() < 0.01:
                    late += rng.random() * 500e3
                raw += late
            if lib.es1869_clock_irq(ctypes.byref(c), self.PERIOD, int(raw)):
                mono = raw + (raw - 1e9) * sys_ppb * 1e-9
                lib.es1869_clock_system(ctypes.byref(c), int(raw), int(mono))
        return start + n * t * 1e9

    def new(self):
        c = Clock()
        library().es1869_clock_init(ctypes.byref(c))
        return c

    def ppb(self, fit):
        return library().es1869_clock_ppb(fit.rate)

    def test_finds_the_rate(self):
        rng = np.random.default_rng(1)
        for ppb in (875, -30000, 45000):
            c = self.new()
            self.feed(c, 300, ppb, rng)
            self.assertLess(abs(self.ppb(c.chip) - ppb), 20, ppb)

    def test_soon_close(self):
        rng = np.random.default_rng(2)
        c = self.new()
        self.feed(c, 30, 12000, rng)
        self.assertLess(abs(self.ppb(c.chip) - 12000), 500)

    def test_the_steered_clock_counts_against_it(self):
        rng = np.random.default_rng(3)
        c = self.new()
        self.feed(c, 300, 875, rng, sys_ppb=-31000)
        self.assertLess(abs(self.ppb(c.system) + 31000), 50)
        off = library().es1869_clock_offset(ctypes.byref(c)) / 4.294967296
        self.assertLess(abs(off + 875 + 31000), 60)

    def test_a_slip_moves_the_line_not_the_rate(self):
        """a stall of the DMA: the rest of the stream runs later"""
        rng = np.random.default_rng(4)
        c = self.new()
        end = self.feed(c, 120, 5000, rng)
        before = self.ppb(c.chip)
        self.feed(c, 120, 5000, rng, start=int(end + 40e6))
        self.assertLess(abs(self.ppb(c.chip) - before), 100)
        self.assertLess(abs(self.ppb(c.chip) - 5000), 100)

    def test_a_new_stretch_keeps_the_rate(self):
        rng = np.random.default_rng(5)
        lib = library()
        c = self.new()
        end = self.feed(c, 120, 7000, rng)
        lib.es1869_clock_stretch(ctypes.byref(c))
        self.feed(c, 60, 7000, rng, start=int(end + 5e9))
        self.assertLess(abs(self.ppb(c.chip) - 7000), 60)

    def test_follows_a_change(self):
        """a crystal warming: its rate moves, and the fit with it"""
        rng = np.random.default_rng(6)
        c = self.new()
        end = self.feed(c, 300, 0, rng)
        self.feed(c, 900, 2000, rng, start=int(end))
        self.assertLess(abs(self.ppb(c.chip) - 2000), 50)

    def test_interrupts_with_no_time(self):
        """those of a later pass count, but aren't samples"""
        lib = library()
        c = self.new()
        lib.es1869_clock_irq(ctypes.byref(c), 100, 0)
        self.assertEqual(c.ticks, 100)
        self.assertFalse(c.have)

    def test_clock_families(self):
        lib = library()
        self.assertEqual(lib.es1869_clock_tpf(793800, 18), 540)
        self.assertEqual(lib.es1869_clock_tpf(768000, 16), 496)
        self.assertEqual(lib.es1869_clock_tpf(396900, 36), 2160)
        self.assertEqual(lib.es1869_clock_base(793800), 0)
        self.assertEqual(lib.es1869_clock_base(396900), 0)
        # 768 kHz is 768193.5 Hz: -251.9526 ppm of the source a ring frame
        base = lib.es1869_clock_base(768000) / 2 ** 32
        self.assertAlmostEqual(base * 1e6, -251.9526, places=3)
        self.assertEqual(lib.es1869_clock_q32(1000000000 // 1000),
                         round(2 ** 32 / 1000))

    def test_trim_items(self):
        """a2_trim's texts in settings.def are what it adds"""
        lib = library()
        with open(os.path.join(DRV, "settings.def"), encoding="utf-8") as f:
            text = f.read()
        row = text[text.index("ES1869_SET(a2_trim, "):]
        items = re.search(r'"(-500 ppm[^"]*)"', row).group(1).split("|")
        self.assertEqual(len(items), 25)
        for i, item in enumerate(items):
            want = 0 if item == "0" else round(float(item.split()[0]) * 1000)
            self.assertEqual(lib.es1869_trim_ppb(i), want, item)


if __name__ == "__main__":
    unittest.main()
