# (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
#
# Licensed under GPL Version 3.0
"""The MIDI suite: songs and General MIDI's rules through both MIDI devices
of ESFM.DRV, device 0 (ESS's synthesizer on the chip's ESFM native mode)
and device 1 (the OPL3 device), in the CPU emulator (tests/esfmemu.py), with
every message checked against the reference of tests/midiref.py.

SongTest plays the MIDI files of Windows 3.0, 98 and XP
(third_party/windows_midi), GROOVE.MID and build/GMCHECK.MID. After every
message:
- no voice sounds a note that the reference has ended (a stuck note), and
  on device 1 every note on that the device can play sounds;
- on device 1, every voice's carrier level is the reference's: GM's 40 log10
  curve of the velocity and controllers 7 and 11, Opl3Headroom and the
  master volume;
- no note of a song's base-level copy sounds (MpcSongs), the driver leaves
  out the notes that the reference does, and it does so in the songs that
  hold two arrangements and in no other.
Each song plays its first 30 seconds, and the whole of it with
ESSREG_MIDI_FULL=1.

HistoryTest: random streams of messages, then a new song starts (a GM
reset, MODM_RESET, or a close and an open), and a probe note's voice
registers must be those of a device that played nothing before. A device
that carries anything of one song into the next, a level, a pitch, a pan or
an instrument's registers, fails it.

RulesTest: General MIDI's rules, from the GM Level 1 Developer Guidelines
and RP-015 (Response to Reset All Controllers), on both devices where both
follow them, with the numbers of the guidelines.
"""

import os
import random
import sys
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, os.path.join(ROOT, "tests"))

import gmcheck  # noqa: E402
import midiref  # noqa: E402
from midiref import Reference, att, read_smf, GM_ON, GS_RESET, XG_ON  # noqa

try:
    from esfmemu import ESFMEmu, SEG_PARA, DRV_ENABLE
    HAVE_UNICORN = True
except ImportError:
    HAVE_UNICORN = False

DRV = os.path.join(ROOT, "build", "ESFM.DRV")
MIDI = os.path.join(ROOT, "third_party", "windows_midi")
SECONDS = None if os.environ.get("ESSREG_MIDI_FULL") == "1" else 30.0

# the songs that hold an extended and a base-level arrangement
# (third_party/windows_midi/README.md): all of Windows 3.0's, and Windows
# 98's classics
TWO = {"win30/" + n for n in (
    "ALMONDS.MID", "BRNDNBRG.MID", "CANYON.MID", "DREAMY.MID", "METBLAST.MID",
    "PASSPORT.MID", "SEASONS.MID", "SOMEBAND.MID", "STOMP.MID")} | {
    "win98/" + n for n in (
        "Bach's Brandenburg Concerto No. 3.rmi",
        "Beethoven's 5th Symphony.rmi", "Beethoven's Fur Elise.rmi",
        "Dance of the Sugar-Plum Fairy.rmi", "Debussy's Claire de Lune.rmi",
        "In the Hall of the Mountain King.rmi")}

DEV_OPL_VOICE, OV_SIZE = 0x54F, 16      # src/esfm/esfmdev.inc
OVF_ON = 1
DEV_VOICES, VOICE_SIZE = 0x70, 0x21
OPTAB = (0, 1, 2, 8, 9, 10, 16, 17, 18)
HEADROOM = round(6 / 0.75)              # Opl3Headroom's default, 6 dB


def songs():
    """(name, path) of every song the suite plays"""
    out = []
    for folder in ("win30", "win98", "winxp"):
        for name in sorted(os.listdir(os.path.join(MIDI, folder))):
            out.append((folder + "/" + name, os.path.join(MIDI, folder, name)))
    out.append(("GROOVE.MID", os.path.join(ROOT, "digital_recording",
                                           "GROOVE.MID")))
    out.append(("GMCHECK.MID", os.path.join(ROOT, "build", "GMCHECK.MID")))
    return out


