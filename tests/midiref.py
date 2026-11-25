# (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
#
# Licensed under GPL Version 3.0
"""What a General MIDI device does with a stream of messages, the reference
that tests/test_midisuite.py checks both devices of ESFM.DRV against, and a
reader of standard MIDI files (SMF, and RIFF RMID's).

The reference keeps each channel's controllers and the notes that may sound,
as the MIDI spec, RP-015 and the drivers' documented changes have it:
* a note off, or a note on with velocity 0, ends a note, and while the
  sustain pedal is down the note sounds on until the pedal goes up;
* all notes off (controllers 123 to 127) is a note off for every note of
  the channel, and all sound off (120) ends them at once, pedal or not;
* reset all controllers (121) sets the expression to 127 and lets the pedal
  up, and keeps the volume;
* a program change lets the pedal up (PedalRelease);
* a GM, GM2, GS or XG reset, or MODM_RESET, ends every note and sets every
  channel back, the master volume too;
* a note of channels 13 to 16 is left out while channels 1 to 10 have had a
  message and 11 and 12 none (MpcSongs, src/esfm/esfmmpc.asm); a note that
  started before sounds on until its note off.

The drivers may end a note sooner than the reference: ESS's code stops the
notes that keys hold at all notes off, pedal or not. They may never sound a
note that the reference has ended.
"""

import math
import os
import struct

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


# GM's curve for velocity, volume and expression: 40 log10(x / 127) dB, in
# the chip's 0.75 dB steps
def att(x):
    if x == 0:
        return 63
    return min(63, round(-40 * math.log10(x / 127) / 0.75))


def _master_curve():
    """fix_master_curve of src/esfm/esfmgm.asm, both devices' attenuation
    for the master volume's 14 bits / 512"""
    with open(os.path.join(ROOT, "src", "esfm", "esfmgm.asm")) as f:
        src = f.read()
    body = src[src.index("fix_master_curve:"):].split("\n")[1:3]
    return [int(x) for line in body for x in line.replace("db", "").split(",")]


MASTER_CURVE = _master_curve()


def master_att(v14):
    return MASTER_CURVE[v14 >> 9]


GM_ON = bytes([0xF0, 0x7E, 0x7F, 0x09, 0x01, 0xF7])
GM2_ON = bytes([0xF0, 0x7E, 0x7F, 0x09, 0x03, 0xF7])
GS_RESET = bytes([0xF0, 0x41, 0x10, 0x42, 0x12, 0x40, 0x00, 0x7F, 0x00,
                  0x41, 0xF7])
XG_ON = bytes([0xF0, 0x43, 0x10, 0x4C, 0x00, 0x00, 0x7E, 0x00, 0xF7])


def master_volume(v14):
    return bytes([0xF0, 0x7F, 0x7F, 0x04, 0x01, v14 & 0x7F, v14 >> 7, 0xF7])


class Reference:
    """the channels and the notes that may sound, for one device"""

    def __init__(self, mpc=True, pedal_release=True):
        self.mpc = mpc
        self.pedal_release = pedal_release
        self.dropped = 0
        self.reset()

    def reset(self):
        self.program = [0] * 16
        self.volume = [100] * 16
        self.expression = [127] * 16
        self.pedal = [False] * 16
        self.master = 0                 # attenuation, 0.75 dB steps
        self.notes = {}                 # (channel, key) -> [velocity, held]
        self.left_out = set()           # notes on that MpcSongs left out
        self.ext = self.mid = False     # MpcSongs: channels 1-10, 11-12

    def level(self, channel, key, headroom=0):
        """the OPL3 device's carrier attenuation for a sounding note"""
        n = self.notes[(channel, key)]
        return min(63, att(n[0]) + att(self.volume[channel]) +
                   att(self.expression[channel]) + headroom + self.master)

    def copy_left_out(self):
        return self.mpc and self.ext and not self.mid

    def _end(self, channel, now=False):
        for k in [k for k in self.notes if k[0] == channel]:
            if self.pedal[channel] and not now:
                self.notes[k][1] = True
            else:
                del self.notes[k]

    def _pedal_up(self, channel):
        self.pedal[channel] = False
        for k in [k for k, n in self.notes.items()
                  if k[0] == channel and n[1]]:
            del self.notes[k]

    def message(self, status, d1=0, d2=0):
        """a channel message; True if MpcSongs leaves it out"""
        channel, kind = status & 15, status & 0xF0
        if kind == 0xF0:
            return False
        if self.mpc:
            used = kind != 0x80 and not (kind == 0xB0 and d1 >= 120)
            if used and channel <= 9:
                self.ext = True
            if used and channel in (10, 11):
                self.mid = True
            if kind == 0x90 and d2 and channel >= 12 and \
                    self.copy_left_out():
                self.dropped += 1
                self.left_out.add((channel, d1))
                return True
        if kind == 0x90 and d2:
            self.notes[(channel, d1)] = [d2, False]
            self.left_out.discard((channel, d1))
        elif kind in (0x80, 0x90):
            n = self.notes.get((channel, d1))
            if n is not None:
                if self.pedal[channel]:
                    n[1] = True
                else:
                    del self.notes[(channel, d1)]
        elif kind == 0xC0:
            self.program[channel] = d1
            if self.pedal_release and self.pedal[channel]:
                self._pedal_up(channel)
        elif kind == 0xB0:
            if d1 == 7:
                self.volume[channel] = d2
            elif d1 == 11:
                self.expression[channel] = d2
            elif d1 == 64:
                if d2 >= 64:
                    self.pedal[channel] = True
                else:
                    self._pedal_up(channel)
            elif d1 == 120:
                self._end(channel, now=True)
            elif d1 == 121:
                self.expression[channel] = 127
                self._pedal_up(channel)
            elif d1 >= 123:
                self._end(channel)
        return False

    def sysex(self, data):
        d = bytes(data)
        if d in (GM_ON, GM2_ON) or (d[:2] == b"\xF0\x7E" and len(d) == 6 and
                                    d[3] == 9 and d[4] in (1, 3)):
            self.reset()
        elif d[:2] == b"\xF0\x41" and d[3:] == GS_RESET[3:]:
            self.reset()
        elif d[:2] == b"\xF0\x43" and len(d) == 9 and \
                d[2] & 0xF0 == 0x10 and d[3:] == XG_ON[3:]:
            self.reset()
        elif d[:2] == b"\xF0\x7F" and len(d) == 8 and d[3:5] == b"\x04\x01":
            self.master = master_att(d[5] | d[6] << 7)


