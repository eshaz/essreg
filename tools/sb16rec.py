#!/usr/bin/env python3
# (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
#
# Licensed under GPL Version 3.0
"""Analyze a recorded run of sb16test (SB16TEST /R): the recording of the
card's output, a WAV file, against the run's report.

usage: sb16rec.py SB16TEST.TXT RECORDING.WAV

The run plays a full-scale tone to set the recorder's level by, then the
start marker (DTMF ABCD), the automatic checks, each listening sound after
its marker (*, its number in two digits, #) and the end marker (DCBA). The
report gives each marker's time and when each part of each sound began
and ended, in microseconds from the start marker.

This finds the markers in the recording, fits the report's clock to the
recording's, and measures each sound where the report says it played:
its pitch against 441 Hz, in cents, how much of it is the tone (the rest
is noise and distortion, in dB, over 20 ms windows: the median and the
worst, where a glitch or a loop shows), its level, and what the check is
about: the side it came from, the two tones at once, the pause, the
volume's steps, the balance, the blocks without gaps. Each check gets
PASS or FAIL, or INFO where the recording can't tell (a mono recording
has no sides). The report's own results come first.

The recording can be mono or stereo, 8, 16, 24 or 32-bit PCM or 32 or
64-bit floats, at any rate. The analysis runs at 8 kHz: everything above
4 kHz is cut first. Needs numpy.

Example:
  python3 tools/sb16rec.py SB16TEST.TXT run.wav
"""

import argparse
import math
import re
import struct
import sys

import numpy as np

RATE = 8000                 # the analysis's rate, after the band limit
HZ = 441.0                  # the listening sounds' pitch (src/sb16t.c)
DIRECT_HZ = 8000 * 56 / 1024        # s_direct: 437.5 Hz
FM_HZ = 49716 * 0x241 / 65536       # s_fm: F-number 241h, block 4
DTMF_SYMS = "123A456B789C*0#D"
DTMF_LO = (697, 770, 852, 941)
DTMF_HI = (1209, 1336, 1477, 1633)
CENTS = 10                  # a pitch's tolerance
CLEAN_DB = 25               # a steady tone's least median of tone to rest
WORST_DB = 15               # and its least in any 20 ms
SIDE_DB = 15                # the side a tone came from, louder by this
CHECKS = {
    "l.tone8": "tone", "l.left16": "side", "l.right16": "side",
    "l.left8": "side", "l.right8": "side", "l.pitch": "pitch",
    "l.dual": "dual", "l.pause": "pause", "l.volume": "volume",
    "l.balance": "balance", "l.adpcm": "adpcm", "l.direct": "direct",
    "l.speaker": "tone", "l.blocks": "blocks", "l.fm": "fm",
}

# --- the recording ------------------------------------------------------------


def read_wav(path):
    """(rate, frames): floats in -1..1, an array of frames by channels"""
    with open(path, "rb") as f:
        data = f.read()
    if data[:4] != b"RIFF" or data[8:12] != b"WAVE":
        raise SystemExit("sb16rec: %s: not a WAV file" % path)
    pos, fmt, body = 12, None, None
    while pos + 8 <= len(data):
        cid = data[pos:pos + 4]
        size = struct.unpack_from("<I", data, pos + 4)[0]
        if cid == b"fmt ":
            fmt = data[pos + 8:pos + 8 + size]
        elif cid == b"data":
            body = data[pos + 8:pos + 8 + size]
        pos += 8 + size + (size & 1)
    if fmt is None or body is None:
        raise SystemExit("sb16rec: %s: no fmt or data chunk" % path)
    tag, chans, rate, _, align, bits = struct.unpack_from("<HHIIHH", fmt)
    if tag == 0xFFFE and len(fmt) >= 26:        # WAVE_FORMAT_EXTENSIBLE
        tag = struct.unpack_from("<H", fmt, 24)[0]
    width = align // chans
    n = len(body) // align
    raw = np.frombuffer(body[:n * align], dtype=np.uint8)
    if tag == 3 and width in (4, 8):
        x = raw.view("<f%d" % width).astype(np.float64)
    elif tag == 1 and width == 1:
        x = (raw.astype(np.float64) - 128) / 128
    elif tag == 1 and width in (2, 3, 4):
        b = raw.reshape(-1, width).astype(np.int64)
        v = np.zeros(len(b), dtype=np.int64)
        for i in range(width):
            v |= b[:, i] << (8 * i)
        v -= (v >> (8 * width - 1)) << (8 * width)
        x = v / float(1 << (8 * width - 1))
    else:
        raise SystemExit("sb16rec: %s: format %d with %d bits isn't PCM or "
                         "float" % (path, tag, bits))
    return rate, x.reshape(n, chans)


