#!/usr/bin/env python3
# (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
#
# Licensed under GPL Version 3.0
"""snd-es1869's rate change and clock fit, on the host.

resample.c is built for this machine and called through ctypes. The tests
hold its integer table to a Kaiser-windowed sinc from numpy, its C and MMX
runs to the same bytes, on the worst signals for the 32-bit sums too, its
noise on tones to what docs/LINUX.md says of it, the tables with the DAC's
correction to the model of tools/es1869treble.py, and its clock fit, on
interrupts that come late, to the rate they were made with.

usage: python3 -m unittest tests.test_es1869rs   (needs gcc and numpy)
"""

import ctypes
import os
import platform
import re
import shutil
import subprocess
import sys
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
    _fields_ = [("tab", s16p), ("scale", ctypes.c_uint32),
                ("ch", s16p * 2), ("odd", (s16p * 3) * 2),
                ("dst", ctypes.POINTER(ctypes.c_uint8)),
                ("frames", ctypes.c_uint32), ("room", ctypes.c_uint32),
                ("used", ctypes.c_uint32), ("frac", ctypes.c_uint32),
                ("whole", ctypes.c_uint32), ("step", ctypes.c_uint32),
                ("over", ctypes.c_uint32), ("stereo", ctypes.c_bool),
                ("dither", ctypes.c_bool), ("seed", ctypes.c_uint32),
                ("last", ctypes.c_int32 * 2)]

    def pace(self, step, over=1):
        """what a ring frame takes, as the driver sets it"""
        adv = ((1 << 32) + step) // over
        self.whole = adv >> 32
        self.step = adv & 0xFFFFFFFF
        self.over = over


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
    _fields_ = [("tab", s16p), ("flat", s16p * 3), ("treble", ctypes.c_bool),
                ("on", ctypes.c_bool), ("mmx", ctypes.c_bool),
                ("clock", ctypes.c_bool), ("dither", ctypes.c_bool),
                ("lined", ctypes.c_bool),
                ("frac", ctypes.c_uint32), ("trim", ctypes.c_int32),
                ("base", ctypes.c_int32), ("step", ctypes.c_int32),
                ("over", ctypes.c_uint32), ("adv", ctypes.c_uint64),
                ("w", ctypes.c_ulong), ("pos", ctypes.c_ulong),
                ("since", ctypes.c_uint32), ("seed", ctypes.c_uint32),
                ("last", ctypes.c_int32 * 2),
                ("ch", (ctypes.c_int16 * (256 + TAPS + 2)) * 2),
                ("odd", ((ctypes.c_int16 * (256 + TAPS + 2)) * 3) * 2)]


