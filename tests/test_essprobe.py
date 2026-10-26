# (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
#
# Licensed under GPL Version 3.0
"""essprobe's cases on the chip model, its window under EMM386, and the
program itself.

src/essp.c, the cases that essprobe.exe runs on the machine, is built for
the host and called through ctypes, its hardware layer on the bare chip
model (sbfronts.ChipCard) with the short sounds (/T). Every case runs: the
rates the DMA measures must be the registers', the pauses must stop it,
the chip must see no undefined use but in the cases that look for one
(78h bit 5), the DMA no cycle the wrong way round but in the cases that
make one, and the run must give back everything it changed: the mixer,
the PnP registers, the interrupt mask and the VGA DAC. With emm386dma's
EMM386 in front of the DMA controllers, the h1 cases must tell when the
8237 gets a program's programming and whose flip-flop it is.

A recorded run's recording is made from what the chip's two DACs played,
each frame at the time the run's timeline gives, through 14h's and 7Ch's
levels: tools/essprec.py has to pass it, and to fail it broken the ways a
card's sounds break (a flat tone, a loop, a dead side, a sound where
there should be none, another rate).

src/essp0.asm, the window, runs in tsremu's model of EMM386's call of a
trap handler at ring 0; build/essprobe.exe runs in tests/dosrun.py, with
its refusals and its restore file.

usage: python3 -m unittest tests.test_essprobe   (needs gcc, nasm and
unicorn)
"""

import ctypes
import os
import re
import struct
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(
    os.path.abspath(__file__))), "tools"))

from test_vxdext import HAVE_UNICORN, have_nasm  # noqa: E402

try:
    import numpy as np
    import essprec
except ImportError:             # the analysis needs numpy
    np = essprec = None

if HAVE_UNICORN:
    from sbfronts import ChipCard, TSRCard  # noqa: E402
    from tsremu import EmmCpu, EmuError  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "src")
EXE = os.path.join(ROOT, "build", "essprobe.exe")
AREA = 0x40000              # the DMA area, 64 KB in one page
O_RECORD, O_SHORT, O_SILENT, O_TINY = 0x01, 0x02, 0x04, 0x08
G_ALL = 0x1FF
IDLE_US = 25                # the time an idle() of the cases takes
GROUPS = ("env", "rst", "a1", "a2", "both", "h1", "h4", "stream", "fix")
EXPECTS = {"tone", "left", "right", "dual", "low", "none", "pause", "lpause",
           "glide", "loop", "any", "-"}
# the cases that look for what the data sheet doesn't define
WIDE = {"a2.wide", "a2.wide_packed", "stream.a2w"}
WRONG = {"h4.wrong1", "h4.wrong2"}

u8, u16, ul = ctypes.c_uint8, ctypes.c_uint16, ctypes.c_ulong


class IO(ctypes.Structure):
    _fields_ = [("kind", u8), ("value", u8), ("port", u16)]


INB = ctypes.CFUNCTYPE(u8, u16)
OUTB = ctypes.CFUNCTYPE(None, u16, u8)
NOW = ctypes.CFUNCTYPE(ul)
VOID = ctypes.CFUNCTYPE(None)
MEM = ctypes.CFUNCTYPE(None, ul, ctypes.POINTER(u8), u16)
HOOK = ctypes.CFUNCTYPE(None, u8)
MASK = ctypes.CFUNCTYPE(None, ctypes.c_int)
ASK = ctypes.CFUNCTYPE(ctypes.c_int, ctypes.c_char_p, ctypes.c_char_p,
                       ctypes.c_char_p)
TEXT = ctypes.CFUNCTYPE(None, ctypes.c_char_p)
ESC = ctypes.CFUNCTYPE(ctypes.c_int)
KEEP = ctypes.CFUNCTYPE(None, ctypes.POINTER(u8), u16)
PHYS = ctypes.CFUNCTYPE(ctypes.c_int, ctypes.POINTER(IO), u16)


class HAL(ctypes.Structure):
    _fields_ = [("inb", INB), ("outb", OUTB), ("now", NOW), ("idle", VOID),
                ("poke", MEM), ("peek", MEM), ("lock", VOID),
                ("unlock", VOID), ("hook", HOOK), ("irq_mask", MASK),
                ("ask", ASK), ("line", TEXT), ("say", TEXT), ("esc", ESC),
                ("keep", KEEP), ("phys", PHYS)]


class CARD(ctypes.Structure):
    _fields_ = [("base", u16), ("hz", ul), ("area", ul), ("flags", u8),
                ("env", ctypes.POINTER(ctypes.c_char_p))]