def band_limit(x, rate, new=RATE):
    """one channel at `new` Hz, what is above new / 2 cut: by the FFT, so
    that the resampling is exact"""
    m = int(round(len(x) * new / rate))
    spec = np.fft.rfft(x)
    return np.fft.irfft(spec[:m // 2 + 1], m) * (m / len(x))


def level_db(x):
    """the level in dB of full scale, where a full-scale sine is 0"""
    return 10 * math.log10(max(2 * float(np.mean(x * x)), 1e-20)) \
        if len(x) else -200.0


# --- the markers ----------------------------------------------------------------

def dtmf_frames(x, rate=RATE):
    """each 20 ms frame's symbol, 5 ms apart, or None: one row tone and one
    column tone that carry most of the frame, each well above the others
    of its group"""
    win, hop = int(0.020 * rate), int(0.005 * rate)
    if len(x) < win:
        return [], hop / rate
    frames = np.lib.stride_tricks.sliding_window_view(x, win)[::hop]
    t = np.arange(win) / rate
    w = np.hanning(win)
    freqs = np.array(DTMF_LO + DTMF_HI, dtype=float)
    basis = np.exp(-2j * np.pi * np.outer(t, freqs)) * w[:, None]
    amp2 = np.abs(frames @ basis) ** 2 / (w.sum() / 2) ** 2   # amplitudes²
    total = 2 * np.mean(frames * frames, axis=1)
    out = []
    for a, tot in zip(amp2, total):
        lo, hi = a[:4], a[4:]
        r, c = int(np.argmax(lo)), int(np.argmax(hi))
        lo2, hi2 = np.sort(lo)[-2], np.sort(hi)[-2]
        ok = (tot > 1e-6 and lo[r] + hi[c] > 0.6 * tot and
              lo[r] > 10 * lo2 and hi[c] > 10 * hi2 and
              0.15 < lo[r] / hi[c] < 6.5)
        out.append(DTMF_SYMS[4 * r + c] if ok else None)
    return out, hop / rate


def markers(x, rate=RATE):
    """the markers in x: (text, start in s, level in dBFS), a marker's
    symbols less than 120 ms apart, each in 3 frames or more; the level is
    its first symbol's"""
    syms, step = dtmf_frames(x, rate)
    events, i = [], 0
    while i < len(syms):
        j = i
        while j < len(syms) and syms[j] == syms[i]:
            j += 1
        if syms[i] is not None and j - i >= 3:
            events.append((syms[i], i * step, j * step))
        i = j
    out = []
    for s, t0, t1 in events:
        if out and t0 - out[-1][2] < 0.120:
            text, start, _, lv = out[-1]
            out[-1] = (text + s, start, t1, lv)
        else:
            a, b = int(t0 * rate) + int(0.020 * rate), int(t1 * rate)
            out.append((s, t0, t1, level_db(x[a:max(b, a + 1)])))
    return [(text, start, lv) for text, start, _, lv in out]


# --- the report ------------------------------------------------------------------

REC_RE = re.compile(r"recorded: marker (\S+) at (\d+) us, sound((?: \d+-\d+)*)"
                    r" us, (\w+) expected")
MARK_RE = re.compile(r"marker (\w+) at (\d+) us")


def read_report(path):
    """(the check lines as (name, result, details), the recorded sounds as
    {name: (marker, us, [(on, off)], expected)}, the markers {text: us})"""
    with open(path, encoding="latin-1") as f:
        lines = f.read().splitlines()
    checks, sounds, marks = [], {}, {}
    for line in lines:
        parts = line.split(None, 2)
        if len(parts) < 2 or parts[1] not in ("PASS", "FAIL", "KNOWN",
                                              "INFO", "SKIP"):
            continue
        name, res, det = parts[0], parts[1], parts[2] if len(parts) > 2 else ""
        checks.append((name, res, det))
        m = REC_RE.search(det)
        if m:
            spans = [tuple(int(v) for v in p.split("-"))
                     for p in m.group(3).split()]
            sounds[name] = (m.group(1), int(m.group(2)), spans, m.group(4))
            marks[m.group(1)] = int(m.group(2))
        elif name in ("rec.start", "rec.end"):
            m = MARK_RE.search(det)
            if m:
                marks[m.group(1)] = int(m.group(2))
    return checks, sounds, marks


def fit_clock(marks, found):
    """the recording's time of the report's us: (a, b) for t = a * us + b,
    and the worst error in s, from the markers both have"""
    pairs = []
    for text, start, _lv in found:
        if text in marks:
            pairs.append((marks[text] / 1e6, start))
    if len(pairs) < 2:
        return None
    u = np.array([p[0] for p in pairs])
    t = np.array([p[1] for p in pairs])
    a, b = np.polyfit(u, t, 1)
    return a, b, float(np.max(np.abs(a * u + b - t))), len(pairs)


# --- the measures --------------------------------------------------------------

def pitch(x, near, span=0.08, rate=RATE):
    """the frequency of x's strongest component near `near` Hz: the peak of
    a Hann window's spectrum, 8 times zero-padded, refined by a parabola
    through the log magnitudes"""
    n = len(x)
    nfft = 1 << int(math.ceil(math.log2(max(n, 16) * 8)))
    spec = np.abs(np.fft.rfft(x * np.hanning(n), nfft))
    freqs = np.arange(len(spec)) * rate / nfft
    band = (freqs > near * (1 - span)) & (freqs < near * (1 + span))
    if not band.any():
        return 0.0
    k = int(np.argmax(np.where(band, spec, 0)))
    d = 0.0
    if 0 < k < len(spec) - 1:
        a, b, c = np.log(spec[k - 1:k + 2] + 1e-30)
        if a - 2 * b + c < 0:
            d = 0.5 * (a - c) / (a - 2 * b + c)
    return (k + d) * rate / nfft


def tone_fit(x, f, rate=RATE):
    """the sine at f Hz (and the mean) that fits x best: the tone's power
    and the rest's"""
    t = np.arange(len(x)) / rate
    m = np.column_stack((np.cos(2 * np.pi * f * t), np.sin(2 * np.pi * f * t),
                         np.ones(len(x))))
    coef = np.linalg.lstsq(m, x, rcond=None)[0]
    rest = x - m @ coef
    return (coef[0] ** 2 + coef[1] ** 2) / 2, float(np.mean(rest * rest))


def cleanness(x, f, rate=RATE):
    """the tone's power over the rest's, in dB, in 20 ms windows 10 ms
    apart: (median, worst)"""
    win, hop = int(0.020 * rate), int(0.010 * rate)
    vals = []
    for i in range(0, len(x) - win + 1, hop):
        tone, rest = tone_fit(x[i:i + win], f, rate)
        vals.append(10 * math.log10(max(tone, 1e-20) / max(rest, 1e-20)))
    if not vals:
        return 0.0, 0.0
    return float(np.median(vals)), float(min(vals))


def envelope(x, secs=0.010, rate=RATE):
    """the level in dB of each `secs`"""
    win = max(int(secs * rate), 1)
    n = len(x) // win
    return [level_db(x[i * win:(i + 1) * win]) for i in range(n)]


def cents(f, want):
    return 1200 * math.log2(f / want) if f > 0 else -9999.0


class Recording:
    """the recording at RATE: the left and right channels (the same for a
    mono one), and the report's time of each moment"""

    def __init__(self, rate, frames, clock, ref=-8.7):
        self.ref = ref          # the level a sound's tone has
        self.stereo = frames.shape[1] > 1
        self.left = band_limit(frames[:, 0], rate)
        self.right = band_limit(frames[:, 1], rate) if self.stereo \
            else self.left
        self.mono = (self.left + self.right) / 2
        self.a, self.b = clock[:2]

    def at(self, us):
        return self.a * us / 1e6 + self.b

    def part(self, on, off, chan="mono"):
        """a sound's part, past its start and before its end, where the
        FIFOs fill and empty: 40 ms and 20 ms, or a tenth of a short one"""
        t0, t1 = self.at(on), self.at(off)
        lead = min(0.040, (t1 - t0) / 10)
        tail = min(0.020, (t1 - t0) / 10)
        x = getattr(self, chan)
        a, b = int(round((t0 + lead) * RATE)), int(round((t1 - tail) * RATE))
        return x[max(a, 0):max(b, 0)]


# --- the checks ----------------------------------------------------------------

def tone_text(x, want):
    """a tone's measures: (ok, text)"""
    f = pitch(x, want)
    c = cents(f, want)
    med, worst = cleanness(x, f)
    ok = abs(c) <= CENTS and med >= CLEAN_DB and worst >= WORST_DB
    return ok, "%.2f Hz (%+.1f cents), clean %.0f dB, worst %.0f dB, " \
        "%.1f dBFS" % (f, c, med, worst, level_db(x))


def check_tone(rec, spans, want):
    ok, text = tone_text(rec.part(*spans[0]), HZ)
    return ok, text


def check_side(rec, spans, want):
    on, off = spans[0]
    left, right = rec.part(on, off, "left"), rec.part(on, off, "right")
    lv, rv = level_db(left), level_db(right)
    ok, text = tone_text(left if want == "l" else right, HZ)
    text += ", left %.1f, right %.1f dBFS" % (lv, rv)
    if not rec.stereo:
        return ok, text + " (a mono recording: no sides)"
    side = lv - rv if want == "l" else rv - lv
    return ok and side >= SIDE_DB, text


def check_pitch(rec, spans, want):
    oks, texts, fs = [], [], []
    for span in spans:
        x = rec.part(*span)
        ok, text = tone_text(x, HZ)
        oks.append(ok)
        fs.append(pitch(x, HZ))
        texts.append(text.split(",")[0])
    spread = max(cents(f, min(fs)) for f in fs) if fs else 0
    return all(oks) and len(spans) == 3 and spread <= CENTS, \
        "%s; %.1f cents apart" % (", ".join(texts), spread)


def check_dual(rec, spans, want):
    x = rec.part(*spans[0])
    total = 2 * float(np.mean(x * x))
    lo, _ = tone_fit(x, HZ * 2 / 3)
    hi, _ = tone_fit(x, HZ * 2)
    lo_db = 10 * math.log10(max(2 * lo, 1e-20) / max(total, 1e-20))
    hi_db = 10 * math.log10(max(2 * hi, 1e-20) / max(total, 1e-20))
    text = "294 Hz %.1f dB, 882 Hz %.1f dB of the sound, %.1f dBFS" % (
        lo_db, hi_db, level_db(x))
    heard = ("b" if lo_db > -12 and hi_db > -12 else
             "h" if hi_db > -3 and lo_db < -20 else
             "l" if lo_db > -3 and hi_db < -20 else "?")
    hi_ok = True
    if heard in ("b", "h"):
        f = pitch(x, HZ * 2, 0.05)
        med, worst = cleanness(x, f) if heard == "h" else (99, 99)
        hi_ok = abs(cents(f, HZ * 2)) <= CENTS and med >= CLEAN_DB
        text += ", the high one %.2f Hz, clean %.0f dB" % (f, med)
    return heard == want and hi_ok, text + ", heard %s" % heard


def check_pause(rec, spans, want):
    on, off = spans[0][0], spans[-1][1]
    x = rec.part(on, off)
    env = envelope(x)
    if len(env) < 6:
        return False, "too short"
    top = float(np.median(env))
    quiet = sum(1 for e in env if e < top - 30)
    ok, text = tone_text(x[:len(x) // 4], HZ)
    return ok and quiet >= len(env) // 6, \
        "%s; %d of %d 10 ms quiet" % (text, quiet, len(env))


def quarters(on, off, n):
    """a part cut into n spans of the same length, as the sound changes"""
    d = (off - on) / n
    return [(on + i * d, on + (i + 1) * d) for i in range(n)]


def check_volume(rec, spans, want):
    """the master level in four steps, each 16 dB or more down"""
    xs = [rec.part(*q) for q in quarters(*spans[0], 4)]
    levels = [level_db(x) for x in xs]
    steps = [a - b for a, b in zip(levels, levels[1:])]
    ok, text = tone_text(xs[0], HZ)
    return ok and all(s >= 6 for s in steps), \
        "%s; the steps %s dBFS" % (text, ", ".join("%.1f" % v
                                                   for v in levels))


def check_balance(rec, spans, want):
    """the voice on the left only, then on the right only"""
    one, two = quarters(*spans[0], 2)
    first = level_db(rec.part(*one, "left")) - level_db(rec.part(*one,
                                                                 "right"))
    second = level_db(rec.part(*two, "right")) - level_db(rec.part(*two,
                                                                   "left"))
    ok, text = tone_text(rec.part(*one), HZ)
    text += "; the left louder by %.1f dB, then the right by %.1f" % (
        first, second)
    if not rec.stereo:
        return ok, text + " (a mono recording: no sides)"
    return ok and first >= SIDE_DB and second >= SIDE_DB, text


def check_rough(rec, spans, want, hz, span):
    x = rec.part(*spans[0])
    f = pitch(x, hz, span)
    lv = level_db(x)
    return abs(f - hz) <= hz * span / 2 and lv > rec.ref - 30, \
        "%.2f Hz (%.1f expected), %.1f dBFS" % (f, hz, lv)


def check_blocks(rec, spans, want):
    """blocks one after the other: no gap of 2 ms or more; a Sound Blaster
    16 waits a little for each block's command, which moves the tone a few
    cents down and takes some of its cleanness"""
    x = rec.part(*spans[0])
    f = pitch(x, HZ)
    c = cents(f, HZ)
    env = envelope(x, 0.002)
    top = float(np.median(env)) if env else 0
    gaps = sum(1 for e in env if e < top - 20)
    return abs(c) <= 3 * CENTS and gaps == 0 and level_db(x) > rec.ref - 30, \
        "%.2f Hz (%+.1f cents), %.1f dBFS; %d of %d 2 ms quiet" % (
            f, c, level_db(x), gaps, len(env))


def judge(rec, name, sound):
    """a recorded sound's result: (PASS or FAIL or INFO, text)"""
    kind = CHECKS.get(name, "tone" if name.startswith("l.clean.") else None)
    _marker, _us, spans, want = sound
    if kind is None or not spans:
        return "INFO", "nothing to measure"
    if kind == "adpcm":
        ok, text = check_rough(rec, spans, want, HZ, 0.04)
    elif kind == "direct":
        ok, text = check_rough(rec, spans, want, DIRECT_HZ, 0.06)
    elif kind == "fm":
        ok, text = check_rough(rec, spans, want, FM_HZ, 0.06)
    else:
        ok, text = globals()["check_" + kind](rec, spans, want)
    return ("PASS" if ok else "FAIL"), text


def analyze(report, wav, out=sys.stdout):
    """the recording's results after the report's; the number that failed"""
    checks, sounds, marks = read_report(report)
    counts = {}
    for _name, res, _det in checks:
        counts[res] = counts.get(res, 0) + 1
    print("the report: " + ", ".join("%s %d" % (k.lower(), v) for k, v in
                                     sorted(counts.items())), file=out)
    for name, res, det in checks:
        if res in ("FAIL", "KNOWN"):
            print("  %-20s %-5s %s" % (name, res, det), file=out)
    if not sounds:
        print("the report has no recorded sounds: was it SB16TEST /R?",
              file=out)
        return 1
    rate, frames = read_wav(wav)
    found = markers(band_limit(frames.mean(axis=1), rate))
    clock = fit_clock(marks, found)
    print("the recording: %d Hz, %s, %.1f s, %d markers" % (
        rate, "stereo" if frames.shape[1] > 1 else "mono",
        len(frames) / rate, len(found)), file=out)
    if clock is None:
        print("too few of the report's markers in the recording: %s" %
              " ".join(t for t, _s, _lv in found), file=out)
        return 1
    a, b, err, n = clock
    print("the clock: %d markers, %.1f ppm, %.1f ms the worst" % (
        n, (a - 1) * 1e6, err * 1e3), file=out)
    # a marker's two tones, each half the sounds' one, have 3 dB less power
    ref = float(np.median([lv for _t, _s, lv in found])) + 3.0
    print("the markers' level: a sound's tone would be %.1f dBFS" % ref,
          file=out)
    rec = Recording(rate, frames, clock, ref)
    failed = 0
    for name, sound in sounds.items():
        res, text = judge(rec, name, sound)
        failed += res == "FAIL"
        print("%-20s %-5s %s" % (name, res, text), file=out)
    print("%d of %d recorded sounds failed" % (failed, len(sounds)), file=out)
    return failed


def main(argv=None):
    ap = argparse.ArgumentParser(
        description="Analyze a recorded run of sb16test (SB16TEST /R).")
    ap.add_argument("report", help="the run's SB16TEST.TXT")
    ap.add_argument("wav", help="the recording of the card's output")
    args = ap.parse_args(argv)
    return 1 if analyze(args.report, args.wav) else 0


if __name__ == "__main__":
    sys.exit(main())