class Stream(ctypes.Structure):
    _fields_ = [("src", ctypes.c_void_p), ("ring", ctypes.c_void_p),
                ("buffer", ctypes.c_ulong), ("boundary", ctypes.c_ulong),
                ("lap", ctypes.c_ulong), ("appl", ctypes.c_ulong),
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
        lib.es1869_rs_table.argtypes = [s16p, ctypes.c_uint]
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
        lib.es1869_rs_limit.argtypes = [ul, ul, ul, ul, ul]
        lib.es1869_rs_limit.restype = ul
        lib.es1869_rs_fill.argtypes = [ctypes.POINTER(RS),
                                       ctypes.POINTER(Stream), ul, ul]
        lib.es1869_rs_fill.restype = ctypes.c_uint32
        lib.es1869_rs_seek.argtypes = [ctypes.POINTER(RS),
                                       ctypes.POINTER(Stream), ctypes.c_long]
        lib.es1869_rs_seek.restype = None
        lib.es1869_rs_shift.argtypes = [ctypes.POINTER(RS), ctypes.POINTER(Job),
                                        ctypes.c_uint32, ctypes.c_bool]
        lib.es1869_rs_shift.restype = None
        lib.es1869_rs_pace.argtypes = [ctypes.POINTER(RS), ctypes.c_int32]
        lib.es1869_rs_pace.restype = None
        lib.es1869_rs_at.argtypes = [ctypes.POINTER(RS), ul, ul]
        lib.es1869_rs_at.restype = ul
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


def table(flat=0):
    """the plain filter, or with the DAC's correction at flat times the rate"""
    flat = int(flat)
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


def sum_scale(flat=0):
    """the bits of a frame's sum below a sample, as the driver's job has
    them: one less with the DAC's correction, whose worst signal comes to
    over 4 times full scale"""
    return 13 if flat else 14


def scales(flat=0):
    """each tap's scale in bits: a table with the DAC's correction has every
    word a bit less fine, but for the centre word, which has 15"""
    s = np.array(SCALE)
    if flat:
        s = s - 1
        s[32:36] = 15
    return s


def row_taps(rows, i, flat=0):
    """a row of the table as the taps' values, with the 1/2 of the two taps
    around the place, which rs_half adds"""
    v = rows[i, :TAPS] / 2.0 ** scales(flat)
    v[BEHIND] += 0.5
    v[BEHIND + 1] += 0.5
    return v


STAGES = (((0, 16, 1, 15), 1), ((2, 14), 2), ((3, 13), 1), ((4, 12), 1),
          ((5, 11), 1), ((6, 10), 2))


def worst_sums(rows, flat=0):
    """the largest of a run's sums on the worst signal over 2^31: lane by
    lane at each stage of the shifts, and the two lanes added with the
    rounding at the end, on the sum's scale of 14 bits, or 13 with the
    DAC's correction. Below 1 nothing wraps"""
    scale = sum_scale(flat)
    most = 0
    for i in range(ROWS):
        for f in (0, 16384, 32767):
            c = rows[i, :TAPS].astype(np.int64) + \
                (rows[i, TAPS:].astype(np.int64) * f >> 16)
            end = 2 << scale  # the rounding, with dither at its largest
            for n in (0, 1):
                def lane(w):
                    a = 4 * w + 2 * n
                    return 32768 * int(np.abs(c[a:a + 2]).sum())

                acc = 0
                for words, shift in STAGES:
                    for w in words:
                        most = max(most, lane(w), acc + lane(w))
                        acc += lane(w)
                    acc >>= shift
                near = lane(7) + lane(9)
                acc += near >> 1
                most = max(most, near, acc)
                acc >>= 2
                most = max(most, lane(8))
                acc += (lane(8) >> 2) + 32768 * (1 << (scale - 1))
                most = max(most, acc)
                end += acc
            most = max(most, end)
    return most / 2 ** 31


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
    j.scale = sum_scale(flat)
    j.ch[0] = a.ctypes.data_as(s16p)
    j.ch[1] = b.ctypes.data_as(s16p)
    alive = shifted(j, (a, b))
    j.dst = ctypes.cast(out, ctypes.POINTER(ctypes.c_uint8))
    j.frames = frames
    j.room = room
    j.frac = frac & 0xFFFFFFFF
    j.pace(step)
    j.stereo = stereo
    j.dither = dither
    j.seed = seed
    n = (lib.es1869_rs_run_mmx if mmx else lib.es1869_rs_run_c)(ctypes.byref(j))
    del alive
    return bytes(out[:n * (8 if stereo else 4)]), j.used, j.frac


def through(x, step, y=None, frac=0, mmx=False, dither=False, seed=12345,
            flat=False, over=1):
    """arrays through one long run, all but the last TAPS + 2 frames; the
    ring frames as arrays, the first at x[BEHIND] and frac past it"""
    lib = library()
    n = len(x) - TAPS - 2
    room = over * n + n // 256 + 64
    stereo = y is not None
    out = np.zeros(room * (8 if stereo else 4), dtype=np.uint8)
    a = np.asarray(x, dtype=np.int16)
    b = np.asarray(y if stereo else x, dtype=np.int16)
    j = Job()
    j.tab = ctypes.cast(table(flat), s16p)
    j.scale = sum_scale(flat)
    j.ch[0] = a.ctypes.data_as(s16p)
    j.ch[1] = b.ctypes.data_as(s16p)
    alive = shifted(j, (a, b)) if mmx else None
    j.dst = out.ctypes.data_as(ctypes.POINTER(ctypes.c_uint8))
    j.frames = n
    j.room = room
    j.frac = frac & 0xFFFFFFFF
    j.pace(step, over)
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
        self.assertLess(int(np.abs(rows[:, :TAPS].astype(int)).max()), 32769)
        self.assertLess(worst_sums(rows), 1.0)


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
        j.scale = 14
        j.pace(0)
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
class ShiftTest(unittest.TestCase):
    """what follows the window in the driver's struct: a full chunk's copies
    wrote 4 bytes past it, over the field after, the cause of a kernel panic"""

    def tearDown(self):
        # the copies use MMX, and a run of no frames ends with its EMMS, which
        # the driver's caller also leaves to the run
        if X86:
            j = Job()
            j.over = 1
            library().es1869_rs_run_mmx(ctypes.byref(j))

    def test_stays_in_the_window(self):
        class Guarded(ctypes.Structure):
            _fields_ = [("rs", RS), ("guard", ctypes.c_uint32 * 16)]

        g = Guarded()
        for i in range(16):
            g.guard[i] = 0xA5A5A5A5
        for c in range(2):
            for i in range(256 + TAPS + 2):
                g.rs.ch[c][i] = 1000 + i
        j = Job()
        for n in (1, 4, 255, 256):
            library().es1869_rs_shift(ctypes.byref(g.rs), ctypes.byref(j), n,
                                      True)
            self.assertEqual(list(g.guard), [0xA5A5A5A5] * 16, n)

    def test_every_tap_of_a_full_chunk_is_there(self):
        """the last frame's taps lie in the copy it reads"""
        rs = RS()
        for i in range(256 + TAPS + 2):
            rs.ch[0][i] = rs.ch[1][i] = i + 1
        j = Job()
        library().es1869_rs_shift(ctypes.byref(rs), ctypes.byref(j), 256, True)
        for k in (1, 2, 3):
            for t in range(TAPS):
                self.assertEqual(rs.odd[1][k - 1][252 + t], 252 + t + k + 1)


@unittest.skipUnless(np is not None and shutil.which("gcc"), "needs numpy, gcc")
class OverTest(unittest.TestCase):
    """the ring at two and three times the stream: tones where they belong,
    nothing where their images would be, and the same bytes from MMX"""

    def tone(self, f, over, eps=0.0, mmx=False, n=1 << 16):
        t = np.arange(n + 200)
        x = np.round(20000 * np.sin(2 * np.pi * f * t / FS))
        y = through(x, q32(eps), mmx=mmx, over=over)
        return y[1024:-1024].astype(float)

    def test_tones_and_images(self):
        for over in (2, 3):
            for f in (997.3, 10000.9, 19000.1, 20000.3):
                y = self.tone(f, over)
                n = len(y)
                t = np.arange(n) / (FS * over) * (2 * np.pi * f)
                b = np.stack([np.cos(t), np.sin(t), np.ones(n)], 1)
                co = np.linalg.lstsq(b, y, rcond=None)[0]
                self.assertLess(abs(20 * np.log10(np.hypot(co[0], co[1]) /
                                                  20000)), 0.01, (over, f))
                # what's left, up to the ring's Nyquist, images included
                r = y - b @ co
                w = np.blackman(n) ** 2
                a = np.abs(np.fft.rfft(r * w)) / (w.sum() / 2)
                self.assertLess(20 * np.log10(a.max() / 20000), -86.0,
                                (over, f))

    def test_step_stays_true(self):
        """a change of rate on top: the tone moves by it, no more"""
        y = self.tone(10000.9, 2, eps=30e-6)
        f = 10000.9 * (1 + 30e-6) * 2 / 2
        t = np.arange(len(y)) / (2 * FS) * (2 * np.pi * f)
        b = np.stack([np.cos(t), np.sin(t), np.ones(len(y))], 1)
        co = np.linalg.lstsq(b, y, rcond=None)[0]
        r = y - b @ co
        self.assertLess(20 * np.log10(r.std() / 14142), -80.0)

    def test_mmx_is_the_c_run(self):
        if not X86:
            self.skipTest("x86 only")
        rng = np.random.default_rng(6)
        x = np.pad(rng.integers(-20000, 20000, 2000), (40, 40))
        for over in (2, 3):
            for eps in (0.0, 30e-6, -252e-6):
                c = through(x, q32(eps), over=over)
                m = through(x, q32(eps), over=over, mmx=True)
                self.assertTrue(np.array_equal(c, m), (over, eps))

    def test_cost_of_a_held_phase(self):
        """the outer words of each place outlast a frame at a small step"""
        y = self.tone(997.3, 3, eps=30e-6, mmx=X86)
        self.assertGreater(len(y), 1000)


def treble():
    """tools/es1869treble.py, which holds the DAC's model and fits treble.h"""
    if "treble" not in _LIB:
        sys.path.insert(0, os.path.join(ROOT, "tools"))
        import es1869treble
        _LIB["treble"] = es1869treble
    return _LIB["treble"]


def dac_db(f, over=1):
    """the DAC's gain at f, in dB, with the ring at over times the stream"""
    return float(20 * np.log10(treble().dac(2 * np.pi * f / FS, over)))


def kaiser(t):
    """the window at t frames from the place"""
    u = np.clip(1 - (t / (TAPS / 2)) ** 2, 0, 1)
    return np.where(np.abs(t) < TAPS / 2, np.i0(BETA * np.sqrt(u)), 0) / \
        np.i0(BETA)


def flat_row(i, side):
    """the filter with the DAC's correction at the place i, by numpy: the
    correction's taps on the sinc, over the window at their frames, then
    the window"""
    k = np.arange(1, len(side) + 1)
    t = np.arange(TAPS) - BEHIND - i / ROWS
    s = (1 - 2 * np.sum(side)) * np.sinc(t)
    for kk, c in zip(k, side / kaiser(k.astype(float))):
        s = s + c * (np.sinc(t - kk) + np.sinc(t + kk))
    return s * kaiser(t)


def header_taps():
    """treble.h's side taps for each rate, as fractions"""
    with open(os.path.join(DRV, "treble.h"), encoding="utf-8") as f:
        text = f.read()
    rows = re.findall(r"\{([-\d,\s]+)\}", text.split("= {", 1)[1])
    return [np.array([int(v) for v in r.split(",")]) / 2.0 ** 30
            for r in rows]


@unittest.skipUnless(np is not None and shutil.which("gcc"), "needs numpy, gcc")
class TrebleTest(unittest.TestCase):
    """the tables with the DAC's response taken back, at 1x, 2x and 3x:
    flat with the DAC up to half the stream's rate where every frame keeps
    its place, and up to the filter's own edge where the filter moves them,
    at the same cost a frame, as clean as the plain table, and on the worst
    signal clipped, not wrapped"""

    AMP = 16000  # 4.95 dB under full scale: the most the correction adds

    def level(self, f, eps=30e-6, over=1, mmx=False, flat=None):
        """a tone's level through the table and the DAC in dB, its THD+N,
        and the worst of the rest up to the ring's Nyquist, images included"""
        n = 1 << 17
        x = np.round(self.AMP * np.sin(2 * np.pi * f * np.arange(n + 200) /
                                       FS))
        flat = over if flat is None else flat
        y = through(x, q32(eps), flat=flat, mmx=mmx, over=over)
        y = y[1024:-1024].astype(float)
        k = np.arange(len(y)) * (1 + exact(eps)) / over * 2 * np.pi * f / FS
        b = np.stack([np.cos(k), np.sin(k), np.ones(len(y))], 1)
        co = np.linalg.lstsq(b, y, rcond=None)[0]
        r = y - b @ co
        a = np.hypot(co[0], co[1])
        w = np.blackman(len(r)) ** 2
        top = np.abs(np.fft.rfft(r * w)).max() / (w.sum() / 2)
        db = 20 * np.log10(a / self.AMP) + (dac_db(f, over) if flat else 0)
        return (db, 20 * np.log10(r.std() / (a / 2 ** 0.5)),
                20 * np.log10(top / a))

    def rows(self, flat):
        return np.ctypeslib.as_array(table(flat)).reshape(ROWS, STRIDE)

    def test_header_is_current(self):
        r = subprocess.run([sys.executable, os.path.join(
            ROOT, "tools", "es1869treble.py"), "--check"],
            capture_output=True, text=True)
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)

    def test_flat_to_half_the_rate(self):
        """at 1x with no change of rate every frame stays on its place, the
        first row's: with the DAC within 0.06 dB up to 22.05 kHz, where 7
        taps a side left 0.75 dB"""
        g = row_taps(self.rows(1), 0, 1)
        f = np.linspace(0, FS / 2, 2206)
        w = 2 * np.pi * f / FS
        h = np.abs(np.exp(-1j * np.outer(w, np.arange(TAPS) - BEHIND)) @ g)
        db = 20 * np.log10(h) + 20 * np.log10(treble().dac(w, 1))
        self.assertLess(np.abs(db).max(), 0.06)
        for hz in (997.3, 15000.1, 21000.7, 21999.1):
            got, thdn, _ = self.level(hz, eps=0.0)
            self.assertLess(abs(got), 0.06, f"{hz} Hz")
            self.assertLess(thdn, -86.0, f"{hz} Hz")

    def test_with_a_step(self):
        """the filter moves the frames: flat with the DAC within 0.06 dB up
        to 20 kHz, and above that the plain filter's own edge"""
        for f in (100.1, 997.3, 5000.7, 10000.9, 15000.1, 18000.3, 20000.3):
            db, _, _ = self.level(f)
            self.assertLess(abs(db), 0.06, f"{f} Hz")
        for f in (21000.7, 21500.3):
            db, _, _ = self.level(f)
            edge, _, _ = self.level(f, flat=0)
            self.assertLess(edge, -0.3, f"{f} Hz")
            self.assertAlmostEqual(db, edge, delta=0.05, msg=f"{f} Hz")

    def test_oversampled(self):
        """against the DAC at the ring's rate: flat within 0.02 dB up to 20
        kHz, the plain filter's edge above, and images as far down as the
        fixed places' rounding allows"""
        for over in (2, 3):
            for f in (997.3, 10000.9, 15000.1, 20000.3):
                db, _, top = self.level(f, eps=0.0, over=over)
                self.assertLess(abs(db), 0.02, (over, f))
                self.assertLess(top, -84.0, (over, f))
            db, _, _ = self.level(21000.7, eps=0.0, over=over)
            edge, _, _ = self.level(21000.7, eps=0.0, over=over, flat=0)
            self.assertAlmostEqual(db, edge, delta=0.05, msg=over)

    def test_clean(self):
        """at a step, as the Clock mode plays: THD+N within a few dB of the
        plain table's, and no spur over -88 dB below the tone"""
        for over, most in ((1, -85.0), (2, -83.0), (3, -83.0)):
            for f in (997.3, 10000.9, 20000.3):
                _, thdn, top = self.level(f, over=over)
                self.assertLess(thdn, most, (over, f))
                self.assertLess(top, -88.0, (over, f))

    def test_peak_gain(self):
        """what the correction adds at most, which a loud top end loses as
        headroom: at half the rate, as treble.h has it"""
        fit = treble().taps()
        f = np.linspace(0, FS / 2, 2206)
        w = 2 * np.pi * f / FS
        for flat in (1, 2, 3):
            g = row_taps(self.rows(flat), 0, flat)
            db = 20 * np.log10(np.abs(
                np.exp(-1j * np.outer(w, np.arange(TAPS) - BEHIND)) @ g))
            self.assertAlmostEqual(db.max(), fit[flat - 1][2], delta=0.01)
            self.assertGreater(f[np.argmax(db)], 0.49 * FS, flat)
        self.assertLess(fit[0][2], 5.0)

    def test_header_fits_the_model(self):
        """treble.h's taps with the DAC as tools/es1869treble.py has it"""
        w = np.linspace(0, np.pi, 4097)
        for over, side in zip((1, 2, 3), header_taps()):
            db = 20 * np.log10(treble().response(side, w) *
                               treble().dac(w, over))
            self.assertLess(np.abs(db).max(), (0.06, 0.02, 0.01)[over - 1])

    def test_rows_are_the_windowed_correction(self):
        """every coefficient within what the rounding and the sum's fix
        leave, against numpy's"""
        for flat, side in zip((1, 2, 3), header_taps()):
            rows = self.rows(flat)
            for i in range(0, ROWS, 3):
                err = np.abs(row_taps(rows, i, flat) - flat_row(i, side))
                self.assertLess(np.max(np.delete(err, [BEHIND, BEHIND + 1])),
                                5e-5, (flat, i))
                self.assertLess(np.max(err), 1.1e-4, (flat, i))

    def test_gain_is_one(self):
        for flat in (1, 2, 3):
            rows = np.array(table(flat), dtype=np.int64).reshape(ROWS, STRIDE)
            for i in range(0, ROWS, 7):
                total = float((rows[i, :TAPS] / 2.0 ** scales(flat)).sum())
                self.assertAlmostEqual(total + 1, 1.0, places=3)

    def test_fits_its_words(self):
        """no tap met the 16 bits, which would be a clamp and a click"""
        for flat in (1, 2, 3):
            rows = self.rows(flat).astype(int)
            self.assertLess(int(np.abs(rows[:, :TAPS]).max()), 32760)

    def test_sums_stay_in_32_bits(self):
        """the correction lifts the tails by its gain near half the rate, so
        that every word but the centre one is a bit less fine than the plain
        table's, and the sum ends on 13 bits"""
        for flat in (1, 2, 3):
            self.assertLess(worst_sums(self.rows(flat), flat), 1.0, flat)

    def test_worst_signal_clips(self):
        """the signs of a row's taps at full scale come to over 4 times full
        scale at 1x, which a sum on 14 bits wrapped: it clips, in C and MMX"""
        most = 0
        for flat in (1, 2, 3):
            rows = self.rows(flat)
            for place in range(0, ROWS, 4):
                c = rows[place, :TAPS].astype(np.int64)
                x = np.zeros(300 + TAPS + 8, dtype=np.int64)
                x[:TAPS] = np.where(c >= 0, 32767, -32768)
                want = float(np.sum(row_taps(rows, place, flat) * x[:TAPS]))
                most = max(most, want)
                for mmx in (False, True) if X86 else (False,):
                    raw, _, _ = run(x, x, 4, place << 25, 0, stereo=False,
                                    mmx=mmx, flat=flat)
                    got = samples(raw, False)[0][0]
                    self.assertLess(abs(np.clip(want, -32768, 32767) - got),
                                    64, (flat, place, mmx))
        self.assertGreater(most, 4 * 32768)

    def test_mmx_is_the_c_run(self):
        if not X86:
            self.skipTest("x86 only")
        rng = np.random.default_rng(5)
        x = rng.integers(-20000, 20000, 256 + TAPS + 8)
        y = rng.integers(-20000, 20000, 256 + TAPS + 8)
        for eps in (0, 30e-6, -252e-6, 1e-3):
            for stereo in (True, False):
                for dither in (False, True):
                    c = run(x, y, 256, 12345, q32(eps), stereo,
                            dither=dither, flat=1)
                    m = run(x, y, 256, 12345, q32(eps), stereo, mmx=True,
                            dither=dither, flat=1)
                    self.assertEqual(c, m, (eps, stereo, dither))
        z = np.pad(rng.integers(-32768, 32768, 2000), (40, 40))
        for over in (2, 3):
            for eps in (0.0, 30e-6, -252e-6):
                c = through(z, q32(eps), flat=over, over=over)
                m = through(z, q32(eps), flat=over, over=over, mmx=True)
                self.assertTrue(np.array_equal(c, m), (over, eps))