_LIB = {}


def library():
    """src/essp.c built as a shared library, once"""
    if "lib" not in _LIB:
        tmp = _LIB["tmp"] = tempfile.TemporaryDirectory()
        out = os.path.join(tmp.name, "libessp.so")
        subprocess.run(["gcc", "-std=gnu99", "-Wall", "-Wextra", "-Werror",
                        "-DESS_HOST", "-O1", "-fPIC", "-shared", "-I" + SRC,
                        "-o", out, os.path.join(SRC, "essp.c"),
                        os.path.join(SRC, "sbtsound.c"), "-lm"],
                       check=True, capture_output=True)
        lib = ctypes.CDLL(out)
        lib.esp_run.argtypes = [ctypes.POINTER(HAL), ctypes.POINTER(CARD),
                                ctypes.c_uint, ctypes.c_uint,
                                ctypes.c_char_p]
        lib.esp_run.restype = ctypes.c_int
        lib.esp_parse_check.argtypes = [ctypes.c_char_p]
        lib.esp_parse_check.restype = ctypes.c_int
        for name in ("esp_case_id", "esp_case_spec", "esp_case_expect"):
            getattr(lib, name).argtypes = [u16]
            getattr(lib, name).restype = ctypes.c_char_p
        lib.esp_cases.restype = u16
        _LIB["lib"] = lib
    return _LIB["lib"]


def cases():
    """[(id, spec, expect)] of the table"""
    lib = library()
    return [(lib.esp_case_id(i).decode(), lib.esp_case_spec(i).decode(),
             lib.esp_case_expect(i).decode())
            for i in range(lib.esp_cases())]


def results(lines):
    """{case: [(result, details)]} of a report's lines"""
    out = {}
    for line in lines:
        parts = line.split(None, 2)
        if len(parts) >= 2 and parts[1] in ("PASS", "FAIL", "INFO", "SKIP"):
            out.setdefault(parts[0], []).append(
                (parts[1], parts[2] if len(parts) > 2 else ""))
    return out


def snapshot(card):
    """what a run must give back"""
    chip = card.chip
    return {"mixer": [chip.mixer[r] for r in range(0x80) if r != 0x40],
            "pnp": dict(chip.pnp), "cfg": list(chip.cfg_ports),
            "dac": [list(e) for e in card.vga.dac]}


class Run:
    """the cases on a card; `phys` "direct" gives the window as real mode
    has it (the ports themselves), "past" past emm386dma, None none"""

    def __init__(self, card, opts=O_TINY, only=None, phys="direct",
                 groups=G_ALL, timeline=False):
        self.card = card
        self.lines, self.errors, self.kept, self.hooked = [], [], [], []
        # (us, frames each DAC had played) where they changed, for a
        # recording; (us, register, value) of the mixer's writes
        self.timeline = [] if timeline else None
        self.still = None
        self.mix_log, self.index = [], 0
        lib = library()
        card.isr = lambda c: lib.esp_irq()
        self.phys_mode = phys
        self.hal = HAL(INB(self._inb), OUTB(self._outb), NOW(self._now),
                       VOID(self._idle), MEM(self._poke), MEM(self._peek),
                       VOID(lambda: None), VOID(lambda: None),
                       HOOK(self._hook), MASK(lambda m: None),
                       ASK(lambda i, q, k: 13), TEXT(self._line),
                       TEXT(lambda s: None), ESC(lambda: 0), KEEP(self._keep),
                       PHYS(self._phys) if phys else PHYS())
        env = (ctypes.c_char_p * 2)(b"machine: the host tests", None)
        self.info = CARD(0x220, 1000000, AREA, 0, env)
        self.failed = lib.esp_run(ctypes.byref(self.hal),
                                  ctypes.byref(self.info), opts, groups,
                                  only.encode() if only else None)
        self.results = results(self.lines)

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
            self.mix_log.append((self.card.chip.clock.us, self.index, v))
        self.card.out(port, v)

    @_guard
    def _now(self):
        return self.card.chip.clock.us

    @_guard
    def _idle(self):
        self.card.wait(IDLE_US)
        if self.timeline is not None:
            chip = self.card.chip
            now, heard = chip.clock.us, (len(chip.played), len(chip.played2))
            if not self.timeline or heard != self.timeline[-1][1]:
                if self.still and self.still != self.timeline[-1]:
                    self.timeline.append(self.still)
                self.timeline.append((now, heard))
                self.still = None
            else:
                self.still = (now, heard)

    @_guard
    def _poke(self, addr, src, n):
        self.card.poke(addr, bytes(src[:n]))

    @_guard
    def _peek(self, addr, dst, n):
        for i, b in enumerate(self.card.peek(addr, n)):
            dst[i] = b

    @_guard
    def _hook(self, irq):
        self.hooked.append(irq)

    @_guard
    def _line(self, s):
        self.lines.append(s.decode("latin-1"))

    @_guard
    def _keep(self, data, n):
        self.kept.append(bytes(data[:n]))

    @_guard
    def _phys(self, io, n):
        past = self.phys_mode == "past"
        for i in range(n):
            if io[i].kind == 1:
                io[i].value = (self.card.phys_in(io[i].port) if past else
                               self.card.inp(io[i].port)) & 0xFF
            else:
                (self.card.phys_out if past else self.card.out)(
                    io[i].port, io[i].value)
        return 1

    def text(self, case):
        return " / ".join(d for _r, d in self.results.get(case, []))


