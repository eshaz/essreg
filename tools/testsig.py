#!/usr/bin/env python3
# (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
#
# Licensed under GPL Version 3.0
"""Make the test signals that measure the ES1869's audio paths.

usage:
  python3 tools/testsig.py [-o OUTDIR]        every signal, into out/testsig
  python3 tools/testsig.py [-o OUTDIR] --only NAME...
  python3 tools/testsig.py --rate HZ [--only NAME...]
  python3 tools/testsig.py --side l|r [--only NAME...]
  python3 tools/testsig.py --list [--only NAME...]
  python3 tools/testsig.py --doc [--check]    the docs' generated tables

SIGNALS is the one list of them: each signal's family, rates, level and
parameters, and what it measures, from which docs/TEST_SIGNALS.md takes its
catalog. Every file is 16-bit PCM and comes out the same byte for byte at
every run, from fixed seeds. OUTDIR also gets manifest.json, with what each
file's analysis needs (tools/sigan.py), and SHA256SUMS, for sha256sum -c.
NAME is a file's name (sweep-44k, sweep-l-44k), a signal's (sweep, all its
files) or a pattern (noise-*). --rate makes the chosen signals at HZ
instead of their own rates, and --side their left or right side alone.
Needs numpy.
"""

import argparse
import fnmatch
import hashlib
import json
import math
import os
import sys
import wave
import zlib

import numpy as np

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "out", "testsig")
DOC = os.path.join(ROOT, "docs", "TEST_SIGNALS.md")

FULL = 32768.0              # the sample of 1.0, full scale
MARK_S = 0.1                # a marker: a chirp this long
MARK_F = (500.0, 4000.0)    # from and to, which fits every rate from 11025
MARK_AMP = 0.1              # -20 dBFS at its peak
LEAD_S, GAP_S, TAIL_S = 0.2, 0.3, 0.2
SEP_S = 0.05                # from the left marker's end to the right's start
HOT_DB = -6.0               # a true peak at or above this is hot
FADE_S = 0.010              # a tone's or a noise's ramps
THIRDS = (20, 25, 31.5, 40, 50, 63, 80, 100, 125, 160, 200, 250, 315, 400,
          500, 630, 800, 1000, 1250, 1600, 2000, 2500, 3150, 4000, 5000,
          6300, 8000, 10000, 12500, 16000, 20000)
TAGS = {44100: "44k", 48000: "48k", 22050: "22k", 11025: "11k"}