@unittest.skipUnless(np is not None and shutil.which("gcc"), "needs numpy, gcc")
class FillTest(unittest.TestCase):
    """the driver's fills, with a client that keeps its buffer full, as MPD
    and es1869test do: what the DMA reads, against where the line has it"""

    BUFFER = 8192
    PERIOD = 220
    AHEAD = 34

    GUARD = 4096        # bytes past the ring that have to stay as they were

    def stream(self, eps, limit=True, frames=40000, exact=False, first=0,
               rewind=0, budget=256, over=1, lap=None, room=None):
        """frames read off the line by more than 8 of 16 bits, or with exact
        those that differ at all from one long run through the filter. The
        stream's first frame is first, across the boundary if near it, and
        with rewind the client takes that many frames back at each tenth
        period, and writes them again. A fill takes budget frames at a
        time, and the plan goes on from the line, as the driver's does.
        lap is the ring's frames, over times the buffer's unless the ring
        hasn't the room, and room what the fill's limit takes it to hold.
        A fill writes the frames from the one it reports it began at to
        where the line went, those and no others, which the zero step's
        remap takes;
        self.seeks counts the fills that began off the line"""
        lib = library()
        b, period = self.BUFFER, self.PERIOD
        lap = lap or b * over
        room = room or lap // over
        bound = b << 16
        src = (ctypes.c_int16 * (2 * b))()
        ring = (ctypes.c_uint8 * (8 * lap + self.GUARD))()
        ctypes.memset(ctypes.addressof(ring) + 8 * lap, 0xA5, self.GUARD)
        rot = ctypes.c_ulong(0)
        rs = RS()
        rs.tab = ctypes.cast(table(), s16p)
        rs.over = over
        lib.es1869_rs_pace(ctypes.byref(rs), int(round(eps * 2 ** 32)))
        rs.pos = first % bound
        plan = Fill(buffer=b, boundary=bound, guard=4, lead=1764, slack=16,
                    filled=first % bound, seen=first % bound)
        st = Stream(src=ctypes.addressof(src), ring=ctypes.addressof(ring),
                    buffer=b, boundary=bound, lap=lap,
                    rot=ctypes.pointer(rot), stereo=True)
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
                    to = lib.es1869_rs_limit(to, state["hw"] % bound, room, b,
                                             bound)
                start = ctypes.c_ulong(0)
                n = lib.es1869_fill_plan(ctypes.byref(plan), to,
                                         state["hw"] % bound, budget,
                                         ctypes.byref(start))
                if not n:
                    return
                # the filter reads only the stream, so a ring of 0xA5 shows
                # what the fill wrote, and the rest is put back
                body = np.ctypeslib.as_array(ring)[:8 * lap]
                kept = body.copy()
                body[:] = 0xA5
                line = rs.w
                w0 = lib.es1869_rs_fill(ctypes.byref(rs), ctypes.byref(st),
                                        start.value, n)
                wrote = (body != 0xA5).reshape(lap, 8).any(axis=1)
                body[np.repeat(~wrote, 8)] = kept[np.repeat(~wrote, 8)]
                span = (rs.w - w0) % lap
                self.assertEqual(span, int(wrote.sum()), (w0, rs.w))
                self.assertTrue(np.all((np.flatnonzero(wrote) - w0) % lap <
                                       span), (w0, rs.w))
                self.seeks += w0 != line
                last = not plan.backlog
                if last:
                    lib.es1869_rs_seek(ctypes.byref(rs), ctypes.byref(st),
                                       (-self.AHEAD - 1) * over)
                plan.filled = rs.pos
                if last:
                    return

        self.seeks = 0
        write(b)
        fill()
        got = np.ctypeslib.as_array(ring)
        done, bad = 0, 0
        pos = np.arange(frames * 2 * over) * (1 + eps) / over
        want = 20000 * np.sin(2 * np.pi * 997.0 / 48000 * pos)
        if exact:
            whole = np.concatenate([np.zeros(BEHIND), np.rint(tone)])
            want = through(whole, rs.step, over=over)
        period *= over
        frames *= over
        while done + period < frames:
            at = np.arange(done, done + period) % lap
            v = (got[8 * at].astype(np.int32) |
                 got[8 * at + 2].astype(np.int32) << 8)
            v = np.where(v >= 32768, v - 65536, v)
            off = np.abs(v - want[done:done + period])
            # between frames, a stream's sudden start rings for the filter's
            # reach, which isn't on the line of a tone that was always there
            if over > 1 and not exact and done < TAPS * over:
                off[:] = 0
            bad += int(np.sum(off > (0 if exact else 8)))
            done += period
            state["hw"] = first + int(done * (1 + eps) / over)
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
        self.assertTrue(np.all(got[8 * lap:] == 0xA5), "past the ring")
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
        """what the client writes again comes out as it did the first time,
        from a fill that moves the line back first"""
        for eps in (252e-6, -252e-6):
            self.assertEqual(self.stream(eps, exact=True, rewind=1024), 0, eps)
            self.assertGreater(self.seeks, 0, eps)

    def test_a_step_down_needs_the_limit(self):
        """the bug it mends: with no limit a full buffer's last frames land
        on what the DMA has yet to play"""
        self.assertGreater(self.stream(-1000e-6, limit=False), 100)
        self.assertEqual(self.stream(1000e-6, limit=False), 0)

    def test_ring_at_twice_and_three_times_the_stream(self):
        for over in (2, 3):
            for eps in (0.0, 30e-6, -252e-6):
                self.assertEqual(self.stream(eps, over=over), 0, (over, eps))
                self.assertEqual(self.stream(eps, exact=True, over=over), 0,
                                 (over, eps))

    def test_a_lap_shorter_than_the_buffer(self):
        """a ring without the room for over laps of the buffer: a lap as long
        as the buffer, so that it holds a half or a third of it. The DMA still
        reads what one long run would have made, and nothing lands past the
        ring"""
        for over in (2, 3):
            for eps in (0.0, 30e-6, -252e-6, 1000e-6, -1000e-6):
                self.assertEqual(
                    self.stream(eps, exact=True, over=over, lap=self.BUFFER),
                    0, (over, eps))

    def test_a_short_lap_in_short_pieces(self):
        for over in (2, 3):
            self.assertEqual(
                self.stream(1000e-6, exact=True, budget=16, frames=60000,
                            over=over, lap=self.BUFFER), 0, over)

    def test_a_short_lap_across_the_boundary(self):
        bound = self.BUFFER << 16
        for over in (2, 3):
            self.assertEqual(
                self.stream(-252e-6, exact=True, first=bound - 20000, over=over,
                            lap=self.BUFFER), 0, over)

    def test_a_short_lap_needs_the_limit_of_its_room(self):
        """the fill held to the buffer's frames runs over what the DMA has yet
        to play, a lap on"""
        for over in (2, 3):
            self.assertGreater(
                self.stream(0.0, exact=True, over=over, lap=self.BUFFER,
                            room=self.BUFFER), 100, over)

    def test_the_line_reads_back(self):
        """the frame of the stream at a ring frame, as the pointer asks"""
        lib = library()
        for over, eps in ((1, 0.0), (1, 1e-3), (1, -1e-3), (2, 0.0),
                          (2, 30e-6), (3, -252e-6)):
            rs = RS()
            rs.over = over
            lib.es1869_rs_pace(ctypes.byref(rs), int(round(eps * 2 ** 32)))
            rs.pos = 100000
            rs.frac = 0x40000000
            b = 16384
            for a in (0, 1, 7, 1000, 12000):
                got = lib.es1869_rs_at(ctypes.byref(rs), a, b)
                want = int(np.floor(100000.25 - a * (1 + eps) / over)) % b
                self.assertEqual(got, want, (over, eps, a))

    def test_limit(self):
        lib = library()
        b, bound = 16384, 16384 << 16
        most = b - 64 - b // 256
        self.assertEqual(lib.es1869_rs_limit(1000, 0, b, b, bound), 1000)
        self.assertEqual(lib.es1869_rs_limit(b, 0, b, b, bound), most)
        # across the boundary, and with the DMA past the frame
        self.assertEqual(lib.es1869_rs_limit(b - 100, bound - 100, b, b, bound),
                         (bound - 100 + most) % bound)
        self.assertEqual(lib.es1869_rs_limit(100, 200, b, b, bound), 100)
        # a ring that holds a third of the buffer, with the buffer full
        third = b // 3
        most = third - 64 - third // 256
        self.assertEqual(lib.es1869_rs_limit(b, 0, third, b, bound), most)
        self.assertEqual(lib.es1869_rs_limit(1000, 0, third, b, bound), 1000)


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
