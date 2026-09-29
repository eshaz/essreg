#!/usr/bin/env python3
# (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
#
# Licensed under GPL Version 3.0
"""Make 4-channel WAV files for the dual playback of build/ES1869.DRV.

usage: dualwav.py MODE [options] (INPUT.wav | --tone HZ | --sweep |
                                  --noise | --silence) OUTPUT.wav

Channels 1-2 play on the Audio 1 DAC and 3-4 on the Audio 2 DAC, at the
same clock (docs/AUDIO1.md). The mode says what each DAC plays of the
input x, and the two add up in the mixer:

  same     both play x: the sum is 2x, 6 dB up, while the two DACs' own
           noise adds as noise does, 3 dB up: 3 dB less noise than one DAC
  invert   Audio 1 plays x, Audio 2 -x: the sum is only what differs
           between the two DACs (the null test, for --delay2 and the gains)
  split    x plus a slow offset, and x minus it: each DAC plays other codes
           than the other, the sum is 2x and the offset cancels
  half     Audio 2 plays x half a sample later: the sum is x through a
           two-tap filter, which falls to nothing at half the sample rate
  hilbert  Audio 2 plays x turned 90 degrees, from 1/150 of the sample
           rate up: the sum has x's level spectrum, 3 dB up, every
           frequency shifted 45 degrees

Each DAC plays at the input's level, so the sum can be up to 6 dB louder:
turn Audio 1's volume (mixer 14h) and the Wave volume (7Ch) down by the
same amount.

Example:
  python3 tools/dualwav.py invert --tone 1000 --rate 44100 null.wav
  python3 tools/dualwav.py same music.wav music4.wav

Needs Python 3 only. The filters run in Python: a minute of stereo at
44.1 kHz takes about 20 seconds in the half mode and 2 minutes in the
hilbert mode.
"""

import argparse
import math
import random
import struct
import sys
import wave

MODES = ("same", "invert", "split", "half", "hilbert")
TAPS = 63                       # the fractional-delay filter
HTAPS = 511                     # the Hilbert filter, good from 1/150 of the rate


