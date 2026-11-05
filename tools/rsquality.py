#!/usr/bin/env python3
# (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
#
# Licensed under GPL Version 3.0
"""What snd-es1869's rate correction does to a signal: resample.c, built for
this machine, on tones up the band, levels, steps, two tones, many tones and
a sweep, with a perfect filter as the reference, by numpy's FFT.

The measures are those of tests/test_es1869rs.py, which holds the filter to
them, on finer grids. A second resample.c (--against DIR, that directory
holding its resample.c, resample.h and hw_compat.h) goes through the same
grids, for a comparison.

usage: tools/rsquality.py [-o report.json] [--against DIR]
"""

import argparse
import ctypes
import json
import os
import subprocess
import sys
import tempfile

import numpy as np

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, ROOT)

FS = 44100.0
BH7 = (0.27105140069342, -0.43329793923448, 0.21812299954311,
       -0.06592544638803, 0.01081174209837, -0.00077658482522,
       0.00001388721735)
BAND = (20.0, 20000.0)


class Job(ctypes.Structure):
    s16p = ctypes.POINTER(ctypes.c_int16)
    _fields_ = [("tab", s16p), ("centre", ctypes.c_uint32),
                ("ch", s16p * 2), ("odd", (s16p * 3) * 2),
                ("dst", ctypes.POINTER(ctypes.c_uint8)),
                ("frames", ctypes.c_uint32), ("room", ctypes.c_uint32),
                ("used", ctypes.c_uint32), ("frac", ctypes.c_uint32),
                ("step", ctypes.c_int32), ("stereo", ctypes.c_bool),
                ("dither", ctypes.c_bool), ("seed", ctypes.c_uint32),
                ("last", ctypes.c_int32 * 2)]


class Filter:
    """a resample.c, built here"""

    def __init__(self, src):
        self.tmp = tempfile.TemporaryDirectory()
        so = os.path.join(self.tmp.name, "rs.so")
        subprocess.run(["gcc", "-std=gnu11", "-O2", "-fPIC", "-shared",
                        "-I" + src, "-o", so,
                        os.path.join(src, "resample.c")],
                       check=True, capture_output=True)
        self.lib = ctypes.CDLL(so)
        with open(os.path.join(src, "resample.h"), encoding="utf-8") as f:
            text = f.read()
        self.taps = int(text.split("#define ES_RS_TAPS")[1].split()[0])
        self.behind = int(text.split("#define ES_RS_BEHIND")[1].split()[0])
        rows = int(text.split("#define ES_RS_ROWS")[1].split()[0])
        self.tab = (ctypes.c_int16 * (rows * 2 * self.taps))()
        self.lib.es1869_rs_table(self.tab, False)
        self.lib.es1869_rs_run_c.argtypes = [ctypes.POINTER(Job)]
        self.lib.es1869_rs_run_c.restype = ctypes.c_uint32

    def run(self, x, step, frac=0, dither=False):
        """x through one run: the ring frames, the first at x[behind]"""
        n = len(x) - self.taps - 2
        room = n + n // 256 + 64
        out = np.zeros(room * 4, dtype=np.uint8)
        a = np.asarray(x, dtype=np.int16)
        j = Job()
        j.tab = ctypes.cast(self.tab, Job.s16p)
        j.centre = 2
        j.ch[0] = a.ctypes.data_as(Job.s16p)
        j.ch[1] = a.ctypes.data_as(Job.s16p)
        j.dst = out.ctypes.data_as(ctypes.POINTER(ctypes.c_uint8))
        j.frames = n
        j.room = room
        j.frac = frac & 0xFFFFFFFF
        j.step = step
        j.stereo = False
        j.dither = dither
        j.seed = 12345
        m = self.lib.es1869_rs_run_c(ctypes.byref(j))
        d = out[:m * 4]
        v = d[0::4].astype(np.int32) | (d[2::4].astype(np.int32) << 8)
        return v.astype(np.uint16).astype(np.int16).astype(np.float64)


