# (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
#
# Licensed under GPL Version 3.0
"""sb16test's checks on the Sound Blaster 16 model and on both fronts.

src/sb16t.c, the checks that sb16test.exe runs on the machine, is built
for the host and called through ctypes, with its hardware layer on an
sbgame card: the model of a Sound Blaster 16 (sbgame.ModelCard), and
esssb16.com and ES1869.VXD's front in front of the chip model
(sbfronts). A listener answers the listening checks from what the card
played while each sound ran, so a sound that doesn't do what its question
says fails here. On the model every automatic check has to pass; on the
fronts too, but for the differences SB16.md lists (KNOWN). The ES1869's
record source 7 is the frames Audio 2 played, so the fronts' loopback
checks run.

build/sb16test.exe itself runs in a CPU emulator with DOS and the BIOS
(tests/dosrun.py), on the model and on esssb16.com: its interrupt
handler, clock, DMA area, report, restore file and keyboard, with a
person who answers the questions.

usage: python3 -m unittest tests.test_sb16test   (needs gcc, nasm and
unicorn for the fronts and the program)
"""

import ctypes
import math
import os
import re
import struct
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from test_vxdext import HAVE_UNICORN, have_nasm  # noqa: E402
from sbgame import ModelCard  # noqa: E402

if HAVE_UNICORN:
    from sbfronts import TSRCard, VxDCard  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "src")
EXE = os.path.join(ROOT, "build", "sb16test.exe")
AREA = 0x40000              # the DMA area, free in tsremu's first megabyte
O_AUTO, O_LISTEN, O_SHORT, O_QUICK, O_RECORD = 0x01, 0x02, 0x04, 0x08, 0x10
IDLE_US = 25                # the time an idle() of the checks takes
# what the fronts can't do as the card does (docs/SB16.md)
KNOWN = {"dsp.ram", "dsp.e2", "dsp.reset_held", "ctl.unacknowledged"}
# esssb16 under EMM386, which doesn't trap the DMA controllers: the 16-bit
# channel's count stands still, the transfer runs on the 8-bit one's, but
# for an auto-initialized playback where Audio 2's channel is the game's,
# which Audio 2 moves (the pace)
EMM_KNOWN = {"p16.b0h", "rec.beh_mono", "rec.beh_stereo"}
# a front whose Audio 2 has no 8-bit channel: one transfer at a time
ALONE_KNOWN = {"dual.play", "dual.end16", "dual.duplex"}

u8, u16, ul = ctypes.c_uint8, ctypes.c_uint16, ctypes.c_ulong
INB = ctypes.CFUNCTYPE(u8, u16)
OUTB = ctypes.CFUNCTYPE(None, u16, u8)
NOW = ctypes.CFUNCTYPE(ul)
VOID = ctypes.CFUNCTYPE(None)
MEM = ctypes.CFUNCTYPE(None, ul, ctypes.POINTER(u8), u16)
MASK = ctypes.CFUNCTYPE(None, ctypes.c_int)
ASK = ctypes.CFUNCTYPE(ctypes.c_int, ctypes.c_char_p, ctypes.c_char_p,
                       ctypes.c_char_p)
SOUND = ctypes.CFUNCTYPE(None, ul)
TEXT = ctypes.CFUNCTYPE(None, ctypes.c_char_p)
ESC = ctypes.CFUNCTYPE(ctypes.c_int)
KEEP = ctypes.CFUNCTYPE(None, ctypes.POINTER(u8), u16)


class HAL(ctypes.Structure):
    _fields_ = [("inb", INB), ("outb", OUTB), ("now", NOW), ("idle", VOID),
                ("poke", MEM), ("peek", MEM), ("lock", VOID),
                ("unlock", VOID), ("irq_mask", MASK), ("ask", ASK),
                ("sound", SOUND), ("line", TEXT), ("say", TEXT),
                ("esc", ESC), ("keep", KEEP), ("state", TEXT)]


class CARD(ctypes.Structure):
    _fields_ = [("base", u16), ("irq", u8), ("dma8", u8), ("dma16", u8),
                ("mpu", u16), ("hz", ul), ("area", ul), ("flags", u8),
                ("env", ctypes.POINTER(ctypes.c_char_p))]


_LIB = {}