def rate_tag(rate):
    return TAGS.get(rate, "%dk" % (rate // 1000))


def db(v):
    return 20 * math.log10(max(v, 1e-12))


# --- the pieces --------------------------------------------------------------

def tukey(n, alpha=0.5):
    """a window flat in the middle, with cosine tapers on alpha of it"""
    w = np.ones(n)
    k = int(alpha * (n - 1) / 2)
    if k > 0:
        t = 0.5 - 0.5 * np.cos(np.pi * np.arange(k) / k)
        w[:k] = t
        w[n - k:] = t[::-1]
    return w


def ramp(x, n_in, n_out=None):
    """x with raised-cosine ramps at its ends, in place"""
    n_out = n_in if n_out is None else n_out
    if n_in:
        w = 0.5 - 0.5 * np.cos(np.pi * (np.arange(n_in) + 0.5) / n_in)
        x[:n_in] *= w.reshape((-1,) + (1,) * (x.ndim - 1))
    if n_out:
        w = 0.5 + 0.5 * np.cos(np.pi * (np.arange(n_out) + 0.5) / n_out)
        x[len(x) - n_out:] *= w.reshape((-1,) + (1,) * (x.ndim - 1))
    return x


def marker(rate, down=False):
    """the chirp that starts and ends a file: 500 Hz to 4 kHz in 0.1 s,
    tapered, or down from 4 kHz"""
    n = int(round(MARK_S * rate))
    t = np.arange(n) / rate
    f0, f1 = MARK_F[::-1] if down else MARK_F
    ph = 2 * np.pi * (f0 * t + (f1 - f0) * t * t / (2 * n / rate))
    return MARK_AMP * tukey(n) * np.sin(ph)


def frame(body, rate):
    """body between the start and the end markers: at each end a chirp on
    the left channel, then one on the right, so that a recorder that hears
    the sides' sum or difference still hears both. marks holds the left
    chirps' starts, and the right one follows each by mark_sep"""
    lead, gap, tail, sep = (int(round(s * rate))
                            for s in (LEAD_S, GAP_S, TAIL_S, SEP_S))
    m = marker(rate)
    n_m = len(m)
    pair = 2 * n_m + sep
    out = np.zeros((lead + 2 * (pair + gap) + len(body) + tail,
                    body.shape[1]))
    b = lead + pair + gap
    c = b + len(body) + gap
    for start in (lead, c):
        for i in range(min(body.shape[1], 2)):
            a = start + i * (n_m + sep)
            out[a:a + n_m, i] = m
    out[b:b + len(body)] = body
    return out, {"body": [b, len(body)], "marks": [lead, c],
                 "mark_len": n_m, "mark_sep": n_m + sep}


def tpdf(rng, shape):
    """triangular dither of 1 LSB peak"""
    return rng.random_sample(shape) - rng.random_sample(shape)


def quantize(x, rng=None, shared=False):
    """floats of full scale 1.0 to 16-bit samples, dithered when rng, with
    one dither for every channel when shared"""
    v = x * FULL
    if rng is not None:
        v = v + (tpdf(rng, (len(v), 1)) if shared else tpdf(rng, v.shape))
    q = np.rint(v)
    if q.max() > 32767 or q.min() < -32768:
        raise ValueError("the signal clips")
    return q.astype(np.int16)


def shaped(rng, n, rate, kind):
    """Gaussian noise of unit rms with the spectrum kind: white, pink (equal
    power an octave from 20 Hz), bass (pink under 250 Hz), mid (pink from
    250 Hz to 4 kHz), treble (pink over 4 kHz) or music (pink from 40 Hz,
    falling 3 dB an octave more over 4 kHz)"""
    w = rng.standard_normal(n)
    if kind == "white":
        return w / np.sqrt(np.mean(w * w))
    f = np.fft.rfftfreq(n, 1.0 / rate)
    f[0] = 1e-3
    g = 1 / np.sqrt(f)

    def hp(fc, order):
        return 1 / np.sqrt(1 + (fc / f) ** (2 * order))

    def lp(fc, order):
        return 1 / np.sqrt(1 + (f / fc) ** (2 * order))

    if kind == "pink":
        g *= hp(20, 2)
    elif kind == "bass":
        g *= hp(20, 2) * lp(250, 4)
    elif kind == "mid":
        g *= hp(250, 4) * lp(4000, 4)
    elif kind == "treble":
        g *= hp(4000, 4)
    elif kind == "music":
        g *= hp(40, 2) / np.sqrt(np.sqrt(1 + (f / 4000) ** 2))
    else:
        raise ValueError(kind)
    g[0] = 0
    x = np.fft.irfft(np.fft.rfft(w) * g, n)
    return x / np.sqrt(np.mean(x * x))


def amp_of(level):
    """a sine's peak at an AES17 level"""
    return 10 ** (level / 20)


def rms_of(level):
    return 10 ** (level / 20) / math.sqrt(2)


def ess_wave(f1, f2, n, rate):
    """Farina's exponential sweep from f1 to f2 in n samples, and its rate
    constant L in seconds"""
    big_l = n / rate / math.log(f2 / f1)
    t = np.arange(n) / rate
    return np.sin(2 * np.pi * f1 * big_l * np.expm1(t / big_l)), big_l


def lin_wave(f1, f2, n, rate):
    t = np.arange(n) / rate
    return np.sin(2 * np.pi * (f1 * t + (f2 - f1) * t * t / (2 * n / rate)))


def top(rate, v):
    """a frequency, or "nyq" as a fraction of half the rate"""
    if isinstance(v, str):
        return float(v.split(":")[1]) * rate / 2
    return float(v)


def schroeder(bins, amps):
    """phases that make a multitone sweep once a period, from the tones'
    shares of the power (Schroeder's, for any bins)"""
    k = np.asarray(bins, float)
    step = np.gcd.reduce(np.diff(np.asarray(bins))) if len(bins) > 1 else 1
    k = (k - k[0]) / step
    p = amps ** 2 / np.sum(amps ** 2)
    s0 = np.concatenate([[0], np.cumsum(p)[:-1]])
    s1 = np.concatenate([[0], np.cumsum(p * k)[:-1]])
    return -2 * np.pi * (k * s0 - s1)


def low_crest(n, bins, amps, iters=300, clip=1.4):
    """one period of n samples with these tones, the phases clipped and put
    back iters times from Schroeder's, the lowest crest factor kept"""
    spec = np.zeros(n // 2 + 1, complex)
    spec[bins] = amps * np.exp(1j * schroeder(bins, amps))
    best, best_cf = None, None
    for _i in range(iters):
        x = np.fft.irfft(spec, n)
        r = np.sqrt(np.mean(x * x))
        cf = np.abs(x).max() / r
        if best_cf is None or cf < best_cf:
            best, best_cf = spec[bins].copy(), cf
        c = clip * r
        x = np.fft.rfft(np.clip(x, -c, c))
        spec[bins] = amps * np.exp(1j * np.angle(x[bins]))
    spec[bins] = best
    return np.fft.irfft(spec, n), np.angle(best)


def multitone_bins(period, rate, lo, hi):
    """every odd bin in lo-hi Hz: those of 1 mod 4 for the left channel,
    of 3 mod 4 for the right"""
    a = int(math.ceil(lo * period / rate))
    b = int(math.floor(hi * period / rate))
    return ([k for k in range(a, b + 1) if k % 4 == 1],
            [k for k in range(a, b + 1) if k % 4 == 3])


def mls_bits(order):
    """a maximum-length sequence of 2^order - 1 bits, from the taps of a
    primitive polynomial"""
    taps = {12: (12, 11, 10, 4), 14: (14, 13, 12, 2), 15: (15, 14),
            16: (16, 14, 13, 11)}[order]
    # shifting right, the polynomial's term x^t is bit order - t
    bits = [order - t for t in taps]
    state = (1 << order) - 1
    out = np.zeros((1 << order) - 1, dtype=np.int8)
    for i in range(len(out)):
        bit = 0
        for t in bits:
            bit ^= (state >> t) & 1
        out[i] = state & 1
        state = (state >> 1) | (bit << (order - 1))
    return out


def pilot_table(f, rate):
    """the shortest table of whole cycles of f at rate: its frames, cycles"""
    g = math.gcd(int(round(f)), rate)
    return rate // g, int(round(f)) // g


def wav_bytes(data, rate):
    """a 16-bit PCM WAV file"""
    import io
    buf = io.BytesIO()
    with wave.open(buf, "wb") as w:
        w.setnchannels(data.shape[1])
        w.setsampwidth(2)
        w.setframerate(rate)
        w.writeframes(data.astype("<i2").tobytes())
    return buf.getvalue()


# --- the families ------------------------------------------------------------
#
# each takes the signal, the rate and a seeded generator, and gives the
# samples as floats of full scale 1.0 (frames by channels) or as 16-bit
# samples, and the layout the analysis needs, in frames of the file

def fam_ess(s, rate, rng):
    p = s.params
    f1, f2 = top(rate, p["f1"]), top(rate, p["f2"])
    n = int(round(p["sweep"] * rate))
    x, big_l = ess_wave(f1, f2, n, rate)
    # ramps over the first sixth and the last 24th of an octave
    n_in = int(big_l * math.log(2) / 6 * rate)
    n_out = int(big_l * math.log(2) / 24 * rate)
    x = ramp(x, n_in, n_out) * amp_of(s.level)
    if p.get("mode") == "lr":
        gap = int(round(0.5 * rate))
        body = np.zeros((2 * n + gap, 2))
        body[:n, 0] = x
        body[n + gap:, 1] = x
        segs = [[0, n, "L"], [n + gap, n, "R"]]
    else:
        body = np.column_stack([x, x])
        segs = [[0, n, "LR"]]
    out, info = frame(body, rate)
    b = info["body"][0]
    info.update(f1=f1, f2=f2, L=big_l, amp=amp_of(s.level),
                segments=[[b + a, m, c] for a, m, c in segs],
                band=[f1 * 2 ** (1 / 6), f2 / 2 ** (1 / 24)])
    return out, info


def fam_linsweep(s, rate, rng):
    p = s.params
    f1, f2 = top(rate, p["f1"]), top(rate, p["f2"])
    n = int(round(p["sweep"] * rate))
    nr = int(round(FADE_S * rate))
    x = ramp(lin_wave(f1, f2, n, rate), nr) * amp_of(s.level)
    out, info = frame(np.column_stack([x, x]), rate)
    b = info["body"][0]
    info.update(f1=f1, f2=f2, amp=amp_of(s.level), segments=[[b, n, "LR"]],
                ramp=nr)
    return out, info


def fam_multitone(s, rate, rng):
    p = s.params
    n, k = p["period"], p["periods"]
    lo, hi = p.get("band", (20.0, 20000.0))
    hi = min(hi, 0.45 * rate)
    bins = multitone_bins(n, rate, lo, hi)
    one = np.zeros((n, 2))
    phases = []
    for c in (0, 1):
        b = np.array(bins[c])
        a = np.ones(len(b)) if p["color"] == "white" else \
            1 / np.sqrt(b * rate / n)
        x, ph = low_crest(n, b, a)
        one[:, c] = x / np.sqrt(np.mean(x * x)) * rms_of(s.level)
        phases.append([round(float(v), 6) for v in ph])
    # one period quantized, then repeated: the middle periods are the same
    # samples, the first ramps up and the last down
    q = quantize(one, rng).astype(np.float64)
    w = 0.5 - 0.5 * np.cos(np.pi * (np.arange(n) + 0.5) / n)
    body = np.tile(q, (k, 1))
    body[:n] = np.rint(q * w[:, None])
    body[-n:] = np.rint(q * w[::-1, None])
    info = {"period": n, "periods": k, "bins": [bins[0], bins[1]],
            "color": p["color"]}
    if p.get("phases"):
        info["phases"] = phases
    if s.markers:
        out, lay = frame(body / FULL, rate)
        out = quantize(out).astype(np.float64)
        lay.update(info)
        return out.astype(np.int16), lay
    info.update(body=[0, len(body)], marks=[])
    return body.astype(np.int16), info


def fam_noise(s, rate, rng):
    p = s.params
    n = int(round(p["seconds"] * rate))
    a = shaped(rng, n, rate, p["color"])
    b = a if p["mode"] == "same" else shaped(rng, n, rate, p["color"])
    body = np.column_stack([a, b]) * rms_of(s.level)
    ramp(body, int(round(FADE_S * rate)))
    out, info = frame(body, rate)
    info.update(color=p["color"], mode=p["mode"])
    return out, info


def fam_mls(s, rate, rng):
    p = s.params
    bits = mls_bits(p["order"]).astype(np.float64) * 2 - 1
    n = len(bits)
    a = np.rint(rms_of(s.level) * FULL)
    one = bits * a
    k = p["periods"]
    body = np.tile(one, k)
    w = 0.5 - 0.5 * np.cos(np.pi * (np.arange(n) + 0.5) / n)
    body[:n] = np.rint(one * w)
    body[-n:] = np.rint(one * w[::-1])
    out, info = frame(np.column_stack([body, body]) / FULL, rate)
    info.update(period=n, periods=k, order=p["order"], amp=float(a))
    return quantize(out), info


def fam_impulses(s, rate, rng):
    p = s.params
    n, k = p["period"], p["periods"]
    a = np.rint(amp_of(s.level) * FULL)
    body = np.zeros((n * k, 2))
    body[::n] = a / FULL
    out, info = frame(body, rate)
    info.update(period=n, periods=k, amp=float(a),
                at=[info["body"][0] + i * n for i in range(k)])
    return quantize(out), info


def step_list(s, rate):
    """a tones signal's steps: (left Hz, right Hz, level, seconds)"""
    p = s.params
    kind = p["steps"]
    lim = 0.45 * rate
    if kind == "thirds":
        return [(f, f, s.level, p["hold"]) for f in THIRDS if f <= lim]
    if kind == "levels":
        return [(p["f"], p["f"], v, p["hold"]) for v in p["levels"]]
    if kind == "list":
        return [(f, f, s.level, p["hold"]) for f in p["freqs"]]
    if kind == "fraction":
        return [(f * rate, f * rate, s.level, p["hold"]) for f in p["freqs"]]
    if kind == "pair":
        return [(p["pair"][0], p["pair"][1], s.level, p["hold"])]
    raise ValueError(kind)


def fam_tones(s, rate, rng):
    steps = step_list(s, rate)
    nr = int(round(FADE_S * rate))
    gap = int(round(s.params.get("gap", 0.04) * rate))
    parts, lay, at = [], [], 0
    for fl, fr, level, hold in steps:
        m = int(round(hold * rate))
        t = np.arange(m + 2 * nr) / rate
        x = np.column_stack([np.sin(2 * np.pi * fl * t),
                             np.sin(2 * np.pi * fr * t)]) * amp_of(level)
        ramp(x, nr)
        parts += [x, np.zeros((gap, 2))]
        lay.append({"at": at, "ramp": nr, "len": m, "f": [fl, fr],
                    "level": level, "amp": amp_of(level)})
        at += len(x) + gap
    out, info = frame(np.concatenate(parts), rate)
    b = info["body"][0]
    for st in lay:
        st["at"] += b
    info["steps"] = lay
    return out, info


def fam_silence(s, rate, rng):
    n = int(round(s.params["seconds"] * rate))
    out, info = frame(np.zeros((n, 2)), rate)
    q = quantize(out)
    if s.params["mode"] == "tpdf":
        b, m = info["body"]
        d = np.rint(tpdf(rng, (m, 2)))
        q[b:b + m] = d.astype(np.int16)
    info["mode"] = s.params["mode"]
    return q, info


def fam_lsb(s, rate, rng):
    p = s.params
    per = p["period"].get(rate, int(round(rate / 1000)))
    hold = int(round(p["hold"] * rate)) // per * per
    gap = int(round(0.25 * rate))
    sq = np.where(np.arange(hold) % per < per // 2, 1.0, -1.0)
    parts, lay, at = [], [], 0
    for k in p["lsb"]:
        parts += [sq * k, np.zeros(gap)]
        lay.append({"at": at, "len": hold, "lsb": k, "f": rate / per})
        at += hold + gap
    body = np.concatenate(parts)
    out, info = frame(np.column_stack([body, body]) / FULL, rate)
    b = info["body"][0]
    for st in lay:
        st["at"] += b
    info.update(steps=lay, period=per)
    return quantize(out), info


def fam_dc(s, rate, rng):
    parts, lay, at = [], [], 0
    for v, secs in s.params["plateaus"]:
        m = int(round(secs * rate))
        code = np.rint(v * FULL)
        parts.append(np.full(m, code))
        lay.append({"at": at, "len": m, "code": float(code)})
        at += m
    body = np.concatenate(parts)
    out, info = frame(np.column_stack([body, body]) / FULL, rate)
    b = info["body"][0]
    for st in lay:
        st["at"] += b
    info["plateaus"] = lay
    return quantize(out), info


def snap(f, rate, n=65536):
    """f on the nearest bin of an n-point transform at rate"""
    return round(f * n / rate) * rate / n


def fam_imd(s, rate, rng):
    p = s.params
    n = int(round(p["seconds"] * rate))
    f1, f2 = snap(p["f1"], rate), snap(p["f2"], rate)
    peak = amp_of(p["peak"])
    a1 = peak * p["ratio"] / (p["ratio"] + 1)
    a2 = peak / (p["ratio"] + 1)
    t = np.arange(n) / rate
    x = a1 * np.sin(2 * np.pi * f1 * t) + a2 * np.sin(2 * np.pi * f2 * t)
    body = ramp(np.column_stack([x, x]), int(round(0.02 * rate)))
    out, info = frame(body, rate)
    info.update(f1=f1, f2=f2, a1=a1, a2=a2, kind=p["kind"])
    return out, info


def fam_jtest(s, rate, rng):
    p = s.params
    n = int(round(p["seconds"] * rate)) // 192 * 192
    a = p["code"]
    i = np.arange(n)
    x = np.where(i % 4 < 2, a, -a) - ((i // 96) % 2)
    out, info = frame(np.zeros((n, 2)), rate)
    q = quantize(out)
    b = info["body"][0]
    q[b:b + n] = np.column_stack([x, x]).astype(np.int16)
    info.update(code=a, fc=rate / 4, fm=rate / 192)
    return q, info


def fam_pilot(s, rate, rng):
    p = s.params
    f = p["f"]
    per, cyc = pilot_table(f, rate)
    a = amp_of(s.level)
    table = np.rint(a * FULL * np.sin(2 * np.pi * cyc * np.arange(per) / per
                                      + np.pi / 4))
    n = int(round(p["seconds"] * rate)) // per * per
    x = np.tile(table, n // per)
    nr = int(round(0.05 * rate)) // per * per
    x[:nr] = np.rint(x[:nr] * np.linspace(0, 1, nr, endpoint=False))
    x[n - nr:] = np.rint(x[n - nr:] * np.linspace(1, 0, nr, endpoint=False))
    out, info = frame(np.zeros((n, 2)), rate)
    q = quantize(out)
    b = info["body"][0]
    q[b:b + n] = np.column_stack([x, x]).astype(np.int16)
    info.update(f=f, table=per, cycles=cyc, amp=float(np.abs(table).max()),
                steady=[b + nr, n - 2 * nr])
    return q, info


def fam_chirps(s, rate, rng):
    p = s.params
    every = int(round(p["every"] * rate))
    m = marker(rate)
    sep = len(m) + int(round(SEP_S * rate))
    k = p["count"]
    lead = int(round(LEAD_S * rate))
    n = every * (k - 1) + sep + len(m)
    out = np.zeros((lead + n + lead, 2))
    at = [lead + i * every for i in range(k)]
    for a in at:
        out[a:a + len(m), 0] = m
        out[a + sep:a + sep + len(m), 1] = m
    return out, {"body": [lead, n], "marks": [at[0], at[-1]],
                 "mark_len": len(m), "mark_sep": sep, "at": at}


def fam_dual(s, rate, rng):
    # the left channel of each DAC only, so that the sides' difference
    # keeps both chirps
    p = s.params
    every = int(round(p["every"] * rate))
    up, down = marker(rate), marker(rate, True)
    k = p["count"]
    lead = int(round(LEAD_S * rate))
    n = every * (k - 1) + len(up)
    out = np.zeros((lead + n + lead, 4))
    at = [lead + i * every for i in range(k)]
    for a in at:
        out[a:a + len(up), 0] = up
        out[a:a + len(up), 2] = down
    return out, {"body": [lead, n], "marks": [at[0], at[-1]],
                 "mark_len": len(up), "mark_sep": 0, "at": at}


def fam_bursts(s, rate, rng):
    p = s.params
    a = amp_of(s.level)
    parts, lay, at = [], [], 0
    for f in [f for f in p["freqs"] if f <= 0.45 * rate]:
        sig = p["cycles"] / f * rate
        half = int(math.ceil(5 * sig))
        t = np.arange(-half, half + 1)
        x = a * np.exp(-0.5 * (t / sig) ** 2) * np.sin(2 * np.pi * f * t / rate)
        gap = int(round(0.05 * rate))
        parts += [x, np.zeros(gap)]
        lay.append({"f": f, "c": at + half, "sigma": sig})
        at += len(x) + gap
    body = np.concatenate(parts)
    out, info = frame(np.column_stack([body, body]), rate)
    b = info["body"][0]
    for st in lay:
        st["c"] += b
    info.update(bursts=lay, amp=a)
    return out, info


def fam_polarity(s, rate, rng):
    p = s.params
    w = int(round(p["width"] * rate))
    pulse = amp_of(s.level) * np.sin(np.pi * (np.arange(w) + 0.5) / w)
    every = int(round(p["every"] * rate))
    k = p["count"]
    body = np.zeros((2 * k * every, 2))
    cent = {"L": [], "R": []}
    for c, name in enumerate("LR"):
        for i in range(k):
            a = (c * k + i) * every + every // 2
            body[a:a + w, c] = pulse
            cent[name].append(a + w / 2)
    out, info = frame(body, rate)
    b = info["body"][0]
    info.update(width=w, L=[b + v for v in cent["L"]],
                R=[b + v for v in cent["R"]], amp=amp_of(s.level))
    return out, info


def program(rng, n, rate, corr, swing):
    """music's long-term spectrum, the channels correlated by corr, under a
    slow envelope of swing dB rms"""
    a, b = math.sqrt(corr), math.sqrt(1 - corr)
    common = shaped(rng, n, rate, "music")
    x = np.column_stack([a * common + b * shaped(rng, n, rate, "music"),
                         a * common + b * shaped(rng, n, rate, "music")])
    # the envelope: noise under 3 Hz, of unit rms, as a gain in dB
    e = rng.standard_normal(n)
    f = np.fft.rfftfreq(n, 1.0 / rate)
    e = np.fft.irfft(np.fft.rfft(e) * np.exp(-0.5 * (f / 3.0) ** 2), n)
    e /= np.sqrt(np.mean(e * e))
    return x * (10 ** (swing * e / 20))[:, None]


def notch(x, rate, f, width):
    """x without what lies within width Hz of f"""
    n = len(x)
    fr = np.fft.rfftfreq(n, 1.0 / rate)
    g = np.clip((np.abs(fr - f) - width / 2) / (width / 2), 0, 1)
    return np.fft.irfft(np.fft.rfft(x, axis=0) * g[:, None], n, axis=0)


def fam_program(s, rate, rng):
    p = s.params
    n = int(round(p["seconds"] * rate))
    x = program(rng, n, rate, p["corr"], p["swing"])
    pf = p.get("pilot")
    if pf:
        f = pf.get(rate, 19600)
        x = notch(x, rate, f, 200)
    x *= rms_of(s.level) / np.sqrt(np.mean(x * x))
    if pf:
        per, cyc = pilot_table(f, rate)
        t = np.arange(n)
        x += amp_of(p["pilot_level"]) * np.sin(
            2 * np.pi * cyc * (t % per) / per + np.pi / 4)[:, None]
    ramp(x, int(round(FADE_S * rate)))
    out, info = frame(x, rate)
    info.update(corr=p["corr"], pilot=f if pf else None,
                pilot_amp=amp_of(p["pilot_level"]) if pf else None)
    return out, info


def fam_track(s, rate, rng):
    nr = int(round(0.02 * rate))
    parts, lay, at = [], [], 0
    for kind, secs, level in s.params["segments"]:
        n = int(round(secs * rate))
        if kind == "silence":
            x = np.zeros((n, 2))
        elif kind == "program":
            x = program(rng, n, rate, 0.0, 1.5)
            x *= rms_of(level) / np.sqrt(np.mean(x * x))
        else:
            x = np.column_stack([shaped(rng, n, rate, kind),
                                 shaped(rng, n, rate, kind)]) * rms_of(level)
        ramp(x, nr)
        parts.append(x)
        lay.append({"kind": kind, "at": at, "len": n, "level": level})
        at += n
    out, info = frame(np.concatenate(parts), rate)
    b = info["body"][0]
    for st in lay:
        st["at"] += b
    info["segments"] = lay
    return out, info


FAMILIES = {"ess": fam_ess, "linsweep": fam_linsweep,
            "multitone": fam_multitone, "noise": fam_noise, "mls": fam_mls,
            "impulses": fam_impulses, "tones": fam_tones,
            "silence": fam_silence, "lsb": fam_lsb, "dc": fam_dc,
            "imd": fam_imd, "jtest": fam_jtest, "pilot": fam_pilot,
            "chirps": fam_chirps, "dual": fam_dual, "bursts": fam_bursts,
            "polarity": fam_polarity, "program": fam_program,
            "track": fam_track}


# --- the signals -------------------------------------------------------------

class Sig:
    """a test signal: its family's parameters, its rates and level (AES17
    dBFS: a sine's peak, or the rms of anything else plus 3 dB), and what it
    is for, in the doc's words. paths: L the internal loop, O the line out
    to a second card, I the second card into the Aux input, D Audio 2 read
    back through the FM (the dig path), Q the four-channel device. sides
    (from SIDES): the one-side variants made with it, at 44.1 kHz, or at
    48 kHz, or at its own rates when it has neither"""

    def __init__(self, name, family, rates, level, group, paths, what, gives,
                 good, dither=True, markers=True, **params):
        self.name, self.family, self.rates = name, family, tuple(rates)
        self.level, self.group, self.paths = level, group, paths
        self.what, self.gives, self.good = what, gives, good
        self.dither, self.markers, self.params = dither, markers, params
        self.sides = ""

    def side_rates(self):
        r = [v for v in self.rates if v in (44100, 48000)]
        return r[:1] or list(self.rates)


R4 = (44100, 48000, 22050, 11025)
R2 = (44100, 48000)
R1 = (44100,)

GROUPS = (("response", "Frequency and phase response"),
          ("channels", "Channels and polarity"),
          ("timebase", "Timebase"),
          ("amplitude", "Amplitude and noise"),
          ("distortion", "Distortion, images and jitter"),
          ("loop", "For the loop's correction"))

SIGNALS = [
    Sig("sweep", "ess", R4, -20, "response", "LOID",
        "An exponential sweep (Farina's) runs from 10 Hz to 0.95 of half the "
        "rate in 10 s, the same on both channels.",
        "Its deconvolution gives the impulse response, the magnitude, phase "
        "and group delay to 0.92 of half the rate, and the 2nd to 5th "
        "harmonics against frequency, each from its own impulse response.",
        "Expect a flat response on the dig path, and at the line out, with "
        "the DAC's stages off at 44.1 kHz, the DAC's droop: 1.2 dB at 10 kHz "
        "and 4.2 dB at 20 kHz.",
        f1=10.0, f2="nyq:0.95", sweep=10.0),
    Sig("sweep-quick", "ess", R1, -20, "response", "LOID",
        "The same sweep lasts 2 s.",
        "Its analysis gives the same results with about 7 dB more noise than "
        "the 10 s sweep's, and less room between the harmonics' impulse "
        "responses.",
        "Expect the 10 s sweep's response within its noise.",
        f1=10.0, f2="nyq:0.95", sweep=2.0),
    Sig("sweep-low", "ess", R1, -40, "response", "LOI",
        "The 10 s sweep plays 20 dB lower.",
        "Against the sweep at -20 dBFS, its response shows what depends on "
        "the level.",
        "Expect the response at -20 dBFS within 0.1 dB where the noise "
        "allows, and harmonics that fall with the level.",
        f1=10.0, f2="nyq:0.95", sweep=10.0),
    Sig("sweep-hot", "ess", R1, -3, "response", "LOI",
        "The 10 s sweep plays at -3 dBFS, near full scale.",
        "Its response and harmonics show the path near clipping, against the "
        "sweeps at -20 and -40 dBFS.",
        "Expect a response within 0.2 dB of the one at -20 dBFS. Harmonics "
        "that rise by much more than the 17 dB of the level show where a "
        "stage runs out of room.",
        f1=10.0, f2="nyq:0.95", sweep=10.0),
    Sig("sweep-lr", "ess", R2, -20, "channels", "LOI",
        "The 5 s sweep plays on the left channel alone, then on the right "
        "alone.",
        "Its analysis gives all four transfers of the stereo pair, left to "
        "left, left to right, right to left and right to right, and so the "
        "crosstalk against frequency.",
        "Expect crosstalk well under -60 dB through the line paths and near "
        "-66 dB on the internal loop. A cross term as large as the direct one "
        "means a mono capture or a path that sums the sides.",
        f1=10.0, f2="nyq:0.95", sweep=5.0, mode="lr"),
    Sig("sweep-hf", "linsweep", R2, -20, "response", "LOID",
        "A linear sweep runs from 15 kHz to half the rate in 8 s.",
        "Its analysis follows the top of the band at a resolution of a few "
        "hertz: the filter edge, the dip near 18 to 19 kHz, and any image or "
        "alias, which it tracks as other components beside the sweep.",
        "Expect a smooth roll-off to the edge with the DAC's hold, 4.2 dB "
        "down at 20 kHz at 44.1 kHz, and no notch. An image of a tone at f "
        "lies at the rate less f.",
        f1=15000.0, f2="nyq:0.999", sweep=8.0),
    Sig("sweep-sub", "ess", R1, -20, "response", "LOI",
        "An exponential sweep runs from 2 Hz to 2 kHz in 12 s.",
        "Its analysis gives the response under 20 Hz, where the coupling of "
        "the DAC, the line stages and the ADC sets the high-pass corners.",
        "Expect a high-pass of one or two poles. Its corner, where the level "
        "is 3 dB under the level at 1 kHz, is the figure to note.",
        f1=2.0, f2=2000.0, sweep=12.0),
    Sig("multitone", "multitone", R2, -20, "response", "LOID",
        "A periodic multitone holds every odd bin of a 16384-point transform "
        "from 20 Hz to 20 kHz, the bins of 1 mod 4 on the left channel and of "
        "3 mod 4 on the right, all of one amplitude, for 16 periods. Its "
        "crest factor is about 3 dB, as low as a sine's.",
        "Its analysis gives the four transfers of the stereo pair at once, "
        "averaged over 14 periods with no window, and the distortion and "
        "noise in the bins that no tone excites.",
        "Expect the sweep's response at the tones. The empty bins read the "
        "noise floor, and their rise over the silence's floor is the "
        "distortion.",
        period=16384, periods=16, color="white"),
    Sig("multitone-pink", "multitone", R1, -20, "response", "LOID",
        "The multitone's amplitude falls 3 dB an octave, for the same power "
        "in every octave.",
        "Its analysis gives the multitone's results with more of the signal "
        "at low frequencies, as in music.",
        "Expect the multitone's response, with less noise under 1 kHz and "
        "more above.",
        period=16384, periods=16, color="pink"),
    Sig("noise-white", "noise", R2, -20, "response", "LOID",
        "Gaussian white noise plays for 10 s, independent on the two "
        "channels.",
        "Welch's method gives the four transfers of the stereo pair with the "
        "coherence of each. The cross terms, from one channel to the other, "
        "show the coherence's bias for this many averages.",
        "Expect a coherence near 1 wherever the path passes the signal. A "
        "coherence that falls with frequency while the level holds means a "
        "clock that drifts during the analysis.",
        seconds=10.0, color="white", mode="ind"),
    Sig("noise-pink", "noise", R1, -20, "response", "LOID",
        "Pink noise, the same power in every octave from 20 Hz, plays for 10 "
        "s, independent on the two channels.",
        "Its analysis gives the white noise's results with more of the signal "
        "at low frequencies.",
        "Expect the white noise's response, with a coherence that holds lower "
        "down and less of it at the top.",
        seconds=10.0, color="pink", mode="ind"),
    Sig("noise-white-same", "noise", R1, -20, "channels", "LOID",
        "The white noise plays the same on both channels.",
        "Its analysis sets the two outputs against each other, for the "
        "difference in level, the phase and the delay between the channels "
        "against frequency.",
        "Expect the channels to differ by under 0.1 dB, a few degrees and a "
        "few microseconds, unless one channel's path differs.",
        seconds=10.0, color="white", mode="same"),
    Sig("noise-pink-same", "noise", R1, -20, "channels", "LOID",
        "The pink noise plays the same on both channels.",
        "Its analysis gives what the white noise on both channels gives, with "
        "more of the signal at low frequencies.",
        "Expect the results of the white noise on both channels.",
        seconds=10.0, color="pink", mode="same"),
    Sig("mls", "mls", R1, -20, "response", "LOI",
        "A maximum-length sequence of order 16, 65535 samples, plays 6 "
        "periods on both channels.",
        "Circular correlation gives the impulse response, averaged over 4 "
        "periods, and the response from it.",
        "Expect the sweep's response. An MLS spreads distortion as noise over "
        "the response, so the two differ only where the path is not linear.",
        dither=False, order=16, periods=6),
    Sig("impulses", "impulses", R1, -12, "response", "LOI",
        "A single sample at -12 dBFS comes every 8192 frames for 10 s, a "
        "click train of an exact period.",
        "Synchronous averaging gives the impulse response, its shape (the "
        "DAC's hold and filters) and the response from it, and each click's "
        "time against the recorder's clock.",
        "Expect an impulse response that settles within a few milliseconds, "
        "and clicks evenly spaced on a line whose slope is the clocks' ratio.",
        dither=False, period=8192, periods=54),
    Sig("steps", "tones", R4, -20, "response", "LOI",
        "Sines at the third-octave frequencies from 20 Hz to 20 kHz, or to "
        "0.45 of the rate, play for 0.25 s each on both channels.",
        "A sine fit gives the level and phase at each frequency, and THD+N "
        "and the 2nd to 5th harmonics against frequency.",
        "Expect the sweep's response at the same frequencies within 0.1 dB, "
        "and THD+N that the noise sets except at the top.",
        steps="thirds", hold=0.25),
    Sig("steps-hot", "tones", R1, -3, "response", "LOI",
        "The stepped sines play at -3 dBFS, near full scale.",
        "Its analysis gives THD+N and the harmonics against frequency at a "
        "high level.",
        "Expect each harmonic within 10 dB of its level at -20 dBFS, plus the "
        "17 dB of the level for a second harmonic and 34 dB for a third.",
        steps="thirds", hold=0.25),
    Sig("bursts", "bursts", R1, -20, "response", "LOI",
        "Tone bursts with a Gaussian envelope of 3 cycles sound at the "
        "octaves from 31.5 Hz to 16 kHz.",
        "The delay of each burst's envelope gives the group delay at its "
        "octave, and its level, apart from the transfer function's phase.",
        "Expect group delays within a few hundred microseconds of each other "
        "over 100 Hz. A high-pass shows as a longer delay at the bottom.",
        freqs=(31.5, 63, 125, 250, 500, 1000, 2000, 4000, 8000, 16000),
        cycles=3),
    Sig("polarity", "polarity", R1, -20, "channels", "LOI",
        "Positive half-sine pulses of 1 ms play, 8 on the left channel and "
        "then 8 on the right.",
        "The correlation with the pulse tells which output carries which "
        "input, and the sign of each.",
        "Expect the left pulses on the left output and the right ones on the "
        "right, both positive.",
        width=0.001, every=0.25, count=8),
    Sig("pilot-19600", "pilot", R1, -30, "timebase", "LOI",
        "A sine of 19600 Hz at -30 dBFS plays for 15 s, 4 cycles in 9 frames, "
        "the samples repeating exactly.",
        "Its phase gives the player's clock against the recorder's in ppm, "
        "the time error over the file, its Allan deviation and the jitter "
        "around the tone, as es1869clock reads its pilot.",
        "Expect 0 ppm on the internal loop with `a2_resample` at 0 and about "
        "+29 ppm in the Clock mode, and a few ppm against a second card, "
        "steady over the file.",
        dither=False, f=19600, seconds=15.0),
    Sig("pilot-11025", "pilot", R1, -30, "timebase", "LOI",
        "A sine of 11025 Hz, a quarter of the rate, plays at -30 dBFS for 15 "
        "s, 1 cycle in 4 frames.",
        "Its analysis gives the 19600 Hz pilot's results at a frequency where "
        "jitter weighs less and the path's response is flatter.",
        "Expect the 19600 Hz pilot's results.",
        dither=False, f=11025, seconds=15.0),
    Sig("pilot-12000", "pilot", (48000,), -30, "timebase", "LOI",
        "A sine of 12000 Hz plays at -30 dBFS for 15 s, 1 cycle in 4 frames. "
        "The chip's 48 kHz is 48012.09 Hz, so the cycles are whole in frames "
        "and the tone sounds at 12003.02 Hz.",
        "Its analysis gives the 19600 Hz pilot's results at 48 kHz.",
        "Expect +252 ppm against a recorder whose 48 kHz is true, plus the "
        "two crystals' difference.",
        dither=False, f=12000, seconds=15.0),
    Sig("chirps", "chirps", R1, None, "timebase", "LOI",
        "The marker chirp plays every second, 15 times.",
        "Each chirp's time, to a hundredth of a frame, gives the clocks' "
        "ratio from their line and the time error at each.",
        "Expect residuals of a few microseconds or less. A wandering crystal "
        "shows as a curve.",
        every=1.0, count=15),
    Sig("dual-skew", "dual", (44100, 22050), None, "timebase", "Q",
        "Four channels for the dual device carry the marker chirp on Audio "
        "1's left channel (channel 1) and the same chirp downward on Audio "
        "2's (channel 3), together every 0.5 s for 6 s, with the right "
        "channels silent.",
        "The two chirps, found apart in one recording of the mixed output, "
        "show when each DAC plays the same frame, to a hundredth of a frame.",
        "Expect a skew that stays the same within a run: 1 to 4.5 frames at "
        "44.1 kHz and up to 15 at 22050 Hz on the Armada so far.",
        every=0.5, count=13),
    Sig("levels", "tones", R2, None, "amplitude", "LOI",
        "A 997 Hz sine steps from -100 to -20 dBFS in steps of 10 dB, 0.5 s "
        "each.",
        "Its analysis gives the gain at each level against the gain at -20 "
        "dBFS, which is the linearity, and THD+N against level.",
        "Expect the gain within 0.1 dB down to -60 dBFS and within 1 dB at "
        "-80, and THD+N that rises 10 dB a step once the noise dominates.",
        steps="levels", f=997.0, hold=0.5,
        levels=(-100, -90, -80, -70, -60, -50, -40, -30, -20)),
    Sig("levelcal-hot", "tones", R2, None, "amplitude", "LOID",
        "The level calibration is a 997 Hz sine from -60 dBFS up to -0.1 "
        "dBFS, 0.5 s a step, in steps of 0.5 dB over the top 6 dB.",
        "Its analysis gives the path's gain and the level at which a stage "
        "starts to clip: the lowest step whose gain leaves the small-signal "
        "gain by 0.1 dB, or whose harmonics rise 10 dB over their trend. It "
        "then gives the highest peak that a test signal may have, by the rule "
        "of TEST_LEVELS.md.",
        "Expect a gain of -1.8 dB on the internal loop at its calibrated "
        "setting, linear to 0.01 dB, with THD+N of -62 dB up to -0.5 dBFS. A "
        "clip point that the record gain moves belongs to the ADC.",
        steps="levels", f=997.0, hold=0.5,
        levels=(-60, -40, -30, -24, -20, -17, -14, -12, -10, -9, -8, -7,
                -6, -5.5, -5, -4.5, -4, -3.5, -3, -2.5, -2, -1.5, -1, -0.5,
                -0.1)),
    Sig("separation", "tones", R2, -20, "channels", "LOI",
        "A tone of 997 Hz plays on the left side and one of 1499 Hz on the "
        "right, at -20 dBFS, for 3 s.",
        "Each tone's level in each recorded channel shows which side each "
        "channel hears and how far apart the sides stay. Run it first on a "
        "path whose cabling is new.",
        "Expect each channel to hear its own tone and the other far lower: "
        "-66 dB on the internal loop, 66 to 73 dB apart into the VX, and 60 "
        "dB back into the Aux input, whose sides are swapped. A path of one "
        "channel hears one tone, and a sum or difference hears both.",
        steps="pair", pair=(997.0, 1499.0), hold=3.0),
    Sig("dr60", "tones", R1, -60, "amplitude", "LOI",
        "A 997 Hz sine plays at -60 dBFS for 6 s.",
        "Its THD+N, unweighted and A-weighted, plus 60 dB is AES17's dynamic "
        "range.",
        "Expect the noise floor's distance from full scale, some 65 to 70 dB "
        "through the chip's record paths.",
        steps="list", freqs=(997.0,), hold=6.0),
    Sig("silence", "silence", R2, None, "amplitude", "LOID",
        "Digital silence lasts 8 s between the markers.",
        "Its analysis gives the noise floor: its level unweighted and "
        "A-weighted from 20 Hz to 20 kHz, its spectrum, the DC and the idle "
        "tones above it.",
        "Expect the record mixer's floor, which read -68 dBFS at Capture "
        "Volume 15, with a line at 15625 Hz near -84 dBFS. A DC offset is the "
        "ADC's.",
        dither=False, seconds=8.0, mode="zero"),
    Sig("dither", "silence", R2, None, "amplitude", "LOID",
        "Triangular dither of 1 LSB plays alone for 8 s, about -96 dBFS.",
        "Against the digital silence, its floor shows the idle tones that "
        "only digital zero brings out, with the DAC's input moving.",
        "Expect the silence's floor within 1 dB, since 16-bit dither lies "
        "some 30 dB under the analog noise.",
        dither=False, seconds=8.0, mode="tpdf"),
    Sig("lsb", "lsb", R1, None, "amplitude", "LOID",
        "Square waves of 1, 2, 4 and 8 LSB about zero play for 3 s each at "
        "1002 Hz, 44 frames a period, with no dither.",
        "Each square's fundamental against 4/pi of its height, and its even "
        "harmonics, show the DAC's linearity at its last bits.",
        "Expect each step 6 dB over the one before, within the fit's noise, "
        "which the report gives.",
        dither=False, period={44100: 44, 48000: 48}, hold=3.0,
        lsb=(1, 2, 4, 8)),
    Sig("dc-step", "dc", R1, None, "amplitude", "LOID",
        "DC of +0.1 of full scale plays for 2 s, then zero for 2 s, -0.1 for "
        "2 s and zero again.",
        "Each step's decay tells whether the path passes DC, and gives the "
        "time constant and corner of its high-pass.",
        "Expect a decay of one pole whose corner matches `sweep-sub`'s, and "
        "the recording's own DC between the steps.",
        dither=False, plateaus=((0.1, 2.0), (0.0, 2.0), (-0.1, 2.0),
                                (0.0, 2.0))),
    Sig("imd-smpte", "imd", R1, None, "distortion", "LOI",
        "Tones of 60 Hz and 7 kHz play at 4 to 1, with a combined peak of -12 "
        "dBFS, on exact bins, for 5 s.",
        "The sidebands of 7 kHz at multiples of 60 Hz, against 7 kHz, give "
        "the SMPTE intermodulation.",
        "Expect sidebands near the noise. A rise with the level points at a "
        "stage that the low tone modulates.",
        kind="smpte", f1=60.0, f2=7000.0, ratio=4.0, peak=-12.0,
        seconds=5.0),
    Sig("imd-smpte-hot", "imd", R1, None, "distortion", "LOI",
        "The SMPTE pair plays with a combined peak of -3 dBFS.",
        "Its analysis gives the SMPTE intermodulation near full scale.",
        "Expect sidebands that rise with the level, against the pair at -12 "
        "dBFS.",
        kind="smpte", f1=60.0, f2=7000.0, ratio=4.0, peak=-3.0,
        seconds=5.0),
    Sig("imd-ccif", "imd", R2, None, "distortion", "LOI",
        "Tones of 19 kHz and 20 kHz play at one amplitude, with a combined "
        "peak of -12 dBFS, on exact bins, for 5 s.",
        "Its analysis gives the twin-tone intermodulation: the difference "
        "tone at 1 kHz, the products at 18 and 21 kHz, and the folded "
        "products of a path without an anti-alias filter.",
        "Expect products near the noise. The ADC with its filter off folds "
        "what lies above half the rate back into the band.",
        kind="ccif", f1=19000.0, f2=20000.0, ratio=1.0, peak=-12.0,
        seconds=5.0),
    Sig("imd-ccif-hot", "imd", R1, None, "distortion", "LOI",
        "The twin tone plays with a combined peak of -3 dBFS.",
        "Its analysis gives the twin-tone intermodulation near full scale.",
        "Expect products that rise with the level, against the pair at -12 "
        "dBFS.",
        kind="ccif", f1=19000.0, f2=20000.0, ratio=1.0, peak=-3.0,
        seconds=5.0),
    Sig("jtest", "jtest", R1, -21.1, "distortion", "LOID",
        "Dunn's J-test plays at a low level: a quarter of the rate as samples "
        "of +-2048, with an LSB square wave at a 192nd of the rate, exact.",
        "The tone's sidebands at multiples of a 192nd of the rate, beyond the "
        "file's own, give data-dependent jitter and interference as jitter.",
        "Expect no sideband over the file's own. A pair of sidebands at -100 "
        "dBc of an 11 kHz tone is about 0.3 ns of jitter.",
        dither=False, code=2048, seconds=5.0),
    Sig("jtest-hot", "jtest", R2, -3, "distortion", "LOID",
        "The J-test plays as Dunn defined it: +-16384, a sine of -3 dBFS, "
        "with the LSB square that flips every bit under the top two.",
        "Its analysis gives the J-test's results with the most bits changing.",
        "Expect sidebands no higher than at the low level.",
        dither=False, code=16384, seconds=5.0),
    Sig("nyquist", "tones", R1, -20, "distortion", "O",
        "Sines from 15 kHz to 21.9 kHz play for 1 s each.",
        "Its analysis gives the level near the top of the band and the DAC's "
        "images at the rate less f, which a second card at 48 kHz hears up to "
        "24 kHz.",
        "Expect images some 10 dB or more under the tone with the DAC's "
        "stages off, and far lower with `a2_oversample` or the filters.",
        steps="list", hold=1.0,
        freqs=(15000.0, 18000.0, 19000.0, 20000.0, 20500.0, 21000.0,
               21500.0, 21900.0)),
    Sig("alias", "tones", (48000,), -20, "distortion", "I",
        "Sines at 48 kHz from 20 kHz to 23.9 kHz play for 1 s each, for a "
        "second card to play into the Aux input.",
        "The ADC's anti-aliasing at 44.1 kHz shows in each tone over 22.05 "
        "kHz, which comes back at 44.1 kHz less f.",
        "Expect aliases close to the tone's level with the Audio 1 filter "
        "off, and far lower with it on.",
        steps="list", hold=1.0,
        freqs=(20000.0, 21000.0, 22000.0, 22500.0, 23000.0, 23500.0,
               23900.0)),
    Sig("images", "tones", (22050, 11025), -20, "distortion", "LO",
        "Sines at fractions of the rate from 0.05 to 0.45 play for 1 s each, "
        "at the low rates.",
        "Recorded at 44.1 or 48 kHz, they show the DAC's images at the rate "
        "less f, in the audible band at these rates.",
        "Expect, at 22050 Hz, an image of 5 kHz at 17050 Hz, about 11 dB "
        "under the tone with the stages off, and far lower with the filter.",
        steps="fraction", hold=1.0,
        freqs=(0.05, 0.1, 0.2, 0.25, 0.35, 0.45)),
    Sig("cal", "multitone", R2, -20, "loop", "LOI",
        "The boot calibration plays 10 periods of 8192 frames with every odd "
        "bin from 20 Hz to 20 kHz, the bins of 1 mod 4 on the left channel "
        "and of 3 mod 4 on the right, each octave over 63 Hz within 1 dB of "
        "the same power. Its crest factor is about 3.2 dB, a ramp covers the "
        "first and the last period, and it has no markers.",
        "Its analysis gives the loop's four transfers from 8 periods averaged "
        "with no window, the noise and distortion in the empty bins and how "
        "far the periods agree, and runs the boot method of the section on "
        "the loop's calibration on the same recording.",
        "Expect 0 ppm between the periods, agreement near 1, the sweep's "
        "response at the bins, and the boot method within 0.01 dB of the full "
        "analysis.",
        markers=False, period=8192, periods=10, color="pink", phases=True),
    Sig("verify", "ess", R2, -20, "loop", "LOI",
        "The exponential sweep lasts 4 s, for checking a correction.",
        "Its analysis gives the response after a correction, which should "
        "come back flat, and its widest swing from 20 Hz to 20 kHz.",
        "Expect a response within a few tenths of a dB from 20 Hz to 20 kHz "
        "once a correction applies.",
        f1=10.0, f2="nyq:0.95", sweep=4.0),
    Sig("program", "program", R2, -24, "loop", "LOI",
        "Noise with music's long-term spectrum plays for 15 s, the channels "
        "correlated by 0.6, with a crest factor near 16 dB from a slow "
        "envelope.",
        "Welch's method gives the four transfers with correlated inputs, as a "
        "live analysis meets them in music, and the coherence that a "
        "music-like spectrum leaves at the top.",
        "Expect the white noise's response wherever the coherence is near 1, "
        "and a coherence that falls at the top, where music is quiet.",
        seconds=15.0, corr=0.6, swing=1.5),
    Sig("program-pilot", "program", R2, -24, "loop", "LOI",
        "The program plays for 30 s with a pilot at -40 dBFS, 19600 Hz at "
        "44.1 kHz and 19200 Hz at 48 kHz, and nothing else within 100 Hz of "
        "it.",
        "Its analysis gives the loop's response and the clocks at once, as a "
        "live analysis would watch both under music.",
        "Expect the program's response, and the pilot's ppm as on its own.",
        seconds=30.0, corr=0.6, swing=1.5,
        pilot={44100: 19600, 48000: 19200}, pilot_level=-40.0),
    Sig("track", "track", R1, None, "loop", "LOI",
        "Segments of 4 s change the spectrum: pink, white, bass, treble, mid "
        "band, pink 20 dB lower, 2 s of silence and program.",
        "Its analysis gives how fast a running estimate of the loop settles "
        "after each change, how far it strays in the bands a segment leaves "
        "empty, and what silence does to it.",
        "Expect each segment's response within 0.5 dB of the white noise's in "
        "the bands it excites, and an estimate that silence leaves alone.",
        segments=(("pink", 4.0, -20), ("white", 4.0, -20),
                  ("bass", 4.0, -20), ("treble", 4.0, -20),
                  ("mid", 4.0, -20), ("pink", 4.0, -40),
                  ("silence", 2.0, None), ("program", 4.0, -24))),
]

# the one-side variants made by default, for a recorder that hears one
# side, or the sides' sum or difference, and for checks of one side: the
# signals whose sides carry the same body. --side makes the other side,
# and any signal's
SIDES = {"sweep": "lr", "sweep-hf": "lr", "verify": "lr", "steps": "lr",
         "levelcal-hot": "lr",
         "levels": "lr", "dr60": "lr",
         "sweep-quick": "l", "sweep-low": "l", "sweep-hot": "l",
         "sweep-sub": "l", "steps-hot": "l", "bursts": "l",
         "pilot-19600": "l", "pilot-11025": "l", "pilot-12000": "l",
         "lsb": "l", "dc-step": "l", "imd-smpte": "l",
         "imd-smpte-hot": "l", "imd-ccif": "l", "imd-ccif-hot": "l",
         "jtest": "l", "jtest-hot": "l", "nyquist": "l", "images": "l"}
for _s in SIGNALS:
    _s.sides = SIDES.get(_s.name, "")

BY_NAME = {s.name: s for s in SIGNALS}

# the driver's settings that change a path, with the signals that show it
SETTINGS = [
    ("`a2_resample`", "0 and 2", "pilot-19600, chirps, cal",
     "The loop reads 0 ppm with 0 (Off). The Clock mode resamples Audio 2 "
     "to true time, so the loop then reads its correction, about +29 ppm, "
     "and a periodic signal no longer comes back periodic."),
    ("`a2_treble`", "0 and 1", "sweep, sweep-hf, verify",
     "It shows the treble correction's lift, up to 4 dB near 20 kHz on the "
     "dig path. At the line out, with the DAC's stages off, the droop "
     "comes back to within 0.2 dB."),
    ("`audio2_oversampling`", "0 and 1", "sweep, sweep-hf, nyquist",
     "It is ESS's 4x mode, which takes 1.74 dB off everything, falls 6 dB "
     "by 20.4 kHz and puts the images 25 dB lower."),
    ("`audio2_filter`", "0 and 1", "sweep, images",
     "It puts the switched-capacitor filter after Audio 2's DAC, with its "
     "corner at `filter_corner` below 44.1 kHz."),
    ("`audio1_filter`", "0 and 1", "alias, sweep, imd-ccif",
     "It is the ADC's anti-alias filter. With 0, what lies over half the "
     "rate folds back; with 1, the filter's response takes it."),
    ("`a2_oversample`", "0, 1 and 2", "nyquist, sweep-hf, silence",
     "It runs Audio 2's DAC at 2x or 3x of the stream, which moves the "
     "images up and takes less of the treble."),
    ("`filter_corner`", "875 and others", "sweep, images",
     "It moves the filters' corner at the rates under 44.1 kHz, with a "
     "filter on."),
    ('"PCM Capture Volume" and "Capture Volume"', "the steps",
     "levelcal-hot, silence, sweep-hot",
     "They set the internal loop's gain, noise floor and clip point. 14 and "
     "3 are its calibrated setting."),
    ('"Aux Capture Volume"', "the steps", "levelcal-hot, silence",
     "It is the second card's way into the ES1869. 15, with \"Capture "
     "Volume\" at 4, is its calibrated setting."),
    ('"Master Playback Volume"', "55 and others", "levelcal-hot, sweep-hot",
     "It sets the line out's level into the second card, and 55 (-6 dB) "
     "keeps the VX at \"Mic Capture Volume\" 1 from clipping."),
    ("`adc_offset`", "0 and the saved value", "dc-step, silence",
     "It sets the recording's DC."),
    ("`dual_playback`", "1", "dual-skew",
     "The four-channel device opens only with it."),
]


# --- making them -------------------------------------------------------------

def file_name(sig, rate, side=None):
    return "%s%s-%s" % (sig.name, "-" + side if side else "", rate_tag(rate))


class Made:
    """a rendered signal: 16-bit samples and the layout"""

    def __init__(self, sig, rate, side, data, info):
        self.sig, self.rate, self.side = sig, rate, side
        self.data, self.info = data, info
        self.name = file_name(sig, rate, side)

    def floats(self):
        return self.data.astype(np.float64) / FULL


def seed_of(name):
    return zlib.crc32(name.encode("ascii"))


def render(sig, rate, side=None):
    """a signal at a rate, the same samples at every call; side "l" or "r"
    keeps that side's body alone, the markers as they are"""
    name = file_name(sig, rate)
    rng = np.random.RandomState(seed_of(name))
    out, info = FAMILIES[sig.family](sig, rate, rng)
    if out.dtype != np.int16:
        dither = np.random.RandomState(seed_of(name + "/dither")) \
            if sig.dither else None
        # a body the same on both sides stays the same sample for sample
        b, n = info["body"]
        same = out.shape[1] == 2 and np.array_equal(out[b:b + n, 0],
                                                    out[b:b + n, 1])
        out = quantize(out, dither, same)
    if side:
        b, n = info["body"]
        out[b:b + n, "lr".index(side) ^ 1] = 0
    info.update(family=sig.family, rate=rate, level=sig.level, side=side)
    return Made(sig, rate, side, out, info)


def true_peak(x):
    """the highest level between the samples too, 4x oversampled, in dBFS"""
    n = len(x)
    spec = np.fft.rfft(x, axis=0)
    up = np.fft.irfft(spec, 4 * n, axis=0) * 4
    return db(max(np.abs(up).max(), np.abs(x).max()))


def measures(m):
    """the level of the body (AES17) and the true peak of the file"""
    x = m.floats()
    b, n = m.info["body"]
    body = x[b:b + n]
    rms = np.sqrt(np.mean(body * body, axis=0))
    level = db(math.sqrt(2) * float(rms.max())) if n else -200.0
    return level, true_peak(x)


def manifest_entry(m, data):
    level, peak = measures(m)
    info = dict(m.info)
    return {"name": m.name, "file": m.name + ".wav", "signal": m.sig.name,
            "side": m.side, "family": m.sig.family, "rate": m.rate,
            "channels": int(m.data.shape[1]), "frames": int(len(m.data)),
            "seconds": round(len(m.data) / m.rate, 4),
            "sha256": hashlib.sha256(data).hexdigest(),
            "level_dbfs": round(level, 2), "true_peak_dbfs": round(peak, 2),
            "hot": peak >= HOT_DB, "layout": jsonable(info)}


def jsonable(v):
    if isinstance(v, dict):
        return {str(k): jsonable(x) for k, x in v.items()}
    if isinstance(v, (list, tuple)):
        return [jsonable(x) for x in v]
    if isinstance(v, (np.integer,)):
        return int(v)
    if isinstance(v, (float, np.floating)):
        return round(float(v), 9)
    return v


def variants(s, rate=None, side=None):
    """a signal's files: (rate, side), its own and its default sides', or
    those of one rate or one side"""
    rates = (rate,) if rate else s.rates
    if side:
        return [(r, side) for r in rates]
    out = [(r, None) for r in rates]
    srates = (rate,) if rate else s.side_rates()
    out += [(r, d) for r in srates for d in s.sides]
    return out


def wanted(names, rate=None, side=None):
    """(signal, rate, side) for the names, patterns or all"""
    out = []
    for s in SIGNALS:
        for r, d in variants(s, rate, side):
            full = file_name(s, r, d)
            if not names or any(fnmatch.fnmatchcase(full, p) or
                                fnmatch.fnmatchcase(s.name, p)
                                for p in names):
                out.append((s, r, d))
    return out


RATES = (8000, 11025, 16000, 22050, 24000, 32000, 44100, 48000, 88200, 96000)


def find(name, rate=None):
    """the signal, rate and side a file's name stands for"""
    base = os.path.basename(name)
    if base.lower().endswith(".wav"):
        base = base[:-4]
    head, tag = base, None
    if base not in BY_NAME:
        head, _, tag = base.rpartition("-")
    side = None
    if head not in BY_NAME and head[-2:] in ("-l", "-r"):
        head, side = head[:-2], head[-1]
    s = BY_NAME.get(head)
    if s is None:
        raise KeyError("no signal %s" % name)
    if rate or tag is None:
        return s, rate or s.rates[0], side
    for r in s.rates + RATES:
        if rate_tag(r) == tag:
            return s, r, side
    raise KeyError("no rate %s for %s" % (tag, s.name))


def write_all(outdir, pairs, quiet=False):
    os.makedirs(outdir, exist_ok=True)
    path = os.path.join(outdir, "manifest.json")
    entries = {}
    if os.path.exists(path):
        with open(path, encoding="ascii") as f:
            entries = {e["name"]: e for e in json.load(f)["signals"]}
    for s, r, d in pairs:
        m = render(s, r, d)
        data = wav_bytes(m.data, r)
        with open(os.path.join(outdir, m.name + ".wav"), "wb") as f:
            f.write(data)
        e = manifest_entry(m, data)
        entries[m.name] = e
        if not quiet:
            print("%-24s %6d Hz %d ch %7.2f s %7.2f dBFS peak%s" % (
                e["file"], r, e["channels"], e["seconds"],
                e["true_peak_dbfs"], ", hot" if e["hot"] else ""))
    order = {file_name(s, r, d): i
             for i, (s, r, d) in enumerate(wanted([]))}
    rows = sorted(entries.values(), key=lambda e: (order.get(e["name"], 1e9),
                                                   e["name"]))
    with open(path, "w", encoding="ascii") as f:
        json.dump({"generator": "tools/testsig.py", "full_scale": FULL,
                   "signals": rows}, f, indent=1)
        f.write("\n")
    with open(os.path.join(outdir, "SHA256SUMS"), "w", encoding="ascii") as f:
        for e in rows:
            f.write("%s  %s\n" % (e["sha256"], e["file"]))
    total = sum(os.path.getsize(os.path.join(outdir, e["file"]))
                for e in rows if os.path.exists(os.path.join(outdir,
                                                             e["file"])))
    if not quiet:
        print("%d files, %.1f MB in %s" % (len(rows), total / 1e6, outdir))


# --- the docs ----------------------------------------------------------------

LEVELS_DOC = os.path.join(ROOT, "docs", "TEST_LEVELS.md")
CAT_BEGIN, CAT_END = "<!-- signals -->", "<!-- /signals -->"
MAT_BEGIN, MAT_END = "<!-- paths -->", "<!-- /paths -->"
SET_BEGIN, SET_END = "<!-- settings -->", "<!-- /settings -->"
LEV_BEGIN, LEV_END = "<!-- levels -->", "<!-- /levels -->"
MARGIN = 3.0                # the levels rule's margin under each clip point

# the paths, with the gain from the file's dBFS to the recording's at 997
# Hz in the calibrated settings of TEST_LEVELS.md (measured 2026-11-08),
# the higher where the sides differ
PATHS = (("L", "Loop", -1.8), ("O", "Out", -2.65), ("I", "In", -1.2),
         ("D", "Dig", 0.0), ("Q", "Quad", None))

_STATS = {}


def stats(s):
    """a signal's length in seconds, the level of its body (AES17 dBFS) and
    its true peak, at its first rate"""
    if s.name not in _STATS:
        m = render(s, s.rates[0])
        level, peak = measures(m)
        _STATS[s.name] = (len(m.data) / m.rate, level, peak)
    return _STATS[s.name]


def khz(rate):
    return {44100: "44.1", 22050: "22.05", 11025: "11.025"}.get(
        rate, "%g" % (rate / 1000))


def level_text(s, long=False):
    """a signal's level in dBFS, short for the table or as a phrase"""
    if s.level is not None:
        return "%g%s" % (s.level, " dBFS" if long else "")
    if s.family == "tones":
        lv = [v for _f, _g, v, _h in step_list(s, s.rates[0])]
        return "%g to %g%s" % (min(lv), max(lv), " dBFS" if long else "")
    if s.family == "imd":
        return "%g%s" % (s.params["peak"], " dBFS peak" if long else " pk")
    if s.family == "lsb":
        return "1 to 8 LSB" if long else "1-8 LSB"
    if s.family == "dc":
        return "DC of -20 dBFS" if long else "-20 DC"
    if s.family == "silence":
        if s.params["mode"] == "zero":
            return "digital zero" if long else "zero"
        return "-96 dBFS" if long else "-96"
    if s.family == "track":
        return "-40 to -20 dBFS" if long else "-40 to -20"
    return "-20 dBFS peak" if long else "-20 pk"


def rates_text(s):
    r = [khz(v) for v in s.rates]
    return (", ".join(r[:-1]) + " and " + r[-1] if len(r) > 1 else r[0]) + \
        " kHz"


def catalog():
    out = [CAT_BEGIN, "", "| Signal | Rates | Length | Level | "
           "Sides |", "|---|---|---|---|---|"]
    for s in SIGNALS:
        out.append("| `%s` | %s | %.1f s | %s | %s |" % (
            s.name, " ".join(rate_tag(r) for r in s.rates), stats(s)[0],
            level_text(s), ", ".join(s.sides)))
    for key, title in GROUPS:
        out += ["", "### %s" % title, ""]
        for s in [s for s in SIGNALS if s.group == key]:
            out.append("* **%s** (%s, %.1f s, %s). %s %s %s" % (
                s.name, rates_text(s), stats(s)[0], level_text(s, True),
                s.what, s.gives, s.good))
    out += ["", CAT_END]
    return "\n".join(out)


def matrix():
    out = [MAT_BEGIN, "", "| Signal | " + " | ".join(t for _c, t, _g in PATHS)
           + " |", "|---|" + "---|" * len(PATHS)]
    for s in SIGNALS:
        out.append("| `%s` | " % s.name + " | ".join(
            "x" if c in s.paths else "" for c, _t, _g in PATHS) + " |")
    out += ["", MAT_END]
    return "\n".join(out)


def settings_list():
    out = [SET_BEGIN, ""]
    for key, values, sigs, text in SETTINGS:
        names = ", ".join("`%s`" % v.strip() for v in sigs.split(","))
        out.append("* %s (%s; %s). %s" % (key, values, names, text))
    out += ["", SET_END]
    return "\n".join(out)


def levels_tables():
    """for each path, every signal it plays: the body's level and the true
    peak in the file, the peak at the recorder at the path's calibrated
    gain, and the room left under the recorder's full scale"""
    out = [LEV_BEGIN]
    for code, title, gain in PATHS:
        if gain is None:
            continue
        out += ["", "### %s, %+.2f dB" % (title, gain), "",
                "| Signal | Level | Peak | At rec | Room |",
                "|---|---|---|---|---|"]
        for s in [s for s in SIGNALS if code in s.paths]:
            _n, level, peak = stats(s)
            room = -(peak + gain)
            out.append("| `%s` | %.1f | %.1f | %.1f | %.1f%s |" % (
                s.name, level, peak, peak + gain, room,
                " short" if room < MARGIN else ""))
    out += ["", LEV_END]
    return "\n".join(out)


DOCS = {DOC: ((CAT_BEGIN, CAT_END, catalog), (MAT_BEGIN, MAT_END, matrix),
              (SET_BEGIN, SET_END, settings_list)),
        LEVELS_DOC: ((LEV_BEGIN, LEV_END, levels_tables),)}


def doc_text(path, old):
    """a doc with the text between each pair of its markers made again"""
    sys.path.insert(0, os.path.join(ROOT, "tools"))
    import mdfmt
    nl = "\r\n" if "\r\n" in old else "\n"
    for begin, end, make in DOCS[path]:
        a, b = old.find(begin), old.find(end)
        if a < 0 or b <= a:
            raise SystemExit("%s: no %s markers" % (path, begin))
        new = mdfmt.format_text(make()).rstrip("\n").replace("\n", nl)
        old = old[:a] + new + old[b + len(end):]
    return old


def doc(check):
    stale = 0
    for path in DOCS:
        with open(path, encoding="ascii", newline="") as f:
            old = f.read()
        new = doc_text(path, old)
        if new == old:
            continue
        if check:
            print("%s is stale: run python3 tools/testsig.py --doc" % path)
            stale = 1
            continue
        with open(path, "w", encoding="ascii", newline="") as f:
            f.write(new)
    return stale


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("outdir", nargs="?")
    ap.add_argument("-o", dest="out", metavar="OUTDIR",
                    help="the directory, out/testsig by default")
    ap.add_argument("--only", nargs="+", metavar="NAME", default=[])
    ap.add_argument("--rate", type=int, metavar="HZ")
    ap.add_argument("--side", choices=("l", "r"),
                    help="the left or the right side alone")
    ap.add_argument("--list", action="store_true")
    ap.add_argument("--doc", action="store_true")
    ap.add_argument("--check", action="store_true")
    args = ap.parse_args(argv)
    if args.doc:
        return doc(args.check)
    # every pattern names something, so that a directory after --only,
    # which takes it as a name, is an error and not the default directory
    for p in args.only:
        if not wanted([p], args.rate, args.side):
            ap.error("no signal matches %s (give the directory with -o)" % p)
    pairs = wanted(args.only, args.rate, args.side)
    if args.list:
        for s, r, d in pairs:
            print("%-24s %-9s %s" % (file_name(s, r, d), s.family,
                                     s.what.split(".")[0]))
        return 0
    write_all(args.out or args.outdir or OUT, pairs)
    return 0


if __name__ == "__main__":
    sys.exit(main())