def window(n):
    t = 2 * np.pi * np.arange(n) / n
    return sum(a * np.cos(k * t) for k, a in enumerate(BH7))


def step_of(ppm):
    return round(ppm * 1e-6 * 2 ** 32)


def tone(f, db, n):
    a = 32767 * 10 ** (db / 20)
    return np.rint(a * np.sin(2 * np.pi * f / FS * np.arange(n)))


def remove(y, freqs):
    """the fitted tones taken off y: what's left, and each one's amplitude"""
    t = np.arange(len(y)) * 2 * np.pi / FS
    b = [np.ones(len(y))]
    for f in freqs:
        b += [np.cos(f * t), np.sin(f * t)]
    b = np.stack(b, 1)
    c = np.linalg.lstsq(b, y, rcond=None)[0]
    return y - b @ c, [float(np.hypot(c[1 + 2 * i], c[2 + 2 * i]))
                       for i in range(len(freqs))]


def spectrum(r):
    """the amplitude spectrum of r, a tone's peak reading true, and the
    noise bandwidth of a bin"""
    n = len(r)
    w = window(n)
    v = np.abs(np.fft.rfft(r * w)) / (w.sum() / 2)
    return v, (w ** 2).sum() / w.sum() ** 2 * n


def measure(y, f, skip=4000):
    """THD+N and the worst spur in the band against the tone, its level in
    dBFS, and where the spur is"""
    y = y[skip:-skip]
    y = y[:1 << int(np.log2(len(y)))]
    r, (a,) = remove(y, [f])
    v, enbw = spectrum(r)
    hz = np.arange(len(v)) * FS / len(y)
    band = (hz >= BAND[0]) & (hz <= BAND[1])
    noise = np.sqrt((v[band] ** 2).sum() / 2 / enbw)
    return {"thdn": 20 * np.log10(noise / (a / 2 ** .5)),
            "spur": 20 * np.log10(v[band].max() / a),
            "level": 20 * np.log10(a / 32767),
            "at": float(hz[band][np.argmax(v[band])])}


def frames_for(ppm):
    """enough for the place to go round a few times"""
    return 1 << (22 if abs(ppm) < 3 else 20 if abs(ppm) < 20 else 18)


def band(flt, ppm):
    out = []
    fs = sorted(set(np.round(np.geomspace(20, 21500, 40), 1).tolist() +
                    [18000.3, 19000.1, 19500.7, 20000.3]))
    step = step_of(ppm)
    n = frames_for(ppm)
    for f in fs:
        y = flt.run(tone(f, -1, n + 9000), step)
        m = measure(y, f * (1 + step / 2 ** 32))
        m["f"] = f
        out.append(m)
    return out


def ideal(ppm):
    """the same tones from a filter with no error: 16-bit input and output"""
    out = []
    step = step_of(ppm)
    n = 1 << 18
    for f in (20.0, 100.0, 997.0, 5000.7, 10000.9, 15000.1, 18000.3, 20000.3):
        a = 32767 * 10 ** (-1 / 20)
        at = np.arange(n) * (1 + step / 2 ** 32)
        once = np.rint(a * np.sin(2 * np.pi * f / FS * np.arange(n)))
        # what a perfect filter passes on of the input's rounding, as noise
        noise = once - a * np.sin(2 * np.pi * f / FS * np.arange(n))
        y = np.rint(a * np.sin(2 * np.pi * f / FS * at) + noise)
        m = measure(y, f * (1 + step / 2 ** 32))
        m["f"] = f
        out.append(m)
    return out


def levels(flt, ppm=30, f=997.0):
    out = []
    step = step_of(ppm)
    for db in (0, -1, -3, -6, -12, -20, -30, -40, -50, -60, -70, -80, -90):
        y = flt.run(tone(f, db, (1 << 18) + 9000), step)
        m = measure(y, f * (1 + step / 2 ** 32))
        m["db"] = db
        out.append(m)
    return out