@unittest.skipUnless(HAVE_UNICORN, "needs unicorn")
class CasesTest(unittest.TestCase):
    """the table: every spec parses, the ids are unique and grouped, and
    each says what the recording should hold"""

    def test_specs_parse(self):
        lib = library()
        for cid, spec, _e in cases():
            self.assertEqual(lib.esp_parse_check(spec.encode()), 0, cid)
        for bad in ("f=9x", "clk=mid", "fifo=more", "side=c", "zz",
                    "run=nothing", "pause=x", "order=dos", "b7=twice"):
            self.assertGreater(lib.esp_parse_check(bad.encode()), 0, bad)

    def test_ids(self):
        ids = [c[0] for c in cases()]
        self.assertEqual(len(ids), len(set(ids)))
        for cid, spec, expect in cases():
            self.assertIn(cid.split(".")[0], GROUPS, cid)
            self.assertIn(expect, EXPECTS, cid)
            self.assertLess(len(cid), 21, cid)

    def test_short_run(self):
        """the short run has cases of every group"""
        short = {c[0].split(".")[0] for c in cases()
                 if re.search(r"(^| )S( |$)", c[1])}
        self.assertEqual(short, set(GROUPS))


@unittest.skipUnless(HAVE_UNICORN, "needs unicorn")
class ModelTest(unittest.TestCase):
    """every case on the chip model, in real mode: the window is the
    ports themselves"""

    @classmethod
    def setUpClass(cls):
        cls.card = ChipCard()
        cls.before = snapshot(cls.card)
        cls.probe = Run(cls.card)

    def test_no_errors(self):
        self.assertEqual(self.probe.errors, [])
        self.assertEqual(self.probe.hooked, [5, 0])

    def test_every_case(self):
        ran = set(self.probe.results)
        for cid, _s, _e in cases():
            self.assertIn(cid, ran)
        fails = [(c, r) for c, rs in self.probe.results.items() for r in rs
                 if r[0] == "FAIL"]
        self.assertEqual(fails, [])
        self.assertEqual(self.probe.failed, 0)
        skips = sorted(c for c, rs in self.probe.results.items()
                       if any(r[0] == "SKIP" for r in rs))
        self.assertEqual(skips, [])

    def test_undefined_uses(self):
        """the chip sees only 78h bit 5, D0h and D4h in Extended mode and
        B9h 11b, which the cases look for, and the DMA runs the wrong way
        round only where h4.wrong asks for it"""
        self.assertEqual({w for _t, w in self.card.chip.violations},
                         {"78h bit 5, which the data sheet reserves",
                          "Compatibility command D0h in Extended mode",
                          "Compatibility command D4h in Extended mode",
                          "B9h 11b, at which the DMA runs ahead of the rate "
                          "(a1.b9_3)"})
        self.assertEqual({(ch, mode & 0x0C) for ch, mode in
                          self.card.chip.dmac.bad}, {(1, 0x04), (5, 0x04)})

    def test_given_back(self):
        """the mixer, the PnP registers, the interrupt mask and the VGA
        DAC as before, and the restore list empty at the end"""
        self.assertEqual(snapshot(self.card), self.before)
        self.assertEqual(self.probe.kept[-1], b"")
        self.assertGreater(max(len(k) for k in self.probe.kept), 100)

    def test_resets(self):
        self.assertIn("Compatibility mode; mixer kept; regs A1:6E>DC",
                      self.probe.text("rst.ext"))
        self.assertIn("mixer 14:99>88 36:77>88 38:66>00 3A:55>00 3E:33>00",
                      self.probe.text("rst.mixer"))
        self.assertIn("mixer kept; regs kept", self.probe.text("rst.fifo"))

    def test_real_mode_dma(self):
        """without a memory manager the 8237 has the programming at once,
        a program reads it, and the flip-flop is the 8237's"""
        for ch in "15":
            self.assertIn("it gets it at once", self.probe.text("h1.reach" + ch))
            self.assertIn("the program reads the 8237",
                          self.probe.text("h1.read" + ch))
            self.assertIn("a read toggles the 8237's flip-flop, a write "
                          "the 8237's", self.probe.text("h1.ff" + ch))
            self.assertIn("(the program's buffer)",
                          self.probe.text("h1.cross" + ch))
        self.assertIn("(the game's)", self.probe.text("h1.stale"))

    def test_machine(self):
        self.assertRegex(self.probe.text("h4.idle"),
                         r"the retrace every 142[6-9]\d us")
        for c in ("h4.a1", "h4.a2s", "h4.pace", "h4.both"):
            self.assertIn("0 DAC entries changed; 0 guard bytes changed",
                          self.probe.text(c), c)
        self.assertIn("wrote: FF FF FF FF", self.probe.text("h4.wrong1"))

    def test_streams(self):
        """the game's refills: its channel's count moves, but for the one
        carried over to Audio 1's, which stands"""
        for c in ("stream.a1", "stream.a1_8", "stream.a2"):
            self.assertRegex(self.probe.text(c), r"in 8 ticks, [1-9]\d* laps",
                             c)
        self.assertIn("mixed 0 frames in 8 ticks, 0 laps",
                      self.probe.text("stream.mirror"))

    def test_pauses(self):
        for c in ("a1.pause_b2", "a1.pause_b8", "a2.pause_f", "a2.pause_d"):
            self.assertRegex(self.probe.text(c), r", \d moved in the pause", c)