def library():
    """src/sb16t.c built as a shared library, once"""
    if "lib" not in _LIB:
        tmp = _LIB["tmp"] = tempfile.TemporaryDirectory()
        out = os.path.join(tmp.name, "libsb16t.so")
        subprocess.run(["gcc", "-std=gnu99", "-Wall", "-Wextra", "-Werror",
                        "-DESS_HOST", "-O1", "-fPIC", "-shared", "-I" + SRC,
                        "-o", out, os.path.join(SRC, "sb16t.c"),
                        os.path.join(SRC, "sbtsound.c"), "-lm"],
                       check=True, capture_output=True)
        lib = ctypes.CDLL(out)
        lib.sbt_run.argtypes = [ctypes.POINTER(HAL), ctypes.POINTER(CARD),
                                ctypes.c_uint]
        lib.sbt_run.restype = ctypes.c_int
        lib.sbt_tc_to_v.argtypes = [u8]
        lib.sbt_tc_to_v.restype = u8
        lib.sbt_rate_to_v.argtypes = [u16]
        lib.sbt_rate_to_v.restype = u8
        _LIB["lib"] = lib
    return _LIB["lib"]


def clock(card):
    """the card's time in microseconds"""
    if isinstance(card, ModelCard):
        return card.clock.us
    return card.chip.clock.us


def rms(samples):
    return math.sqrt(sum(s * s for s in samples) / len(samples)) \
        if samples else 0


def crossings_hz(samples, rate):
    """a tone's frequency from its rising zero crossings, placed between
    the samples"""
    pairs = zip(samples, samples[1:])
    at = [i + a / (a - b) for i, (a, b) in enumerate(pairs) if a < 0 <= b]
    if len(at) < 2:
        return 0
    return (len(at) - 1) * rate / (at[-1] - at[0])


def glitched(xs, rate):
    """a sample of a steady tone lost, repeated or out of place: the tone's
    own recurrence, x[n+1] + x[n-1] = 2 cos(w) x[n], fails by more than the
    steps of 8-bit samples make it; a silent channel has none"""
    if rms(xs) < 300:
        return False
    k = 2 * math.cos(2 * math.pi * crossings_hz(xs, rate) / rate)
    return any(abs(b + d - k * a) > 1200 for d, a, b in zip(xs, xs[1:], xs[2:]))


class Input:
    """what the card's ADC hears: on the ES1869 with record source 7, the
    frames Audio 2 played, else a 300 Hz tone"""

    def __init__(self, card):
        self.card, self.at = card, 0

    def __call__(self, n):
        chip = getattr(self.card, "chip", None)
        if chip is not None and (chip.mixer[0x1C] & 7) == 7:
            # the frame Audio 2's DAC plays now, a little behind the FIFO
            f = chip.played2[-32] if len(chip.played2) >= 32 else (0, 0)
            return [f if len(f) == 2 else f * 2] * n
        out = []
        for _ in range(n):
            self.at += 1
            s = int(12000 * math.sin(2 * math.pi * 300 * self.at / 22050))
            out.append((s, s))
        return out


def results(lines):
    """{check: (result, details)} of a report's check lines"""
    out = {}
    for line in lines:
        parts = line.split(None, 2)
        if len(parts) >= 2 and parts[1] in ("PASS", "FAIL", "KNOWN", "INFO",
                                            "SKIP"):
            out[parts[0]] = (parts[1], parts[2] if len(parts) > 2 else "")
    return out