def steps(flt):
    out = []
    for ppm in (-1000, -252, -100, -30, -10, -1, -0.1, 0.1, 1, 10, 30, 100,
                252, 1000):
        row = {"ppm": ppm}
        step = step_of(ppm)
        for f in (997.0, 10000.9, 19000.1):
            y = flt.run(tone(f, -1, frames_for(ppm) + 9000), step)
            row[str(int(f))] = measure(y, f * (1 + step / 2 ** 32))
        out.append(row)
    return out


def two_tones(flt):
    out = {}
    step = step_of(30)
    e = 1 + step / 2 ** 32
    for name, f1, a1, f2, a2 in (("ccif", 19000.3, 14000, 20000.3, 14000),
                                 ("smpte", 60.1, 21000, 7000.7, 5250),
                                 ("dim", 3150.3, 21000, 15000.7, 5250)):
        n = 1 << 18
        t = np.arange(n + 9000)
        x = np.rint(a1 * np.sin(2 * np.pi * f1 / FS * t) +
                    a2 * np.sin(2 * np.pi * f2 / FS * t))
        y = flt.run(x, step)[4000:]
        y = y[:n]
        r, _ = remove(y, [f1 * e, f2 * e])
        v, _ = spectrum(r)
        hz = np.arange(len(v)) * FS / n
        keep = (hz >= BAND[0]) & (hz <= BAND[1])
        out[name] = {"worst": 20 * np.log10(v[keep].max() / 32767),
                     "at": float(hz[keep][np.argmax(v[keep])])}
    return out


def many_tones(flt):
    """500 tones falling 3 dB an octave, against the same at the places"""
    rng = np.random.default_rng(7)
    n = 1 << 17
    step = step_of(30)
    hz = np.exp(rng.uniform(np.log(30), np.log(20000), 500))
    amp = 1 / np.sqrt(hz)
    ph = rng.uniform(0, 2 * np.pi, 500)

    def sines(at):
        v = np.zeros(len(at))
        for i in range(0, 500, 25):
            v += (amp[i:i + 25] * np.sin(
                np.outer(at, 2 * np.pi * hz[i:i + 25] / FS) +
                ph[i:i + 25])).sum(1)
        return v

    pure = sines(np.arange(n, dtype=float))
    k = 32000 / np.abs(pure).max()
    y = flt.run(np.rint(k * pure), step)
    at = flt.behind + np.arange(len(y)) * (1 + step / 2 ** 32)
    keep = at < n - flt.taps - 2
    want = k * sines(at[keep])
    e = y[keep] - want
    v, enbw = spectrum(e[:1 << int(np.log2(len(e)))])
    m = len(v)
    edges = np.geomspace(20, 22050, 25)
    bands = []
    for lo, hi in zip(edges, edges[1:]):
        a, b = int(lo / 22050 * m), max(int(hi / 22050 * m), int(lo / 22050 * m) + 1)
        bands.append({"f": float((lo * hi) ** .5),
                      "db": float(10 * np.log10((v[a:b] ** 2).mean() + 1e-30))})
    return {"rms_lsb": float(np.std(e)),
            "rms_dbfs": float(20 * np.log10(np.std(e) / 32767)),
            "signal_dbfs": float(20 * np.log10(np.std(want) / 32767)),
            "rounding_lsb": float(np.std(np.rint(want) - want)),
            "bands": bands}


