#!/usr/bin/env python3
# (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
#
# Licensed under GPL Version 3.0
"""Analyze a recording of one of tools/testsig.py's signals.

usage: sigan.py NAME REC.wav [--ref REF.wav] [-o PREFIX] [--sig FILE.wav]
                [--rec-rate HZ] [--ref-rate HZ] [--chans A,B] [--rate HZ]

NAME is the signal's file name as testsig.py gives it (sweep-44k,
sweep-l-44k) or the path of the file played. The analysis finds the signal
anywhere in the recording by its markers, takes the ratio of the player's
clock to the recorder's from their distance, maps the recording onto the
signal's samples, and reports what the signal's family measures. --ref adds
a second recording made at the same time, such as a second card's or what
Audio 2 played, and the first against it. -o writes PREFIX.txt and
PREFIX-*.csv. --sig compares with that file instead of the signal made
again here. --rec-rate takes the recording at another rate than its header
gives, such as 44100 for PCM 1's 49613 Hz on the dig path, where each of
the stream's samples takes a frame. --chans picks two of a recording's
channels, counted from 0. Needs numpy.
"""

import argparse
import math
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import testsig as ts  # noqa: E402
from clockan import adev  # noqa: E402
from recan import read_wav  # noqa: E402

THR = 0.35                  # a marker's least normalized correlation
POINTS = tuple(sorted(set(ts.THIRDS) | {2, 2.5, 3.15, 4, 5, 6.3, 8, 10,
                                        12.5, 16, 17000, 18000, 19000,
                                        21000, 22000, 23000}))
SIDE = ("L", "R")
BH7 = (0.27105140069342, -0.43329793923448, 0.21812299954311,
       -0.06592544638803, 0.01081174209837, -0.00077658482522,
       0.00001388721735)


def next_pow2(n):
    return 1 << max(0, int(n - 1).bit_length())


def db(v):
    return 20 * math.log10(max(abs(v), 1e-15))


def pdb(v):
    return 10 * math.log10(max(v, 1e-30))


def fold(f, rate):
    """where f lands in 0..rate/2 when sampled at rate"""
    f = abs(f) % rate
    return rate - f if f > rate / 2 else f


def a_weight(f):
    """the A-weighting's gain in dB"""
    f = np.maximum(np.asarray(f, dtype=float), 1e-3)
    f2 = f * f
    ra = (12194.0 ** 2 * f2 * f2) / (
        (f2 + 20.6 ** 2) * np.sqrt((f2 + 107.7 ** 2) * (f2 + 737.9 ** 2)) *
        (f2 + 12194.0 ** 2))
    return 20 * np.log10(ra) + 2.0


def bh7(n):
    t = 2 * np.pi * np.arange(n) / n
    return sum(a * np.cos(k * t) for k, a in enumerate(BH7))


def hann(n):
    """the periodic Hann window, as es1869scope's loop.c has it"""
    return 0.5 - 0.5 * np.cos(2 * np.pi * np.arange(n) / n)


# --- the report --------------------------------------------------------------

class Report:
    """the text report, and the CSV files beside it"""

    def __init__(self, prefix=None):
        self.prefix, self.lines, self.files = prefix, [], []
        self.values = {}      # the results as numbers, for the tests

    def say(self, text=""):
        self.lines.append(text)

    def put(self, key, value):
        self.values[key] = value

    def table(self, head, rows):
        """rows of already formatted cells, under a header, aligned"""
        cells = [head] + rows
        w = [max(len(r[i]) for r in cells) for i in range(len(head))]
        for r in cells:
            self.say("  " + "  ".join(c.rjust(n) for c, n in zip(r, w)))

    def csv(self, tag, head, rows):
        if not self.prefix:
            return
        path = "%s-%s.csv" % (self.prefix, tag)
        with open(path, "w", encoding="ascii") as f:
            f.write(",".join(head) + "\n")
            for r in rows:
                f.write(",".join(v if isinstance(v, str) else "%.6g" % v
                                 for v in r) + "\n")
        self.files.append(path)

    def done(self):
        if self.files:
            self.say("csv      " + ", ".join(os.path.basename(p)
                                             for p in self.files))
        text = "\n".join(self.lines) + "\n"
        if self.prefix:
            with open(self.prefix + ".txt", "w", encoding="ascii") as f:
                f.write(text)
        return text


# --- recordings and alignment ------------------------------------------------

class Rec:
    """a recording: frames by channels, floats of full scale 1"""

    def __init__(self, path, rate=None, chans=None):
        r, x = read_wav(path)
        if chans:
            x = x[:, chans]
        elif x.shape[1] > 2:
            x = x[:, :2]
        self.path, self.rate, self.x = path, int(rate or r), x

    @classmethod
    def of(cls, x, rate, name="<sim>"):
        r = cls.__new__(cls)
        r.path, r.rate = name, int(rate)
        r.x = x if x.ndim == 2 else x[:, None]
        return r


def highpass(x, rate, lo=200.0):
    """x without what lies under lo Hz, a ramp over the octave below it:
    the ADC's offset and hum out of the markers' way"""
    n = len(x)
    f = np.fft.rfftfreq(n, 1.0 / rate)
    g = np.clip((f - lo / 2) / (lo / 2), 0, 1)
    return np.fft.irfft(np.fft.rfft(x, axis=0) * g[:, None], n, axis=0)