@unittest.skipUnless(HAVE_UNICORN, "needs unicorn")
class EMM386Test(unittest.TestCase):
    """the h1 cases with EMM386's DMA translation in front of the
    controllers, the window past it"""

    def h1(self, **kw):
        card = ChipCard(emm386=kw)
        return Run(card, only="h1", phys="past")

    def test_at_unmask(self):
        run = self.h1(at_unmask=True)
        self.assertEqual(run.errors, [])
        for ch in "15":
            self.assertIn("it gets it at the unmask",
                          run.text("h1.reach" + ch))
        self.assertIn("(stale)", run.text("h1.stale"))
        self.assertIn("(the game's)", run.text("h1.fresh"))

    def test_flip_flop(self):
        """a program's reads go to the 8237's flip-flop; its writes to
        EMM386's own where it keeps one"""
        own = self.h1(virtual_ff=True)
        self.assertIn("a read toggles the 8237's flip-flop, a write the "
                      "memory manager's own", own.text("h1.ff1"))
        self.assertIn("a read toggles the 8237's flip-flop, a write the "
                      "8237's", self.h1(virtual_ff=False).text("h1.ff5"))

    def test_without_window(self):
        card = ChipCard(emm386={})
        run = Run(card, only="h1.reach,h1.ff", phys=None)
        self.assertEqual({r for rs in run.results.values() for r, _d in rs},
                         {"SKIP"})


def gains(run, tau, reg):
    """a DAC's level (14h or 7Ch, four bits a side) at each time, as the run
    wrote it: 0 mutes, 0Ch is the tones' level, 2 dB a step"""
    out = np.ones((len(tau), 2))
    level, t_from = None, -float("inf")
    log = [(us, v) for us, r, v in run.mix_log if r == reg] + \
        [(float("inf"), None)]
    for us, v in log:
        if level is not None:
            sel = (tau >= t_from) & (tau < us)
            for c, n in enumerate((level >> 4, level & 0x0F)):
                out[sel, c] = 0.0 if n == 0 else 10 ** ((n - 12) / 10)
        level, t_from = v, us
    return out