class Run:
    """the checks on one card: the hardware layer's calls, what the card
    played while each listening sound ran, and the report"""

    def __init__(self, card, front, opts=O_SHORT, irq=5, dma8=1, dma16=5,
                 timeline=False):
        self.card, self.front = card, front
        # (us, frames each DAC had played) as time passes, for a recording
        self.timeline = [] if timeline else None
        self.lines, self.errors, self.asked = [], [], {}
        self.sections = []
        self.segments = []          # (rate, start, end) of each sound
        self.sounds = {}            # each question's segments
        self.mix_log = []           # (us, register, value) written
        self.out_log = []           # (us, port, value) of the FM ports
        self.index = 0
        self.locks = 0
        self.kept = []
        self.sound_start = None
        lib = library()
        card.isr = self._isr
        card.record_from(Input(card))
        self.hal = HAL(INB(self._inb), OUTB(self._outb), NOW(self._now),
                       VOID(self._idle), MEM(self._poke), MEM(self._peek),
                       VOID(self._lock), VOID(self._unlock),
                       MASK(self._mask), ASK(self._ask), SOUND(self._sound),
                       TEXT(self._line), TEXT(lambda s: None),
                       ESC(lambda: 0), KEEP(self._keep),
                       TEXT(self._state))
        env = (ctypes.c_char_p * 2)(b"machine: the host tests", None)
        self.cardinfo = CARD(0x220, irq, dma8, dma16, 0, 1000000, AREA, 0,
                             env)
        self.failed = lib.sbt_run(ctypes.byref(self.hal),
                                  ctypes.byref(self.cardinfo), opts)

    # --- the hardware layer --------------------------------------------------
    def _guard(fn):
        def call(self, *a):
            try:
                return fn(self, *a)
            except Exception as e:      # ctypes would swallow it
                import traceback
                self.errors.append("%s: %r\n%s" % (fn.__name__, e,
                                                   traceback.format_exc()))
                return 0
        return call

    @_guard
    def _inb(self, port):
        return self.card.inp(port) & 0xFF

    @_guard
    def _outb(self, port, v):
        if port == 0x224:
            self.index = v
        elif port == 0x225:
            self.mix_log.append((clock(self.card), self.index, v))
        elif port in (0x388, 0x389, 0x220, 0x221, 0x228, 0x229):
            self.out_log.append((clock(self.card), port, v))
        self.card.out(port, v)

    @_guard
    def _now(self):
        return clock(self.card)

    @_guard
    def _idle(self):
        self.card.wait(IDLE_US)
        if self.timeline is not None:
            # the times where the DACs' counts change, and the last time
            # each count held
            now, heard = clock(self.card), self.heard_now()
            if not self.timeline or heard != self.timeline[-1][1]:
                if self.still and self.still != self.timeline[-1:][0:1]:
                    self.timeline.append(self.still)
                self.timeline.append((now, heard))
                self.still = None
            else:
                self.still = (now, heard)

    still = None

    @_guard
    def _poke(self, addr, src, n):
        self.card.poke(addr, bytes(src[:n]))

    @_guard
    def _peek(self, addr, dst, n):
        data = self.card.peek(addr, n)
        for i, b in enumerate(data):
            dst[i] = b

    # the handler runs only between the checks' locked port accesses
    def _lock(self):
        if self.locks == 0:
            self.was_in_isr = self.card.in_isr
            self.card.in_isr = True
        self.locks += 1

    def _unlock(self):
        self.locks -= 1
        if self.locks == 0:
            self.card.in_isr = self.was_in_isr

    def _mask(self, masked):
        self.card.isr = None if masked else self._isr

    def _isr(self, card):
        library().sbt_irq()

    @_guard
    def _line(self, text):
        self.lines.append(text.decode())

    @_guard
    def _state(self, section):
        self.sections.append(section.decode())

    @_guard
    def _keep(self, pairs, n):
        self.kept.append(bytes(pairs[:n]))

    @_guard
    def _sound(self, rate):
        if rate:
            self.sound_start = (rate, clock(self.card), self.heard_now())
        elif self.sound_start is not None:
            rate, t0, start = self.sound_start
            self.segments.append((rate, t0, clock(self.card), start,
                                  self.heard_now()))
            self.sound_start = None

    # --- the listener --------------------------------------------------------
    def heard_now(self):
        """where each audible output is: the model's one DAC, the chip's
        two"""
        if self.front:
            return (len(self.card.chip.played), len(self.card.chip.played2))
        return (len(self.card.played),)

    def parts(self, seg):
        """the frames each DAC played during a sound"""
        rate, t0, t1, start, end = seg
        lists = ([self.card.chip.played, self.card.chip.played2]
                 if self.front else [self.card.played])
        return [lst[a:b] for lst, a, b in zip(lists, start, end)]

    def frames(self, seg):
        return [f for part in self.parts(seg) for f in part]

    def side(self, seg):
        fr = self.frames(seg)
        left = rms([f[0] for f in fr])
        right = rms([f[-1] for f in fr])
        if max(left, right) < 300:
            return "n"
        if left > 4 * right:
            return "l"
        if right > 4 * left:
            return "r"
        return "b"

    def present(self, seg):
        return rms([s for f in self.frames(seg) for s in f]) > 300

    def glitchy(self, seg):
        """a steady tone, on either DAC, with a glitch (glitched)"""
        for part in self.parts(seg):
            body = part[64:-64]
            for c in range(len(body[0]) if body else 0):
                if glitched([f[c] for f in body], seg[0]):
                    return True
        return False

    def tone_hz(self, frames, rate):
        return crossings_hz([f[0] for f in frames], rate)

    def answer(self, id_, keys):
        if id_ == "r.level":
            return "\r"            # the recorder is set: the run begins
        segs = self.segments[self.asked_from:]
        seg = segs[-1] if segs else None
        if seg is None:
            return "n"
        if id_ in ("l.left16", "l.right16", "l.left8", "l.right8"):
            return self.side(seg)
        if id_ == "l.pitch":
            hz = [self.tone_hz(self.frames(s), s[0]) for s in segs[-3:]]
            return "y" if len(hz) == 3 and max(hz) < min(hz) * 1.05 and \
                min(hz) > 300 else "n"
        if id_ == "l.dual":
            parts = [p for p in self.parts(seg) if rms([f[0] for f in p])
                     > 300]
            if len(parts) == 2:
                return "b"
            if not parts:
                return "n"
            hz = self.tone_hz(parts[0], seg[0])
            return "h" if hz > 600 else "l"
        if id_ == "l.pause":
            rate, t0, t1 = seg[:3]
            got = max(len(p) for p in self.parts(seg))
            return "y" if got < 0.85 * (t1 - t0) * rate / 1e6 else "n"
        if id_ == "l.blocks":
            rate, t0, t1 = seg[:3]
            got = max(len(p) for p in self.parts(seg))
            return "y" if got > 0.85 * (t1 - t0) * rate / 1e6 else "n"
        if id_ in ("l.volume", "l.balance"):
            writes = [(r, v) for us, r, v in self.mix_log
                      if seg[1] <= us <= seg[2]]
            if id_ == "l.volume":
                levels = [v for r, v in writes if r == 0x30]
                return "y" if len(levels) >= 3 and levels == sorted(
                    levels, reverse=True) and self.present(seg) else "n"
            first = [v for r, v in writes if r == 0x33][:1]
            last = [v for r, v in writes if r == 0x33][-1:]
            return "y" if first == [0] and last == [0xF8] else "n"
        if id_ == "l.fm":
            keyon = [v for us, p, v in self.out_log
                     if seg[1] <= us <= seg[2] and v & 0x20 and p & 1]
            return "y" if keyon else "n"
        if id_ == "l.record":
            fr = self.frames(seg)
            hz = self.tone_hz(fr, seg[0]) if fr else 0
            return "y" if self.present(seg) and hz > 0 else "n"
        if not self.present(seg):
            return "n"
        if id_.startswith("l.clean.") or id_ == "l.tone8":
            return "d" if self.glitchy(seg) else "y"
        return "y"

    @_guard
    def _ask(self, id_, question, keys):
        id_ = id_.decode()
        a = self.answer(id_, keys.decode())
        self.asked[id_] = a
        self.sounds.setdefault(id_, []).extend(
            self.segments[self.asked_from:])
        self.asked_from = len(self.segments)
        return ord(a)

    asked_from = 0

    # --- the report ----------------------------------------------------------
    def results(self):
        return results(self.lines)

    def report(self):
        return "\n".join(self.lines)


