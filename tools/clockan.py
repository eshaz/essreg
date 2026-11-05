#!/usr/bin/env python3
# (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
#
# Licensed under GPL Version 3.0
"""The log of linux/tools/clockcmp.c: each clock's rate against the raw
system clock, and its Allan deviation there, which falls as 1 / tau as long
as what's left is only the reading's noise, and two clocks are one crystal.

usage: tools/clockan.py LOG
"""

import sys

import numpy as np


def load(path):
    rows = []
    with open(path, encoding="ascii") as f:
        for line in f:
            p = line.split()
            if len(p) == 12 and p[0].isdigit():
                rows.append([int(x) for x in p])
    return np.array(rows, dtype=np.int64)


def adev(x, taus):
    """the Allan deviation of a phase x in seconds, a sample a second"""
    out = []
    for m in taus:
        if 2 * m >= len(x):
            break
        d = x[2 * m:] - 2 * x[m:-m] + x[:-2 * m]
        out.append((m, np.sqrt(np.mean(d ** 2) / 2) / m))
    return out


def line(t, c):
    """a counter's rate from its readings, and its phase against the line"""
    secs = (t - t[0]) / 1e9
    rate = np.polyfit(secs, (c - c[0]).astype(np.float64), 1)[0]
    return rate, secs - (c - c[0]) / rate


def show(name, t, c, nominal):
    rate, phase = line(t, c)
    dev = " ".join(f"{m}s:{v * 1e9:.1f}" for m, v in
                   adev(phase, (1, 16, 64, 256, 512)))
    print(f"{name}: {rate:.3f} Hz, {(rate / nominal - 1) * 1e6:+.2f} ppm of "
          f"{nominal:.0f}; Allan deviation in ppb {dev}")


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    a = load(sys.argv[1])
    _, rtc, mono, tt, tsc, tn, nic, tu, usb, _, _, _ = a.T
    secs = (rtc - rtc[0]) / 1e9
    print(f"{len(a)} readings over {secs[-1]:.0f} s")
    slope = np.polyfit(secs, (mono - rtc).astype(np.float64) / 1e3, 1)[0]
    print(f"the steered clock: {slope:+.3f} ppm of raw")
    show("TSC", tt, tsc, 400e6)
    wide = np.concatenate([[0], np.cumsum(np.diff(nic) % (1 << 32))])
    show("PCI", tn, wide, 100e6 / 3)
    frames = np.diff(usb) % 2048
    frames = frames + np.round((np.diff(tu) / 1e6 * 1.000167 - frames) /
                               2048) * 2048
    show("USB frames", tu, np.concatenate([[0], np.cumsum(frames)]),
         12e6 / 11998)
    ticks = np.round(np.diff(secs))
    late = np.diff(secs) - ticks
    keep = np.abs(late - np.median(late)) < 150e-6
    rate = np.sum(late[keep]) / np.sum(ticks[keep])
    print(f"RTC: {-rate * 1e6:+.2f} ppm of raw, {100 * keep.mean():.0f} % of "
          "its ticks in line")


if __name__ == "__main__":
    main()