def synthesize(run, rate=48000):
    """the recording of what the chip's DACs played, stereo at rate: each
    stretch of playing, where the timeline saw the counts rise less than 1
    ms apart, at even times, through the DAC's level"""
    chip = run.card.chip
    tl = run.timeline
    times = np.array([t for t, _ in tl], dtype=float)
    base = times[0] - 50000
    n = int((times[-1] - base) * rate / 1e6) + rate // 20
    out = np.zeros((n, 2))
    tau = base + np.arange(n) * 1e6 / rate
    for d, (frames, reg) in enumerate(((chip.played, 0x14),
                                       (chip.played2, 0x7C))):
        if not frames:
            continue
        arr = np.array([f if len(f) == 2 else (f[0], f[0]) for f in frames],
                       dtype=float) / 32768
        counts = np.array([c[d] for _, c in tl], dtype=float)
        rose = np.nonzero(np.diff(counts) > 0)[0] + 1
        ev_t, ev_n = times[rose], counts[rose]
        dac = np.zeros((n, 2))
        for seg in np.split(np.arange(len(rose)),
                            np.nonzero(np.diff(ev_t) > 1000)[0] + 1):
            if len(seg) < 2:
                continue
            t0, t1 = ev_t[seg[0]], ev_t[seg[-1]]
            n0, n1 = counts[rose[seg[0]] - 1], ev_n[seg[-1]]
            per = (t1 - t0) / (n1 - 1 - n0)
            a = int(np.ceil((t0 - base) * rate / 1e6))
            b = min(int((t1 + per - base) * rate / 1e6), n)
            pos = n0 + ((base + np.arange(a, b) * 1e6 / rate) - t0) / per
            k = np.clip(pos.astype(int), 0, len(arr) - 1)
            k2 = np.clip(k + 1, 0, min(int(n1) - 1, len(arr) - 1))
            frac = (pos - np.floor(pos))[:, None]
            dac[a:b] += arr[k] * (1 - frac) + arr[k2] * frac
        out += dac * gains(run, tau, reg)
    return out, base


# the recorded cases: one of each kind the recording should hold
RECORDED = ("a1.base,a1.left,a1.right,a1.pause_b2,a1.muted,a2.base,"
            "a2.slaved,both.base,both.muted2,stream.a1,stream.mirror,"
            "a2.wide_packed,fix.late100a,fix.a2pfd1,fix.pb2late,"
            "fix.b2late1,fix.pb7a")
# those whose Audio 1 stream plays a byte off, its DMA running when its
# start's first request came, as on the card (docs/ESS_EXTENDED.md); the
# names are prefixes, so fix.pb2late runs fix.pb2late2 and 3 too
BYTE_OFF = {"a1.base", "a1.left", "a1.right", "a1.pause_b2", "both.base",
            "both.muted2", "stream.a1", "fix.b2late1"}


@unittest.skipUnless(HAVE_UNICORN and np is not None,
                     "needs unicorn and numpy")
