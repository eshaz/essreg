#!/usr/bin/env python3
# (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
#
# Licensed under GPL Version 3.0
"""Analyze a recorded run of esswchk (ESSWCHK /R): the recording of the
card's line out, a WAV file, against the run's report.

usage: wchkrec.py WCHECK.TXT RECORDING.WAV

The run plays a level tone, then the start marker (DTMF BADC), each sound
after its marker (#, its number in three digits, *) and the end marker
(CDAB). essprobe's markers differ, so one recording can hold both runs,
and each tool finds its own part of it.

This finds the markers, fits the report's clock to the recording's, and
measures each sound by what it should hold:
* left: 441 Hz on the left, clean and louder than the right, as essprec
  measures it; a 16-bit stream a byte off plays as noise, and the line
  names it;
* lpause: the same before and after a pause, and quiet in it;
* step: 441 Hz from both DACs at once, Audio 1's on the left and Audio 2's
  on the right (dual playback): the right's delay against the left, in
  frames at 44,100 Hz, within STEP_FRAMES when both started in step;
* fml, fmr: the FM's note, a piano and not a clean tone, on that side by
  its level; where the report says TrueSides is off, ESS's ESFM.DRV plays
  it on the other side, which is KNOWN.
At the end, the dual sounds' delays and their spread, which a start that
races changes from one sound to the next.

The recording can be mono or stereo, at any rate (tools/recan.py); a mono
one has no sides, which is INFO. Needs numpy.

Example:
  python3 tools/wchkrec.py WCHECK.TXT run.wav
"""

import argparse
import math
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from essprec import check_pause, check_side  # noqa: E402
from recan import (HZ, RATE, SIDE_DB, Recording, band_limit,  # noqa: E402
                   cleanness, fit_clock, level_db, markers, pitch,
                   read_report, read_wav)

# both DACs' tones this close are in step: a frame at 44,100 Hz, the
# first run on the card to calibrate it, since their analog paths differ
STEP_FRAMES = 1.0
DUAL_HZ = 44100.0           # the dual sounds' rate


def phase(x, f, rate=RATE):
    """the phase of x's sine at f Hz, in radians, as cos(2 pi f t + it)"""
    t = np.arange(len(x)) / rate
    m = np.column_stack((np.cos(2 * np.pi * f * t),
                         np.sin(2 * np.pi * f * t), np.ones(len(x))))
    a, b, _c = np.linalg.lstsq(m, x, rcond=None)[0]
    return math.atan2(-b, a)


def noise_text(x):
    """what a tone's cleanness under 0 dB says: a stream a byte off"""
    if not len(x):
        return ""
    med, _worst = cleanness(x, pitch(x, HZ))
    if med >= 0:
        return ""
    return "; noise %.0f dB over the tone: a stream a byte off" % -med


def check_step(rec, spans):
    """the two DACs' tones, the right's delay in frames at 44,100 Hz"""
    on, off = spans[0]
    if not rec.stereo:
        return None, "a mono recording: no sides", None
    left, right = rec.part(on, off, "left"), rec.part(on, off, "right")
    if len(left) < RATE // 10:
        return False, "too short", None
    f = pitch(left, HZ)
    lv, rv = level_db(left), level_db(right)
    cl, _w = cleanness(left, f)
    cr, _w = cleanness(right, f)
    d = (phase(left, f) - phase(right, f) + math.pi) % (2 * math.pi) - math.pi
    frames = d / (2 * math.pi * f) * DUAL_HZ
    text = "%.2f Hz; left %.1f, right %.1f dBFS, clean %.0f and %.0f dB; " \
        "the right %+.2f frames after the left" % (f, lv, rv, cl, cr, frames)
    if cl < 0 or cr < 0:
        return False, text + "; noise over a tone: a stream a byte off", \
            frames
    return cl >= 25 and cr >= 25 and abs(frames) <= STEP_FRAMES, text, frames


def check_fm(rec, spans, want, swapped):
    """the FM's note on the side want gives, by its level: (ok, text)"""
    on, off = spans[0]
    left, right = rec.part(on, off, "left"), rec.part(on, off, "right")
    lv, rv = level_db(left), level_db(right)
    text = "left %.1f, right %.1f dBFS" % (lv, rv)
    if not rec.stereo:
        return None, text + " (a mono recording: no sides)"
    if max(lv, rv) < rec.ref - 30:
        return False, text + "; no note"
    side = "left" if (want == "fml") != swapped else "right"
    if swapped:
        text += "; TrueSides off: ESS's ESFM.DRV plays it on the " + side
    return (lv - rv if side == "left" else rv - lv) >= SIDE_DB, text


def judge(rec, case, sound, swapped):
    """a recorded sound's result: (PASS, FAIL, KNOWN or INFO, text, the
    dual sound's delay or None)"""
    _marker, _us, spans, want = sound
    frames = None
    if not spans:
        return "INFO", "nothing to measure", None
    if want == "left":
        ok, text = check_side(rec, spans, case, {}, "left")
        if rec.stereo:
            text += noise_text(rec.part(*spans[0], "left"))
    elif want == "lpause":
        ok, text = check_pause(rec, spans, case, {}, "left")
    elif want == "step":
        ok, text, frames = check_step(rec, spans)
    elif want in ("fml", "fmr"):
        ok, text = check_fm(rec, spans, want, swapped)
        if ok and swapped:
            return "KNOWN", text, None
    else:
        return "INFO", "level %.1f dBFS" % level_db(rec.part(*spans[0])), \
            None
    if ok is None:
        return "INFO", text, frames
    return ("PASS" if ok else "FAIL"), text, frames


def analyze(report, wav, out=sys.stdout):
    """the recording's results after the report's; the number that failed"""
    checks, sounds, marks = read_report(report)
    counts = {}
    for _name, res, _det in checks:
        counts[res] = counts.get(res, 0) + 1
    print("the report: " + ", ".join("%s %d" % (k.lower(), v) for k, v in
                                     sorted(counts.items())), file=out)
    for name, res, det in checks:
        if res == "FAIL":
            print("  %-20s %-5s %s" % (name, res, det), file=out)
    if not sounds:
        print("the report has no recorded sounds: was it ESSWCHK /R?",
              file=out)
        return 1
    swapped = any(name == "fm.pan0" and "TrueSides off" in det
                  for name, _res, det in checks)
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
    ref = float(np.median([lv for t, _s, lv in found if t in marks])) + 3.0
    rec = Recording(rate, frames, clock, ref)
    failed, steps = 0, []
    for name, sound in sounds.items():
        res, text, d = judge(rec, name, sound, swapped)
        failed += res == "FAIL"
        if d is not None:
            steps.append(d)
        print("%-20s %-5s %s" % (name, res, text), file=out)
    print("%d of %d recorded sounds failed" % (failed, len(sounds)), file=out)
    if steps:
        print("the dual sounds: the right %s frames after the left, a "
              "spread of %.2f" % (", ".join("%+.2f" % d for d in steps),
                                  max(steps) - min(steps)), file=out)
    return failed


def main(argv=None):
    ap = argparse.ArgumentParser(
        description="Analyze a recorded run of esswchk (ESSWCHK /R).")
    ap.add_argument("report", help="the run's WCHECK.TXT")
    ap.add_argument("wav", help="the recording of the card's line out")
    args = ap.parse_args(argv)
    return 1 if analyze(args.report, args.wav) else 0


if __name__ == "__main__":
    sys.exit(main())