def correlate(x, m):
    """each channel's correlation with m at each start, analytic, over m's
    energy (a gain, its sign and phase), and over both energies (0 to 1)"""
    n, k = len(x), len(m)
    if n < k:
        return None, None
    nfft = next_pow2(n + k)
    mm = np.conj(np.fft.rfft(m, nfft))
    gains, norms = [], []
    e = np.concatenate([np.zeros((1, x.shape[1])), np.cumsum(x * x, axis=0)])
    for c in range(x.shape[1]):
        r = np.fft.rfft(x[:, c], nfft) * mm
        z = np.zeros(nfft, complex)
        z[:len(r)] = r
        z[1:nfft // 2] *= 2
        a = np.fft.ifft(z)[:n - k + 1]
        g = a / np.dot(m, m)
        # the window's energy no less than -100 dBFS of noise, so that
        # digital silence divides by no rounding error
        den = np.sqrt(np.maximum(e[k:, c] - e[:-k, c], k * 1e-10) *
                      np.dot(m, m))
        gains.append(g)
        norms.append(np.minimum(np.abs(a) / den, 1.0))
    return np.array(gains), np.array(norms)


def peaks(e, least, apart):
    """the local tops of e above least, at least apart from each other:
    (index, value), the index refined by a parabola"""
    idx = np.flatnonzero(e > least)
    out = []
    if not len(idx):
        return out
    runs = np.split(idx, np.flatnonzero(np.diff(idx) > 1) + 1)
    for r in runs:
        i = int(r[np.argmax(e[r])])
        if out and i - out[-1][0] < apart:
            if e[i] > out[-1][1]:
                out[-1] = (i, float(e[i]))
            continue
        out.append((i, float(e[i])))
    res = []
    for i, v in out:
        d = 0.0
        if 0 < i < len(e) - 1:
            a, b, c = e[i - 1], e[i], e[i + 1]
            if a - 2 * b + c < 0:
                d = 0.5 * (a - c) / (a - 2 * b + c)
        res.append((i + d, v))
    return res


class Align:
    """where a signal lies in a recording: signal frame s is at recording
    frame lag + s * rho"""

    def __init__(self, lag, rho, r0, how):
        self.lag, self.rho, self.r0, self.how = lag, rho, r0, how
        self.ppm = (r0 / rho - 1) * 1e6
        self.gains = None       # marker gains [recorded channel][side]
        self.notes = []

    def at(self, s):
        return self.lag + s * self.rho

    def hz(self, f):
        """a frequency of the signal as the recorder measures it"""
        return f * self.r0 / self.rho


def find_markers(rec, made):
    """the markers in a recording: an Align, or None"""
    info = made.info
    marks = info.get("marks") or []
    if not marks:
        return None
    r0 = rec.rate / made.rate
    m = ts.marker(rec.rate)
    k = len(m)
    x = highpass(rec.x, rec.rate)
    gains, norms = correlate(x, m)
    if gains is None:
        return None
    cands = []
    for c in range(rec.x.shape[1]):
        for t, v in peaks(norms[c], THR, k // 2):
            cands.append((t, c, v))
    if not cands:
        return None
    sep = info.get("mark_sep", 0)
    offs = [0.0]
    if sep:
        offs.append(sep * r0)
    if len(marks) > 1:
        span = (marks[1] - marks[0]) * r0
        offs += [span] + ([span + sep * r0] if sep else [])
    best = None
    for t0, _c0, _v0 in cands:
        score, hits = 0.0, []
        for o in offs:
            tol = 0.002 * rec.rate + abs(o) * 2e-3
            near = [(abs(t - (t0 + o)), v, t, c) for t, c, v in cands
                    if abs(t - (t0 + o)) <= tol]
            if near:
                _d, v, t, c = max(near, key=lambda q: q[1])
                score += v
                hits.append((t, c, v))
            else:
                hits.append(None)
        # the earliest wins unless a later one hears a marker more, so
        # that an end marker missed doesn't take the start's place
        if best is None or score > best[0] + 0.3:
            best = (score, t0, hits)
    _score, t0, hits = best
    ev = {"sL": hits[0]}
    if sep:
        ev["sR"] = hits[1]
    if len(marks) > 1:
        ev["eL"] = hits[2 if sep else 1]
        if sep:
            ev["eR"] = hits[3]
    # time the signal from the left marker, or from the right one where a
    # recorder hears only that
    key, off = ("sL", 0) if ev["sL"] else ("sR", sep)
    if not ev.get(key):
        return None
    t_s, c_s, _v = ev[key]
    end = ev.get("e" + key[1])
    rho, how = r0, "start marker only, the nominal rate"
    if end:
        d = refine_gap(x[:, c_s], t_s, end[0], k)
        rho = d / (marks[1] - marks[0])
        how = "markers %.3f s apart" % (d / rec.rate)
    big_t = int(round(ts.MARK_S * made.rate)) / made.rate
    eps = r0 / rho - 1
    t_s = refine_marker(x[:, c_s], t_s, rec.rate, big_t, eps)
    al = Align(t_s - (marks[0] + off) * rho, rho, r0, how)
    al.events = ev
    al.gains = marker_gains(gains, ev, rec.x.shape[1], rec.rate)
    if sep and ev.get("sL") and ev.get("sR"):
        # the right channel's own start, against the left's
        t_r = refine_marker(x[:, ev["sR"][1]], ev["sR"][0], rec.rate,
                            big_t, eps)
        al.skew = (t_r - t_s) / rho - sep
    return al


def template(rr, big_t, eps=0.0, down=False):
    """the marker as a recorder at rr hears it from a player eps fast:
    shorter by 1 + eps, which a linear chirp's correlation would otherwise
    read as a shift of its time"""
    n = int(round(big_t * rr / (1 + eps)))
    t = np.arange(n) * (1 + eps) / rr
    f0, f1 = ts.MARK_F[::-1] if down else ts.MARK_F
    ph = 2 * np.pi * (f0 * t + (f1 - f0) * t * t / (2 * big_t))
    u = t / big_t
    env = np.ones(n)
    a = u < 0.25
    env[a] = 0.5 - 0.5 * np.cos(4 * np.pi * u[a])
    b = u > 0.75
    env[b] = 0.5 - 0.5 * np.cos(4 * np.pi * (1 - u[b]))
    return ts.MARK_AMP * env * np.sin(ph)


def refine_marker(x, t, rr, big_t, eps, down=False):
    """a marker's start near t, from the stretched template's correlation"""
    m = template(rr, big_t, eps, down)
    k = len(m)
    a = int(round(t)) - k // 2
    if a < 0 or a + 2 * k > len(x):
        return t
    _g, e = correlate(x[a:a + 2 * k, None], m)
    i = int(np.argmax(e[0, :k + 1]))
    return a + refine_peak(e[0], i)


def refine_gap(x, t_s, t_e, k):
    """the distance from one marker to the other, from the correlation of
    the two as recorded"""
    a, b = int(round(t_s)), int(round(t_e))
    pad = k // 2
    if a - pad < 0 or b + k + pad > len(x):
        return t_e - t_s
    s = x[a:a + k]
    e = x[b - pad:b + k + pad]
    c = np.correlate(e, s, mode="valid")
    i = int(np.argmax(np.abs(c)))
    d = 0.0
    if 0 < i < len(c) - 1:
        y0, y1, y2 = np.abs(c[i - 1:i + 2])
        if y0 - 2 * y1 + y2 < 0:
            d = 0.5 * (y0 - y2) / (y0 - 2 * y1 + y2)
    return (b - pad + i + d) - a


def marker_gains(gains, ev, nch, rr):
    """the gain from each side's start marker to each recorded channel, its
    phase taken back over the fraction of a frame by the chirp's middle
    frequency"""
    fc = sum(ts.MARK_F) / 2
    out = []
    for c in range(nch):
        row = []
        for key in ("sL", "sR"):
            hit = ev.get(key)
            if not hit:
                row.append(None)
                continue
            i = min(int(round(hit[0])), len(gains[c]) - 1)
            turn = np.exp(2j * np.pi * fc * (hit[0] - i) / rr)
            row.append(complex(gains[c][i] * turn))
        out.append(row)
    return out


def align_periodic(rec, made):
    """a periodic signal with no markers: the lag from the correlation of
    every recorded channel with every side of it, the ratio nominal"""
    x = made.floats()
    r0 = rec.rate / made.rate
    y = rec.x
    if rec.rate != made.rate:
        y = warp(rec.x, 0.0, r0, int(len(rec.x) / r0))
    n = len(y) + len(x)
    nfft = next_pow2(n)
    e = np.zeros(nfft)
    for c in range(y.shape[1]):
        yf = np.fft.rfft(y[:, c], nfft)
        for s in range(x.shape[1]):
            r = np.fft.irfft(yf * np.conj(np.fft.rfft(x[:, s], nfft)), nfft)
            e += r * r
    lags = np.concatenate([e[:len(y)], e[nfft - len(x) + 1:]])
    pos = np.concatenate([np.arange(len(y)), np.arange(-len(x) + 1, 0)])
    i = int(np.argmax(lags))
    d = 0.0
    if 0 < i < len(lags) - 1 and pos[i - 1] + 1 == pos[i] == pos[i + 1] - 1:
        a, b, c = lags[i - 1:i + 2]
        if a - 2 * b + c < 0:
            d = 0.5 * (a - c) / (a - 2 * b + c)
    return Align((pos[i] + d) * r0, r0, r0, "correlation with the signal")


def warp(x, lag, rho, n):
    """x (frames by channels) at lag + k * rho for k < n: the span's DFT
    gives its band-limited interpolation, cut to the lower of the two
    rates' bands, and Bluestein's chirp-z transform evaluates it on that
    grid exactly, whatever the ratio"""
    i0 = int(math.floor(lag)) - 2
    d = lag - i0
    m = int(math.ceil(d + (n - 1) * rho)) + 3
    seg = np.zeros((m, x.shape[1]))
    a, b = max(i0, 0), min(i0 + m, len(x))
    if b > a:
        seg[a - i0:b - i0] = x[a:b]
    spec = np.fft.fft(seg, axis=0)
    # the frequencies kept, -top to top bins, under both Nyquists
    top = int(min(m / 2.0, m / (2.0 * rho))) - 1
    f = np.arange(-top, top + 1)
    a_g = spec[f % m] * np.exp(2j * np.pi * f * d / m)[:, None]
    g = np.arange(len(f), dtype=np.float64)
    k = np.arange(n, dtype=np.float64)
    size = next_pow2(len(f) + n - 1)
    # W^(g k) = W^(g^2/2) W^(k^2/2) W^(-(k-g)^2/2), W = e^(2 pi i rho / m)
    u = np.zeros((size, x.shape[1]), complex)
    u[:len(f)] = a_g * np.exp(1j * np.pi * rho * (g * g / m))[:, None]
    v = np.zeros(size, complex)
    v[:n] = np.exp(-1j * np.pi * rho * (k * k / m))
    back = np.arange(1, len(f), dtype=np.float64)
    v[size - len(back):] = np.exp(-1j * np.pi * rho * (back * back / m))[::-1]
    z = np.fft.ifft(np.fft.fft(u, axis=0) * np.fft.fft(v)[:, None],
                    axis=0)[:n]
    z *= np.exp(1j * np.pi * rho * (k * k / m))[:, None]
    # the sum ran from -top: its first term's frequency
    z *= np.exp(-2j * np.pi * top * k * rho / m)[:, None]
    return z.real / m


def active(v, least=1e-4):
    """whether a side carries more than dither: an rms over -77 dBFS"""
    return len(v) > 0 and float(np.sqrt(np.mean(v * v))) > least


def same_channels(y):
    """whether the recorded channels carry one signal"""
    if y.shape[1] < 2:
        return True
    a, b = y[:, 0], y[:, 1]
    sa, sb = np.dot(a, a), np.dot(b, b)
    if sa <= 0 or sb <= 0:
        return False
    return abs(np.dot(a, b)) / math.sqrt(sa * sb) > 0.999 and \
        abs(10 * math.log10(sa / sb)) < 0.1


class Ctx:
    """one analysis: the signal, the recording and where one is in the
    other"""

    def __init__(self, made, rec, rep, ref=None):
        self.made, self.rec, self.rep, self.ref = made, rec, rep, ref
        self.x = made.floats()
        self.info = made.info
        self.al = None
        self.y = None           # the recording on the signal's frames
        self.yr, self.ral = None, None  # the same of the ref, its Align
        self.outs = list(range(rec.x.shape[1]))

    def sides(self):
        """the sides that carry the body, over the dither"""
        b, n = self.info["body"]
        return [s for s in range(min(2, self.x.shape[1]))
                if active(self.x[b:b + n, s])]

    def played(self):
        """the sides the file plays: one in a side variant, else both"""
        if self.made.side:
            return ["lr".index(self.made.side)]
        return list(range(min(2, self.x.shape[1])))

    def onto(self):
        """the recording warped onto the signal's frames"""
        if self.y is None:
            self.y = warp(self.rec.x, self.al.lag, self.al.rho, len(self.x))
            b, n = self.info["body"]
            if len(self.outs) > 1 and same_channels(self.y[b:b + n]):
                self.outs = [0]
                self.rep.say("note     the recorded channels carry the same "
                             "signal: one is analyzed")
        return self.y

    def oname(self, c):
        return "out %s" % (SIDE[c] if len(self.outs) > 1 else "")

    def raw(self, s0, s1, c):
        """the raw recording from signal frame s0 to s1, channel c"""
        a, b = int(round(self.al.at(s0))), int(round(self.al.at(s1)))
        a, b = max(a, 0), min(b, len(self.rec.x))
        return self.rec.x[a:b, c] if b > a else np.zeros(0)


def describe(ctx):
    """the header: the signal, the recording and the alignment"""
    rep, made, rec, al = ctx.rep, ctx.made, ctx.rec, ctx.al
    rep.say("signal   %s (%s), %d Hz, %d channels, %.2f s%s" % (
        made.name, made.sig.family, made.rate, made.data.shape[1],
        len(made.data) / made.rate,
        ", %s side only" % SIDE["lr".index(made.side)] if made.side else ""))
    rep.say("record   %s, %d Hz, %d channels, %.2f s" % (
        os.path.basename(rec.path), rec.rate, rec.x.shape[1],
        len(rec.x) / rec.rate))
    rep.say("align    signal frame 0 at %.3f s (%.2f frames); %s" % (
        al.lag / rec.rate, al.lag, al.how))
    rep.put("lag", al.lag)
    rep.put("ppm", al.ppm)
    rep.put("gains", al.gains)
    if "nominal" not in al.how:
        rep.say("clock    the player against the recorder %+.3f ppm" %
                al.ppm)
    if al.gains:
        rows = []
        for c, row in enumerate(al.gains):
            cells = []
            for g in row:
                cells.append("-" if g is None else "%.1f dB %+4.0f deg" % (
                    db(abs(g)), math.degrees(np.angle(g))))
            rows.append(["rec %d" % c] + cells)
        rep.say("markers  the start markers' gain into each recorded "
                "channel:")
        rep.table(["", "left marker", "right marker"], rows)
        kind = mix_kind(al.gains)
        if kind:
            rep.say("         " + kind)
            al.notes.append(kind)
            rep.put("mix", kind)
        if hasattr(al, "skew"):
            rep.say("         the right marker %+.2f frames after the "
                    "left's place" % al.skew)


def unswap(rec, al):
    """a recorder whose channels hear the other sides, as the VX into the
    ES1869's Aux input does, swapped back so that rec 0 is the left"""
    g = al.gains
    if not g or len(g) != 2 or any(v is None for row in g for v in row):
        return False
    if abs(g[0][1]) > 10 * abs(g[0][0]) and abs(g[1][0]) > 10 * abs(g[1][1]):
        rec.x = rec.x[:, ::-1]
        al.gains = [g[1], g[0]]
        return True
    return False


def mix_kind(gains):
    """what a recorder hears of the two sides, from the markers"""
    out = []
    for c, (gl, gr) in enumerate(gains):
        if gl is None or gr is None:
            continue
        a, b = abs(gl), abs(gr)
        if a > 10 * b:
            what = "the left side"
        elif b > 10 * a:
            what = "the right side"
        else:
            ph = abs(math.degrees(np.angle(gr / gl)))
            what = "the sides' difference" if ph > 135 else \
                "the sides' sum" if ph < 45 else "both sides"
        out.append("rec %d hears %s" % (c, what))
    text = "; ".join(out)
    if "difference" in text:
        text += ". A body on both sides cancels: play the -l and -r files"
    return text


# --- shared measures ---------------------------------------------------------

def fit_sine(x, rate, f, refine=True):
    """the sine near f that fits x best, with a mean: (amp, phase at the
    middle as a sine's, f, residual)"""
    n = len(x)
    if n < 8 or f <= 0 or f >= rate / 2:
        return 0.0, 0.0, f, x - np.mean(x) if n else x
    t = np.arange(n) - (n - 1) / 2.0
    w = 2 * np.pi * f / rate
    for _i in range(12 if refine else 1):
        c, s = np.cos(w * t), np.sin(w * t)
        cols = [c, s, np.ones(n)]
        p = np.linalg.lstsq(np.column_stack(cols), x, rcond=None)[0]
        if not refine:
            break
        dcol = t * (p[1] * c - p[0] * s)
        q = np.linalg.lstsq(np.column_stack(cols + [dcol]), x,
                            rcond=None)[0]
        step = q[3]
        if not np.isfinite(step) or abs(step) > 0.2 * w:
            break
        w += step
        if abs(step) < 1e-12 * w:
            break
    c, s = np.cos(w * t), np.sin(w * t)
    p = np.linalg.lstsq(np.column_stack([c, s, np.ones(n)]), x,
                        rcond=None)[0]
    resid = x - (p[0] * c + p[1] * s + p[2])
    return float(np.hypot(p[0], p[1])), float(np.arctan2(p[0], p[1])), \
        w * rate / (2 * np.pi), resid


def fit_tones(x, rate, freqs):
    """the amplitudes of sines at known frequencies, fitted together with a
    mean"""
    t = np.arange(len(x)) - (len(x) - 1) / 2.0
    cols = [np.ones(len(x))]
    for f in freqs:
        w = 2 * np.pi * f / rate
        cols += [np.cos(w * t), np.sin(w * t)]
    p = np.linalg.lstsq(np.column_stack(cols), x, rcond=None)[0]
    return [float(np.hypot(p[1 + 2 * i], p[2 + 2 * i]))
            for i in range(len(freqs))]


def band_rms(x, rate, lo, hi, weight=None):
    """x's rms within lo-hi Hz, A-weighted if asked"""
    n = len(x)
    if n < 4:
        return 0.0
    s = np.fft.rfft(x)
    f = np.fft.rfftfreq(n, 1.0 / rate)
    g = ((f >= lo) & (f <= hi)).astype(float)
    if weight == "A":
        g *= 10 ** (a_weight(f) / 20)
    p = np.abs(s * g) ** 2
    p[1:] *= 2
    if n % 2 == 0:
        p[-1] /= 2
    return math.sqrt(p.sum()) / n


def tone_measures(x, rate, f, top=20000.0):
    """a tone's amplitude, phase, frequency, THD+N (unweighted and A) and
    harmonics 2-5 in dB against it, each with where it lands"""
    amp, ph, fr, r = fit_sine(x, rate, f)
    hi = min(top, 0.45 * rate)
    out = {"amp": amp, "phase": ph, "f": fr}
    if amp <= 0:
        out.update(thdn=0.0, thdna=0.0, harm=[], noise=0.0)
        return out
    ref = amp / math.sqrt(2)
    out["thdn"] = db(band_rms(r, rate, 20, hi) / ref)
    out["thdna"] = db(band_rms(r, rate, 20, hi, "A") / ref)
    harm, rest, taken = [], r.copy(), [fold(fr, rate)]
    for k in range(2, 6):
        fk = fold(k * fr, rate)
        # one that folds onto the tone or an earlier harmonic can't be told
        # apart from it
        if fk < 10 or fk > 0.49 * rate or \
                min(abs(fk - g) for g in taken) < 2.0:
            harm.append((k, fk, None))
            continue
        taken.append(fk)
        a, _p, _f, rest = fit_sine(rest, rate, fk, refine=False)
        harm.append((k, fk, db(a / amp)))
    out["harm"] = harm
    out["noise"] = db(math.sqrt(2) * band_rms(rest, rate, 20, hi))
    return out


def spectrum(x, rate):
    """the amplitude spectrum through the 7-term Blackman-Harris window, a
    tone's peak reading its amplitude"""
    n = len(x)
    w = bh7(n)
    v = np.abs(np.fft.rfft(x * w)) / (w.sum() / 2)
    return np.fft.rfftfreq(n, 1.0 / rate), v


def level_at(f, v, hz, span=3):
    """the largest amplitude within span bins of hz"""
    k = int(round(hz / (f[1] - f[0])))
    a, b = max(k - span, 0), min(k + span + 1, len(v))
    return float(v[a:b].max()) if b > a else 0.0


def top_peaks(f, v, lo, hi, count, floor_db=12, skip=()):
    """the strongest lines in lo-hi Hz standing floor_db over the local
    floor, away from the frequencies in skip"""
    sel = np.flatnonzero((f >= lo) & (f <= hi))
    if not len(sel):
        return []
    df = f[1] - f[0]
    med = np.median(v[sel]) + 1e-15
    out = []
    order = sel[np.argsort(v[sel])[::-1]]
    for i in order:
        if len(out) >= count or v[i] < med * 10 ** (floor_db / 20):
            break
        if any(abs(f[i] - g) < 10 * df for g in skip):
            continue
        if any(abs(f[i] - g) < 10 * df for g, _a in out):
            continue
        if 0 < i < len(v) - 1 and (v[i] < v[i - 1] or v[i] < v[i + 1]):
            continue
        out.append((float(f[i]), float(v[i])))
    return out


def smooth_at(f, h, points, frac=1 / 6):
    """the complex mean of h over a band of frac octave about each point,
    what h holds as NaN left out"""
    out = []
    ok = np.isfinite(h)
    for p in points:
        lo, hi = p * 2 ** (-frac / 2), p * 2 ** (frac / 2)
        sel = (f >= lo) & (f <= hi) & ok
        if not sel.any():
            k = int(np.argmin(np.abs(f - p)))
            sel = np.zeros(len(f), bool)
            sel[k] = ok[k]
        out.append(np.mean(h[sel]) if sel.any() else np.nan)
    return np.array(out)


def delay_of(f, h, rate, lo=300.0, hi=3000.0, w=None):
    """the delay in frames of rate of a response's phase slope over lo-hi
    Hz, by a weighted line"""
    sel = (f >= lo) & (f <= hi) & np.isfinite(h) & (np.abs(h) > 0)
    if sel.sum() < 4:
        return 0.0
    ph = np.unwrap(np.angle(h[sel]))
    ww = np.abs(h[sel]) if w is None else w[sel]
    a = np.polyfit(f[sel], ph, 1, w=ww)[0]
    return -a / (2 * np.pi) * rate


def direct(key):
    """whether a transfer's key names a side into its own output"""
    a, b = key.split(">")
    return a == b or b == "o" or "+" in a


def group_delay(f, h):
    """-d phase / d omega, on the grid, in seconds"""
    ph = np.unwrap(np.angle(h))
    return -np.gradient(ph, 2 * np.pi * f)


def response_report(ctx, tag, f, hs, coh=None, band=None, names=None):
    """the response table of transfers hs ({name: H on f}), with the delay
    of each taken out of its phase, the corners and the extremes"""
    rep, rate = ctx.rep, ctx.made.rate
    lo, hi = band or (10.0, 0.5 * min(rate, ctx.rec.rate))
    pts = [p for p in POINTS if lo <= p <= hi]
    names = names or list(hs)
    taus = {}
    for k in names:
        taus[k] = delay_of(f, hs[k], rate)
    head, rows = ["Hz"], []
    for k in names:
        head += [k + " dB", "deg"]
    if coh:
        head += ["coh " + k for k in coh]
    sm = {k: smooth_at(f, hs[k] * np.exp(2j * np.pi * f / rate * taus[k]),
                       pts) for k in names}
    sc = {k: smooth_at(f, coh[k], pts) for k in coh} if coh else {}
    for i, p in enumerate(pts):
        r = ["%g" % p]
        for k in names:
            r += ["%.2f" % db(abs(sm[k][i])),
                  "%.0f" % math.degrees(np.angle(sm[k][i]))]
        for k in sc:
            r.append("%.3f" % sc[k][i].real)
        rows.append(r)
    rep.say()
    rep.say("response (dB of output over input, phase with the delay "
            "below taken out)")
    rep.table(head, rows)
    for k in names:
        if direct(k):
            rep.say("  %s: delay %.2f frames (%.1f us)%s" % (
                k, taus[k], taus[k] / rate * 1e6, corners(f, hs[k], lo, hi)))
            continue
        own = [d for d in names if direct(d) and d.split(">")[0] ==
               k.split(">")[0]]
        if own:
            rep.say("  %s: %.1f dB under %s at 1 kHz" % (
                k, level_near(f, hs[own[0]]) - level_near(f, hs[k]), own[0]))
    for k in names:
        rep.put("%s:%s" % (tag, k),
                (f, hs[k] * np.exp(2j * np.pi * f / rate * taus[k])))
        rep.put("delay:%s:%s" % (tag, k), taus[k])
    for k in coh or {}:
        rep.put("coh:%s:%s" % (tag, k), (f, coh[k]))
    cols, keep = [], (f >= lo) & (f <= hi)
    for k in names:
        hk = hs[k] * np.exp(2j * np.pi * f / rate * taus[k])
        cols += [20 * np.log10(np.abs(hk[keep]) + 1e-15),
                 np.degrees(np.angle(hk[keep])),
                 group_delay(f[keep], hs[k][keep]) * 1e3]
    head = ["hz"]
    for k in names:
        head += [k + "_db", k + "_deg", k + "_gd_ms"]
    if coh:
        for k in coh:
            cols.append(np.real(coh[k][keep]))
            head.append("coh_" + k)
    rows = np.column_stack([f[keep]] + cols)
    step = max(1, len(rows) // 4000)
    rep.csv(tag, [h.replace(" ", "_").replace(">", "to") for h in head],
            rows[::step].tolist())


def level_near(f, h, at=1000.0):
    """the level in dB of h about a frequency, over a sixth of an octave"""
    v = smooth_at(f, np.abs(h), [at])[0]
    return db(v) if np.isfinite(v) else float("nan")


def corners(f, h, lo, hi):
    """the level at 1 kHz, where it falls 3 dB under that, and the widest
    swing from 20 Hz to 20 kHz, on the response smoothed over a twelfth of
    an octave"""
    grid = lo * 2 ** (np.arange(int(48 * math.log2(hi / lo)) + 1) / 48)
    m = np.array([db(v) if np.isfinite(v) else np.nan
                  for v in smooth_at(f, np.abs(h), grid, 1 / 12)])
    ref = level_near(f, h)
    out = ", %.2f dB at 1 kHz" % ref
    below = np.flatnonzero((grid < 1000) & (m < ref - 3))
    above = np.flatnonzero((grid > 1000) & (m < ref - 3))
    if len(below):
        out += ", -3 dB at %.1f Hz" % grid[below[-1]]
    if len(above):
        out += " and %.0f Hz" % grid[above[0]]
    top = min(20000, hi)
    sel = (grid >= max(20, lo)) & (grid <= top) & np.isfinite(m)
    if sel.any():
        out += ", %+.2f to %+.2f dB from %g Hz to %g kHz" % (
            m[sel].min() - ref, m[sel].max() - ref, max(20, round(lo, 1)),
            round(top / 1000, 2))
    return out


# --- the families ------------------------------------------------------------

def an_ess(ctx):
    """Farina's deconvolution: the impulse responses, the response and the
    harmonics"""
    rep, made = ctx.rep, ctx.made
    rate = made.rate
    y = ctx.onto()
    info = ctx.info
    big_l = info["L"] * rate
    f1 = info["f1"]
    # the band from half an octave over the sweep's start, which the
    # inverse filter's taper leaves alone
    lo = max(info["band"][0], f1 * math.sqrt(2))
    hi = min(info["band"][1], 0.49 * min(rate, ctx.rec.rate))
    hs, harm_rows, ir_cols = {}, [], {}
    for start, n, _chans in info["segments"]:
        act = [s for s in (0, 1) if active(ctx.x[start:start + n, s])]
        if not act:
            continue
        s = act[0]
        x = ctx.x[start:start + n, s]
        # the band-limited response rings on both sides of its peak: the
        # window reaches back to just short of the 2nd harmonic's, and the
        # band starts where it holds two cycles
        pre = int(min(0.5 * rate, 0.45 * big_l * math.log(2)))
        lo = max(lo, 2.0 * rate / pre)
        # long enough for the lowest corner's ringing, 4 cycles of lo
        post = int(min(max(0.5 * rate, 4 * rate / lo), 0.25 * n))
        adv = big_l * math.log(6)
        nfft = next_pow2(n + post + pre + int(adv) + 1)
        xf = np.fft.rfft(x, nfft)
        f = np.fft.rfftfreq(nfft, 1.0 / rate)
        inv = np.conj(xf) / (np.abs(xf) ** 2 + 1e-20) * taper(
            f, f1, lo, hi, min(info["f2"], 0.5 * min(rate, ctx.rec.rate)))
        name_in = "+".join(SIDE[a] for a in act)
        for c in ctx.outs:
            seg = y[max(start - pre, 0):start + n + post, c]
            hfull = np.fft.irfft(np.fft.rfft(seg, nfft) * inv, nfft)
            hfull = np.roll(hfull, -pre)
            # the linear part: from -pre to +post, tapered at its ends
            lin = np.concatenate([hfull[-pre:], hfull[:post]])
            lin *= ts.tukey(len(lin), 0.1)
            hf = np.fft.rfft(np.roll(np.concatenate(
                [lin, np.zeros(nfft - len(lin))]), -pre), nfft)
            key = "%s>%s" % (name_in, SIDE[c] if len(ctx.outs) > 1 else "o")
            hs[key] = np.where((f >= lo) & (f <= hi), hf, np.nan)
            ir_cols[key] = lin
            if direct(key):
                harm_rows.append((key, f, hf, harmonics(hfull, big_l, nfft,
                                                        rate)))
    if not hs:
        rep.say("no sweep found in the signal's body")
        return
    response_report(ctx, "response", f, hs, band=(lo, hi))
    rep.say()
    rep.say("harmonics (dB under the fundamental, by the input's frequency)")
    for key, f, h1, hn in harm_rows:
        pts = [p for p in POINTS if lo <= p <= hi / 2]
        rows, csv_rows = [], []
        m1 = smooth_at(f, h1, pts, 1 / 3)
        hm = {k: smooth_at(f, v, [p * k for p in pts], 1 / 3)
              for k, v in hn.items()}
        for i, p in enumerate(pts):
            cells, tot = ["%g" % p], 0.0
            for k in range(2, 6):
                if k * p > hi or k not in hm:
                    cells.append("-")
                    continue
                r = abs(hm[k][i]) / max(abs(m1[i]), 1e-15)
                tot += r * r
                cells.append("%.1f" % db(r))
            cells.append("%.1f" % db(math.sqrt(tot)) if tot else "-")
            rows.append(cells)
            csv_rows.append([p] + [float(c) if c != "-" else float("nan")
                                   for c in cells[1:]])
        rep.say("  %s" % key)
        rep.table(["Hz", "H2", "H3", "H4", "H5", "THD"], rows)
        rep.put("harm:" + key, {r[0]: r[1:] for r in csv_rows})
        rep.csv("harmonics-" + key.replace(">", "to").replace("+", ""),
                ["hz", "h2_db", "h3_db", "h4_db", "h5_db", "thd_db"],
                csv_rows)
    t = (np.arange(len(next(iter(ir_cols.values())))) - pre) / rate * 1e3
    rep.csv("ir", ["ms"] + [k.replace(">", "to").replace("+", "")
                            for k in ir_cols],
            np.column_stack([t] + list(ir_cols.values())).tolist())


def taper(f, f0, lo, hi, f1):
    """1 from lo to hi, falling to 0 at f0 and at f1 as a raised cosine in
    log frequency, so that the inverse filter rings little"""
    g = ((f >= lo) & (f <= hi)).astype(float)
    lf = np.log(np.maximum(f, 1e-9))
    if lo > f0 > 0:
        a = (f > f0) & (f < lo)
        g[a] = 0.5 - 0.5 * np.cos(np.pi * (lf[a] - math.log(f0)) /
                                  (math.log(lo) - math.log(f0)))
    if f1 > hi:
        b = (f > hi) & (f < f1)
        g[b] = 0.5 + 0.5 * np.cos(np.pi * (lf[b] - math.log(hi)) /
                                  (math.log(f1) - math.log(hi)))
    return g


def harmonics(hfull, big_l, nfft, rate):
    """each harmonic's response from its impulse response, which the
    deconvolution puts L ln k ahead of the linear one"""
    out = {}
    for k in range(2, 6):
        c = nfft - big_l * math.log(k)
        lo_gap = big_l * (math.log(k) - math.log(k - 0.5))
        hi_gap = big_l * (math.log(k + 0.5) - math.log(k))
        a, b = int(c - hi_gap), int(c + lo_gap)
        if a < nfft // 2:
            break
        part = hfull[a:b] * ts.tukey(b - a, 0.2)
        hk = np.zeros(nfft)
        hk[:b - a] = part
        hk = np.roll(hk, -(int(c) - a))
        out[k] = np.fft.rfft(hk, nfft)
    return out


def an_linsweep(ctx):
    """the response from the sweep's own phase in the raw recording, and
    what else sounds beside it"""
    rep, made, al = ctx.rep, ctx.made, ctx.al
    rate, rr = made.rate, ctx.rec.rate
    info = ctx.info
    start, n, _c = info["segments"][0]
    f1, f2, amp = info["f1"], info["f2"], info["amp"]
    big_t = n / rate
    act = [s for s in (0, 1) if active(ctx.x[start:start + n, s])]
    a0, a1 = int(math.ceil(al.at(start))), int(al.at(start + n))
    if a0 < 0 or a1 > len(ctx.rec.x) or not act:
        rep.say("the sweep lies outside the recording")
        return
    m = np.arange(a0, a1)
    t = ((m - al.lag) / al.rho - start) / rate
    ph = 2 * np.pi * (f1 * t + (f2 - f1) * t * t / (2 * big_t))
    fi = f1 + (f2 - f1) * t / big_t
    nr = info.get("ramp", 0) / rate
    keep = (t > nr) & (t < big_t - nr)
    width = 25.0
    edges = np.arange(f1, f2 + width, width)
    rows_csv, hs = [], {}
    for c in ctx.outs:
        seg = ctx.rec.x[a0:a1, c]
        nf = next_pow2(len(seg))
        s = np.fft.fft(seg, nf)
        s[nf // 2 + 1:] = 0
        s[1:nf // 2] *= 2
        ya = np.fft.ifft(s)[:len(seg)]
        z = ya * np.exp(-1j * (ph - np.pi / 2)) / amp
        idx = np.digitize(fi, edges)
        hb = np.array([z[keep & (idx == i)].mean() if np.any(
            keep & (idx == i)) else np.nan for i in range(1, len(edges))])
        fc = edges[:-1] + width / 2
        key = "%s>%s" % ("+".join(SIDE[a] for a in act),
                         SIDE[c] if len(ctx.outs) > 1 else "o")
        hs[key] = (fc, hb)
        spur = spurs_of(ya, z, ph, fi, keep, rr, rate, al, amp)
        rows_csv.append((key, fc, hb, spur))
    rep.say()
    rep.say("response by the sweep's frequency (dB, phase in degrees)")
    for key, fc, hb, spur in rows_csv:
        ok = np.isfinite(hb)
        tau = delay_of(fc[ok], hb[ok], rr, lo=fc[ok][0],
                       hi=fc[ok][min(40, ok.sum() - 1)]) if ok.sum() > 4 \
            else 0.0
        hb2 = hb * np.exp(2j * np.pi * fc / rr * tau)
        rep.put("sweep:" + key, (fc, hb2))
        rows = []
        for p in [p for p in range(int(f1) + 500, int(f2), 500)]:
            i = int((p - f1) / width)
            if 0 <= i < len(hb2) and np.isfinite(hb2[i]):
                rows.append(["%d" % p, "%.2f" % db(abs(hb2[i])),
                             "%.0f" % math.degrees(np.angle(hb2[i]))])
        rep.say("  %s, delay %.2f frames taken out of the phase" % (key,
                                                                    tau))
        rep.table(["Hz", "dB", "deg"], rows)
        dips = local_dips(fc, hb2)
        if dips:
            rep.say("  dips: " + ", ".join("%.0f Hz %.2f dB" % d
                                           for d in dips))
        rep.csv("response-" + key.replace(">", "to").replace("+", ""),
                ["hz", "db", "deg"],
                [[a, db(abs(b)), math.degrees(np.angle(b))]
                 for a, b in zip(fc, hb2) if np.isfinite(b)])
        if spur:
            rep.say("  other components, the strongest in each 0.25 s, "
                    "against the sweep:")
            rep.table(["sweep Hz", "at Hz", "dB", "what"],
                      [["%.0f" % a, "%.0f" % b, "%.1f" % c, w]
                       for a, b, c, w in spur])
            rep.csv("spurs-" + key.replace(">", "to").replace("+", ""),
                    ["sweep_hz", "at_hz", "db", "what"],
                    [[a, b, c, w] for a, b, c, w in spur])


def local_dips(f, h, depth=1.0):
    """notches of the level of more than depth dB under both sides"""
    m = 20 * np.log10(np.abs(h) + 1e-15)
    out = []
    for i in range(8, len(m) - 8):
        if not np.isfinite(m[i]):
            continue
        if m[i] == np.nanmin(m[i - 8:i + 9]) and \
                np.nanmax(m[i - 8:i]) - m[i] > depth and \
                np.nanmax(m[i + 1:i + 9]) - m[i] > depth:
            out.append((f[i], m[i]))
    return out[:6]


def spurs_of(ya, z, ph, fi, keep, rr, rate, al, amp):
    """the strongest component beside the sweep in each quarter second, and
    what it likely is"""
    n = int(0.25 * rr)
    out = []
    w = np.hanning(n)
    fr = np.fft.fftfreq(n, 1.0 / rr)
    for a in range(0, len(ya) - n, n):
        sel = slice(a, a + n)
        if not keep[sel].all():
            continue
        h = z[sel].mean()
        resid = ya[sel] - amp * h * np.exp(1j * (ph[sel] - np.pi / 2))
        s = np.abs(np.fft.fft(resid * w)) / w.sum()
        f0 = al.hz(fi[a + n // 2])
        s[(np.abs(np.abs(fr) - f0) < 300) | (np.abs(fr) > 0.995 * rr / 2)] = 0
        i = int(np.argmax(s))
        lvl = db(s[i] / max(amp * abs(h), 1e-15))
        if lvl < -70:
            continue
        at = abs(fr[i])
        fs_p = al.hz(rate)
        guess = {"image": fold(fs_p - f0, rr), "2nd": fold(2 * f0, rr),
                 "3rd": fold(3 * f0, rr)}
        what = [k for k, g in guess.items() if abs(g - at) < 3 * rr / n]
        out.append((fi[a + n // 2], at, lvl, what[0] if what else ""))
    return out


def periods_of(ctx):
    """the steady periods of a periodic signal in the warped recording"""
    info = ctx.info
    n, k = info["period"], info["periods"]
    b = info["body"][0]
    y = ctx.onto()
    return [y[b + j * n:b + (j + 1) * n] for j in range(1, k - 1)]


def an_periodic(ctx):
    """a multitone: the transfers at its bins from the periods' average,
    the noise and distortion between them, and each period's drift"""
    rep, made = ctx.rep, ctx.made
    rate = made.rate
    info = ctx.info
    n = info["period"]
    b = info["body"][0]
    xs = ctx.x[b + n:b + 2 * n]
    ys = periods_of(ctx)
    if len(ys) < 2:
        rep.say("too few periods in the recording")
        return
    avg = np.mean(ys, axis=0)
    xf = np.fft.rfft(xs, axis=0)
    yf = np.fft.rfft(avg, axis=0)
    yj = np.array([np.fft.rfft(v, axis=0) for v in ys])
    f = np.fft.rfftfreq(n, 1.0 / rate)
    bins = [np.array(info["bins"][s]) for s in (0, 1)]
    act = [s for s in (0, 1) if np.abs(xf[bins[s], s]).max() > 0]
    hs, coh = {}, {}
    for c in ctx.outs:
        for s in act:
            k = bins[s]
            key = "%s>%s" % (SIDE[s], SIDE[c] if len(ctx.outs) > 1 else "o")
            h = np.full(len(f), np.nan, complex)
            h[k] = yf[k, c] / xf[k, s]
            hs[key] = (k, h)
            # the periods' agreement: |mean|^2 over the mean of |each|^2
            num = np.abs(yj[:, k, c].mean(axis=0)) ** 2
            den = (np.abs(yj[:, k, c]) ** 2).mean(axis=0)
            g = np.full(len(f), np.nan)
            g[k] = num / np.maximum(den, 1e-30)
            coh[key] = g
    # the other side's own dither lies on a side's bins too, about 80 dB
    # under a tone: what it brings through its direct path, interpolated
    # from that side's bins, comes off the cross terms
    if len(act) == 2 and len(ctx.outs) == 2:
        for c in ctx.outs:
            for s in act:
                o = 1 - s
                ko, kk = bins[o], bins[s]
                direct_o = hs["%s>%s" % (SIDE[o], SIDE[c])][1][ko]
                if SIDE[o] == SIDE[c]:
                    lead = np.interp(f[kk], f[ko], np.abs(direct_o)) * \
                        np.exp(1j * np.interp(f[kk], f[ko],
                                              np.unwrap(np.angle(direct_o))))
                    key = "%s>%s" % (SIDE[s], SIDE[c])
                    hs[key][1][kk] -= lead * xf[kk, o] / xf[kk, s]
    rep.say()
    rep.say("transfers at the tones, %d periods of %d frames averaged" % (
        len(ys), n))
    keyed = {}
    for key, (k, h) in hs.items():
        tau = delay_of(f[k], h[k], rate)
        keyed[key] = (k, h, tau)
        rep.put("bins:" + key, (f[k], h[k] * np.exp(2j * np.pi * f[k] / rate * tau)))
        rep.put("agree:" + key, coh[key][k])
    pts = [p for p in POINTS if 20 <= p <= min(20000, 0.45 * rate)]
    head, rows = ["Hz"], []
    for key in keyed:
        head += [key + " dB", "deg"]
    head += ["agree " + k for k in keyed if k[0] == k[-1] or
             len(ctx.outs) == 1][:2]
    for p in pts:
        r = ["%g" % p]
        for key, (k, h, tau) in keyed.items():
            sel = k[(f[k] >= p * 2 ** (-1 / 12)) & (f[k] <= p * 2 ** (1 / 12))]
            if not len(sel):
                sel = k[[int(np.argmin(np.abs(f[k] - p)))]]
            v = np.mean(h[sel] * np.exp(2j * np.pi * f[sel] / rate * tau))
            r += ["%.2f" % db(abs(v)), "%.0f" % math.degrees(np.angle(v))]
        for key in [k for k in keyed if k[0] == k[-1] or
                    len(ctx.outs) == 1][:2]:
            k = keyed[key][0]
            sel = k[(f[k] >= p * 2 ** (-1 / 12)) & (f[k] <= p * 2 ** (1 / 12))]
            if not len(sel):
                sel = k[[int(np.argmin(np.abs(f[k] - p)))]]
            r.append("%.4f" % np.nanmean(coh[key][sel]))
        rows.append(r)
    rep.table(head, rows)
    for key, (k, h, tau) in keyed.items():
        if direct(key):
            rep.say("  %s: delay %.2f frames%s" % (
                key, tau, corners(f[k], h[k], 20, 0.45 * rate)))
            continue
        own = [d for d in keyed if direct(d) and d[0] == key[0]]
        if own:
            ko, ho, _t = keyed[own[0]]
            rep.say("  %s: %.1f dB under %s at 1 kHz" % (
                key, level_near(f[ko], ho[ko]) - level_near(f[k], h[k]),
                own[0]))
    csv_cols, head = [], ["hz"]
    for key, (k, h, tau) in keyed.items():
        hh = h * np.exp(2j * np.pi * f / rate * tau)
        csv_cols += [20 * np.log10(np.abs(hh) + 1e-15),
                     np.degrees(np.angle(hh)), coh[key]]
        head += [key.replace(">", "to") + s for s in ("_db", "_deg",
                                                      "_agree")]
    rep.csv("response", head, np.column_stack([f] + csv_cols).tolist())
    # the empty bins: what the tones leave there is distortion and noise
    used = np.zeros(len(f), bool)
    for s in (0, 1):
        used[bins[s]] = True
    used[0] = True
    empty = ~used & (f >= 20) & (f <= min(20000, 0.45 * rate))
    rep.say()
    rep.say("between the tones, in octaves: the empty bins' level against "
            "the tones' bins, each a bin")
    rows = []
    for c in ctx.outs:
        for lo in (20, 40, 80, 160, 320, 640, 1280, 2560, 5120, 10240):
            sel = (f >= lo) & (f < 2 * lo)
            e = np.abs(yf[empty & sel, c]) ** 2
            t = np.abs(yf[used & sel & (f > 0), c]) ** 2
            if not len(e) or not len(t):
                continue
            rows.append([ctx.oname(c), "%d-%d" % (lo, 2 * lo),
                         "%.1f" % pdb(e.mean() / t.mean()),
                         "%.1f" % pdb(2 * e.mean() / (n / 2) ** 2)])
    rep.table(["", "Hz", "dB", "dBFS a bin"], rows)
    drift(ctx, yj, xf, bins, act, f)
    if made.sig.name == "cal":
        boot_method(ctx, hs)


def period_times(ctx, yj, bins, f):
    """each period's time against their mean, from its phase at the tones
    of the first side that plays, and the drift in frames a period"""
    rate = ctx.made.rate
    k = bins[ctx.sides()[0]]
    c = ctx.outs[0]
    ref = yj[:, k, c].mean(axis=0)
    tau = np.array([delay_of(f[k], v / ref, rate, lo=100, hi=10000)
                    for v in yj[:, k, c]])
    slope = np.polyfit(np.arange(len(tau)), tau, 1)[0] if len(tau) > 2 \
        else 0.0
    return tau, slope


def settle_ratio(ctx):
    """the ratio of a periodic signal with no markers, from its periods'
    drift, which the correlation that found it can't see; the alignment
    keeps the middle period where it was"""
    info = ctx.info
    n, k = info["period"], info["periods"]
    f = np.fft.rfftfreq(n, 1.0 / ctx.made.rate)
    bins = [np.array(info["bins"][s]) for s in (0, 1)]
    for _i in range(3):
        yj = np.array([np.fft.rfft(v, axis=0) for v in periods_of(ctx)])
        if len(yj) < 3:
            return
        _tau, slope = period_times(ctx, yj, bins, f)
        eps = -slope / n
        if abs(eps) < 1e-9:
            return
        al = ctx.al
        mid = info["body"][0] + n * k / 2
        rho = al.rho / (1 + eps)
        ctx.al = Align(al.lag + mid * (al.rho - rho), rho, al.r0,
                       "correlation with the signal, the ratio from the "
                       "periods' drift")
        ctx.y = None


def drift(ctx, yj, xf, bins, act, f):
    """each period's time against the average, from its phase at the tones"""
    rep = ctx.rep
    tau, slope = period_times(ctx, yj, bins, f)
    n = ctx.info["period"]
    rep.say()
    rep.say("periods  time of each against their mean, frames: %s" %
            " ".join("%+.3f" % v for v in tau))
    rep.say("         drift %+.4f frames a period, %+.3f ppm" % (
        slope, -slope / n * 1e6))


def boot_method(ctx, hs):
    """the boot calibration as C code would do it: the onset by a level,
    the periods from there with no warp, against the analysis above"""
    rep, made, rec = ctx.rep, ctx.made, ctx.rec
    if rec.rate != made.rate:
        return
    if abs(ctx.al.ppm) > 0.05:
        rep.say("boot     the boot method takes one clock for both ends; "
                "these differ by %+.3f ppm, so it isn't tried" % ctx.al.ppm)
        return
    n, k = ctx.info["period"], ctx.info["periods"]
    x = rec.x
    pre = int(0.05 * rec.rate)
    noise = np.sqrt(np.mean(x[:pre] ** 2)) if len(x) > pre else 0
    lvl = np.sqrt(np.mean(x ** 2))
    thr = max(8 * noise, 0.1 * lvl)
    hit = np.flatnonzero(np.abs(x).max(axis=1) > thr)
    if not len(hit):
        return
    c0 = int(hit[0])
    j = k - 3
    if c0 + n + j * n > len(x):
        rep.say("boot     the recording ends before %d periods" % j)
        return
    avg = np.mean([x[c0 + n + i * n:c0 + 2 * n + i * n] for i in range(j)],
                  axis=0)
    yf = np.fft.rfft(avg, axis=0)
    b = ctx.info["body"][0]
    xf = np.fft.rfft(ctx.x[b + n:b + 2 * n], axis=0)
    worst = 0.0
    for key, (kk, h) in hs.items():
        if not direct(key):
            continue
        s = SIDE.index(key[0])
        c = 0 if key[-1] in "Lo" else 1
        hb = yf[kk, c] / xf[kk, s]
        d = np.abs(20 * np.log10(np.abs(hb) / np.abs(h[kk])))
        worst = max(worst, float(np.percentile(d, 99)))
    rep.put("boot_db", worst)
    rep.say("boot     onset at frame %d by a level of %.1f dBFS; %d periods "
            "from one period later" % (c0, db(thr), j))
    rep.say("         against the aligned analysis, 99 %% of the bins "
            "within %.3f dB" % worst)


def welch(xs, ys, nfft):
    """Welch's spectral matrices of inputs xs and outputs ys: Hann windows
    of nfft, half overlapping. Gxx[f, i, j] sums X_i conj(X_j), Gyx[f, c,
    i] Y_c conj(X_i), Gyy[f, c, d] Y_c conj(Y_d)"""
    w = hann(nfft)
    hop = nfft // 2
    starts = list(range(0, len(xs) - nfft + 1, hop))
    nf = nfft // 2 + 1
    gxx = np.zeros((nf, xs.shape[1], xs.shape[1]), complex)
    gyx = np.zeros((nf, ys.shape[1], xs.shape[1]), complex)
    gyy = np.zeros((nf, ys.shape[1], ys.shape[1]), complex)
    for i in range(0, len(starts), 32):
        idx = np.array(starts[i:i + 32])[:, None] + np.arange(nfft)
        xf = np.fft.rfft(xs[idx] * w[None, :, None], axis=1)
        yf = np.fft.rfft(ys[idx] * w[None, :, None], axis=1)
        gxx += np.einsum("kfi,kfj->fij", xf, np.conj(xf))
        gyx += np.einsum("kfc,kfi->fci", yf, np.conj(xf))
        gyy += np.einsum("kfc,kfd->fcd", yf, np.conj(yf))
    return gxx, gyx, gyy, len(starts)


def transfers(gxx, gyx, gyy):
    """H = Gyx Gxx^-1, the ordinary coherences and each output's multiple
    coherence"""
    nf, nc, ns = gyx.shape
    if ns == 1:
        h = gyx / np.maximum(gxx.real, 1e-30)
    else:
        reg = np.eye(ns)[None] * (np.trace(gxx, axis1=1, axis2=2).real *
                                  1e-12)[:, None, None]
        h = np.swapaxes(np.linalg.solve(
            np.swapaxes(gxx + reg, 1, 2), np.swapaxes(gyx, 1, 2)), 1, 2)
    coh = np.abs(gyx) ** 2 / np.maximum(
        np.einsum("fcc->fc", gyy).real[:, :, None] *
        np.einsum("fii->fi", gxx).real[:, None, :], 1e-30)
    mult = np.einsum("fci,fci->fc", gyx, np.conj(h)).real / np.maximum(
        np.einsum("fcc->fc", gyy).real, 1e-30)
    return h, coh, mult


def nfft_for(rate):
    return 16384 if rate > 30000 else 8192 if rate > 15000 else 4096


def an_welch(ctx, x=None, y=None, label=None, sides=None):
    """the transfers by Welch's method, with the coherences"""
    rep, made = ctx.rep, ctx.made
    rate = made.rate
    b, n = ctx.info["body"]
    y = ctx.onto() if y is None else y
    x = ctx.x if x is None else x
    sides = ctx.sides() if sides is None else sides
    same = len(sides) == 2 and np.array_equal(x[b:b + n, 0], x[b:b + n, 1])
    ins = sides[:1] if same else sides
    nfft = nfft_for(rate)
    xs, ys = x[b:b + n][:, ins], y[b:b + n][:, ctx.outs]
    gxx, gyx, gyy, k = welch(xs, ys, nfft)
    if not k:
        rep.say("the body is shorter than a window of %d" % nfft)
        return
    h, coh, mult = transfers(gxx, gyx, gyy)
    f = np.fft.rfftfreq(nfft, 1.0 / rate)
    hs, cs = {}, {}
    for ci, c in enumerate(ctx.outs):
        for si, s in enumerate(ins):
            key = "%s>%s" % ("L+R" if same else SIDE[s],
                             SIDE[c] if len(ctx.outs) > 1 else "o")
            hs[key] = h[:, ci, si]
            cs[key] = coh[:, ci, si]
    pilot = ctx.info.get("pilot")
    if pilot:
        # the pilot is the same on both sides, which leaves the two inputs
        # one there: those bins are left out
        near = np.abs(f - pilot) < 250
        for d in (hs, cs):
            for key in d:
                d[key] = np.where(near, np.nan, d[key])
    rep.say()
    rep.say("%s: Welch, Hann windows of %d, half overlapping, %d of them; "
            "coherence's bias for none about %.3f" % (
                label or "transfers", nfft, k, 1.0 / k))
    response_report(ctx, "response" if label is None else "vs-ref", f, hs,
                    coh=cs, band=(20.0, 0.48 * min(rate, ctx.rec.rate)))
    if len(ins) == 2:
        rep.say("  multiple coherence, each output from both inputs, at "
                "1 kHz: %s" % ", ".join(
                    "%s %.4f" % (ctx.oname(c), np.interp(1000, f, mult[:, i]))
                    for i, c in enumerate(ctx.outs)))
    for key, cv in [(k, v) for k, v in cs.items() if direct(k)]:
        good = f[(cv > 0.9) & (f >= 20)]
        if len(good):
            bad = f[(f > 1000) & (smooth_line(cv) < 0.9)]
            rep.say("  %s: coherence over 0.9 from %.0f Hz%s" % (
                key, good[0], " to %.0f Hz" % bad[0] if len(bad) else ""))
    if same and len(ctx.outs) == 2:
        between(ctx, f, gyy, rate)
    block_drift(ctx, xs[:, 0], ys, rate)


def smooth_line(v, n=9):
    return np.convolve(v, np.ones(n) / n, mode="same")


def between(ctx, f, gyy, rate):
    """the outputs against each other: level, phase and delay"""
    rep = ctx.rep
    h = gyy[:, 1, 0] / np.maximum(gyy[:, 0, 0].real, 1e-30)
    tau = delay_of(f, h, rate, lo=200, hi=5000)
    pts = [p for p in POINTS if 20 <= p <= 0.45 * rate]
    v = smooth_at(f, h, pts)
    rep.say()
    rep.say("right output against the left: delay %.3f frames (%.2f us)" % (
        tau, tau / rate * 1e6))
    rep.table(["Hz", "dB", "deg"],
              [["%g" % p, "%.3f" % db(abs(a)),
                "%.2f" % math.degrees(np.angle(a))] for p, a in zip(pts, v)])
    rep.csv("between", ["hz", "db", "deg"],
            [[a, db(abs(b)), math.degrees(np.angle(b))]
             for a, b in zip(f, h) if a >= 20])


def block_drift(ctx, x, ys, rate):
    """the delay in blocks of 0.74 s, which a clock that wanders moves"""
    n = 32768 if rate > 30000 else 16384
    out = []
    for a in range(0, len(x) - n, n):
        # the peak of the correlation, near no lag
        r = np.fft.irfft(np.fft.rfft(ys[a:a + n, 0], 2 * n) *
                         np.conj(np.fft.rfft(x[a:a + n], 2 * n)), 2 * n)
        r = np.concatenate([r[-32:], r[:33]])
        i = int(np.argmax(np.abs(r)))
        out.append(refine_peak(np.abs(r), i) - 32)
    if len(out) > 2:
        out = np.array(out) - out[0]
        ctx.rep.say("  delay over the file in blocks of %.2f s, against "
                    "the first, us: %s" % (n / rate, " ".join(
                        "%+.2f" % (v / rate * 1e6) for v in out)))


def an_mls(ctx):
    rep, rate = ctx.rep, ctx.made.rate
    info = ctx.info
    n, k = info["period"], info["periods"]
    b = info["body"][0]
    y = ctx.onto()
    x = ctx.x[b + n:b + 2 * n, 0]
    xf = np.fft.fft(x)
    f = np.fft.fftfreq(n, 1.0 / rate)
    keep = f >= 0
    hs, irs = {}, {}
    for c in ctx.outs:
        avg = np.mean([y[b + j * n:b + (j + 1) * n, c]
                       for j in range(1, k - 1)], axis=0)
        h = np.fft.fft(avg) / xf
        key = "L+R>%s" % (SIDE[c] if len(ctx.outs) > 1 else "o")
        hs[key] = h[keep]
        irs[key] = np.real(np.fft.ifft(h))
    rep.say()
    rep.say("by circular correlation, %d periods of %d averaged" % (k - 2, n))
    response_report(ctx, "response", f[keep], hs,
                    band=(20.0, 0.48 * min(rate, ctx.rec.rate)))
    ir_csv(ctx, irs, 0)


def ir_csv(ctx, irs, pre):
    rate = ctx.made.rate
    n = len(next(iter(irs.values())))
    m = min(n, int(0.2 * rate))
    cols = [np.roll(v, pre)[:m] for v in irs.values()]
    t = (np.arange(m) - pre) / rate * 1e3
    peak = {k: (int(np.argmax(np.abs(v))), float(v[np.argmax(np.abs(v))]))
            for k, v in irs.items()}
    for k, (i, v) in peak.items():
        ctx.rep.say("  %s: impulse response's peak %.4f at %.3f ms" % (
            k, v, (i if i < n // 2 else i - n) / rate * 1e3))
    ctx.rep.csv("ir", ["ms"] + [k.replace(">", "to").replace("+", "")
                                for k in irs],
                np.column_stack([t] + cols).tolist())


def an_impulses(ctx):
    rep, rate = ctx.rep, ctx.made.rate
    info = ctx.info
    n, k, amp = info["period"], info["periods"], info["amp"] / ts.FULL
    b = info["body"][0]
    y = ctx.onto()
    pre = int(0.002 * rate)
    hs, irs = {}, {}
    f = np.fft.rfftfreq(n, 1.0 / rate)
    for c in ctx.outs:
        segs = [np.roll(y[b + j * n - pre:b + (j + 1) * n - pre, c], -pre)
                for j in range(1, k) if b + j * n - pre >= 0]
        avg = np.mean(segs, axis=0) / amp
        key = "L+R>%s" % (SIDE[c] if len(ctx.outs) > 1 else "o")
        hs[key] = np.fft.rfft(avg)
        irs[key] = avg
    rep.say()
    rep.say("by synchronous averaging, %d clicks" % (k - 1))
    response_report(ctx, "response", f, hs,
                    band=(20.0, 0.48 * min(rate, ctx.rec.rate)))
    ir_csv(ctx, irs, pre)
    # each click's time in the raw recording
    rr = ctx.rec.rate
    c = ctx.outs[0]
    w = int(0.004 * rr)
    temp = np.mean([ctx.raw(a - w / ctx.al.rho, a + 4 * w / ctx.al.rho, c)
                    [:5 * w] for a in info["at"][1:6]], axis=0)
    times = []
    for a in info["at"]:
        seg = ctx.raw(a - 3 * w / ctx.al.rho, a + 6 * w / ctx.al.rho, c)
        if len(seg) < len(temp) + 2:
            times.append(np.nan)
            continue
        r = np.correlate(seg, temp, mode="valid")
        i = int(np.argmax(np.abs(r)))
        d = 0.0
        if 0 < i < len(r) - 1:
            y0, y1, y2 = np.abs(r[i - 1:i + 2])
            if y0 - 2 * y1 + y2 < 0:
                d = 0.5 * (y0 - y2) / (y0 - 2 * y1 + y2)
        times.append(int(round(ctx.al.at(a - 3 * w / ctx.al.rho))) + i + d)
    timeline(ctx, np.array(info["at"], float), np.array(times), "clicks")


def timeline(ctx, s, t, what):
    """a line through events' recorded times against their frames: the
    clocks' ratio and each event's time error"""
    rep = ctx.rep
    ok = np.isfinite(t)
    if ok.sum() < 3:
        rep.say("%s: too few found" % what)
        return
    rr = ctx.rec.rate
    a, b0 = np.polyfit(s[ok], t[ok], 1)
    ppm = (ctx.al.r0 / a - 1) * 1e6
    err = (t[ok] - (a * s[ok] + b0)) / rr * 1e6
    rep.put(what, {"ppm": ppm, "rms_us": float(np.sqrt(np.mean(err ** 2)))})
    rep.say()
    rep.say("%s    %d in a line: the player against the recorder %+.3f ppm" %
            (what, ok.sum(), ppm))
    rep.say("         time error against the line, rms %.3f us, at most "
            "%.3f us" % (np.sqrt(np.mean(err ** 2)), np.abs(err).max()))
    rep.csv("timing-" + what, ["signal_s", "error_us"],
            np.column_stack([s[ok] / ctx.made.rate, err]).tolist())


def steps_of(ctx):
    return ctx.info.get("steps", [])


def an_tones(ctx):
    """each steady tone: its gain and phase, THD+N, the harmonics, and the
    lines beside it"""
    rep, made, al = ctx.rep, ctx.made, ctx.al
    rate, rr = made.rate, ctx.rec.rate
    kind = made.sig.params["steps"]
    if kind == "pair":
        return separation(ctx)
    skip = int(0.04 * rate)
    rows, csv_rows, lines = [], [], []
    for st in steps_of(ctx):
        f = st["f"][0]
        a0 = st["at"] + st["ramp"] + skip
        a1 = st["at"] + st["ramp"] + st["len"] - int(0.01 * rate)
        for c in ctx.outs:
            x = ctx.raw(a0, a1, c)
            if len(x) < 64:
                continue
            fr = al.hz(f)
            alias = fr > 0.5 * rr
            fit_f = fold(fr, rr) if alias else fr
            m = tone_measures(x, rr, fit_f)
            gain = db(m["amp"] / st["amp"])
            # the signal's own phase at the middle of what was fitted
            s_mid = (int(round(al.at(a0))) + len(x) / 2 - 0.5 - al.lag) / \
                al.rho
            ref_ph = 2 * np.pi * f * (s_mid - st["at"]) / rate
            dph = (math.degrees(m["phase"] - ref_ph) + 180) % 360 - 180
            hs = {h[0]: h[2] for h in m["harm"]}
            rows.append(["%g" % f, ctx.oname(c), "%.1f" % st["level"],
                         "%.3f" % gain, "%.1f" % dph,
                         "%.1f" % m["thdn"], "%.1f" % m["thdna"]] +
                        ["-" if hs.get(k) is None else "%.1f" % hs[k]
                         for k in range(2, 6)] +
                        ["alias" if alias else ""])
            csv_rows.append([f, c, st["level"], gain, dph, m["thdn"],
                             m["thdna"]] + [hs.get(k) if hs.get(k) is not None
                                            else float("nan")
                                            for k in range(2, 6)])
            if kind in ("list", "fraction") and made.sig.name != "dr60":
                fs, v = spectrum(x, rr)
                pk = top_peaks(fs, v, 20, 0.495 * rr, 4, 15,
                               skip=[fit_f])
                for at, a in pk:
                    lines.append(["%g" % f, ctx.oname(c), "%.0f" % at,
                                  "%.1f" % db(a), "%.1f" % db(a / max(
                                      m["amp"], 1e-15)), label(at, f, al,
                                                               rate, rr)])
    rep.say()
    rep.say("tones (gain in dB, phase in degrees against the signal, THD+N "
            "and harmonics in dB under the tone)")
    rep.put("tones", csv_rows)
    rep.table(["Hz", "", "dBFS", "gain", "deg", "THD+N", "A-wtd", "H2",
               "H3", "H4", "H5", ""], rows)
    rep.csv("tones", ["hz", "out", "level", "gain_db", "deg", "thdn_db",
                      "thdn_a_db", "h2_db", "h3_db", "h4_db", "h5_db"],
            csv_rows)
    if lines:
        rep.say()
        rep.say("other lines in each tone's recording")
        rep.table(["tone Hz", "", "at Hz", "dBFS", "dB re tone", "what"],
                  lines)
        rep.csv("lines", ["tone_hz", "out", "at_hz", "dbfs", "db_re_tone",
                          "what"], lines)
    if kind == "levels":
        linearity(ctx, csv_rows)
    if made.sig.name == "dr60" and csv_rows:
        r = csv_rows[0]
        rep.say()
        rep.say("dynamic range (AES17): %.1f dB, A-weighted %.1f dB" % (
            60 - r[5], 60 - r[6]))


def label(at, f, al, rate, rr):
    """what a line at `at` Hz in a recording of a tone at f likely is"""
    fr = al.hz(f)
    fs = al.hz(rate)
    guesses = [("tone", fold(fr, rr)), ("image", fold(fs - fr, rr)),
               ("image+", fold(fs + fr, rr))]
    guesses += [("H%d" % k, fold(k * fr, rr)) for k in range(2, 6)]
    for name, g in guesses:
        if abs(g - at) < max(2.0, 2e-4 * rr):
            return name
    return ""


MARGIN = 3.0               # the rule's margin under every clip point, dB


def linearity(ctx, rows):
    """the gain at each level against the small-signal gain, and the clip
    point: the lowest step whose gain leaves it by 0.1 dB, or whose
    harmonics pass -40 dB and rise 10 dB over their median from -30 to -14
    dBFS; then the rule's highest peak for a test signal"""
    rep = ctx.rep
    for c in ctx.outs:
        mine = sorted([r for r in rows if r[1] == c], key=lambda r: r[2])
        small = [r[3] for r in mine if -40 <= r[2] <= -20]
        if not small:
            continue
        g0 = float(np.median(small))
        thd = [db(math.sqrt(sum(10 ** (h / 10) for h in r[7:11]
                                if np.isfinite(h)))) for r in mine]
        ref = [t for r, t in zip(mine, thd) if -30 <= r[2] <= -14]
        tref = float(np.median(ref)) if ref else -200.0
        rep.say()
        rep.say("levels, %s: the gain at each level against the gain of "
                "%.3f dB at -40 to -20 dBFS, and the harmonics (dB)" % (
                    ctx.oname(c), g0))
        rep.table(["dBFS", "gain", "THD"], [
            ["%g" % r[2], "%+.3f" % (r[3] - g0), "%.1f" % t]
            for r, t in zip(mine, thd)])
        top = mine[-1][2]
        if top < -6:
            continue
        clip, why = None, ""
        for r, t in zip(mine, thd):
            if r[2] < -30:
                continue
            if abs(r[3] - g0) > 0.1:
                clip, why = r[2], "the gain leaves it by %+.2f dB" % (
                    r[3] - g0)
                break
            if t > -40 and t > tref + 10:
                clip, why = r[2], "the harmonics reach %.1f dB" % t
                break
        found = clip is not None
        if not found:
            rep.say("  no stage clips up to %g dBFS in, %.1f dBFS at the "
                    "recorder" % (top, top + g0))
            clip = top
        else:
            rep.say("  a stage starts to clip at %g dBFS in, %.1f dBFS at "
                    "the recorder: %s" % (clip, clip + g0, why))
        peak = min(clip - MARGIN, -MARGIN - g0)
        rep.put("level:%d" % c, {"g0": g0, "peak": peak,
                                 "clip": clip if found else None})
        rep.say("  the rule: a test signal's true peak may reach %.1f dBFS "
                "at the player, %g dB under the clip point and under the "
                "recorder's full scale" % (peak, MARGIN))
        at_rec = clip + g0
        if at_rec > -1:
            rep.say("  the recorder clips first: a lower record gain moves "
                    "the clip point up by as much")
        elif at_rec < -MARGIN:
            rep.say("  %s: the record gain may rise by up to %.1f dB before "
                    "that comes within %g dB of the recorder's full scale" % (
                        "a stage before the recorder clips first" if found
                        else "nothing clips up to the player's full scale",
                        -MARGIN - at_rec, MARGIN))


def separation(ctx):
    """each recorded channel's level of each side's tone"""
    rep, al = ctx.rep, ctx.al
    rate, rr = ctx.made.rate, ctx.rec.rate
    st = steps_of(ctx)[0]
    a0 = st["at"] + st["ramp"] + int(0.1 * rate)
    a1 = st["at"] + st["ramp"] + st["len"] - int(0.05 * rate)
    rows, hears = [], []
    for c in range(ctx.rec.x.shape[1]):
        x = ctx.raw(a0, a1, c)
        gl, gr = fit_tones(x, rr, [al.hz(st["f"][0]), al.hz(st["f"][1])])
        dl, dr = db(gl / st["amp"]), db(gr / st["amp"])
        rows.append(["rec %d" % c, "%.1f" % dl, "%.1f" % dr,
                     "%.1f" % abs(dl - dr)])
        hears.append("rec %d hears %s" % (
            c, "the left side" if dl > dr + 20 else "the right side"
            if dr > dl + 20 else "both sides"))
    rep.say()
    rep.say("separation: each side's tone in each recorded channel, dB "
            "against the signal, and the difference")
    rep.put("separation", [(float(r[1]), float(r[2])) for r in rows])
    rep.table(["", "left", "right", "apart"], rows)
    rep.say("  " + "; ".join(hears))


def an_silence(ctx):
    """the noise floor: its level, A-weighted level, bands, DC and lines"""
    rep, made = ctx.rep, ctx.made
    rr = ctx.rec.rate
    b, n = ctx.info["body"]
    s0, s1 = b + int(0.1 * made.rate), b + n - int(0.1 * made.rate)
    hi = min(20000.0, 0.45 * rr)
    rows, lines, bands = [], [], []
    ref = ctx.x[s0:s1, 0]
    rep.say()
    if np.any(ref):
        rep.say("the file's own floor there: %.1f dBFS from 20 Hz to %.0f "
                "kHz" % (db(math.sqrt(2) * band_rms(ref, made.rate, 20, hi)),
                         hi / 1000))
    for c in ctx.outs:
        x = ctx.raw(s0, s1, c)
        dc = float(np.mean(x))
        lvl = db(math.sqrt(2) * band_rms(x - dc, rr, 20, hi))
        lva = db(math.sqrt(2) * band_rms(x - dc, rr, 20, hi, "A"))
        rep.put("noise:%d" % c, (lvl, lva, dc))
        rows.append([ctx.oname(c), "%.1f" % lvl, "%.1f" % lva,
                     "%+.1f" % (dc * ts.FULL)])
        nf = 16384 if rr > 30000 else 8192
        gxx, _g, _h, k = welch((x - dc)[:, None], (x - dc)[:, None], nf)
        w = hann(nf)
        p = gxx[:, 0, 0].real / k / (w.sum() ** 2) * 2
        f = np.fft.rfftfreq(nf, 1.0 / rr)
        enbw = (w * w).sum() * nf / w.sum() ** 2
        for lo in ts.THIRDS:
            if lo > hi:
                break
            sel = (f >= lo * 2 ** (-1 / 6)) & (f < lo * 2 ** (1 / 6))
            if sel.any():
                bands.append([lo, c, pdb(2 * p[sel].sum() / enbw)])
        sel = (f >= 20) & (f <= hi)
        idx = np.flatnonzero(sel)
        for i in idx[np.argsort(p[idx])[::-1]]:
            loc = p[max(i - 40, 0):i + 41]
            if p[i] < 10 * np.median(loc) or len(lines) >= 10 * (c + 1):
                break
            if p[i] < p[i - 1] or p[i] < p[i + 1]:
                continue
            lines.append([ctx.oname(c), "%.1f" % f[i], "%.1f" % pdb(2 * p[i]),
                          "%.1f" % pdb(p[i] / np.median(loc))])
    rep.put("lines", lines)
    rep.say("noise floor (dBFS from 20 Hz to %.0f kHz, DC in LSB)" % (
        hi / 1000))
    rep.table(["", "level", "A-wtd", "DC"], rows)
    if lines:
        rep.say("lines over the floor (dBFS as a tone, dB over the local "
                "floor)")
        rep.table(["", "Hz", "dBFS", "over"], lines)
    rep.csv("bands", ["hz", "out", "dbfs"], bands)


def an_lsb(ctx):
    rep, al = ctx.rep, ctx.al
    rr = ctx.rec.rate
    rows = []
    for c in ctx.outs:
        top_gain = None
        for st in reversed(steps_of(ctx)):
            if not np.any(ctx.x[st["at"]:st["at"] + st["len"]]):
                continue
            x = ctx.raw(st["at"] + int(0.1 * ctx.made.rate),
                        st["at"] + st["len"] - int(0.05 * ctx.made.rate), c)
            fr = al.hz(st["f"])
            amp, _p, _f, r = fit_sine(x, rr, fr, refine=False)
            a3, _p, _f, _r = fit_sine(r, rr, fold(3 * fr, rr), refine=False)
            a2, _p, _f, _r = fit_sine(r, rr, fold(2 * fr, rr), refine=False)
            want = 4 / math.pi * st["lsb"] / ts.FULL
            err = np.std(r) * math.sqrt(2.0 / len(x))
            g = db(amp / want)
            if top_gain is None:
                top_gain = g
            rows.append([ctx.oname(c), "%d" % st["lsb"], "%.2f" % g,
                         "%+.2f" % (g - top_gain),
                         "%.2f" % (8.686 * err / max(amp, 1e-15)),
                         "%.1f" % db(a3 / max(amp, 1e-15)),
                         "%.1f" % db(a2 / max(amp, 1e-15))])
    rep.say()
    rep.say("squares (gain of the fundamental against 4/pi of the square, "
            "against the largest, and the fit's standard error, dB)")
    rep.put("lsb", rows)
    rep.table(["", "LSB", "gain", "vs top", "+-", "H3", "H2"], rows)
    rep.say("  a square's third harmonic is -9.5 dB of its fundamental")


def an_dc(ctx):
    rep = ctx.rep
    rate = ctx.made.rate
    rows = []
    pl = ctx.info["plateaus"]
    y = ctx.onto()
    tail = int(0.4 * rate)
    for c in ctx.outs:
        prev, before = 0.0, None
        for st in pl:
            step = (st["code"] - prev) / ts.FULL
            prev = st["code"]
            a, n = st["at"], st["len"]
            # the level just before the edge, settled
            if before is None:
                before = float(np.median(y[a - int(0.2 * rate):a, c]))
            seg = y[a + int(0.001 * rate):a + n, c] - before
            end = float(np.median(seg[-tail:]))
            nxt = before + end
            if not step:
                before = nxt
                continue
            d0 = float(np.mean(seg[:int(0.002 * rate)]))
            tau_s = float("nan")
            d = seg - end
            if abs(d0 - end) > 0.05 * abs(d0) and abs(d0) > 1e-6:
                ok = (np.sign(d) == np.sign(d0 - end)) & \
                    (np.abs(d) > 0.05 * abs(d0 - end))
                run = np.flatnonzero(~ok)
                stop = run[0] if len(run) else len(d)
                if stop > 8:
                    t = np.arange(stop) / rate
                    k = np.polyfit(t, np.log(np.abs(d[:stop])), 1)[0]
                    if k < 0:
                        tau_s = -1 / k
            rows.append([ctx.oname(c), "%+.3f" % step, "%.4f" % (d0 / step),
                         "%.1f" % (tau_s * 1e3) if np.isfinite(tau_s)
                         else "-",
                         "%.3f" % (1 / (2 * math.pi * tau_s))
                         if np.isfinite(tau_s) else "-",
                         "%.3f" % (end / d0 if d0 else 0),
                         "%+.1f" % (before * ts.FULL)])
            before = nxt
    rep.say()
    rep.say("DC steps: the step's start against the input, the time "
            "constant of its decay, the corner that gives, the share left "
            "at the plateau's end and the level before the step in LSB")
    rep.put("dc", rows)
    rep.table(["", "step", "gain", "ms", "Hz", "left", "before"], rows)
    rep.say("  a path that passes DC leaves near 1; one with a high-pass "
            "of one pole decays to 1/e in the time constant")


def an_imd(ctx):
    rep, al = ctx.rep, ctx.al
    rr = ctx.rec.rate
    info = ctx.info
    f1, f2 = al.hz(info["f1"]), al.hz(info["f2"])
    b, n = info["body"]
    s0, s1 = b + int(0.2 * ctx.made.rate), b + n - int(0.2 * ctx.made.rate)
    for c in ctx.outs:
        rows = []
        x = ctx.raw(s0, s1, c)
        x = x[:1 << int(math.log2(max(len(x), 2)))]
        f, v = spectrum(x, rr)
        a1, a2 = level_at(f, v, f1), level_at(f, v, f2)
        if info["kind"] == "smpte":
            prods = [("f2-%df1" % k, f2 - k * f1) for k in (1, 2, 3)] + \
                [("f2+%df1" % k, f2 + k * f1) for k in (1, 2, 3)] + \
                [("2f1", 2 * f1), ("3f1", 3 * f1)]
            refa = a2
        else:
            prods = [("f2-f1", f2 - f1), ("2f1-f2", 2 * f1 - f2),
                     ("2f2-f1", 2 * f2 - f1), ("3f1-2f2", 3 * f1 - 2 * f2),
                     ("3f2-2f1", 3 * f2 - 2 * f1), ("f1+f2", f1 + f2),
                     ("2f1", 2 * f1), ("2f2", 2 * f2)]
            refa = a1
        tot = 0.0
        for name, fp in prods:
            at = fold(fp, rr)
            a = level_at(f, v, at)
            if name.startswith("f2") or name.startswith("2f1-") or \
                    name.startswith("2f2-") or name.startswith("3f"):
                tot += a * a
            rows.append([ctx.oname(c), name, "%.0f" % at,
                         "%.1f" % db(a), "%.1f" % db(a / max(refa, 1e-15)),
                         "folded" if at != fp else ""])
        rep.say()
        rep.say("%s: f1 %.1f dBFS, f2 %.1f dBFS; intermodulation %.1f dB "
                "(%.4f %%)" % (ctx.oname(c), db(a1), db(a2),
                               db(math.sqrt(tot) / max(refa, 1e-15)),
                               100 * math.sqrt(tot) / max(refa, 1e-15)))
        rep.put("imd:%d" % c, {r[1]: float(r[4]) for r in rows})
        rep.table(["", "product", "Hz", "dBFS", "dB re tone", ""], rows)


def an_jtest(ctx):
    rep, al = ctx.rep, ctx.al
    rr = ctx.rec.rate
    info = ctx.info
    fc, fm = al.hz(info["fc"]), al.hz(info["fm"])
    b, n = info["body"]
    s0, s1 = b + int(0.2 * ctx.made.rate), b + n - int(0.2 * ctx.made.rate)
    # the file's own lines: the LSB square's odd harmonics fall on the
    # tone's sidebands, since the tone is the square's 48th
    rate = ctx.made.rate
    xr = ctx.x[s0:s1, ctx.played()[0]]
    xr = xr[:1 << int(math.log2(max(len(xr), 2)))]
    fr_, vr = spectrum(xr, rate)
    car_r = level_at(fr_, vr, info["fc"])
    for c in ctx.outs:
        x = ctx.raw(s0, s1, c)
        x = x[:1 << int(math.log2(max(len(x), 2)))]
        f, v = spectrum(x, rr)
        car = level_at(f, v, fc)
        rows, worst = [], 0.0
        for k in range(1, 17):
            got = [level_at(f, v, fc + sg * k * fm, 1) / max(car, 1e-15)
                   for sg in (-1, 1)]
            own = [level_at(fr_, vr, info["fc"] + sg * k * info["fm"], 1) /
                   max(car_r, 1e-15) for sg in (-1, 1)]
            # what the path added to the file's own line, in quadrature
            extra = [math.sqrt(max(g * g - o * o, 0)) for g, o in
                     zip(got, own)]
            j = sum(extra) / (2 * math.pi * fc)
            worst = max(worst, j)
            rows.append(["%d" % k, "%.1f" % (k * fm), "%.1f" % db(own[0]),
                         "%.1f" % db(got[0]), "%.1f" % db(own[1]),
                         "%.1f" % db(got[1]), "%.1f" % (j * 1e12)])
        rep.say()
        rep.say("%s: the tone at %.2f Hz, %.2f dBFS; its sidebands at "
                "multiples of %.4f Hz, in the file and recorded (dBc), and "
                "the peak jitter that what the path added would be as pure "
                "phase modulation" % (ctx.oname(c), fc, db(car), fm))
        rep.put("jtest:%d" % c, {int(r[0]): float(r[6]) for r in rows})
        rep.table(["k", "offset Hz", "file -", "rec -", "file +", "rec +",
                   "ps"], rows)
        skip = [fc + sg * k * fm for k in range(0, 17) for sg in (-1, 1)]
        other = top_peaks(f, v, fc - 3000, fc + 3000, 5, 15, skip=skip)
        if other:
            rep.say("  other lines near the tone: " + ", ".join(
                "%+.1f Hz %.1f dBc" % (a - fc, db(b / max(car, 1e-15)))
                for a, b in other))
        floor = np.median(v[(f > fc - 3000) & (f < fc + 3000)])
        rep.say("  the floor around it %.1f dBc a bin; the largest pair %.1f "
                "ps" % (db(floor / max(car, 1e-15)), worst * 1e12))


def pilot_phase(x, rr, fp, block):
    """the pilot's phase in blocks: times of the blocks' middles, phase in
    cycles against a reference at fp, amplitudes"""
    w = hann(block)
    t, ph, amp = [], [], []
    n = np.arange(block)
    for a in range(0, len(x) - block + 1, block):
        ref = np.exp(-2j * np.pi * fp * (a + n) / rr)
        z = np.sum(x[a:a + block] * w * ref)
        t.append((a + block / 2) / rr)
        ph.append(np.angle(z) / (2 * np.pi))
        amp.append(2 * abs(z) / w.sum())
    return np.array(t), np.unwrap(np.array(ph) * 2 * np.pi) / (2 * np.pi), \
        np.array(amp)


def an_pilot(ctx, f0=None, s0=None, s1=None, near=(10.0, 1000.0)):
    """the clocks from a pilot's phase, as es1869clock reads it"""
    rep, al = ctx.rep, ctx.al
    rr = ctx.rec.rate
    info = ctx.info
    f0 = f0 or info["f"]
    if s0 is None:
        s0, n = info.get("steady", info["body"])
        s1 = s0 + n
    for c in ctx.outs:
        x = ctx.raw(s0, s1, c)
        if len(x) < rr:
            rep.say("the pilot is shorter than a second in the recording")
            return
        guess = al.hz(f0)
        f, v = spectrum(x[:1 << int(math.log2(len(x)))], rr)
        k = int(np.argmax(np.where(np.abs(f - guess) < 50, v, 0)))
        fp = f[k]
        if 0 < k < len(v) - 1:
            a, b, cc = np.log(v[k - 1:k + 2] + 1e-30)
            if a - 2 * b + cc < 0:
                fp = (k + 0.5 * (a - cc) / (a - 2 * b + cc)) * (f[1] - f[0])
        block = int(round(0.1 * rr))
        t, ph, amp = pilot_phase(x, rr, fp, block)
        a1, a0 = np.polyfit(t, ph, 1)
        fobs = fp + a1
        ppm = (fobs / f0 * ctx.made.rate / ctx.made.rate - 1) * 1e6
        ppm = (fobs / f0 - 1) * 1e6
        xe = (ph - (a1 * t + a0)) / fobs
        rep.put("pilot:%d" % c, {"ppm": ppm, "tie_ns": float(
            np.sqrt(np.mean(xe ** 2)) * 1e9)})
        rep.say()
        rep.say("pilot %s: %.4f Hz at %.1f dBFS; the player against the "
                "recorder %+.3f ppm" % (ctx.oname(c), fobs,
                                        db(np.median(amp)), ppm))
        rep.say("  time error against the line, in blocks of 0.1 s: rms "
                "%.2f ns, from %+.2f to %+.2f ns" % (
                    np.sqrt(np.mean(xe ** 2)) * 1e9, xe.min() * 1e9,
                    xe.max() * 1e9))
        dev = adev(xe, (1, 2, 5, 10, 20, 50))
        if dev:
            rep.say("  Allan deviation: " + ", ".join(
                "%g s %.2e" % (m * 0.1, v / 0.1) for m, v in dev))
        jitter(ctx, x, rr, fobs, *near)
        rep.csv("pilot-%s" % (SIDE[c] if len(ctx.outs) > 1 else "o"),
                ["s", "error_ns", "dbfs"],
                np.column_stack([t, xe * 1e9, 20 * np.log10(amp + 1e-15)])
                .tolist())


def jitter(ctx, x, rr, fp, lo=10.0, hi=1000.0):
    """the phase noise from lo to hi Hz off the pilot, as jitter"""
    n = len(x)
    s = np.fft.rfft(x)
    f = np.fft.rfftfreq(n, 1.0 / rr)
    s[(f < fp - 2 * hi) | (f > fp + 2 * hi)] = 0
    z = np.zeros(n, complex)
    z[:len(s)] = s
    z[1:(n + 1) // 2] *= 2
    za = np.fft.ifft(z) * np.exp(-2j * np.pi * fp * np.arange(n) / rr)
    ph = np.unwrap(np.angle(za))
    ph -= np.polyval(np.polyfit(np.arange(n), ph, 1), np.arange(n))
    nf = next_pow2(int(rr)) // 2
    gxx, _a, _b, k = welch(ph[:, None], ph[:, None], nf)
    if not k:
        return
    w = hann(nf)
    psd = gxx[:, 0, 0].real / k / (w * w).sum() / rr * 2
    fo = np.fft.rfftfreq(nf, 1.0 / rr)
    sel = (fo >= lo) & (fo <= hi)
    rad = math.sqrt(psd[sel].sum() * (fo[1] - fo[0]))
    ctx.rep.put("jitter_ns", rad / (2 * math.pi * fp) * 1e9)
    ctx.rep.say("  phase noise from %g to %g Hz off the tone: %.3e rad rms, "
                "%.2f ns of jitter, an upper bound since the path's noise "
                "counts too" % (lo, hi, rad,
                                rad / (2 * math.pi * fp) * 1e9))


def an_chirps(ctx):
    al = ctx.al
    info = ctx.info
    rr = ctx.rec.rate
    big_t = int(round(ts.MARK_S * ctx.made.rate)) / ctx.made.rate
    m = template(rr, big_t, al.r0 / al.rho - 1)
    _gains, norms = correlate(highpass(ctx.rec.x, rr), m)
    sep = info.get("mark_sep", 0)
    for side, off in (("left", 0), ("right", sep)):
        if side == "right" and not sep:
            continue
        s = np.array(info["at"], float) + off
        times = []
        for v in s:
            t0 = al.at(v)
            a, b = int(t0 - 0.002 * rr), int(t0 + 0.002 * rr)
            best = None
            for c in range(ctx.rec.x.shape[1]):
                if a < 0 or b > norms.shape[1]:
                    continue
                seg = norms[c, a:b]
                i = int(np.argmax(seg))
                if best is None or seg[i] > best[0]:
                    best = (seg[i], c, a + i)
            if best is None or best[0] < THR:
                times.append(np.nan)
                continue
            _v, c, i = best
            times.append(refine_peak(norms[c], i))
        timeline(ctx, s, np.array(times), "chirps-" + side)


def refine_peak(e, i):
    if 0 < i < len(e) - 1:
        a, b, c = e[i - 1:i + 2]
        if a - 2 * b + c < 0:
            return i + 0.5 * (a - c) / (a - 2 * b + c)
    return float(i)


def an_dual(ctx):
    """when each DAC plays the same frame: Audio 1's chirps up, Audio 2's
    down, found apart in one recording"""
    rep, al = ctx.rep, ctx.al
    rr = ctx.rec.rate
    info = ctx.info
    x = highpass(ctx.rec.x[:, ctx.outs[0]][:, None], rr)
    big_t = int(round(ts.MARK_S * ctx.made.rate)) / ctx.made.rate
    eps = al.r0 / al.rho - 1
    _g1, n1 = correlate(x, template(rr, big_t, eps))
    _g2, n2 = correlate(x, template(rr, big_t, eps, True))
    rows, skew = [], []
    for v in info["at"]:
        t0 = al.at(v)
        a, b = int(t0 - 0.03 * rr), int(t0 + 0.03 * rr)
        if a < 0 or b > n1.shape[1]:
            continue
        i1 = a + int(np.argmax(n1[0, a:b]))
        i2 = a + int(np.argmax(n2[0, a:b]))
        t1, t2 = refine_peak(n1[0], i1), refine_peak(n2[0], i2)
        sk = (t1 - t2) / al.rho
        skew.append(sk)
        rows.append(["%.2f" % (v / ctx.made.rate), "%.3f" % n1[0, i1],
                     "%.3f" % n2[0, i2], "%+.3f" % sk])
    rep.say()
    rep.say("Audio 1 against Audio 2, frames (positive: Audio 1 later)")
    rep.table(["s", "A1 corr", "A2 corr", "skew"], rows)
    if skew:
        rep.put("skew", float(np.mean(skew)))
        rep.say("  mean %+.3f frames, spread %.3f" % (np.mean(skew),
                                                      np.std(skew)))
    rep.csv("skew", ["s", "skew_frames"],
            [[float(r[0]), float(r[3])] for r in rows])


def an_bursts(ctx):
    rep = ctx.rep
    rate = ctx.made.rate
    y = ctx.onto()
    rows = []
    for c in ctx.outs:
        for bu in ctx.info["bursts"]:
            f, cen, sig = bu["f"], bu["c"], bu["sigma"]
            h = int(5 * sig)
            a, b = int(cen - h), int(cen + h + 1)
            act = [s for s in (0, 1) if active(ctx.x[a:b, s])]
            if not act or a < 0:
                continue
            ex, ey = envelope(ctx.x[a:b, act[0]], f, rate), \
                envelope(y[a:b, c], f, rate)
            tx, ty = top_of(np.abs(ex)), top_of(np.abs(ey))
            gd = (ty - tx) / rate
            k = int(round(ty))
            g = db(np.abs(ey).max() / np.abs(ex).max())
            ph = math.degrees(np.angle(ey[k] / ex[int(round(tx))])) \
                if 0 <= k < len(ey) else 0.0
            rows.append([ctx.oname(c), "%g" % f, "%.1f" % (gd * 1e6),
                         "%.2f" % g, "%.0f" % ph])
    rep.say()
    rep.say("bursts (group delay from the envelope's top, us; level dB; "
            "phase at the top, degrees)")
    rep.put("bursts", rows)
    rep.table(["", "Hz", "us", "dB", "deg"], rows)


def envelope(x, f, rate):
    """the complex envelope of x about f"""
    n = len(x)
    nf = next_pow2(n)
    s = np.fft.fft(x, nf)
    s[nf // 2 + 1:] = 0
    s[1:nf // 2] *= 2
    return np.fft.ifft(s)[:n] * np.exp(-2j * np.pi * f * np.arange(n) / rate)


def top_of(e):
    """the top of a Gaussian envelope: a parabola through the log of all of
    it above half its peak, which three samples of a broad top can't give"""
    i = int(np.argmax(e))
    idx = np.flatnonzero(e > 0.5 * e[i])
    if len(idx) < 3:
        return float(i)
    a, b, _c = np.polyfit(idx - i, np.log(e[idx]), 2)
    return i - b / (2 * a) if a < 0 else float(i)


def an_polarity(ctx):
    rep = ctx.rep
    y = ctx.onto()
    w = ctx.info["width"]
    pulse = ctx.x[:, 0][int(ctx.info["L"][0] - w / 2):
                        int(ctx.info["L"][0] - w / 2) + w]
    rows, says = [], []
    for c in ctx.outs:
        got = {}
        for side in SIDE:
            vals = []
            for cen in ctx.info[side]:
                a = int(cen - w / 2)
                seg = y[a - w:a + 2 * w, c]
                r = np.correlate(seg, pulse, mode="valid") / np.dot(pulse,
                                                                    pulse)
                vals.append(r[np.argmax(np.abs(r))])
            got[side] = v = float(np.median(vals))
            rows.append([ctx.oname(c), "%s pulses" % side, "%.1f" % db(v),
                         "positive" if v > 0 else "negative"])
        top = max(SIDE, key=lambda k: abs(got[k]))
        other = got["R" if top == "L" else "L"]
        says.append("%s carries the %s side%s%s" % (
            ctx.oname(c).strip(), "left" if top == "L" else "right",
            "" if got[top] > 0 else ", inverted",
            "" if abs(other) < 0.1 * abs(got[top]) else
            " and the other side %.1f dB under it" % db(other / got[top])))
    rep.say()
    rep.say("pulses (gain of each side's pulses into each output, dB, and "
            "their sign)")
    rep.table(["", "", "dB", "sign"], rows)
    rep.put("polarity", says)
    rep.say("  " + "; ".join(says))


def an_program(ctx):
    an_welch(ctx)
    for name, x in (("signal", ctx.x), ("recording", ctx.onto())):
        b, n = ctx.info["body"]
        v = x[b:b + n]
        cf = [db(np.abs(v[:, c]).max() / max(np.sqrt(np.mean(v[:, c] ** 2)),
                                             1e-15)) for c in range(2)
              if c < v.shape[1]]
        ctx.rep.say("crest    %s: %s dB" % (
            name, ", ".join("%.1f" % v for v in cf)))
    if ctx.info.get("pilot"):
        b, n = ctx.info["body"]
        # the program leaves 100 Hz free on each side of the pilot
        an_pilot(ctx, ctx.info["pilot"], b + int(0.1 * ctx.made.rate),
                 b + n - int(0.1 * ctx.made.rate), (5.0, 45.0))


def an_track(ctx):
    """each segment's transfers against the white segment's, and how fast
    a running estimate settles after each change"""
    rep, rate = ctx.rep, ctx.made.rate
    y = ctx.onto()
    x = ctx.x
    nfft = 8192 if rate > 30000 else 4096
    f = np.fft.rfftfreq(nfft, 1.0 / rate)
    segs = ctx.info["segments"]
    c = ctx.outs[0]
    est = {}
    for sg in segs:
        a = sg["at"] + int(0.25 * rate)
        b = sg["at"] + sg["len"] - int(0.25 * rate)
        if sg["kind"] == "silence":
            continue
        gxx, gyx, gyy, k = welch(x[a:b, :1], y[a:b, c:c + 1], nfft)
        est[sg["kind"] + "%g" % sg["level"]] = (sg, gxx[:, 0, 0].real,
                                                gyx[:, 0, 0], gyy[:, 0, 0])
    ref = est.get("white-20")
    if ref is None:
        rep.say("no white segment to compare with")
        return
    href = ref[2] / np.maximum(ref[1], 1e-30)
    bands = [(lo, 2 * lo) for lo in (31.25, 62.5, 125, 250, 500, 1000, 2000,
                                     4000, 8000)]
    rows = []
    for key, (sg, sxx, sxy, syy) in est.items():
        h = sxy / np.maximum(sxx, 1e-30)
        coh = np.abs(sxy) ** 2 / np.maximum(sxx * syy.real, 1e-30)
        cells = [sg["kind"], "%g" % sg["level"]]
        for lo, hi in bands:
            sel = (f >= lo) & (f < hi)
            if sxx[sel].mean() < 1e-3 * sxx.max():
                cells.append("-")
                continue
            d = db(abs(np.mean(h[sel])) / abs(np.mean(href[sel])))
            cells.append("%+.2f/%.2f" % (d, np.mean(coh[sel])))
        rows.append(cells)
    rep.say()
    rep.say("segments against the white one, left in to %s: dB and "
            "coherence in each octave (- where the segment leaves the "
            "octave empty)" % ctx.oname(c))
    rep.table(["kind", "dBFS"] + ["%g" % lo for lo, _h in bands], rows)
    running(ctx, x[:, 0], y[:, c], href, f, nfft, segs)


def running(ctx, x, y, href, f, nfft, segs, tau=2.0):
    """a running estimate with exponential forgetting, as a live analysis
    would keep it: the time after each change until every excited octave
    is within 0.5 dB of the white segment's response"""
    rep, rate = ctx.rep, ctx.made.rate
    hop = nfft // 2
    lam = math.exp(-hop / rate / tau)
    w = hann(nfft)
    sxx = np.zeros(len(f))
    sxy = np.zeros(len(f), complex)
    out = []
    bands = [(lo, 2 * lo) for lo in (62.5, 125, 250, 500, 1000, 2000, 4000,
                                     8000)]
    b0, n = ctx.info["body"]
    times, devs, held = [], [], []
    for a in range(b0, b0 + n - nfft, hop):
        xf = np.fft.rfft(x[a:a + nfft] * w)
        yf = np.fft.rfft(y[a:a + nfft] * w)
        sxx = lam * sxx + np.abs(xf) ** 2
        sxy = lam * sxy + yf * np.conj(xf)
        h = sxy / np.maximum(sxx, 1e-30)
        worst, every = 0.0, 0.0
        pin = np.abs(xf) ** 2
        for lo, hi in bands:
            sel = (f >= lo) & (f < hi)
            d = abs(db(abs(np.mean(h[sel])) / abs(np.mean(href[sel]))))
            every = max(every, d)
            if pin[sel].mean() >= 1e-4 * max(pin.max(), 1e-30):
                worst = max(worst, d)
        times.append((a + nfft / 2) / rate)
        devs.append(worst)
        held.append(every)
    times, devs, held = np.array(times), np.array(devs), np.array(held)
    for sg in segs[1:]:
        t0 = sg["at"] / rate
        sel = (times > t0) & (times < t0 + sg["len"] / rate)
        if not sel.any():
            continue
        if sg["kind"] == "silence":
            # nothing goes in, so the estimate should hold in every octave
            out.append([sg["kind"], "-", "-", "%.2f" % held[sel].max()])
            continue
        ok = np.flatnonzero(sel & (devs < 0.5))
        settle = times[ok[0]] - t0 if len(ok) else float("nan")
        out.append([sg["kind"], "%g" % sg["level"],
                    "%.2f" % settle if np.isfinite(settle) else "never",
                    "%.2f" % devs[sel].max()])
    rep.say()
    rep.say("running estimate, forgetting with a time constant of %.0f s: "
            "seconds from each change until within 0.5 dB, and the worst "
            "dB in the segment" % tau)
    rep.put("running", out)
    rep.table(["kind", "dBFS", "settle s", "worst dB"], out)
    rep.csv("running", ["s", "worst_db"],
            np.column_stack([times, devs]).tolist())


def ref_onto(ctx):
    """the second recording on the signal's frames, aligned by its own
    markers and said in the report once: (frames, Align), or (None, None)
    where its markers aren't found"""
    if ctx.ral is None:
        ref, rep = ctx.ref, ctx.rep
        al = find_markers(ref, ctx.made) if ctx.info.get("marks") else \
            align_periodic(ref, ctx.made)
        ctx.ral = al or False
        if al is None:
            rep.say("ref      no markers found in %s" % ref.path)
            return None, None
        if unswap(ref, al):
            rep.say("ref      its channels hear the other sides: swapped "
                    "back")
        rep.say()
        rep.say("ref      %s, %d Hz: signal frame 0 at %.3f s, the player "
                "against it %+.3f ppm" % (os.path.basename(ref.path),
                                          ref.rate, al.lag / ref.rate,
                                          al.ppm))
        ctx.yr = warp(ref.x, al.lag, al.rho, len(ctx.x))
    return (ctx.yr, ctx.ral) if ctx.ral else (None, None)


def against_ref(ctx):
    """the recording against a second one made at the same time"""
    yr, _al = ref_onto(ctx)
    if yr is None:
        return
    y = ctx.onto()
    an_welch(ctx, x=yr, y=y, label="the recording against the ref",
             sides=list(range(min(2, yr.shape[1]))))


# --- the residual --------------------------------------------------------------

OCTAVES = (31.5, 63, 125, 250, 500, 1000, 2000, 4000, 8000, 16000)
# Welch's windows, Hann and half overlapping, vary as 1.33 / K of K
# independent ones, and a band of M bins as about 1.5 / M of M independent
# bins
K_VAR, M_VAR = 1.33, 1.5


def autos(ys, nfft):
    """Welch's spectral matrix of ys alone and its window count"""
    _gxx, _gyx, gyy, k = welch(ys[:, :1], ys, nfft)
    return gyy, k


def bands_of(f, centres, frac, top):
    """the bands of 1/frac octave about each centre, cut at top, that hold
    a bin: (centre, the bins' indices)"""
    out = []
    for c in centres:
        lo, hi = c * 2 ** (-0.5 / frac), min(c * 2 ** (0.5 / frac), top)
        sel = np.flatnonzero((f >= lo) & (f < hi))
        if lo < top and len(sel):
            out.append((c, sel))
    return out


LOW_HZ = (150.0, 400.0)


def through(x, at, n, h, nfft):
    """x's frames from at to at + n through the transfers h (bins of a
    window of nfft by outputs by inputs) as impulse responses nfft/2 either
    way of their time 0, x taken as silent outside it"""
    half = nfft // 2
    xs = np.zeros((n + 2 * half, x.shape[1]))
    lo, hi = max(at - half, 0), min(at + n + half, len(x))
    xs[lo - at + half:hi - at + half] = x[lo:hi]
    hir = np.roll(np.fft.irfft(h, nfft, axis=0), half, axis=0)
    m = next_pow2(len(xs) + nfft)
    z = np.fft.irfft(np.einsum("fci,fi->fc", np.fft.rfft(hir, m, axis=0),
                               np.fft.rfft(xs, m, axis=0)), m, axis=0)
    return z[nfft:nfft + n]


def blend(hl, fl, hs, fs):
    """the long windows' transfers hl under LOW_HZ and the short ones' hs
    over it, crossfaded between, on the long windows' grid fl"""
    hi = np.empty_like(hl)
    for c in range(hl.shape[1]):
        for i in range(hl.shape[2]):
            hi[:, c, i] = np.interp(fl, fs, hs[:, c, i].real) + \
                1j * np.interp(fl, fs, hs[:, c, i].imag)
    u = np.clip((fl - LOW_HZ[0]) / (LOW_HZ[1] - LOW_HZ[0]), 0, 1)
    wl = (0.5 + 0.5 * np.cos(np.pi * u))[:, None, None]
    return wl * hl + (1 - wl) * hi


def lines_of(p, f, lo, hi, count):
    """the lines of a spectrum of tones' levels p: bins over ten times their
    neighbourhood's median and over their neighbours, the highest first"""
    idx = np.flatnonzero((f >= lo) & (f <= hi))
    out = []
    for i in idx[np.argsort(p[idx])[::-1]]:
        med = np.median(p[max(i - 40, 0):i + 41])
        if p[i] < 10 * med or len(out) >= count:
            break
        if p[i] >= p[i - 1] and p[i] >= p[i + 1]:
            out.append((i, med))
    return out


STOP_FRAMES = 2.0       # a place this far off the drift is past a stop
RUN_S = 2.0             # the shortest run between stops that counts


def track(rec, lag, rho, x, s0, s1, block):
    """the recording's place against the signal x from signal frame s0 to
    s1, in blocks: each block's first frame and its offset from lag + s *
    rho in the recording's frames, followed from block to block, or nan for
    a quiet block"""
    out, off, srch = [], 0.0, block
    mono = x[:, :2].sum(axis=1)
    for a in range(s0, s1 - block + 1, block):
        sig = mono[a:a + block]
        if np.dot(sig, sig) < 1e-6 * block:
            out.append((a, float("nan")))
            continue
        y = warp(rec.x, lag + (a - srch) * rho + off, rho, block + 2 * srch)
        y = y[:, :2].sum(axis=1)
        m = next_pow2(len(y) + block)
        r = np.abs(np.fft.irfft(np.fft.rfft(y, m) *
                                np.conj(np.fft.rfft(sig, m)), m))
        r = r[:2 * srch + 1]
        off += (refine_peak(r, int(np.argmax(r))) - srch) * rho
        out.append((a, off))
    return out


def runs_of(rec, al, x, s0, s1, block, rate):
    """the runs between the player's stops from signal frame s0 to s1, each
    (first frame, frames, the recording's frame at signal frame 0 along
    it), the recorder's frames a signal frame over all of them, and the
    stops, (signal frame, ms). A block next to a stop is left out, and so is
    a run under RUN_S"""
    # the markers' rate holds a stop between them that fell within their
    # tolerance, and a block stretched by it blurs its own place: a first
    # pass takes the rate from the median step, and a second the places
    lag, rho = al.lag, al.rho
    for _pass in range(2):
        tr = track(rec, lag, rho, x, s0, s1, block)
        a = np.array([t[0] for t in tr], float)
        d = np.array([t[1] for t in tr])
        mid = a + block / 2.0
        step = np.diff(d)
        if not np.isfinite(step).any():
            break
        drift = np.nanmedian(step) / block
        if _pass == 0:
            lag += np.nanmedian(d - drift * mid)
            rho += drift
    step = np.diff(d)
    drift = np.nanmedian(step) if np.isfinite(step).any() else 0.0
    jump = np.abs(np.nan_to_num(step - drift)) > STOP_FRAMES
    bad = ~np.isfinite(d)
    bad[1:] |= jump
    bad[:-1] |= jump
    stops = [(int(a[i + 1]), (step[i] - drift) / rec.rate * 1e3)
             for i in np.flatnonzero(jump)]
    groups, cur = [], []
    for i in range(len(d)):
        if bad[i]:
            if cur:
                groups.append(cur)
            cur = []
        else:
            cur.append(i)
    if cur:
        groups.append(cur)
    groups = [g for g in groups if len(g) * block >= RUN_S * rate]
    # one rate over every run, each with its own place; a block off its
    # run's line by half a frame, as the one after a long stop can be,
    # leaves the line and splits its run
    mid = a + block / 2.0
    for _again in range(3):
        num = den = 0.0
        for g in groups:
            ag, dg = mid[g], d[g]
            num += np.sum((ag - ag.mean()) * (dg - dg.mean()))
            den += np.sum((ag - ag.mean()) ** 2)
        c1 = num / den if den > 0 else 0.0
        kept = []
        for g in groups:
            c0 = d[g].mean() - c1 * mid[g].mean()
            off = np.abs(d[g] - c0 - c1 * mid[g]) > 0.5
            cur = []
            for i, o in zip(g, off):
                if o:
                    if cur:
                        kept.append(cur)
                    cur = []
                else:
                    cur.append(i)
            if cur:
                kept.append(cur)
        kept = [g for g in kept if len(g) * block >= RUN_S * rate]
        if kept == groups:
            break
        groups = kept
    runs = []
    for g in groups:
        c0 = d[g].mean() - c1 * mid[g].mean()
        runs.append((int(a[g[0]]), int(a[g[-1]] + block - a[g[0]]),
                     lag + c0))
    return runs, rho + c1, stops


def common_runs(sets, least):
    """the stretches that every recorder's runs cover, each at least least
    frames: (first frame, frames, each recorder's lag along it)"""
    out = []
    for f0, n0, l0 in sets[0]:
        spans = [(f0, f0 + n0, [l0])]
        for other in sets[1:]:
            nxt = []
            for lo, hi, lags in spans:
                for f1, n1, l1 in other:
                    a, b = max(lo, f1), min(hi, f1 + n1)
                    if b > a:
                        nxt.append((a, b, lags + [l1]))
            spans = nxt
        out += [(lo, hi - lo, lags) for lo, hi, lags in spans
                if hi - lo >= least]
    return out


def an_residual(ctx):
    """what the recording holds beyond the program through the path
    measured from it, with the music and in the silence after it. With a
    second recording made at the same time, the cross-spectrum of the two
    residuals keeps what both heard, which the player put out, and leaves
    each recorder's own noise apart"""
    rep, made = ctx.rep, ctx.made
    rate = made.rate
    seg = {s["kind"]: s for s in ctx.info["segments"]}
    cut = int(0.1 * rate)
    span = {k: (seg[k]["at"] + cut, seg[k]["len"] - 2 * cut)
            for k in ("program", "silence")}
    nfft = nfft_for(rate)
    w = hann(nfft)
    f = np.fft.rfftfreq(nfft, 1.0 / rate)
    recs = [(ctx.rec, ctx.al, list(ctx.outs))]
    if ctx.ref is not None:
        _yr, ral = ref_onto(ctx)
        if ral is not None:
            recs.append((ctx.ref, ral, list(range(min(2,
                                                      ctx.ref.x.shape[1])))))
    chans = [(r, c) for r, (_rec, _al, cs) in enumerate(recs) for c in cs]
    top = min(20000.0, 0.48 * min([rate] + [r.rate for r, _a, _c in recs]))
    # the player may stop for a moment, as Audio 2's DAC does while the VX
    # captures: every recorder hears the stop, so the program is taken in
    # the runs between the stops, each aligned on its own
    a, n = span["program"]
    block = int(0.25 * rate)
    found = [runs_of(r, al, ctx.x, a, a + n, block, rate)
             for r, al, _c in recs]
    runs = common_runs([fd[0] for fd in found], int(RUN_S * rate))
    names = ("rec", "ref")
    rep.put("stops", [fd[2] for fd in found])
    if not runs:
        rep.say("residual: no run of %.0f s between the player's stops" %
                RUN_S)
        return

    def place(i, s0, nn, lag):
        """recorder i's frames for the signal's s0 to s0 + nn along lag, a
        margin taken either way, since the warp makes its span periodic"""
        r, _al, cs = recs[i]
        rho, mg = found[i][1], 4096
        y = warp(r.x, lag + (s0 - mg) * rho, rho, nn + 2 * mg)
        return y[mg:mg + nn, cs]

    nlo = 4 * nfft
    runs = [(s0, nn, list(lags)) for s0, nn, lags in runs]

    def measure():
        sums, longs, ys, each = None, None, [], []
        for s0, nn, lags in runs:
            y = np.concatenate([place(i, s0, nn, lags[i])
                                for i in range(len(recs))], axis=1)
            y -= y.mean(axis=0)
            ys.append(y)
            g = welch(ctx.x[s0:s0 + nn, :2], y, nfft)
            each.append(g)
            sums = g if sums is None else \
                tuple(u + v for u, v in zip(sums, g))
            if nn >= 2 * nlo:
                g = welch(ctx.x[s0:s0 + nn, :2], y, nlo)
                longs = g if longs is None else \
                    tuple(u + v for u, v in zip(longs, g))
        return sums, longs, ys, each

    # each run's place to a small part of a frame, and each recorder's rate
    # to a few parts in 10^10: the blocks' correlations of the music leave
    # a place a fraction of a frame out, differently from run to run and
    # path to path, and a rate a few parts in 10^9, which within a run
    # leaves the program in the residual, the more the higher, with the
    # two recorders' errors apart. Pieces of 2 s against the paths of all
    # runs together give delays, and a line through them with each run's
    # own place gives both
    sums, longs, ys, each = measure()
    for _again in range(2):
        h, _coh, _mult = transfers(*sums[:3])
        pts = []
        for r, (s0, nn, _lags) in enumerate(runs):
            cuts = np.linspace(0, nn, max(1, int(nn // (2 * rate))) + 1)
            for u, v in zip(cuts[:-1].astype(int), cuts[1:].astype(int)):
                hr = transfers(*welch(ctx.x[s0 + u:s0 + v, :2],
                                      ys[r][u:v], nfft)[:3])[0]
                pts.append((r, s0 + (u + v) / 2.0, [np.mean([delay_of(
                    f, hr[:, j, c] / np.where(np.abs(h[:, j, c]) > 0,
                                              h[:, j, c], 1), rate, 300.0,
                    min(6000.0, top)) for j, c in [(chans.index((i, c)), c)
                                                   for c in cs]])
                    for i, (_rec, _al, cs) in enumerate(recs)]))
        for i in range(len(recs)):
            num = den = 0.0
            for r in range(len(runs)):
                q = [(t, d[i]) for rr, t, d in pts if rr == r]
                tm = np.mean([t for t, _d in q])
                dm = np.mean([d for _t, d in q])
                num += sum((t - tm) * (d - dm) for t, d in q)
                den += sum((t - tm) ** 2 for t, _d in q)
            c1 = num / den if den > 0 else 0.0
            rho = found[i][1]
            for r, (s0, nn, lags) in enumerate(runs):
                q = [(t, d[i]) for rr, t, d in pts if rr == r]
                c0 = np.mean([d for _t, d in q]) - \
                    c1 * np.mean([t for t, _d in q])
                lags[i] += c0 * rho
            found[i] = (found[i][0], rho * (1 + c1), found[i][2])
        sums, longs, ys, each = measure()
    rep.say()
    for i, (r, _al, _c) in enumerate(recs):
        _rr, rho, stops = found[i]
        rep.say("%s      %d stops of the player in the program, %.0f ms in "
                "all%s; the player against it %+.3f ppm over the runs" % (
                    names[i], len(stops),
                    sum(ms for _f, ms in stops),
                    "".join(", %+.1f ms at %.2f s" % (ms, (fr - a) / rate)
                            for fr, ms in stops[:6]),
                    (r.rate / made.rate / rho - 1) * 1e6))
    gxx, gyx, gyy, k = sums
    h, _coh, _mult = transfers(gxx, gyx, gyy)
    # what the program, through the paths measured, leaves of each channel,
    # taken in time: within a window, a path whose phase turns fast, as a
    # highpass's does, would leak the loud low end of the program into the
    # residual. Low down, windows four times as long estimate the paths
    # with that much less of the window's error, where the program's level
    # makes up for their fewer averages
    hfull, nir = h, nfft
    if longs is not None:
        hl, _c, _m = transfers(*longs[:3])
        hfull, nir = blend(hl, np.fft.rfftfreq(nlo, 1.0 / rate), h, f), nlo
    gee, ke = None, 0
    for (s0, nn, _lags), y in zip(runs, ys):
        g, kk = autos(y - through(ctx.x[:, :2], s0, nn, hfull, nir), nfft)
        gee = g if gee is None else gee + g
        ke += kk
    # the silence, along the last run's places: a stop in it moves no
    # sample, and every recorder's place moves alike
    a, n = span["silence"]
    s0, _nn, lags = runs[-1]
    y = np.concatenate([place(i, a, n, lags[i]) for i in range(len(recs))],
                       axis=1)
    gss, k1 = autos(y - y.mean(axis=0), nfft)
    sw2, df = (w * w).sum(), rate / nfft
    band = (f >= 20) & (f <= top)
    wa = 10 ** (a_weight(f) / 10)

    def dens(g, kk):
        """sums over kk windows as a density, dBFS a Hz of a full-scale
        sine, in power"""
        return 4.0 * g / (kk * rate * sw2)

    def total(d, weight=None):
        v = d[band] * (wa[band] if weight else 1)
        return float(v.sum() * df)

    rep.say()
    rep.say("residual: the program's %.1f s in %d runs between the player's "
            "stops, through the paths measured from them, and %.1f s of the "
            "silence after it; Hann windows of %d, half overlapping, %d and "
            "%d of them" % (sum(nn for _s, nn, _l in runs) / rate, len(runs),
                            span["silence"][1] / rate, nfft, ke, k1))
    octs = bands_of(f, OCTAVES, 1, top)
    thirds = bands_of(f, ts.THIRDS, 3, top)
    csv_rows, res = [], {}
    for j, (r, c) in enumerate(chans):
        dm, ds = dens(gee[:, j, j].real, ke), dens(gss[:, j, j].real, k1)
        sig = dens(gyy[:, j, j].real, k)
        res[j] = (dm, ds)
        key = "%s:%s" % (names[r], SIDE[c])
        rep.put("res:" + key, (pdb(total(dm)), pdb(total(ds))))
        rep.put("thirds:" + key, (
            [b for b, _s in thirds],
            [pdb(dm[s].mean()) for _b, s in thirds],
            [pdb(ds[s].mean()) for _b, s in thirds]))
        rep.say("  %s %s: the recording %.1f dBFS; the residual with the "
                "music %.1f (A-wtd %.1f), in the silence %.1f (A-wtd %.1f), "
                "from 20 Hz to %.0f kHz" % (
                    names[r], SIDE[c], pdb(total(sig)), pdb(total(dm)),
                    pdb(total(dm, "A")), pdb(total(ds)),
                    pdb(total(ds, "A")), top / 1000))
        for b, s in thirds:
            csv_rows.append([names[r], SIDE[c], b, pdb(dm[s].mean()),
                             pdb(ds[s].mean())])
    rep.say("the residual in octaves, dBFS a Hz at each recording, with the "
            "music and in the silence:")
    rep.table(["Hz"] + ["%s %s %s" % (names[r], SIDE[c], t)
                        for r, c in chans for t in ("music", "silence")],
              [["%g" % b] + ["%.1f" % pdb(res[j][i][s].mean())
                             for j in range(len(chans)) for i in (0, 1)]
               for b, s in octs])
    rep.csv("residual", ["recorder", "side", "hz", "music_dbfs_hz",
                         "silence_dbfs_hz"], csv_rows)
    rep.put("sums", {"f": f, "h": h, "gee": gee, "ke": ke, "gss": gss,
                     "k1": k1, "chans": chans})
    pairs = [(c, chans.index((0, c)), chans.index((1, c)))
             for c in range(2) if (0, c) in chans and (1, c) in chans]
    if pairs:
        apart(ctx, f, h, gee, gss, ke, k1, pairs, octs, thirds, dens, total)
    found = []
    for j, (r, c) in enumerate(chans):
        p = 4.0 * gss[:, j, j].real / (k1 * w.sum() ** 2)
        for i, med in lines_of(p, f, 20, top, 6):
            heard = "-"
            for cc, ia, ix in pairs:
                if cc == c:
                    o = ix if j == ia else ia
                    s = slice(i - 1, i + 2)
                    coh = np.abs(gss[s, j, o]) ** 2 / np.maximum(
                        gss[s, j, j].real * gss[s, o, o].real, 1e-60)
                    heard = "both" if coh.max() >= 0.5 else "own"
            found.append([names[r], SIDE[c], "%.1f" % f[i],
                          "%.1f" % pdb(p[i]), "%.1f" % pdb(p[i] / med),
                          heard])
    rep.put("lines", found)
    if found:
        rep.say("lines in the silence (dBFS as a tone, dB over the local "
                "floor, and whether both recorders heard it alike):")
        rep.table(["", "", "Hz", "dBFS", "over", "heard"], found)


def apart(ctx, f, h, gee, gss, k, k1, pairs, octs, thirds, dens, total):
    """the part both recorders heard, referred to the player's input, and
    each one's own, from the cross-spectra of the residuals: the paths'
    phases taken out, the cross-spectrum's real part is what both heard
    times both paths' gains, and the rest of each channel its own"""
    rep = ctx.rep
    out, rows = [], []
    for c, ia, ix in pairs:
        ha, hx = h[:, ia, c], h[:, ix, c]
        g = np.maximum(np.abs(ha) * np.abs(hx), 1e-30)
        turn = np.exp(-1j * np.angle(ha * np.conj(hx)))
        parts = {}
        for name, mat, kk in (("music", gee, k), ("silence", gss, k1)):
            both = (mat[:, ia, ix] * turn).real / g
            own_a = mat[:, ia, ia].real / np.abs(ha) ** 2 - both
            own_x = mat[:, ix, ix].real / np.abs(hx) ** 2 - both
            parts[name] = (dens(both, kk), dens(own_a, kk), dens(own_x, kk),
                           mat[:, ia, ia].real, mat[:, ix, ix].real, kk)

        def band(name, s):
            """a band's part both heard and twice its uncertainty"""
            both, _oa, _ox, paa, pxx, kk = parts[name]
            two = 2 * math.sqrt(paa[s].mean() * pxx[s].mean() * K_VAR *
                                M_VAR / (2 * kk * len(s))) / g[s].mean()
            return both[s].mean(), dens(two, kk)

        def cell(v, two):
            return "%.1f" % pdb(v) if v > two else "<%.1f" % pdb(two)

        for b, s in octs:
            vm, tm = band("music", s)
            vs, tsl = band("silence", s)
            out.append(["%g" % b, SIDE[c], cell(vm, tm), cell(vs, tsl),
                        "%.1f" % pdb(parts["silence"][1][s].mean()),
                        "%.1f" % pdb(parts["silence"][2][s].mean())])
        vals = {"hz": [], "music": [], "silence": [], "two": [],
                "own_rec": [], "own_ref": []}
        for b, s in thirds:
            vm, tm = band("music", s)
            vs, tsl = band("silence", s)
            vals["hz"].append(b)
            vals["music"].append(pdb(vm) if vm > tm else float("nan"))
            vals["silence"].append(pdb(vs) if vs > tsl else float("nan"))
            vals["two"].append(pdb(tsl))
            vals["own_rec"].append(pdb(parts["silence"][1][s].mean()))
            vals["own_ref"].append(pdb(parts["silence"][2][s].mean()))
            rows.append([SIDE[c], b, pdb(vm), pdb(tm), pdb(vs), pdb(tsl),
                         vals["own_rec"][-1], vals["own_ref"][-1]])
        tot = {}
        for name in ("music", "silence"):
            both, _oa, _ox, paa, pxx, kk = parts[name]
            tot[name] = (pdb(total(both)), pdb(total(both, "A")))
        vals["total"] = tot
        rep.put("both:%s" % SIDE[c], vals)
    rep.say()
    rep.say("what both recorders heard, which the player put out, and each "
            "one's own, at the player's input in dBFS a Hz; < marks a part "
            "under twice its uncertainty:")
    rep.table(["Hz", "side", "both music", "both silence", "rec own",
               "ref own"], out)
    for c, _ia, _ix in pairs:
        t = ctx.rep.values["both:%s" % SIDE[c]]["total"]
        rep.say("  %s: what both heard from 20 Hz to 20 kHz, %.1f dBFS "
                "(A-wtd %.1f) with the music, %.1f (A-wtd %.1f) in the "
                "silence" % (SIDE[c], t["music"][0], t["music"][1],
                             t["silence"][0], t["silence"][1]))
    rep.say("  with the music, distortion that each recorder adds of the "
            "same program correlates too, so that part bounds the player's; "
            "in the silence it is the player's and what the paths share "
            "before they part")
    rep.csv("both", ["side", "hz", "music_dbfs_hz", "music_2sd",
                     "silence_dbfs_hz", "silence_2sd", "rec_own", "ref_own"],
            rows)


ANALYSES = {"ess": an_ess, "linsweep": an_linsweep,
            "multitone": an_periodic, "noise": an_welch, "mls": an_mls,
            "impulses": an_impulses, "tones": an_tones,
            "silence": an_silence, "lsb": an_lsb, "dc": an_dc,
            "imd": an_imd, "jtest": an_jtest, "pilot": an_pilot,
            "chirps": an_chirps, "dual": an_dual, "bursts": an_bursts,
            "polarity": an_polarity, "program": an_program,
            "track": an_track, "residual": an_residual}


def analyze(made, rec, ref=None, prefix=None):
    """the report of one recording, as text"""
    rep = Report(prefix)
    ctx = Ctx(made, rec, rep, ref)
    if made.info.get("marks"):
        ctx.al = find_markers(rec, made)
    else:
        ctx.al = align_periodic(rec, made)
        settle_ratio(ctx)
    if ctx.al is None:
        rep.say("no markers of %s found in %s" % (made.name, rec.path))
        return rep.done(), None
    swapped = unswap(rec, ctx.al)
    describe(ctx)
    if swapped:
        rep.say("note     the recorder's channels hear the other sides: "
                "they are swapped back, rec 0 taken as the left")
    b, n = made.info["body"]
    x = ctx.x[b:b + n]
    if any("difference" in t for t in ctx.al.notes) and x.shape[1] == 2 \
            and np.array_equal(x[:, 0], x[:, 1]) and active(x[:, 0]):
        rep.say("stop     the body is the same on both sides, so it cancels "
                "in this recording")
        return rep.done(), ctx
    ANALYSES[made.sig.family](ctx)
    # the residual's analysis takes the ref its own way
    if ref is not None and made.sig.family != "residual":
        against_ref(ctx)
    return rep.done(), ctx


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("name", help="the signal's file name, or the file "
                    "played")
    ap.add_argument("rec", help="the recording, any WAV")
    ap.add_argument("--ref", help="a second recording made at the same time")
    ap.add_argument("-o", dest="prefix", help="write PREFIX.txt and "
                    "PREFIX-*.csv")
    ap.add_argument("--sig", help="the file played, against the signal "
                    "made here")
    ap.add_argument("--rate", type=int, help="the signal's rate, where the "
                    "name has none")
    ap.add_argument("--rec-rate", type=int, help="the recording's rate, in "
                    "place of its header's")
    ap.add_argument("--ref-rate", type=int, help="the same for --ref")
    ap.add_argument("--chans", help="two channels of the recording, A,B")
    args = ap.parse_args(argv)
    try:
        sig, rate, side = ts.find(args.name, args.rate)
    except KeyError as e:
        ap.error(str(e))
    made = ts.render(sig, rate, side)
    if args.sig or args.name.lower().endswith(".wav"):
        path = args.sig or args.name
        r, x = read_wav(path)
        q = np.rint(x * ts.FULL).astype(np.int16)
        if r != rate or q.shape != made.data.shape:
            ap.error("%s isn't %s" % (path, made.name))
        if not np.array_equal(q, made.data):
            print("note: %s differs from the signal made here; using the "
                  "file" % path)
            made.data = q
    chans = [int(v) for v in args.chans.split(",")] if args.chans else None
    rec = Rec(args.rec, args.rec_rate, chans)
    ref = Rec(args.ref, args.ref_rate) if args.ref else None
    text, _ctx = analyze(made, rec, ref, args.prefix)
    sys.stdout.write(text)
    return 0


if __name__ == "__main__":
    sys.exit(main())