class GlitchTest(unittest.TestCase):
    """the listener's ear for a distorted or looping tone"""

    def test_glitched(self):
        tone = [int(12000 * math.sin(2 * math.pi * 441 * i / 22050))
                for i in range(2000)]
        eight = [(s >> 8) << 8 for s in tone]
        self.assertFalse(glitched(tone, 22050))
        self.assertFalse(glitched(eight, 22050))        # 8-bit steps
        self.assertFalse(glitched([0] * 2000, 22050))
        self.assertTrue(glitched(tone[:1000] + tone[1001:], 22050))  # lost
        self.assertTrue(glitched(tone[:1000] + tone[999:], 22050))   # again
        # a loop back to another phase; 6 whole cycles would join cleanly
        self.assertTrue(glitched(tone[:1000] + tone[1325:], 22050))
        self.assertFalse(glitched(tone[:1000] + tone[1300:], 22050))


class RateTest(unittest.TestCase):
    """the rates DSP 4.05 computes, as the model has them"""

    def test_rates(self):
        import sb16
        lib = library()
        for tc in range(256):
            self.assertEqual(lib.sbt_tc_to_v(tc), sb16.tc_to_v(tc), hex(tc))
        for rate in list(range(4000, 50000, 37)) + [5000, 22050, 23000,
                                                    44100, 48000, 65535]:
            self.assertEqual(lib.sbt_rate_to_v(rate), sb16.rate_to_v(rate),
                             rate)