def sweep(flt, ppm=1000):
    """a sweep to the top of the band at -1 dBFS: what's left against the
    same sweep at the places, in steps of a third of an octave"""
    n = 1 << 19
    step = step_of(ppm)
    a = 32767 * 10 ** (-1 / 20)
    f0, f1 = 20.0, 22050.0

    def phase(t):
        k = np.log(f1 / f0)
        return 2 * np.pi * f0 * n / FS / k * (np.exp(t / n * k) - 1)

    x = np.rint(a * np.sin(phase(np.arange(n, dtype=float))))
    y = flt.run(x, step)
    at = flt.behind + np.arange(len(y)) * (1 + step / 2 ** 32)
    keep = at < n - flt.taps - 2
    e = y[keep] - a * np.sin(phase(at[keep]))
    out = []
    block = 1 << 13
    for i in range(0, len(e) - block, block):
        f = f0 * (f1 / f0) ** ((at[i + block // 2]) / n)
        out.append({"f": float(f), "db": float(
            20 * np.log10(np.std(e[i:i + block]) / (a / 2 ** .5)))})
    return out


def dither(flt):
    out = []
    for ppm in (0.1, 1, 30):
        step = step_of(ppm)
        n = frames_for(ppm)
        for db in (-60, -70, -80, -90, -96):
            row = {"ppm": ppm, "db": db}
            for on in (False, True):
                y = flt.run(tone(997.0, db, n + 9000), step, dither=on)
                y = y[4000:-4000]
                y = y[:1 << int(np.log2(len(y)))]
                r, _ = remove(y, [997.0 * (1 + step / 2 ** 32)])
                v, enbw = spectrum(r)
                h = np.arange(len(v)) * FS / len(y)
                keep = (h >= BAND[0]) & (h <= BAND[1])
                k = "on" if on else "off"
                row[k] = {
                    "noise": 20 * np.log10(np.sqrt(
                        (v[keep] ** 2).sum() / 2 / enbw) / 32767),
                    "spur": 20 * np.log10(v[keep].max() / 32767)}
            out.append(row)
    return out


def spectra(flt, ppm=30):
    """residual spectra, in 1000 points on a log scale by the bin's peak"""
    out = {}
    step = step_of(ppm)
    for f in (997.0, 10000.9, 19000.1, 20000.3):
        n = 1 << 19
        y = flt.run(tone(f, -1, n + 9000), step)[4000:]
        y = y[:n]
        v, _ = spectrum(y)
        hz = np.arange(len(v)) * FS / n
        edges = np.unique(np.geomspace(1, len(v) - 1, 1400).astype(int))
        pts = [{"f": float(hz[a]),
                "db": float(20 * np.log10(v[a:b].max() / 32767 + 1e-12))}
               for a, b in zip(edges, edges[1:])]
        out[str(int(f))] = pts
    return out


def all_of(flt):
    return {"taps": flt.taps,
            "band_30": band(flt, 30), "band_252": band(flt, -252),
            "band_1": band(flt, 1), "levels": levels(flt),
            "steps": steps(flt), "two_tones": two_tones(flt),
            "many_tones": many_tones(flt), "sweep": sweep(flt),
            "dither": dither(flt), "spectra": spectra(flt)}


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("-o", default=None, help="the numbers, as JSON")
    ap.add_argument("--against", help="a second resample.c's directory")
    args = ap.parse_args()
    rep = {"new": all_of(Filter(os.path.join(ROOT, "linux", "es1869"))),
           "ideal": ideal(30)}
    if args.against:
        rep["old"] = all_of(Filter(args.against))
    for key in ("old", "new"):
        if key not in rep:
            continue
        r = rep[key]
        print(f"{key}: {r['taps']} taps, 30 ppm")
        for m in r["band_30"][::4]:
            print(f"  {m['f']:8.1f} Hz  THD+N {m['thdn']:7.2f}  spur "
                  f"{m['spur']:7.2f}  level {m['level'] + 1:+.4f}")
        print(f"  500 tones: {r['many_tones']['rms_lsb']:.3f} LSB rms, "
              f"against {r['many_tones']['rounding_lsb']:.3f} of one rounding")
        print("  two tones:", {k: round(v["worst"], 1)
                               for k, v in r["two_tones"].items()})
    if args.o:
        with open(args.o, "w", encoding="utf-8") as f:
            json.dump(rep, f)


if __name__ == "__main__":
    main()
