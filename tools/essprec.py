#!/usr/bin/env python3
# (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
#
# Licensed under GPL Version 3.0
"""Analyze a recorded run of essprobe (ESSPROBE /R): the recording of the
card's output, a WAV file, against the run's report.

usage: essprec.py ESSPROBE.TXT RECORDING.WAV

The run plays a full-scale tone to set the recorder's level by, then the
start marker (DTMF ABCD), each case's sound after its marker (*, its
number in three digits, #) and the end marker (DCBA). The report gives
each marker's time, when each part of each sound began and ended, what
the recording should hold, and each case's spec.

This finds the markers, fits the report's clock to the recording's, and
measures each sound by what it should hold:
* tone: 441 Hz within 10 cents, clean (the tone 25 dB over the rest, 15
  in the worst 20 ms), and the rate it played at from its pitch;
* left, right: the same on that side, louder than the other by 3 dB,
  since a recorder's crosstalk can leave the other side 8 dB under it;
* dual: 294 Hz and 882 Hz both; low: 294 Hz without 882 Hz;
* none: 30 dB under a tone's level;
* pause: the tone in both parts and quiet between them; lpause: the same
  with the tone on the left alone, which a stream a byte or two off after
  the pause loses;
* glide: a rise of 400 Hz a second that never steps back, which a loop
  or stale data does, and never stands, which a stall does;
* loop: steps back, as the glide does when the game's count stands still;
* clean: the tone 40 dB over the rest (30 in the worst 20 ms), which a
  quarter of a dB of clipping takes away;
* any: what was heard, for the record.
Then the clean cases' levels, each over the first's, against the gains
their specs set (14h's v1, the master's m), which a clipped tone falls
short of; and a summary of each spec key: how many cases with it passed
and failed, which shows what a fault depends on.

The recording can be mono or stereo, at any rate (tools/recan.py). Needs
numpy.

Example:
  python3 tools/essprec.py ESSPROBE.TXT run.wav
"""

import argparse
import math
import os
import re
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from recan import (CENTS, CLEAN_DB, HZ, RATE, SIDE_DB, WORST_DB,  # noqa: E402
                   Recording, band_limit, cents, cleanness, envelope,
                   fit_clock, level_db, markers, pitch, read_report,
                   read_wav, tone_fit)

LOW_HZ, HIGH_HZ = 294.0, 882.0      # both engines' tones (src/essp.c)
GLIDE_HZ, GLIDE_RISE = 300.0, 400.0  # the stream's glide: Hz, Hz a second
CLIP_DB, CLIP_WORST_DB = 40, 30     # clean: no clipping
# Audio 1's levels 1-15 (14h) in dB (DS p.55, Table 21): 3 dB a step up
# to 8, then 1.5 dB, 15 at +1.5 dB
A1_DB = (None,) + tuple(1.5 - 1.5 * (15 - n) if n >= 8 else -9.0 - 3 * (8 - n)
                        for n in range(1, 16))
SPEC_RE = re.compile(r"^(\S+)\s+spec\s+(.*)$")
RATE_RE = re.compile(r"\b(a[12]) ch \d+ (\d+) Hz set")


def read_specs(path):
    """{case: its spec's keys} and {case: {engine: the rate it set}}"""
    specs, rates = {}, {}
    with open(path, encoding="latin-1") as f:
        for line in f:
            m = SPEC_RE.match(line.strip())
            if m:
                specs[m.group(1)] = m.group(2).split()
                continue
            parts = line.split(None, 2)
            if len(parts) == 3:
                for eng, hz in RATE_RE.findall(parts[2]):
                    rates.setdefault(parts[0], {})[eng] = int(hz)
    return specs, rates


def tone_measure(x, rate_set=None, need=(CLEAN_DB, WORST_DB)):
    """a tone near 441 Hz, looked for from 100 to 1000 Hz, so that one at
    another rate shows, and clean as `need` says (median, worst): (ok,
    text)"""
    f = pitch(x, 550, 0.82)
    c = cents(f, HZ)
    med, worst = cleanness(x, f) if f > 0 else (0.0, 0.0)
    ok = abs(c) <= CENTS and med >= need[0] and worst >= need[1]
    text = "%.2f Hz (%+.1f cents), clean %.0f dB, worst %.0f dB, %.1f dBFS" \
        % (f, c, med, worst, level_db(x))
    if rate_set and f > 0:
        text += ", plays at %.0f Hz" % (rate_set * f / HZ)
    return ok, text


def check_tone(rec, spans, case, rates):
    return tone_measure(rec.part(*spans[0]), rate_of(case, rates))


