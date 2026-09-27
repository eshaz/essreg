#!/usr/bin/env python3
# (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
#
# Licensed under GPL Version 3.0
"""Write build/GMCHECK.MID, a MIDI file that tries the General MIDI features
of the fixed ESFM.DRV one at a time (docs/ESFM_GM.md).

usage: gmcheck.py [-o OUTPUT]
"""

import argparse
import os
import struct

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "build", "GMCHECK.MID")
TICKS = 480             # per quarter note, 120 bpm: 960 per second
GM_ON = (0xF0, 0x7E, 0x7F, 0x09, 0x01, 0xF7)
SQUARE = 80             # GM program 81, Lead 1 (square)

# the steps of docs/TESTING.md (G3): start, end and what to hear
STEPS = [
    (0.5, 4.5, "Modulation: C4, then a slight vibrato at 1.5 s, a deeper "
               "one at 2.5 s, none again at 3.5 s"),
    (5.0, 8.5, "Channel pressure: E4, vibrato from 6 s to 7 s"),
    (9.0, 13.0, "Pan: G4 in the middle, left at 10 s, right at 11 s, "
                "middle at 12 s"),
    (13.5, 17.5, "Fine tuning: C4, a quarter tone sharp at 14.5 s, a quarter "
                 "tone flat at 15.5 s, in tune at 16.5 s"),
    (18.0, 20.8, "Coarse tuning: C4, C5, C4"),
    (21.5, 25.5, "Bend range 12: C4 glides up an octave from 22 s to 23 s, "
                 "then back down at 24 s"),
    (26.0, 30.0, "Master volume: a C chord, quiet at 27 s, louder at 28 s, "
                 "full at 29 s"),
    (30.5, 34.0, "Reset all controllers: C4 on the left, sharp, with "
                 "vibrato. At 32.5 s the vibrato stops and it comes back "
                 "in tune, still on the left"),
]


def steps_table():
    """The steps as the rows of a markdown table."""
    return "".join("| %g-%g s | %s |\n" % s for s in STEPS)


def events():
    """(seconds, message) in time order."""
    ev = []

    def at(t, *msg):
        ev.append((t, bytes(msg)))

    def cc(t, number, value, ch=0):
        at(t, 0xB0 | ch, number, value)

    def note(t, off, key, ch=0):
        at(t, 0x90 | ch, key, 100)
        at(off, 0x80 | ch, key, 0)

    def rpn(t, number, msb, ch=0):
        cc(t, 101, number >> 7, ch)
        cc(t, 100, number & 0x7F, ch)
        cc(t, 6, msb, ch)
        cc(t, 101, 0x7F, ch)
        cc(t, 100, 0x7F, ch)

    def bend(t, value, ch=0):
        at(t, 0xE0 | ch, value & 0x7F, value >> 7)

    def master(t, value):
        at(t, 0xF0, 0x7F, 0x7F, 0x04, 0x01, value & 0x7F, value >> 7, 0xF7)

    at(0.0, *GM_ON)
    at(0.1, 0xC0, SQUARE)

    # controller 1
    note(0.5, 4.5, 60)
    cc(1.5, 1, 40)
    cc(2.5, 1, 127)
    cc(3.5, 1, 0)

    # channel pressure
    note(5.0, 8.5, 64)
    at(6.0, 0xD0, 90)
    at(7.0, 0xD0, 0)

    # pan on a note that sounds
    note(9.0, 13.0, 67)
    cc(10.0, 10, 0)
    cc(11.0, 10, 127)
    cc(12.0, 10, 64)

    # RPN 1, fine tuning: 60h is +50 cents, 20h -50 cents
    note(13.5, 17.5, 60)
    rpn(14.5, 1, 0x60)
    rpn(15.5, 1, 0x20)
    rpn(16.5, 1, 0x40)

    # RPN 2, coarse tuning, from the next note
    note(18.0, 18.8, 60)
    rpn(19.0, 2, 0x40 + 12)
    note(19.0, 19.8, 60)
    rpn(20.0, 2, 0x40)
    note(20.0, 20.8, 60)

    # RPN 0, the bend range
    rpn(21.5, 0, 12)
    note(21.5, 25.5, 60)
    for i in range(1, 17):
        bend(22.0 + i / 16, 0x2000 + i * 0x200 - (i == 16))
    bend(24.0, 0x2000)
    rpn(25.5, 0, 2)

    # master volume
    for key in (60, 64, 67):
        note(26.0, 30.0, key)
    master(27.0, 0x1000)
    master(28.0, 0x2000)
    master(29.0, 0x3FFF)

    # controller 121 keeps the pan, resets the rest
    cc(30.5, 10, 0)
    cc(30.5, 1, 127)
    bend(30.5, 0x3000)
    note(30.5, 34.0, 60)
    cc(32.5, 121, 0)
    cc(34.0, 10, 64)

    at(34.5, *GM_ON)
    return sorted(ev, key=lambda e: e[0])


def vlq(n):
    out = [n & 0x7F]
    n >>= 7
    while n:
        out.append(0x80 | (n & 0x7F))
        n >>= 7
    return bytes(reversed(out))


def smf(evs):
    """A format 0 standard MIDI file of the events."""
    name = b"GM check for ESFM.DRV"
    track = bytearray(vlq(0) + bytes([0xFF, 0x03, len(name)]) + name)
    track += vlq(0) + bytes([0xFF, 0x51, 3]) + (500000).to_bytes(3, "big")
    last = 0
    for t, msg in evs:
        tick = round(t * 2 * TICKS)
        track += vlq(tick - last)
        last = tick
        if msg[0] == 0xF0:
            track += b"\xF0" + vlq(len(msg) - 1) + msg[1:]
        else:
            track += msg
    track += vlq(2 * TICKS) + b"\xFF\x2F\x00"
    return (b"MThd" + struct.pack(">IHHH", 6, 0, 1, TICKS) + b"MTrk" +
            struct.pack(">I", len(track)) + bytes(track))


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("-o", "--output", default=OUT)
    args = ap.parse_args()
    data = smf(events())
    with open(args.output, "wb") as f:
        f.write(data)
    print("%s: %d bytes" % (os.path.relpath(args.output), len(data)))


if __name__ == "__main__":
    main()