class Device:
    """a device of the driver in the emulator, open, with its reference"""

    def __init__(self, device, ini=None):
        with open(DRV, "rb") as f:
            self.e = e = ESFMEmu(f.read())
        for key, value in (ini or {}).items():
            e.ini[("esfm.drv", key.lower())] = value
        assert e.driverproc(DRV_ENABLE) & 0xFFFF == 1
        e.open(device=device)
        self.device = device
        ini = ini or {}
        self.ref = Reference(mpc=ini.get("MpcSongs", "1") != "0",
                             pedal_release=ini.get("PedalRelease", "1") != "0")
        self.headroom = e.fix_state()["opl3_att"]
        self.problems = []
        self.at = 0.0
        self.copy_sounded = False       # a note of channels 13-16 sounded

    def send(self, m):
        """a short message (an int) or a long one (bytes), to the device and
        the reference"""
        if isinstance(m, (bytes, bytearray)):
            assert self.e.longdata(bytes(m))[0] == 0
            self.ref.sysex(m)
            return False
        assert self.e.data(m) == 0
        return self.ref.message(m & 0xFF, m >> 8 & 0x7F, m >> 16 & 0x7F)

    def voices(self):
        """the voices that sound: (voice, channel, key) of the device's
        table, those the chip has keyed on (device 1) or the table keys on
        (device 0)"""
        e = self.e
        if self.device == 1:
            raw = e.rd(SEG_PARA[4], e.dev + DEV_OPL_VOICE, OV_SIZE * 18)
            out = []
            for v in e.chip.opl_keyed():
                b = raw[OV_SIZE * v:OV_SIZE * (v + 1)]
                out.append((v, b[1], b[2], b[0]))
            return out
        raw = e.rd(SEG_PARA[4], e.dev + DEV_VOICES, VOICE_SIZE * 18)
        return [(v, raw[VOICE_SIZE * v + 5], raw[VOICE_SIZE * v + 6],
                 raw[VOICE_SIZE * v]) for v in range(18)
                if raw[VOICE_SIZE * v] & 1]

    def carrier(self, v):
        b, c = 0x100 * (v // 9), v % 9
        return self.e.chip.opl[b + 0x40 + OPTAB[c] + 3] & 0x3F

    def problem(self, *what):
        if len(self.problems) < 8:
            self.problems.append(("%.3f s" % self.at,) + what)

    def check(self, m=None, dropped=False):
        """the device against the reference after message m"""
        ref = self.ref
        sounding = self.voices()
        for v, ch, key, flags in sounding:
            if ch >= 12:
                self.copy_sounded = True
            if (ch, key) not in ref.notes:
                self.problem("base-level copy" if (ch, key) in ref.left_out
                             else "stuck", v, ch + 1, key)
                continue
            if self.device == 1:
                if not flags & OVF_ON:
                    self.problem("keyed off in the table", v, ch + 1, key)
                want = ref.level(ch, key, self.headroom)
                got = self.carrier(v)
                if got != want:
                    self.problem("level", v, ch + 1, key, got, want)
        if self.device == 1 and isinstance(m, int) and m & 0xF0 == 0x90 \
                and m >> 16 & 0x7F and not dropped:
            ch, key = m & 15, m >> 8 & 0x7F
            if (ch != 9 or 35 <= key <= 81) and not any(
                    c == ch and k == key for _v, c, k, _f in sounding):
                self.problem("not played", ch + 1, key)

    def play(self, path, seconds=None):
        for at, kind, data in read_smf(path):
            if seconds is not None and at > seconds:
                break
            self.at = at
            if kind == "sysex":
                self.send(data)
                self.check()
                continue
            m = data[0] | (data[1] << 8 if len(data) > 1 else 0) | \
                (data[2] << 16 if len(data) > 2 else 0)
            dropped = self.send(m)
            self.check(m, dropped)


@unittest.skipUnless(HAVE_UNICORN, "needs the unicorn module")
class SongTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        with open(gmcheck.OUT, "rb") as f:
            assert f.read() == gmcheck.smf(gmcheck.events()), \
                "build/GMCHECK.MID is out of date: run tools/gmcheck.py"

    def play_all(self, device):
        for name, path in songs():
            with self.subTest(song=name):
                d = Device(device)
                d.play(path, SECONDS)
                self.assertEqual(d.problems, [])
                drops = d.e.fix_state()["mpc_drops"]
                self.assertEqual(drops, d.ref.dropped)
                self.assertEqual(drops > 0, name in TWO)

    def test_device_0(self):
        self.play_all(0)

    def test_device_1(self):
        self.play_all(1)

    def test_random_songs(self):
        # every controller the devices play, on sounding notes too, with the
        # pedal, resets and the master volume, checked after every message
        for device in (0, 1):
            for seed in range(8):
                with self.subTest(device=device, seed=seed):
                    rng = random.Random(seed)
                    d = Device(device)
                    for m in stream(rng, 300):
                        if rng.random() < 0.01:
                            m = rng.choice([GM_ON, midiref.master_volume(
                                rng.randrange(0x4000))])
                        dropped = d.send(m)
                        d.check(m, dropped)
                    self.assertEqual(d.problems, [])

    def test_both_arrangements_with_mpc_songs_0(self):
        # every note of the song plays, its copy's too, and nothing sticks;
        # by default no note of the copy sounds
        for device in (0, 1):
            for ini, copy in (({"MpcSongs": "0"}, True), ({}, False)):
                with self.subTest(device=device, ini=ini):
                    d = Device(device, ini)
                    d.play(os.path.join(MIDI, "win30", "CANYON.MID"), 15)
                    self.assertEqual(d.problems, [])
                    self.assertEqual(d.e.fix_state()["mpc_drops"] == 0,
                                     copy)
                    self.assertEqual(d.copy_sounded, copy)


def stream(rng, n):
    """n random channel messages of a song, with RPN 0 in threes"""
    out = []
    for _ in range(n):
        ch = rng.choice([0, 0, 1, 2, 9, 9, 3, 12, 15, 10])
        r = rng.random()
        if r < 0.45:
            out.append(0x90 | ch | rng.randrange(24, 100) << 8 |
                       rng.choice([0, 1, 40, 80, 127]) << 16)
        elif r < 0.65:
            out.append(0x80 | ch | rng.randrange(24, 100) << 8)
        elif r < 0.75:
            out.append(0xC0 | ch | rng.randrange(128) << 8)
        elif r < 0.85:
            out.append(0xB0 | ch | rng.choice([1, 7, 7, 10, 11, 11, 64, 120,
                                               121, 123]) << 8 |
                       rng.randrange(128) << 16)
        elif r < 0.95:
            out.append(0xE0 | ch | rng.randrange(128) << 8 |
                       rng.randrange(128) << 16)
        else:
            out += [0x65B0 | ch, 0x64B0 | ch, 0x06B0 | ch |
                    rng.randrange(13) << 16]
    return out


# a probe on channel 5: program 21, volume 90, expression 110, pan 60,
# a bend, and E4 at velocity 100
PROBE = [0x15C4, 0x5A07B4, 0x6E0BB4, 0x3C0AB4, 0x4000E4, 0x644094]


@unittest.skipUnless(HAVE_UNICORN, "needs the unicorn module")
class HistoryTest(unittest.TestCase):
    SEEDS = 12

    def probe(self, d):
        """the registers of the probe's voices, in no voice's order"""
        for m in PROBE:
            d.send(m)
        e, regs = d.e, []
        for v, ch, key, _f in d.voices():
            if (ch, key) != (4, 0x40):
                continue
            if d.device == 1:
                b, c = 0x100 * (v // 9), v % 9
                op = OPTAB[c]
                regs.append(tuple(e.chip.opl[b + r + op + k]
                                  for k in (0, 3)
                                  for r in (0x20, 0x40, 0x60, 0x80, 0xE0)) +
                            tuple(e.chip.opl[b + r + c]
                                  for r in (0xA0, 0xB0, 0xC0)))
            else:
                regs.append(bytes(e.chip.regs[v * 32:v * 32 + 32]))
        return sorted(regs)

    def check(self, start):
        for device in (0, 1):
            want = self.probe(Device(device))
            self.assertTrue(want)
            for seed in range(self.SEEDS):
                with self.subTest(device=device, seed=seed):
                    rng = random.Random(seed)
                    d = Device(device)
                    for m in stream(rng, rng.randrange(50, 400)):
                        d.send(m)
                    start(d)
                    self.assertEqual(self.probe(d), want)

    def test_after_a_gm_reset(self):
        self.check(lambda d: d.send(GM_ON))

    def test_after_modm_reset(self):
        def reset(d):
            self.assertEqual(d.e.reset(), 0)
            d.ref.reset()
        self.check(reset)

    def test_after_a_new_open(self):
        def reopen(d):
            d.e.close()
            d.e.open(device=d.device, enable=False)
            d.ref.reset()
        self.check(reopen)


def cc(channel, number, value):
    return 0xB0 | channel | number << 8 | value << 16


@unittest.skipUnless(HAVE_UNICORN, "needs the unicorn module")
class RulesTest(unittest.TestCase):
    """General MIDI's rules, each on the devices that follow it"""

    def both(self, ini=None):
        for device in (0, 1):
            with self.subTest(device=device):
                yield Device(device, ini)

    def keys(self, d, channel=0):
        return sorted({k for _v, c, k, _f in d.voices() if c == channel})

    def run_rule(self, msgs, want, device=None):
        for d in self.both():
            if device is not None and d.device != device:
                continue
            for m in msgs:
                d.send(m)
                d.check(m)
            self.assertEqual(d.problems, [])
            self.assertEqual(self.keys(d), want)

    def test_velocity_0_is_a_note_off(self):
        self.run_rule([0x643C90, 0x003C90], [])

    def test_the_pedal_holds_note_offs(self):
        self.run_rule([cc(0, 64, 127), 0x643C90, 0x003C80], [60])
        self.run_rule([cc(0, 64, 127), 0x643C90, 0x003C80, cc(0, 64, 0)],
                      [])

    def test_all_sound_off_now(self):
        # 120 ends every note at once, the pedal's too
        self.run_rule([cc(0, 64, 127), 0x643C90, 0x003C80, 0x644090,
                       cc(0, 120, 0)], [])

    def test_all_notes_off(self):
        # 123: a note off for every note; with the pedal down they sound on
        # until it goes up on device 1, as the spec has it, where ESS's code
        # stops the notes that keys hold (docs/ESFM_GM.md)
        self.run_rule([0x643C90, 0x644090, cc(0, 123, 0)], [])
        self.run_rule([cc(0, 64, 127), 0x643C90, cc(0, 123, 0)], [60],
                      device=1)
        self.run_rule([cc(0, 64, 127), 0x643C90, cc(0, 123, 0),
                       cc(0, 64, 0)], [])

    def test_program_change_keeps_notes(self):
        self.run_rule([0x643C90, 0x05C0], [60])

    def test_program_change_lets_the_pedal_up(self):
        # PedalRelease, the default of both devices
        self.run_rule([cc(0, 64, 127), 0x643C90, 0x003C80, 0x05C0], [])

    def test_reset_all_controllers(self):
        # RP-015: the pedal up and expression 127; the volume stays
        for d in self.both():
            for m in (cc(0, 7, 64), cc(0, 11, 32), cc(0, 64, 127), 0x643C90,
                      0x644090, 0x003C80, cc(0, 121, 0)):
                d.send(m)
                d.check(m)
            self.assertEqual(d.problems, [])
            self.assertEqual(self.keys(d), [64])
            self.assertEqual((d.ref.volume[0], d.ref.expression[0]),
                             (64, 127))

    def test_gm_resets(self):
        for reset in (GM_ON, midiref.GM2_ON, GS_RESET, XG_ON):
            for d in self.both():
                for m in (cc(0, 7, 20), cc(0, 64, 127), 0x643C90, 0x003C80,
                          0x7F4091, reset):
                    d.send(m)
                d.check()
                self.assertEqual(d.voices(), [])
                d.send(0x7F3C90)
                d.check(0x7F3C90)
                self.assertEqual(d.problems, [])
                self.assertEqual(self.keys(d), [60])

    def test_volume_curve(self):
        """the GM1 Developer Guidelines' attenuation of controllers 7 and 11
        together, 40 log10 (cc7 / 127) + 40 log10 (cc11 / 127) dB, on device
        1 in its 0.75 dB steps, within half a step of each"""
        rows = ((127, 127, 0.0), (96, 127, 4.8), (64, 127, 11.9),
                (32, 127, 23.9), (16, 127, 36.0), (64, 64, 23.8),
                (32, 96, 28.8))
        for vol, expr, db in rows:
            with self.subTest(cc7=vol, cc11=expr):
                d = Device(1)
                for m in (cc(0, 7, vol), cc(0, 11, expr), 0x7F3C90):
                    d.send(m)
                d.check()
                self.assertEqual(d.problems, [])
                v = d.voices()[0][0]
                steps = d.carrier(v) - d.headroom
                self.assertLessEqual(abs(steps * 0.75 - db), 0.75)

    def test_controllers_change_sounding_notes(self):
        for m in (cc(0, 7, 40), cc(0, 11, 70), cc(0, 7, 127)):
            d = Device(1)
            d.send(0x643C90)
            d.send(m)
            d.check()
            self.assertEqual(d.problems, [])

    def test_master_volume(self):
        """the universal real-time master volume, F0 7F dd 04 01 ll mm F7,
        on both devices' curve: the notes that sound go down at once, and a
        GM reset puts it back"""
        d = Device(1)
        for m in (0x643C90, midiref.master_volume(0x2000)):
            d.send(m)
            d.check()
        self.assertEqual(d.problems, [])
        self.assertEqual(d.ref.master, midiref.master_att(0x2000))
        self.assertGreater(d.ref.master, 0)
        v = d.voices()[0][0]
        want = min(63, att(100) + att(100) + d.headroom + d.ref.master)
        self.assertEqual(d.carrier(v), want)
        d.send(GM_ON)
        d.send(0x643C90)
        d.check()
        self.assertEqual(d.problems, [])
        self.assertEqual(d.ref.master, 0)

    def test_master_curve(self):
        # 40 log10, as GM's volume, of every 512th 14-bit value
        self.assertEqual(midiref.MASTER_CURVE[31], 0)
        self.assertEqual(midiref.MASTER_CURVE[0], 63)
        self.assertEqual(midiref.MASTER_CURVE,
                         sorted(midiref.MASTER_CURVE, reverse=True))


if __name__ == "__main__":
    unittest.main()