class RecordedTest(unittest.TestCase):
    """a recorded run of the cases of each kind, on the chip model"""

    RATE = 48000

    @classmethod
    def setUpClass(cls):
        cls.probe = Run(ChipCard(), opts=O_TINY | O_RECORD, only=RECORDED,
                        timeline=True)
        cls.tmp = tempfile.TemporaryDirectory()
        cls.report = os.path.join(cls.tmp.name, "ESSPROBE.TXT")
        with open(cls.report, "w") as f:
            f.write("\n".join(cls.probe.lines) + "\n")
        cls.frames, cls.base = synthesize(cls.probe, cls.RATE)
        cls.verdicts = cls.analyze(cls.frames)

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    @classmethod
    def analyze(cls, frames):
        """{case: (result, text)} of essprec on the recording"""
        import io
        import wave
        path = os.path.join(cls.tmp.name, "run.wav")
        data = np.clip(np.round(frames * 32767), -32768, 32767)
        with wave.open(path, "wb") as w:
            w.setnchannels(2)
            w.setsampwidth(2)
            w.setframerate(cls.RATE)
            w.writeframes(data.astype("<i2").tobytes())
        out = io.StringIO()
        essprec.analyze(cls.report, path, out)
        res = {}
        for line in out.getvalue().splitlines():
            parts = line.split(None, 2)
            if len(parts) == 3 and parts[1] in ("PASS", "FAIL", "INFO"):
                res[parts[0]] = (parts[1], parts[2])
        res["_text"] = out.getvalue()
        return res

    def window(self, case):
        """the samples of a case's sound in the recording"""
        _c, sounds, _m = essprec.read_report(self.report)
        on, off = sounds[case][2][0][0], sounds[case][2][-1][1]
        clock = essprec.fit_clock(_m, essprec.markers(essprec.band_limit(
            self.frames.mean(axis=1), self.RATE)))
        a, b = clock[:2]
        return (int((a * on / 1e6 + b) * self.RATE),
                int((a * off / 1e6 + b) * self.RATE))

    def test_every_sound(self):
        v = self.verdicts
        self.assertEqual(self.probe.errors, [])
        for case in RECORDED.split(","):
            self.assertIn(case, v, v["_text"])
        bad = {c for c, r in v.items() if c != "_text" and r[0] == "FAIL"}
        self.assertEqual(bad, BYTE_OFF, v["_text"])
        self.assertRegex(v["fix.b2late1"][1], r"clean -?\d dB")
        self.assertEqual(v["fix.pb2late"][0], "PASS")
        self.assertEqual(v["a2.wide_packed"][0], "INFO")
        self.assertIn("plays at 220", v["a2.slaved"][1])
        self.assertIn("by spec key", v["_text"])

    def broken(self, case, fn):
        x = self.frames.copy()
        a, b = self.window(case)
        fn(x, a, b)
        return self.analyze(x)[case]

    def test_a_flat_tone(self):
        def flat(x, a, b):
            x[a:b] = 0.2
        self.assertEqual(self.broken("a2.base", flat)[0], "FAIL")

    def test_another_rate(self):
        """the tone an octave up: the rate it played at is twice"""
        def octave(x, a, b):
            t = np.arange(b - a) / self.RATE
            x[a:b, 0] = x[a:b, 1] = 0.3 * np.sin(2 * np.pi * 882 * t)
        res, text = self.broken("a2.base", octave)
        self.assertEqual(res, "FAIL")
        self.assertRegex(text, r"plays at 88[12]\d\d Hz")

    def test_a_loop(self):
        """the glide's first 40 ms over and over"""
        def loop(x, a, b):
            lap = int(0.040 * self.RATE)
            for i in range(a + lap, b, lap):
                x[i:min(i + lap, b)] = x[a:a + min(lap, b - i)]
        self.assertEqual(self.verdicts["stream.a1_8"][0], "PASS")
        res, text = self.broken("stream.a1_8", loop)
        self.assertEqual(res, "FAIL", text)

    def test_the_loop_found(self):
        self.assertEqual(self.verdicts["stream.mirror"][0], "PASS")
        self.assertRegex(self.verdicts["stream.mirror"][1],
                         r"[2-9]\d* steps back|\d\d+ steps back")

    def test_a_dead_side(self):
        def dead(x, a, b):
            x[a:b, 0] = 0
        self.assertEqual(self.broken("fix.late100a", dead)[0], "FAIL")

    def test_a_sound_where_none(self):
        def tone(x, a, b):
            t = np.arange(b - a) / self.RATE
            x[a:b, 0] += 0.3 * np.sin(2 * np.pi * 441 * t)
        self.assertEqual(self.broken("a1.muted", tone)[0], "FAIL")

    def test_no_pause(self):
        def fill(x, a, b):
            t = np.arange(b - a) / self.RATE
            x[a:b, 0] = x[a:b, 1] = 0.3 * np.sin(2 * np.pi * 441 * t)
        self.assertEqual(self.broken("fix.pb7a", fill)[0], "FAIL")

    def test_sides_swapped_after_a_pause(self):
        """a stream two bytes off after the pause: the tone on the right"""
        _c, sounds, _m = essprec.read_report(self.report)
        a, b = self.window("fix.a2pfd1")
        resume = sounds["fix.a2pfd1"][2][1][0]
        first = sounds["fix.a2pfd1"][2][0][0]
        cut = a + int((resume - first) * self.RATE / 1e6)

        def swap(x, a, b):
            x[cut:b] = x[cut:b, ::-1].copy()
        res, text = self.broken("fix.a2pfd1", swap)
        self.assertEqual(res, "FAIL", text)
        self.assertEqual(self.verdicts["fix.a2pfd1"][0], "PASS")
        self.assertIn("left", self.verdicts["fix.late100a"][1])


class _Machine:
    """what EmmCpu takes of tsremu's machine"""

    def __init__(self, chip):
        self.chip = chip
        self.memory = ctypes.create_string_buffer(0x100000)
        self.sharers = []


class _Ports:
    """a chip that logs the ports and answers each with its low byte"""

    def __init__(self):
        self.log = []

    def inb(self, port):
        self.log.append(("in", port))
        return port & 0xFF

    def outb(self, port, v):
        self.log.append(("out", port, v))