def check_clean(rec, spans, case, rates):
    return tone_measure(rec.part(*spans[0]), rate_of(case, rates),
                        (CLIP_DB, CLIP_WORST_DB))


def rate_of(case, rates):
    r = rates.get(case, {})
    return r.get("a1") if case.startswith(("a1.", "lvl.")) else \
        r.get("a2") if case.startswith("a2.") else None


def check_side(rec, spans, case, rates, want):
    on, off = spans[0]
    left, right = rec.part(on, off, "left"), rec.part(on, off, "right")
    lv, rv = level_db(left), level_db(right)
    ok, text = tone_measure(left if want == "left" else right,
                            rate_of(case, rates))
    text += ", left %.1f, right %.1f dBFS" % (lv, rv)
    if not rec.stereo:
        return None, text + " (a mono recording: no sides)"
    side = lv - rv if want == "left" else rv - lv
    return ok and side >= SIDE_DB, text


def tones_db(x):
    """294 Hz's and 882 Hz's share of the sound, in dB"""
    total = 2 * float(np.mean(x * x))
    out = []
    for f in (LOW_HZ, HIGH_HZ):
        p, _ = tone_fit(x, f)
        out.append(10 * math.log10(max(2 * p, 1e-20) / max(total, 1e-20)))
    return out


def check_dual(rec, spans, want):
    x = rec.part(*spans[0])
    lo, hi = tones_db(x)
    text = "294 Hz %.1f dB, 882 Hz %.1f dB of the sound, %.1f dBFS" % (
        lo, hi, level_db(x))
    if want == "dual":
        return lo > -12 and hi > -12, text
    return lo > -3 and hi < -20, text


def check_none(rec, spans):
    x = rec.part(*spans[0])
    lv = level_db(x)
    return lv < rec.ref - 30, "%.1f dBFS, a tone's %.1f" % (lv, rec.ref)


def check_pause(rec, spans, case, rates, side=None):
    """the tone before and after the pause, and quiet between; with side,
    on that side alone"""
    if len(spans) < 2:
        return False, "no pause in the report"
    (a, b), (c, d) = spans[0], spans[1]
    oks, texts = [], []
    for on, off in ((a, b), (c, d)):
        ok, text = tone_measure(rec.part(on, off, side or "mono"),
                                rate_of(case, rates))
        text = ",".join(text.split(",")[:2])
        if side and rec.stereo:
            lv = level_db(rec.part(on, off, "left"))
            rv = level_db(rec.part(on, off, "right"))
            ok = ok and (lv - rv if side == "left" else rv - lv) >= SIDE_DB
            text += ", left %.1f, right %.1f dBFS" % (lv, rv)
        oks.append(ok)
        texts.append(text)
    gap = rec.part(b, c)
    env = envelope(gap) if len(gap) else []
    quiet = sum(1 for e in env if e < rec.ref - 30)
    return all(oks) and quiet >= len(env) // 2, \
        "before: %s; after: %s; %d of %d 10 ms of the pause quiet" % (
            texts[0], texts[1], quiet, len(env))


def glide_track(x, win=0.020, hop=0.010, rate=RATE):
    """the glide's frequency in windows: [Hz]"""
    w, h = int(win * rate), int(hop * rate)
    out = []
    for i in range(0, len(x) - w + 1, h):
        seg = x[i:i + w]
        if level_db(seg) < -60:
            out.append(0.0)
        else:
            out.append(pitch(seg, 600, 0.7))
    return out


def glide_steps(track, hop=0.010):
    """(steps back, windows that stand) of a glide's track: a step back is
    a fall of more than half a lap's rise, standing is a rise of less than
    a quarter of what the glide rises in a window"""
    rise = GLIDE_RISE * hop
    back = stand = 0
    for a, b in zip(track, track[1:]):
        if not a or not b:
            continue
        if b < a - rise:
            back += 1
        elif b - a < rise / 4:
            stand += 1
    return back, stand


def check_glide(rec, spans, want):
    x = rec.part(*spans[0])
    track = glide_track(x)
    back, stand = glide_steps(track)
    heard = [f for f in track if f]
    text = "%d windows, %.0f to %.0f Hz, %d steps back, %d standing" % (
        len(track), min(heard) if heard else 0, max(heard) if heard else 0,
        back, stand)
    n = max(len(track) - 1, 1)
    if want == "glide":
        return back == 0 and stand <= n // 4 and len(heard) >= n // 2, text
    return back >= 2, text