# -- standard MIDI files ------------------------------------------------------

def _vlq(d, i):
    v = 0
    for _ in range(4):
        b = d[i]
        i += 1
        v = v << 7 | (b & 0x7F)
        if not b & 0x80:
            return v, i
    raise ValueError("a variable length number of more than 4 bytes")


def _smf(d):
    """the SMF in a file, which RIFF RMID wraps in its "data" chunk"""
    if d[:4] == b"RIFF" and d[8:12] == b"RMID":
        i = 12
        while i + 8 <= len(d):
            cid, n = d[i:i + 4], struct.unpack_from("<I", d, i + 4)[0]
            if cid == b"data":
                return d[i + 8:i + 8 + n]
            i += 8 + n + (n & 1)
        raise ValueError("an RMID file without a data chunk")
    return d


def read_smf(path):
    """a MIDI file's events in time order, as a player sends them: (seconds,
    kind, data), kind "msg" (a channel message with its status) or "sysex"
    (F0 to F7)"""
    with open(path, "rb") as f:
        d = _smf(f.read())
    if d[:4] != b"MThd":
        raise ValueError("not a standard MIDI file")
    fmt, ntrk, div = struct.unpack_from(">HHH", d, 8)
    if div & 0x8000:
        raise ValueError("SMPTE time")
    i = 8 + struct.unpack_from(">I", d, 4)[0]
    ev = []
    for t in range(ntrk):
        while d[i:i + 4] != b"MTrk":
            if i + 8 > len(d):
                raise ValueError("track %d is missing" % t)
            i += 8 + struct.unpack_from(">I", d, i + 4)[0]
        end = min(len(d), i + 8 + struct.unpack_from(">I", d, i + 4)[0])
        j, tick, status = i + 8, 0, 0
        while j < end:
            dt, j = _vlq(d, j)
            tick += dt
            b = d[j]
            if b == 0xFF:
                n, k = _vlq(d, j + 2)
                ev.append((tick, t, "meta", (d[j + 1], d[k:k + n])))
                j = k + n
                continue
            if b in (0xF0, 0xF7):
                n, k = _vlq(d, j + 1)
                body = d[k:k + n]
                if b == 0xF0:
                    ev.append((tick, t, "sysex", bytes([0xF0]) + body))
                j = k + n
                status = 0
                continue
            if b & 0x80:
                status = b
                j += 1
            if not status:
                raise ValueError("data without a status at %d" % j)
            size = 1 if status & 0xE0 == 0xC0 else 2
            ev.append((tick, t, "msg", bytes([status]) + d[j:j + size]))
            j += size
        i = end
    ev.sort(key=lambda e: (e[0], e[1]))
    tempo, last, now, out = 500000, 0, 0.0, []
    for tick, _t, kind, data in ev:
        now += (tick - last) * tempo / 1e6 / div
        last = tick
        if kind == "meta":
            if data[0] == 0x51:
                tempo = int.from_bytes(data[1][:3], "big")
            continue
        out.append((now, kind, data))
    return out