@unittest.skipUnless(HAVE_UNICORN and have_nasm(), "needs nasm and unicorn")
class WindowTest(unittest.TestCase):
    """src/essp0.asm as EMM386 calls a trap handler: a 16-bit far call at
    ring 0 with selectors as long as the block"""

    SEG = 0x3000

    def setUp(self):
        import build_essp0
        self.data, self.syms = build_essp0.build()
        self.ports = _Ports()
        self.m = _Machine(self.ports)
        base = self.SEG * 16
        self.m.memory[base:base + len(self.data)] = self.data
        self.trap = {"ds": self.SEG, "size": len(self.data)}
        self.cpu = EmmCpu(self.m, self.trap)

    def poke(self, off, data):
        base = self.SEG * 16 + off
        self.m.memory[base:base + len(data)] = bytes(data)

    def peek(self, off, n):
        base = self.SEG * 16 + off
        return bytes(self.m.memory[base:base + n])

    def call(self):
        return self.cpu.call(self.syms["handler"], self.syms["table"],
                             0x22D, None, 0)

    def test_header_current(self):
        import build_essp0
        text = build_essp0.header(self.data, self.syms)
        with open(os.path.join(SRC, "essp0.h"), "rb") as f:
            self.assertEqual(f.read(), text.replace("\n", "\r\n").encode())

    def test_passes_unarmed(self):
        al, cf = self.call()
        self.assertTrue(cf)
        self.assertEqual(self.ports.log, [])

    def test_runs_the_list(self):
        req = self.syms["req"]
        self.poke(self.syms["armed"], [self.syms["ESSP0_ARMED"]])
        self.poke(req, struct.pack("<BHB", 1, 0xC6, 0) +
                  struct.pack("<BHB", 2, 0xD8, 0x5A) +
                  struct.pack("<BHB", 1, 0x08, 0) + b"\0")
        al, cf = self.call()
        self.assertFalse(cf)
        self.assertEqual(al, self.syms["ESSP0_DONE"])
        self.assertEqual(self.ports.log, [("in", 0xC6), ("out", 0xD8, 0x5A),
                                          ("in", 0x08)])
        self.assertEqual(self.peek(self.syms["count"], 1), b"\x03")
        self.assertEqual(self.peek(req + 3, 1), b"\xC6")
        self.assertEqual(self.peek(req + 11, 1), b"\x08")

    def test_full_list(self):
        n = self.syms["ESSP0_NREQ"]
        self.poke(self.syms["armed"], [self.syms["ESSP0_ARMED"]])
        self.poke(self.syms["req"], struct.pack("<BHB", 1, 0x81, 0) * n)
        al, cf = self.call()
        self.assertFalse(cf)
        self.assertEqual(len(self.ports.log), n)
        self.assertEqual(self.peek(self.syms["count"], 1), bytes([n]))

    def test_within_its_selectors(self):
        """the handler and the table are inside the block EMM386 maps"""
        self.assertLess(self.syms["handler"], len(self.data))
        self.assertLessEqual(self.syms["table"] + 4, len(self.data))
        self.assertEqual(struct.unpack_from("<H", self.data,
                                            self.syms["table"] + 2)[0],
                         self.syms["handler"])
        with self.assertRaises(EmuError):     # beyond the limit: a fault
            small = EmmCpu(_Machine(_Ports()),
                           {"ds": self.SEG, "size": self.syms["handler"]})
            small.call(self.syms["handler"], self.syms["table"], 0x22D,
                       None, 0)


@unittest.skipUnless(HAVE_UNICORN and os.path.exists(EXE),
                     "needs unicorn and build/essprobe.exe")