class Checks:
    """the checks on one kind of card; known: the checks that may be KNOWN"""

    known = set()
    front = True

    def card(self):
        raise NotImplementedError

    @classmethod
    def setUpClass(cls):
        cls.run_ = Run(cls.card(cls), cls.front)

    def test_no_errors(self):
        self.assertEqual(self.run_.errors, [], self.run_.report())

    def test_every_check_passes(self):
        bad = {k: v for k, v in self.run_.results().items()
               if v[0] == "FAIL" or (v[0] == "KNOWN" and k not in self.known)}
        self.assertEqual(bad, {}, self.run_.report())

    def test_summary(self):
        lines = self.run_.lines
        self.assertTrue(lines[0].startswith("sb16test "))
        self.assertIn("[summary]", lines)
        self.assertTrue(any(line.startswith("pass=") for line in lines))
        self.assertEqual(self.run_.failed, 0)

    def test_listening(self):
        """every listening check asked, and its sound was what it asks"""
        res = self.run_.results()
        listen = [k for k in res if k.startswith("l.")]
        self.assertGreaterEqual(len(listen), 14)
        for k in listen:
            self.assertIn(res[k][0], ("PASS", "SKIP"), (k, res[k]))

    def test_pitch(self):
        """every listening tone at one pitch, 441 Hz, whatever its rate and
        buffer, and the two tones at once at 294 and 882 Hz; measured on
        each DAC and channel that sounds, past the first quarter of each
        sound, where a transfer may end another, to 0.5 %: the short run's
        sounds at 44.1 kHz hold three cycles"""
        run, bad = self.run_, []
        for id_, segs in run.sounds.items():
            if id_ in ("l.direct", "l.fm", "l.record", "l.adpcm"):
                continue
            want = (294, 882) if id_ == "l.dual" else (441,)
            for seg in segs:
                for part in run.parts(seg):
                    part = part[len(part) // 4:]
                    for c in range(len(part[0]) if part else 0):
                        xs = [f[c] for f in part]
                        if rms(xs) < 300:
                            continue
                        hz = crossings_hz(xs, seg[0])
                        if all(abs(hz - w) > w / 200 for w in want):
                            bad.append((id_, c, round(hz, 1)))
        self.assertGreater(len(run.sounds), 14)
        self.assertEqual(bad, [], run.report())

    def test_mixer_back(self):
        """the restore file's pairs, and none at the end"""
        kept = self.run_.kept
        self.assertGreaterEqual(len(kept), 2)
        self.assertGreater(len(kept[0]), 0)
        self.assertEqual(kept[-1], b"")


class ModelTest(Checks, unittest.TestCase):
    """the model of a Sound Blaster 16: what the checks expect"""

    front = False

    def card(self):
        return ModelCard()


@unittest.skipUnless(HAVE_UNICORN and have_nasm(), "needs nasm and unicorn")
class TSRTest(Checks, unittest.TestCase):
    """esssb16.com with Audio 2 on channel 3"""

    known = KNOWN

    def card(self):
        return TSRCard("qpi", dma2=3)

    def test_audio2(self):
        res = self.run_.results()
        self.assertIn("Audio 2", res["route.audio2"][1])
        self.assertEqual(res["loop.left"][0], "PASS")
        self.assertEqual(res["loop.right"][0], "PASS")


@unittest.skipUnless(HAVE_UNICORN and have_nasm(), "needs nasm and unicorn")
class TSREmmTest(Checks, unittest.TestCase):
    """esssb16.com under EMM386, Audio 2 on DMA 5, as on the card of the
    first report: the 16-bit count stands still but where Audio 2 paces
    it, and everything plays on Audio 1, one transfer at a time"""

    known = KNOWN | EMM_KNOWN | ALONE_KNOWN

    def card(self):
        return TSRCard("emm", dma2=5)

    def test_limits_known(self):
        res = self.run_.results()
        for k in EMM_KNOWN | ALONE_KNOWN:
            self.assertEqual(res[k][0], "KNOWN", (k, res[k]))
        self.assertIn("still, ch 1 moved", res["p16.b0h"][1])
        self.assertIn("dma=moving", res["p16.b6h_mono"][1])
        self.assertEqual(res["ctl.count16"][0], "PASS")
        self.assertIn("Audio 2's DMA is 5 (PnP 75h)", res["route.audio2"][1])
        self.assertIn("one at a time", res["dual.play"][1])


@unittest.skipUnless(HAVE_UNICORN and have_nasm(), "needs nasm and unicorn")
class VxDTest(Checks, unittest.TestCase):
    """ES1869.VXD's front in a DOS box, Audio 2 on channel 3"""

    known = KNOWN

    def card(self):
        return VxDCard(dma2=3)


# --- the program --------------------------------------------------------------

def exe_run(card, tail, keys="", files=None, vxd=None, umb=None):
    """build/sb16test.exe on card until it exits, with files on the disk
    (in a DOS box with vxd, and a program in upper memory with umb, as
    dosrun has them): the runner and the exit code"""
    from dosrun import Dos
    with open(EXE, "rb") as f:
        exe = f.read()
    card.record_from(Input(card))
    d = Dos(exe, card, keys=keys, vxd=vxd, umb=umb)
    for name, data in (files or {}).items():
        d.files[name] = bytearray(data)
    return d, d.run(tail)


def report(d):
    return d.files["SB16TEST.TXT"].decode("latin-1").splitlines()


class Person:
    """at the keyboard: each question's answers in turn (g: again), and Esc
    once `esc_after` is on the screen"""

    def __init__(self, answers, esc_after=None):
        self.answers, self.esc_after = answers, esc_after
        self.escaped = False

    def __call__(self, output):
        if self.esc_after and self.esc_after in output and not self.escaped:
            self.escaped = True
            return "\x1b"
        if not output.endswith("> "):
            return None
        asked = re.findall(r"\n([lr]\.[\w.]+): ", output)
        return self.answers[asked[-1]][asked.count(asked[-1]) - 1]


@unittest.skipUnless(HAVE_UNICORN, "needs unicorn")
class ExeModelTest(unittest.TestCase):
    """build/sb16test.exe on the model of a Sound Blaster 16"""

    SECTIONS = ["[env]", "[dsp]", "[mixer]", "[irq]", "[play]", "[rate]",
                "[control]", "[dual]", "[record]", "[misc]", "[fm]",
                "[time]", "[summary]"]

    @classmethod
    def setUpClass(cls):
        cls.card = ModelCard()
        cls.d, cls.code = exe_run(cls.card, "/A /T")
        cls.lines = report(cls.d)
        cls.res = results(cls.lines)

    def test_every_check_passes(self):
        bad = {k: v for k, v in self.res.items() if v[0] in ("FAIL", "KNOWN")}
        self.assertEqual(bad, {}, "\n".join(self.lines))
        self.assertEqual(self.code, 0)
        self.assertIn("0 checks failed.", self.d.output)

    def test_sections(self):
        self.assertEqual([s for s in self.lines if s.startswith("[")],
                         self.SECTIONS)
        self.assertGreater(len(self.res), 90)

    def test_machine(self):
        """what the report says of the machine"""
        for line in ("dos: 7.10", "windows: not running",
                     "esssb16: not resident",
                     "blaster: A220 I5 D1 H5 P330 T6"):
            self.assertIn(line, self.lines)

    def test_left_as_it_was(self):
        """the PIT in the BIOS's mode again, the IRQ masked and its vector
        back, the channels masked, the report the only file"""
        d = self.d
        self.assertEqual(d.pit_mode, 3)
        self.assertTrue(d.pic_mask[0] & 0x20)
        self.assertEqual(d.mem(0x0D * 4, 4), bytes(4))
        self.assertTrue(self.card.dma.ch[1].masked)
        self.assertTrue(self.card.dma.ch[5].masked)
        self.assertEqual(set(d.files), {"SB16TEST.TXT"})


@unittest.skipUnless(HAVE_UNICORN and have_nasm(), "needs nasm and unicorn")
class ExeTSRTest(unittest.TestCase):
    """build/sb16test.exe on esssb16.com, which it finds resident"""

    @classmethod
    def setUpClass(cls):
        cls.d, cls.code = exe_run(TSRCard("qpi", dma2=3), "/A /T /Q")
        cls.lines = report(cls.d)
        cls.res = results(cls.lines)

    def test_every_check_passes(self):
        bad = {k: v for k, v in self.res.items()
               if v[0] == "FAIL" or (v[0] == "KNOWN" and k not in KNOWN)}
        self.assertEqual(bad, {}, "\n".join(self.lines))
        self.assertEqual(self.code, 0)

    def test_front(self):
        self.assertTrue(any(line.startswith("esssb16: resident at 1000h")
                            for line in self.lines))
        self.assertIn("behind a front", self.res["card.es1869"][1])
        self.assertIn("Audio 2", self.res["route.audio2"][1])
        self.assertEqual(self.res["loop.left"][0], "PASS")

    def test_state(self):
        """esssb16's variables after each section, from its signature on"""
        state = [line for line in self.lines if line.startswith("esssb16 ")
                 and ":" in line]
        sections = {line.split()[1] for line in state}
        self.assertLessEqual({"env", "dsp", "play", "dual", "record"},
                             sections)
        for line in state:
            part = line.split()[2]              # 0:<96 bytes>, 1:<96 bytes>
            self.assertEqual(len(part), 2 + 2 * 96, line)
            if part.startswith("0:"):
                self.assertTrue(part[2:].startswith(b"ESSSB16".hex().upper()),
                                line)


@unittest.skipUnless(HAVE_UNICORN, "needs unicorn")
class ExePersonTest(unittest.TestCase):
    """build/sb16test.exe with a person: the listening checks' questions
    and answers, Esc, and the restore file of a run that didn't finish"""

    CLEAN = ("base", "odd_block", "demand", "long", "11k", "44k", "stereo8",
             "16bit", "stereo16")
    ANSWERS = {"l.tone8": "gy", "l.left16": "l", "l.right16": "r",
               "l.left8": "l", "l.right8": "r", "l.pitch": "y",
               "l.dual": "h", "l.pause": "y", "l.volume": "y",
               "l.balance": "y", "l.adpcm": "y", "l.direct": "y",
               "l.speaker": "y", "l.blocks": "y", "l.fm": "y",
               "l.record": "s", **{"l.clean." + c: "y" for c in CLEAN}}

    def test_listening(self):
        d, code = exe_run(ModelCard(), "/L /T", keys=Person(self.ANSWERS))
        res = results(report(d))
        listen = {k: v[0] for k, v in res.items() if k.startswith("l.")}
        self.assertEqual(set(listen), set(self.ANSWERS))
        self.assertEqual(listen.pop("l.record"), "SKIP")
        self.assertEqual(res["l.fm"], ("SKIP", "not on this card"))
        del listen["l.fm"]
        self.assertEqual(set(listen.values()), {"PASS"})
        self.assertEqual(d.output.count("\nl.tone8: "), 2)     # again
        self.assertIn("answer y (y expected)", res["l.tone8"][1])
        self.assertEqual(code, 0)
        # each question and its answers on a line of their own, within the
        # screen's 80 columns
        lines = d.output.split("\n")
        asked = [i for i, s in enumerate(lines) if re.match(r"l\.[\w.]+: ", s)]
        # l.tone8 twice (again), and l.fm not at all (the model has no FM)
        self.assertEqual(len(asked), len(self.ANSWERS))
        for i in asked:
            self.assertLess(len(lines[i]), 80, lines[i])
            self.assertTrue(lines[i + 1].startswith("  ["), lines[i + 1])
            self.assertLess(len(lines[i + 1]), 80, lines[i + 1])

    def test_escape_and_restore(self):
        """SB16TEST.RST puts the mixer back before the checks; Esc stops
        them, and the mixer goes back to what it was when they began"""
        card = ModelCard()
        rst = b"SBTRST1\0" + struct.pack("<HH", 0x220, 4) + \
            bytes((0x30, 0x80, 0x31, 0x90))
        d, _ = exe_run(card, "/A /T", keys=Person({}, "the interrupts"),
                       files={"SB16TEST.RST": rst})
        lines = report(d)
        self.assertIn("restored the mixer from SB16TEST.RST: the last run "
                      "didn't finish", lines)
        self.assertTrue(any(line.startswith("pass=") and
                            line.endswith("(stopped)") for line in lines))
        self.assertNotIn("SB16TEST.RST", d.files)
        self.assertEqual(card.sb.mix,
                         {**card.sb.DEFAULTS, 0x30: 0x80, 0x31: 0x90})

    def test_dos_box(self):
        """in a Windows DOS box: ES1869.VXD's version and settings, which
        its API gives, or none, with ESS's driver"""
        adi = bytearray(0xE9)
        struct.pack_into("<I", adi, 0x55, 0x00012345)     # the devnode
        for info, line in (((0x0117, 0xFFFF, 0x4321),
                            "es1869.vxd: register API 1.17, features FFFF, "
                            "settings 4321"),
                           (None, "es1869.vxd: no register API (ESS's "
                            "driver, or none)")):
            d, _ = exe_run(ModelCard(), "/A /T",
                           keys=Person({}, "the DSP's commands"),
                           vxd={"adi": bytes(adi), "info": info})
            lines = report(d)
            self.assertIn("windows: 4.10, a DOS box", lines)
            self.assertIn(line, lines)
            self.assertEqual([(c[0], c[3]) for c in d.vxd_calls],
                             [(0x0001, 0xE9), (0x0400, None)])
            self.assertEqual(d.vxd_calls[1][2], 0x00012345)

    def test_recorded_run(self):
        """/R: the level tone until Enter, then each listening sound after
        its marker, nothing asked, and the times in the report"""
        d, code = exe_run(ModelCard(), "/R /L /T",
                          keys=Person({"r.level": "\r"}))
        self.assertEqual(code, 0)
        self.assertEqual(d.output.count("\nr.level: "), 1)
        self.assertNotRegex(d.output, r"\nl\.[\w.]+: ")    # no questions
        self.assertIn("A recorded run", d.output)
        lines = report(d)
        res = results(lines)
        self.assertTrue(res["rec.start"][1].startswith("marker ABCD at 0 us"))
        self.assertTrue(res["rec.end"][1].startswith("marker DCBA at "))
        recorded = [(k, v[1]) for k, v in res.items()
                    if v[1].startswith("recorded: ")]
        self.assertGreater(len(recorded), 20)
        last = None
        for i, (k, det) in enumerate(recorded):
            m = re.match(r"recorded: marker \*(\d\d)# at (\d+) us, "
                         r"sound((?: \d+-\d+)+) us, (\w+) expected", det)
            self.assertIsNotNone(m, det)
            at = int(m.group(2))
            if last is not None:        # 50 ms of quiet before a marker
                self.assertGreaterEqual(at - last, 50000, k)
            spans = [tuple(map(int, s.split("-")))
                     for s in m.group(3).split()]
            self.assertTrue(all(at < a < b for a, b in spans), det)
            last = spans[-1][1]
        end = int(res["rec.end"][1].split()[3])
        self.assertGreaterEqual(end - last, 50000)
        self.assertEqual(res["l.record"],
                         ("SKIP", "no one to speak in a recorded run"))

    def test_tsr_in_upper_memory(self):
        """esssb16 loaded high (LH): found with the UMBs linked into the
        chain, and the link put back; its variables in the report"""
        image = bytearray(0x110 * 16)
        image[0x103:0x10B] = b"ESSSB16\0"
        image[0x10B:0x10F] = b"\x12\x34\x56\x78"
        d, _ = exe_run(ModelCard(), "/A /T",
                       keys=Person({}, "the DSP's commands"),
                       umb=(bytes(image), 0x110))
        lines = report(d)
        self.assertIn("esssb16: resident at C801h, 4352 bytes", lines)
        self.assertEqual(d.umb_calls, [1, 0])
        self.assertFalse(d.umb_linked)
        state = [line for line in lines if line.startswith("esssb16 env 0:")]
        self.assertEqual(len(state), 1)
        self.assertTrue(state[0].endswith(":" + (b"ESSSB16\0\x12\x34\x56\x78"
                                                 + bytes(84)).hex().upper()))


if __name__ == "__main__":
    unittest.main()