def judge(rec, case, sound, rates):
    """a recorded sound's result: (PASS, FAIL or INFO, text)"""
    _marker, _us, spans, want = sound
    if not spans:
        return "INFO", "nothing to measure"
    if want == "tone":
        ok, text = check_tone(rec, spans, case, rates)
    elif want in ("left", "right"):
        ok, text = check_side(rec, spans, case, rates, want)
    elif want in ("dual", "low"):
        ok, text = check_dual(rec, spans, want)
    elif want == "none":
        ok, text = check_none(rec, spans)
    elif want == "pause":
        ok, text = check_pause(rec, spans, case, rates)
    elif want == "lpause":
        ok, text = check_pause(rec, spans, case, rates, "left")
    elif want in ("glide", "loop"):
        ok, text = check_glide(rec, spans, want)
    elif want == "clean":
        ok, text = check_clean(rec, spans, case, rates)
    else:
        x = rec.part(*spans[0])
        _ok, text = tone_measure(x, rate_of(case, rates))
        if case.startswith("stream."):
            text += "; " + check_glide(rec, spans, "glide")[1]
        lo, hi = tones_db(x)
        text += "; 294 Hz %.1f dB, 882 Hz %.1f dB" % (lo, hi)
        return "INFO", text
    if ok is None:
        return "INFO", text
    return ("PASS" if ok else "FAIL"), text


def set_gain(keys):
    """the gain in dB that a spec's v1 (14h, CCh unless given) and m (the
    master, 0.75 dB a step) set Audio 1's left side to, or None"""
    kv = dict(k.split("=", 1) for k in keys if "=" in k)
    n = int(kv.get("v1", "204")) >> 4
    if "m" not in kv or not n:
        return None
    return A1_DB[n] - 0.75 * (63 - int(kv["m"]))


def levels(rec, sounds, specs, out):
    """the clean cases' levels, each over the first's, against the gains
    their specs set: a clipped tone comes out short of its gain"""
    rows = []
    for name, (_m, _us, spans, want) in sounds.items():
        if want == "clean" and spans:
            rows.append((name, level_db(rec.part(*spans[0])),
                         set_gain(specs.get(name, []))))
    if not rows:
        return
    first, lv0, g0 = rows[0]
    print("the levels over %s's, measured and set:" % first, file=out)
    for name, lv, g in rows:
        set_text = "%+6.2f dB" % (g - g0) if g is not None and \
            g0 is not None else "?"
        print("  %-20s %6.1f dBFS %+6.2f dB %s" % (name, lv, lv - lv0,
                                                   set_text), file=out)


def summary(verdicts, specs, out):
    """each spec key with its cases' results"""
    keys = {}
    for case, res in verdicts.items():
        for k in specs.get(case, []):
            if k in ("S",) or k.startswith("run="):
                continue
            keys.setdefault(k, {"PASS": 0, "FAIL": 0, "INFO": 0})[res] += 1
    if not keys:
        return
    print("by spec key (pass, fail, info):", file=out)
    for k in sorted(keys):
        v = keys[k]
        print("  %-14s %3d %3d %3d%s" % (k, v["PASS"], v["FAIL"], v["INFO"],
                                         "  <-" if v["FAIL"] else ""),
              file=out)


def analyze(report, wav, out=sys.stdout):
    """the recording's results after the report's; the number that failed"""
    checks, sounds, marks = read_report(report)
    specs, rates = read_specs(report)
    counts = {}
    for _name, res, _det in checks:
        counts[res] = counts.get(res, 0) + 1
    print("the report: " + ", ".join("%s %d" % (k.lower(), v) for k, v in
                                     sorted(counts.items())), file=out)
    for name, res, det in checks:
        if res == "FAIL":
            print("  %-20s %-5s %s" % (name, res, det), file=out)
    if not sounds:
        print("the report has no recorded sounds: was it ESSPROBE /R?",
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
    ref = float(np.median([lv for _t, _s, lv in found])) + 3.0
    print("the markers' level: a sound's tone would be %.1f dBFS" % ref,
          file=out)
    rec = Recording(rate, frames, clock, ref)
    failed, verdicts = 0, {}
    for name, sound in sounds.items():
        res, text = judge(rec, name, sound, rates)
        verdicts[name] = res
        failed += res == "FAIL"
        print("%-20s %-5s %s" % (name, res, text), file=out)
    print("%d of %d recorded sounds failed" % (failed, len(sounds)), file=out)
    levels(rec, sounds, specs, out)
    summary(verdicts, specs, out)
    return failed


def main(argv=None):
    ap = argparse.ArgumentParser(
        description="Analyze a recorded run of essprobe (ESSPROBE /R).")
    ap.add_argument("report", help="the run's ESSPROBE.TXT")
    ap.add_argument("wav", help="the recording of the card's output")
    args = ap.parse_args(argv)
    return 1 if analyze(args.report, args.wav) else 0


if __name__ == "__main__":
    sys.exit(main())