class ExeTest(unittest.TestCase):
    """build/essprobe.exe in the DOS emulator"""

    def exe(self):
        with open(EXE, "rb") as f:
            return f.read()

    def dos(self, card, tail, env="BLASTER=A220 I5 D1 H5 P330 T6", **kw):
        from dosrun import Dos
        d = Dos(self.exe(), card, env=env, **kw)
        code = d.run(tail)
        return d, code

    def report(self, d):
        return bytes(d.files.get("ESSPROBE.TXT", b"")).decode(
            "latin-1").splitlines()

    def test_silent_run(self):
        """/A /T: the cases without a sound, the machine's lines, every
        register given back and the restore file gone"""
        card = ChipCard()
        before = snapshot(card)
        d, code = self.dos(card, "/A /T")
        self.assertEqual(code, 0, d.output)
        lines = self.report(d)
        res = results(lines)
        self.assertIn("h1.reach5", res)
        self.assertNotIn("a1.base", res)
        self.assertTrue(any("dos: " in t for _r, t in res["env.machine"]))
        self.assertTrue(any("pci: none" in t for _r, t in
                            res["env.machine"]))
        self.assertIn("pass=0 fail=0", "\n".join(lines))
        self.assertEqual(snapshot(card), before)
        self.assertNotIn("ESSPROBE.RST", d.files)
        self.assertIn("Restart before you load esssb16", d.output)

    def test_under_emm386(self):
        """/A /T under EMM386: V86 mode, EMMQXXX0's version, the window on
        Audio_Base+Dh, the 8237 seen past EMM386's translation, which
        programs it at the unmask, and the trap removed at the end"""
        card = ChipCard(emm386={})
        before = snapshot(card)
        d, code = self.dos(card, "/A /T /C:env.machine,h1", emm386=(4, 95),
                           noems=True)
        self.assertEqual(code, 0, d.output)
        res = results(self.report(d))
        machine = "\n".join(t for _r, t in res["env.machine"])
        self.assertIn("V86 mode", machine)
        self.assertIn("EMMXXXX0 4.95", machine)
        self.assertIn("window: Audio_Base+Dh trapped", machine)
        for ch in (1, 5):
            reach = res["h1.reach%d" % ch][0][1]
            self.assertIn("it gets it at the unmask", reach)
        self.assertEqual([b for b, _si in d.emm_calls], [0, 1])
        self.assertEqual(d.emm_traps, [])
        self.assertEqual(snapshot(card), before)

    def test_window_refused(self):
        """EMM386 refuses the trap: the 8237 as a program sees it"""
        from dosrun import Dos
        card = ChipCard(emm386={})
        d = Dos(self.exe(), card, emm386=(4, 95))
        d.emm_fail.add(0)
        code = d.run("/A /T /C:env.machine,h1.reach5")
        self.assertEqual(code, 0, d.output)
        res = results(self.report(d))
        self.assertIn("window: refused",
                      "\n".join(t for _r, t in res["env.machine"]))
        self.assertEqual(res["h1.reach5"][0][0], "SKIP")

    def test_window_left(self):
        """a run that didn't end left its window, DOS's block with its trap
        still in: the next start removes that trap and frees the block
        before it puts its own in"""
        import build_essp0
        from dosrun import Dos
        data, syms = build_essp0.build()
        d = Dos(self.exe(), ChipCard(emm386={}), emm386=(4, 95))
        old, paras = 0x3800, (len(data) + 15) >> 4
        # in the free block after the program's: DOS's, and a free rest
        d.mcb(old - 1, b"M", 0x0008, paras)
        d.mcb(old + paras, b"M", 0, 0x3FFF - (old + paras) - 1)
        image = bytearray(data)
        struct.pack_into("<H", image, syms["handle"], 7)
        image[syms["armed"]] = syms["ESSP0_ARMED"]
        d.put(old * 16, bytes(image))
        d.emm_traps.append({"first": 0x22D, "last": 0x22D, "count": 1,
                            "ds": old, "si": syms["table"],
                            "size": len(data), "handle": 7})
        code = d.run("/A /T /C:env.machine")
        self.assertEqual(code, 0, d.output)
        self.assertEqual(d.emm_calls[:2], [(1, 7), (0, syms["table"])])
        self.assertEqual(d.emm_traps, [])
        self.assertEqual(d.mem(old * 16 + syms["armed"], 1), b"\0")

    def test_no_chip(self):
        d, code = self.dos(ChipCard(), "/A /T", env="BLASTER=A240 I5 D1")
        self.assertEqual(code, 2)
        self.assertIn("no ES1869 answers at 240h", d.output)

    def test_windows(self):
        d, code = self.dos(ChipCard(), "/A /T",
                           vxd={"adi": bytes(0xE9), "info": None})
        self.assertEqual(code, 3)
        self.assertIn("needs real DOS", d.output)

    def test_esssb16_resident(self):
        d, code = self.dos(TSRCard("qpi"), "/A /T")
        self.assertEqual(code, 4)
        self.assertIn("esssb16 is resident", d.output)

    def test_bad_option(self):
        d, code = self.dos(ChipCard(), "/Q")
        self.assertEqual(code, 1)
        self.assertIn("usage: essprobe", d.output)

    def test_restore_file(self):
        """a run that didn't finish left ESSPROBE.RST: the next start puts
        its registers back and removes it"""
        from dosrun import Dos
        card = ChipCard()
        card.chip.mixer[0x14] = 0x00
        rst = b"ESPRST1\0" + struct.pack("<HH", 0x220, 3) + b"M\x14\xEE"
        d = Dos(self.exe(), card)
        d.files["ESSPROBE.RST"] = bytearray(rst)
        code = d.run("/A /T /C:env.machine")
        self.assertEqual(code, 0, d.output)
        self.assertEqual(card.chip.mixer[0x14], 0xEE)
        self.assertNotIn("ESSPROBE.RST", d.files)
        self.assertTrue(any("restored the chip" in line
                            for line in self.report(d)))


if __name__ == "__main__":
    unittest.main()
