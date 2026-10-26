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
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from recan import (CENTS, CLEAN_DB, DTMF_HI, DTMF_LO, DTMF_SYMS,  # noqa: E402
                   HZ, OFF_S, RATE, SIDE_DB, WORST_DB, Recording,
                   band_limit, cents, cleanness, envelope, fit_clock,
                   level_db, markers, pitch, read_report, read_wav,
                   tone_fit, tone_text)

DIRECT_HZ = 8000 * 56 / 1024        # s_direct: 437.5 Hz
FM_HZ = 49716 * 0x241 / 65536       # s_fm: F-number 241h, block 4
CHECKS = {
    "l.tone8": "tone", "l.left16": "side", "l.right16": "side",
    "l.left8": "side", "l.right8": "side", "l.pitch": "pitch",
    "l.dual": "dual", "l.pause": "pause", "l.volume": "volume",
    "l.balance": "balance", "l.adpcm": "adpcm", "l.direct": "direct",
    "l.speaker": "tone", "l.blocks": "blocks", "l.fm": "fm",
}
# the names the tests and other tools take from here
__all__ = ["CENTS", "CLEAN_DB", "DTMF_HI", "DTMF_LO", "DTMF_SYMS", "HZ",
           "OFF_S", "RATE", "SIDE_DB", "WORST_DB", "Recording", "band_limit",
           "cents", "cleanness", "envelope", "fit_clock", "level_db",
           "markers", "pitch", "read_report", "read_wav", "tone_fit",
           "tone_text", "analyze", "judge", "main"]


# --- the checks ----------------------------------------------------------------

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


VOLUME_LEVELS = (31, 22, 14, 6)     # s_volume's master: F8h, B0h, 70h, 30h


def check_volume(rec, spans, want):
    """the master level in four steps, each 6 dB or more down, and the dB
    a level of the master gives, which is 2 on a Sound Blaster 16"""
    xs = [rec.part(*q) for q in quarters(*spans[0], 4)]
    levels = [level_db(x) for x in xs]
    steps = [a - b for a, b in zip(levels, levels[1:])]
    ok, text = tone_text(xs[0], HZ)
    each = (levels[0] - levels[-1]) / (VOLUME_LEVELS[0] - VOLUME_LEVELS[-1])
    return ok and all(s >= 6 for s in steps), \
        "%s; the steps %s dBFS, %.2f dB a level" % (
            text, ", ".join("%.1f" % v for v in levels), each)


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
    a, b, err, n, late = clock
    print("the clock: %d markers, %.1f ppm, %.1f ms the worst" % (
        n, (a - 1) * 1e6, err * 1e3), file=out)
    for text, d in late:
        print("  marker %s left out: found %.1f ms %s" % (
            text, abs(d) * 1e3, "late" if d > 0 else "early"), file=out)
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