def read_wav(path):
    """(rate, bits, [left, right]) with samples as floats in [-1, 1)"""
    with wave.open(path, "rb") as w:
        ch, width, rate = w.getnchannels(), w.getsampwidth(), w.getframerate()
        raw = w.readframes(w.getnframes())
    if width not in (1, 2) or ch not in (1, 2):
        raise SystemExit("dualwav: %s: 8 or 16-bit PCM, mono or stereo "
                         "only" % path)
    if width == 2:
        vals = [v / 32768.0 for v in struct.unpack("<%dh" % (len(raw) // 2),
                                                   raw)]
    else:
        vals = [(v - 128) / 128.0 for v in raw]
    chans = [vals[i::ch] for i in range(ch)]
    if ch == 1:
        chans.append(list(chans[0]))
    return rate, width * 8, chans


def write_wav(path, rate, bits, parts):
    """parts: 4 channels of floats; returns the number of samples clipped"""
    n = min(len(p) for p in parts)
    clipped = 0
    out = bytearray()
    full = 32767 if bits == 16 else 127
    for i in range(n):
        for p in parts:
            v = int(round(p[i] * (full + 1)))
            if v > full or v < -full - 1:
                clipped += 1
                v = max(-full - 1, min(full, v))
            if bits == 16:
                out += struct.pack("<h", v)
            else:
                out.append(v + 128)
    with wave.open(path, "wb") as w:
        w.setnchannels(4)
        w.setsampwidth(bits // 8)
        w.setframerate(rate)
        w.writeframes(bytes(out))
    return clipped


def fir(x, h):
    """x through the filter h: len(x) outputs, x zero before its start"""
    k = len(h)
    pad = [0.0] * (k - 1) + list(x)
    hr = list(reversed(h))
    mul = float.__mul__
    return [sum(map(mul, hr, pad[i:i + k])) for i in range(len(x))]


def blackman(n, k):
    return 0.42 - 0.5 * math.cos(2 * math.pi * k / (n - 1)) + \
        0.08 * math.cos(4 * math.pi * k / (n - 1))


def delay_filter(d):
    """TAPS taps that delay by (TAPS - 1) / 2 + d samples, 0 <= d < 1"""
    c = (TAPS - 1) / 2 + d
    h = []
    for k in range(TAPS):
        t = k - c
        s = 1.0 if abs(t) < 1e-12 else math.sin(math.pi * t) / (math.pi * t)
        h.append(s * blackman(TAPS, k))
    g = sum(h)
    return [v / g for v in h]


def hilbert_filter():
    """HTAPS taps of a Hilbert transformer, delay (HTAPS - 1) / 2"""
    c = (HTAPS - 1) // 2
    h = []
    for k in range(HTAPS):
        t = k - c
        v = 0.0 if t % 2 == 0 else 2 / (math.pi * t)
        h.append(v * blackman(HTAPS, k))
    return h


def delayed(x, frames):
    """x later by whole frames, the same length"""
    if frames <= 0:
        return list(x)
    return [0.0] * frames + list(x[:len(x) - frames])


def make_pair(mode, x, rate, offset):
    """(Audio 1's part, Audio 2's part) of one channel, and the delay in
    whole frames both have from the filters"""
    if mode == "same":
        return list(x), list(x), 0
    if mode == "invert":
        return list(x), [-v for v in x], 0
    if mode == "split":
        # a 2 Hz triangle, below what's heard, cancelled in the sum; x made
        # smaller by as much, so neither clips
        a, b = [], []
        period = rate / 2.0
        scale = 1 - offset
        for i, v in enumerate(x):
            ph = (i % period) / period
            tri = 4 * ph - 1 if ph < 0.5 else 3 - 4 * ph
            d = offset * tri
            a.append(v * scale + d)
            b.append(v * scale - d)
        return a, b, 0
    if mode == "half":
        lag = (TAPS - 1) // 2
        return delayed(x, lag), fir(x, delay_filter(0.5)), lag
    if mode == "hilbert":
        lag = (HTAPS - 1) // 2
        return delayed(x, lag), fir(x, hilbert_filter()), lag
    raise SystemExit("dualwav: no mode %r" % mode)


def generate(args):
    """(rate, bits, [left, right]) of a generated signal"""
    rate, n = args.rate, int(args.rate * args.seconds)
    if args.tone:
        x = [0.5 * math.sin(2 * math.pi * args.tone * i / rate)
             for i in range(n)]
    elif args.sweep:
        # logarithmic, 20 Hz to 0.45 of the rate
        f0, f1 = 20.0, 0.45 * rate
        k = math.log(f1 / f0)
        t1 = args.seconds
        x = [0.5 * math.sin(2 * math.pi * f0 * t1 / k *
                            (math.exp(k * i / rate / t1) - 1))
             for i in range(n)]
    elif args.noise:
        rnd = random.Random(1869)
        x = [rnd.uniform(-0.1, 0.1) for _i in range(n)]
    else:
        x = [0.0] * n
    return rate, args.bits, [x, list(x)]


def main(argv=None):
    ap = argparse.ArgumentParser(
        description=__doc__.splitlines()[0],
        epilog="See docs/AUDIO1.md#dual-playback.")
    ap.add_argument("mode", choices=MODES)
    ap.add_argument("files", nargs="+", metavar="FILE",
                    help="INPUT.wav OUTPUT.wav, or OUTPUT.wav with a "
                    "generated signal")
    src = ap.add_mutually_exclusive_group()
    src.add_argument("--tone", type=float, metavar="HZ",
                     help="a sine at -6 dB")
    src.add_argument("--sweep", action="store_true",
                     help="a logarithmic sweep, 20 Hz up")
    src.add_argument("--noise", action="store_true",
                     help="white noise at -20 dB")
    src.add_argument("--silence", action="store_true")
    ap.add_argument("--rate", type=int, default=44100)
    ap.add_argument("--bits", type=int, choices=(8, 16), default=16)
    ap.add_argument("--seconds", type=float, default=5.0)
    ap.add_argument("--delay1", type=float, default=0.0, metavar="FRAMES",
                    help="Audio 1's half later by FRAMES, fractions too")
    ap.add_argument("--delay2", type=float, default=0.0, metavar="FRAMES",
                    help="Audio 2's half later by FRAMES, fractions too")
    ap.add_argument("--gain1", type=float, default=0.0, metavar="DB")
    ap.add_argument("--gain2", type=float, default=0.0, metavar="DB")
    ap.add_argument("--offset", type=float, default=0.125,
                    metavar="FRACTION",
                    help="split: the offset, of full scale (0.125)")
    args = ap.parse_args(argv)
    generated = args.tone or args.sweep or args.noise or args.silence
    if generated:
        if len(args.files) != 1:
            ap.error("one OUTPUT.wav with a generated signal")
        rate, bits, chans = generate(args)
    else:
        if len(args.files) != 2:
            ap.error("INPUT.wav OUTPUT.wav")
        rate, bits, chans = read_wav(args.files[0])
    if not 4000 <= rate <= 49000:
        raise SystemExit("dualwav: the driver plays 4000 to 49000 Hz")
    if args.delay1 < 0 or args.delay2 < 0:
        raise SystemExit("dualwav: delay the other half instead")
    parts = [None] * 4
    gains = (10 ** (args.gain1 / 20.0), 10 ** (args.gain2 / 20.0))
    delays = (args.delay1, args.delay2)
    # a fractional delay has the filter's lag: then both halves get it
    lagged = any(d != math.floor(d) for d in delays)
    for c in (0, 1):
        pair = make_pair(args.mode, chans[c], rate, args.offset)[:2]
        for i, (x, g, d) in enumerate(zip(pair, gains, delays)):
            whole = int(math.floor(d))
            if d != whole:
                x = fir(x, delay_filter(d - whole))
            elif lagged:
                x = delayed(x, (TAPS - 1) // 2)
            x = delayed(x, whole)
            parts[2 * i + c] = [v * g for v in x]
    clipped = write_wav(args.files[-1], rate, bits, parts)
    print("%s: %s, %d Hz, %d-bit, 4 channels, %d frames" % (
        args.files[-1], args.mode, rate, bits, len(parts[0])))
    if clipped:
        print("dualwav: %d samples clipped, try a lower --gain1/--gain2" %
              clipped, file=sys.stderr)


if __name__ == "__main__":
    main()
