# (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
#
# Licensed under GPL Version 3.0
"""esssb16.com in a CPU emulator: the installer under QEMM, QPIEMU, EMM386
and a DPMI host, its refusals and what it says about them, its options,
and the side of the front that a game doesn't see: the chip's Extended
mode registers behind each transfer with the rates ES1869.DRV would pick,
the game's 16-bit DMA channel carried over to the card's, the chip's
Compatibility mode for the firmware's commands, the chip's mixer behind
the Sound Blaster 16's view, the ports the front passes on, and the
front's stack. What the game sees is tests/test_sb16conf.py's, against
the Sound Blaster 16 model. Every resident test runs after DOS has reused
the memory the TSR gave back.

usage: python3 -m unittest tests.test_esssb16   (needs nasm and unicorn)
"""

import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))

from test_vxdext import HAVE_UNICORN, have_nasm  # noqa: E402
from sb16 import rate_to_v, tc_to_v  # noqa: E402
from sbgame import (reset, write, read, read_all, mixer,  # noqa: E402
                    program_dma)

import build_esssb16  # noqa: E402

if HAVE_UNICORN:
    from tsremu import (Machine, PMMachine, Chip, DeadChip,  # noqa: E402
                        PSP_PARA, ENV_PARA, STUB_PARA, OLD_OFF, STUB32,
                        VENDOR_OFF, SEL_CODE16, SEL_DATA, SEL_CODE32, POISON)
    from sbfronts import TSRCard  # noqa: E402

E_USAGE, E_NOCARD, E_NOTRAP, E_WINDOWS, E_LOADED = 1, 2, 3, 4, 5
F_ESS, F_EXT, F_AUTO, F_RUN, F_16 = 0x01, 0x02, 0x04, 0x08, 0x10
IO_DIRECT, IO_HDPMI, IO_QPI, IO_CHAIN = 0, 1, 2, 3
BASE = 0x220
SB_PORTS = {BASE + o for o in (4, 5, 6, 0xA, 0xC, 0xE, 0xF)}
DMA5_PORTS = {0xC4, 0xC6, 0x8B, 0xD4, 0xD6, 0xD8, 0xDA, 0xDC, 0xDE}
OLD_PARAS = 248         # the resident part before the Sound Blaster 16 model
# the 8237's mode register: demand or single transfers, auto-initialize,
# from memory (playback) or into it (recording)
DEMAND, SINGLE, AUTO, PLAY, RECORD = 0x00, 0x40, 0x10, 0x08, 0x04
_BUILD = {}


def build():
    if "image" not in _BUILD:
        _BUILD["image"], _BUILD["syms"] = build_esssb16.build()
    return _BUILD["image"], _BUILD["syms"]


def paragraphs(syms):
    return (syms["resident_end"] + 15) >> 4


HOST = 0x20             # HDPMI's data for a client, in paragraphs


def machine(chip=None, env="BLASTER=A220 I5 D1 H5 P330 T6"):
    image, syms = build()
    return Machine(image, syms, chip, env)


def installed(**kw):
    """a machine with the program resident, under QPIEMU unless told"""
    m = machine(kw.pop("chip", None), kw.pop("env", "BLASTER=A220 I5 D1 H5"))
    if not kw:
        kw = {"qpiemu": True}
    m.install(**kw)
    if m.exit[0] != "tsr":
        raise AssertionError("not resident: %r\n%s" % (m.exit, m.output))
    m.chip.log, m.chip.commands, m.chip.dma_log, m.chained = [], [], [], []
    return m


def card(path="qpi", **kw):
    """a game on the resident program and the chip model, the chip's logs
    empty"""
    c = TSRCard(path, **kw)
    c.chip.commands, c.chip.log, c.chip.dma_log = [], [], []
    c.chip.resets = 0           # the installer's own reset of the chip
    return c


def written(chip):
    """the chip's registers A0h-BFh as the last commands wrote them"""
    return {c[0]: c[1] for c in chip.commands
            if 0xA0 <= c[0] <= 0xBF and len(c) == 2}


def poison_stack(m):
    """the front's stack filled with 5Ah, to see how deep it goes"""
    lo, top = m.addr("stack"), m.addr("stack_top")
    m.put(lo, b"\x5A" * (top - lo))


def stack_depth(m):
    """the bytes of the front's stack used since poison_stack()"""
    lo, top = m.addr("stack"), m.addr("stack_top")
    return len(m.mem(lo, top - lo).lstrip(b"\x5A"))


def vchan(m):
    """the game's 16-bit channel as each path traps it, FFh for none"""
    return list(m.mem(m.addr("vchan"), 4))


# A1h and A2h for the rate generator's value v (v * 44100 / 248 Hz), as
# ES1869.DRV picks them for a rate: the 397.7 kHz clock up to 22050 Hz, the
# 795.5 kHz one above, and the filter's divider from ESS's tables for the
# fast clock's divisors 18 to 35, else 7.16 MHz over 35.9 times the rate
FILTER_STEREO = (3, 5, 5, 5, 6, 6, 6, 7, 7, 7, 7, 8, 8, 8, 8, 9, 9, 9)
FILTER_MONO = (3, 5, 5, 6, 6, 7, 7, 7, 7, 8, 8, 8, 8, 9, 9, 9, 10, 10)


def half_up(x):
    return int(x + 0.5)


def clocks(v, stereo):
    rate = v * 44100 / 248
    if v <= 124:
        d = half_up(397700 / rate)
        a1 = 128 - d
    else:
        d = half_up(795500 / rate)
        a1 = 256 - d
    if v > 124 and 18 <= d <= 35:
        n = (FILTER_STEREO if stereo else FILTER_MONO)[d - 18]
    else:
        n = half_up(1122.5 / v)
    return a1, (256 - n) & 0xFF


class NoESS(Chip):
    """a Sound Blaster Pro: no answer to E7h"""

    def _dsp(self, v):
        if v == 0xE7:
            return
        super()._dsp(v)


class NoConfig(Chip):
    """an ES1869 whose identification gives no config port"""

    def __init__(self):
        super().__init__()
        self.id_cfg = 0


# the levels a DOS mixer program left: master 3Ah, muted on the right,
# voice ABh, FM CDh, CD 12h, line 34h, mic 50h, PC speaker 6, the record
# source line; and the chip's own registers a mixer reset would clear
DOS_MIXER = {0x60: 0x3A, 0x62: 0x7A, 0x14: 0xAB, 0x36: 0xCD, 0x38: 0x12,
             0x3E: 0x34, 0x1A: 0x55, 0x3C: 0x06, 0x1C: 0x16, 0x50: 0x0C,
             0x52: 0x2A, 0x71: 0x0E, 0x7C: 0x99, 0x0E: 0x22, 0x00: 0x77}


def mixer_chip(levels=None):
    """an ES1869 whose mixer has DOS_MIXER, or `levels`"""
    chip = Chip()
    for reg, v in (DOS_MIXER if levels is None else levels).items():
        chip.mixer[reg] = v
    return chip


@unittest.skipUnless(have_nasm(), "needs nasm")
class BuildTest(unittest.TestCase):
    def test_build_is_current(self):
        with open(os.path.join(ROOT, "build", "esssb16.com"), "rb") as f:
            self.assertEqual(f.read(), build()[0],
                             "build/esssb16.com is out of date: run "
                             "tools/build_esssb16.py")

    def test_small(self):
        """the resident part, with the PSP, is no larger than before the
        front answered as the Sound Blaster 16 model does"""
        image, syms = build()
        self.assertLess(len(image), 8192)
        self.assertLessEqual(paragraphs(syms), OLD_PARAS)
        self.assertEqual(syms["RESIDENT_PARAS"], paragraphs(syms))
        self.assertLess(syms["resident_end"], syms["install"] + 1)

    def test_host_area(self):
        """the DPMI host's data go right after the resident part, over the
        routines that run before the switch"""
        _image, syms = build()
        self.assertEqual(syms["host_area"], paragraphs(syms) * 16)
        self.assertEqual(syms["HOST_PARAS"], HOST)
        self.assertGreaterEqual(syms["host_code_end"] - syms["host_area"],
                                HOST * 16)
        for name in ("install", "print", "trap_dpmi", "hdpmi_ranges",
                     "dma_ranges", "dma_trapped", "rmcs", "msg_nohdpmi",
                     "msg_hdpmi", "hdpmi_name"):
            self.assertGreaterEqual(syms[name], syms["host_code_end"], name)

    def test_rate_constants(self):
        """the front's constants divide as the clocks do for every v"""
        for v in range(1, 256):
            rate = v * 44100 / 248
            self.assertEqual(((4472 // v) + 1) >> 1, half_up(397700 / rate),
                             v)
            self.assertEqual(((8947 // v) + 1) >> 1, half_up(795500 / rate),
                             v)
            self.assertEqual(((2245 // v) + 1) >> 1, half_up(1122.5 / v), v)
        for tc in range(256):           # 40h's, without the ROM's table
            if tc not in (0x68, 0x6C, 0xC8):
                self.assertEqual(5623 // (256 - min(tc, 0xEA)), tc_to_v(tc))


@unittest.skipUnless(HAVE_UNICORN and have_nasm(), "needs nasm and unicorn")
class InstallTest(unittest.TestCase):
    def test_qpiemu(self):
        m = machine()
        m.install(qpiemu=True)
        self.assertEqual(m.exit, ("tsr", paragraphs(m.syms)))
        self.assertEqual(m.data8("signature"), ord("E"))
        self.assertEqual(m.mem(m.addr("install"), 1)[0], POISON)
        self.assertIn("ES1869 at 220h, IRQ 5, DMA 1, the game's DMA 5 "
                      "through it", m.output)
        self.assertIn("set BLASTER=A220 I5 D1 H5 P330 T6", m.output)
        self.assertIn("traps: QPI for real-mode games; protected-mode games "
                      "play 8-bit without HDPMI32i", m.output)
        fns = [c[0] for c in m.qpi_calls]
        self.assertEqual(fns, [0x1A06, 0x1A07] + [0x1A09] * 16)
        self.assertEqual(m.qpi_callback, (PSP_PARA, m.sym("qpi_shim")))
        self.assertEqual(m.qpi_trapped, SB_PORTS | DMA5_PORTS)
        self.assertEqual(vchan(m), [0xFF, 0xFF, 5, 5])
        # a callback before ours: the ports that aren't the front's go there
        self.assertEqual(m.mem(m.addr("qpi_old"), 4),
                         bytes([OLD_OFF, 0, STUB_PARA & 0xFF, STUB_PARA >> 8]))
        self.assertEqual(m.mem(PSP_PARA * 16 + m.sym("qpi_how"), 1)[0],
                         IO_CHAIN)
        self.assertEqual(m.freed, [ENV_PARA])
        self.assertEqual(m.umb_links, [1, 0])
        # the card is read, not set up: its channels and mixer stay
        self.assertEqual(m.chip.pnp[(1, 0x75)], 4)
        self.assertEqual((m.chip.mixer[0x71], m.chip.mixer[0x7C]), (0, 0))
        self.assertEqual((m.data16("base"), m.data16("cfg"), m.data8("mix80"),
                          m.data8("mix81"), m.data8("dma8"),
                          m.data8("dma16")),
                         (0x220, 0x250, 0xF2, 0x02, 1, 5))
        self.assertEqual(m.chip.commands, [(0xE7,)])
        self.assertEqual(m.chip.dma_log, [])

    def test_qpi_without_callback(self):
        m = machine()
        m.install(qpiemu=True, old_callback=False)
        self.assertEqual(m.exit[0], "tsr")
        self.assertEqual(m.mem(PSP_PARA * 16 + m.sym("qpi_how"), 1)[0],
                         IO_QPI)

    def test_qpi_dma_traps_all_or_none(self):
        """QPI refuses one of the 16-bit channel's ports: the others are
        given back, and its transfers read the channel back instead"""
        m = machine()
        m.qpi_fail_after = {0x1A09: 9}
        m.install(qpiemu=True)
        self.assertEqual(m.exit[0], "tsr")
        self.assertEqual(m.qpi_trapped, SB_PORTS)
        self.assertEqual([c[1] for c in m.qpi_calls if c[0] == 0x1A0A],
                         [0xC4, 0xC6, 0x8B, 0xD4, 0xD6, 0xD8, 0xDA, 0xDC,
                          0xDE])
        self.assertEqual(vchan(m), [0xFF] * 4)
        self.assertIn("traps: QPI for real-mode games", m.output)

    def test_qemm(self):
        m = machine()
        m.install(qemm=True)
        self.assertEqual(m.exit[0], "tsr")
        self.assertEqual(m.qpi_calls[0][0], 0x0300)
        self.assertIn("traps: QPI", m.output)

    def test_qemm_too_old(self):
        m = machine()
        m.qpi_version = 0x0702
        m.install(qemm=True)
        self.assertEqual(m.exit, ("exit", E_NOTRAP))
        self.assertIn("QEMM's QPI 7.02 is older than 7.03", m.output)
        self.assertIn("no EMM386: no EMMXXXX0 or EMMQXXX0 device", m.output)
        self.assertIn("no port traps", m.output)

    def test_qpi_failure_falls_back(self):
        m = machine()
        m.qpi_fail = {0x1A09}
        m.install(qpiemu=True, emm386=(4, 49))
        self.assertEqual(m.exit[0], "tsr")
        self.assertIn("QPI refused the port traps", m.output)
        self.assertIn("traps: EMM386 4.49 for real-mode games", m.output)

    def test_emm386(self):
        m = machine()
        m.install(emm386=(4, 49))
        self.assertEqual(m.exit[0], "tsr")
        self.assertIn("traps: EMM386 4.49 for real-mode games", m.output)
        shim, skip = m.sym("emm_shim"), m.sym("emm_pass")
        self.assertEqual(len(m.emm_traps), 1)
        call = m.emm_traps[0]
        self.assertEqual((call["first"], call["last"], call["count"]),
                         (BASE + 4, BASE + 0xF, 12))
        self.assertEqual(call["table"],
                         [(BASE + o, shim if o in (4, 5, 6, 0xA, 0xC, 0xE, 0xF)
                           else skip) for o in range(4, 0x10)])
        # EMM386 searches the table where it is: in the resident part, in
        # the size its selectors get
        self.assertEqual((call["ds"], call["si"]),
                         (PSP_PARA, m.sym("emm_table")))
        self.assertEqual(call["size"], paragraphs(m.syms) * 16)
        self.assertLessEqual(call["si"] + 4 * 12, m.sym("stack"))
        self.assertEqual(vchan(m), [0xFF] * 4)      # no DMA traps

    def test_emm386_noems(self):
        """Windows 98's EMM386 with NOEMS: the device is EMMQXXX0"""
        m = machine()
        m.install(emm386=(4, 95), noems=True)
        self.assertEqual(m.exit[0], "tsr")
        self.assertIn("traps: EMM386 4.95 for real-mode games", m.output)

    def test_emm386_refuses(self):
        m = machine()
        m.emm_fail = {0}
        m.install(emm386=(4, 95), noems=True)
        self.assertEqual(m.exit, ("exit", E_NOTRAP))
        self.assertIn("EMM386 4.95 refused the port traps", m.output)
        self.assertIn("no port traps", m.output)

    def test_emm386_too_old(self):
        for version, text in (((4, 45), "4.45"), ((4, 5), "4.05"),
                              ((3, 49), "3.49")):
            m = machine()
            m.install(emm386=version)
            self.assertEqual(m.exit, ("exit", E_NOTRAP))
            self.assertIn("EMM386 %s is older than 4.46" % text, m.output)
            self.assertEqual(m.emm_traps, [])

    def test_no_memory_manager(self):
        m = machine()
        m.install()
        self.assertEqual(m.exit, ("exit", E_NOTRAP))
        self.assertIn("no EMM386: no EMMXXXX0 or EMMQXXX0 device", m.output)
        self.assertIn("no port traps", m.output)

    def test_windows(self):
        m = machine()
        m.install(qpiemu=True, windows=4)
        self.assertEqual(m.exit, ("exit", E_WINDOWS))
        self.assertIn("ES1869.VXD", m.output)

    def test_loaded(self):
        m = machine()
        m.install(qpiemu=True, loaded=True)
        self.assertEqual(m.exit, ("exit", E_LOADED))
        self.assertEqual(m.umb_links, [1, 0])

    def test_no_card(self):
        for chip in (DeadChip(), NoESS()):
            m = machine(chip)
            m.install(qpiemu=True)
            self.assertEqual(m.exit, ("exit", E_NOCARD))
            self.assertIn("no ES1869", m.output)
            self.assertEqual(m.qpi_calls, [])

    def test_usage(self):
        for tail in ("/X", "/?", "-h9", "/H4", "/A", "/F", "/F2", "/O",
                     "/o5"):
            m = machine()
            m.install(tail=tail, qpiemu=True)
            self.assertEqual(m.exit, ("exit", E_USAGE), tail)
            self.assertIn("usage: esssb16", m.output)
            self.assertIn("/F0|/F1", m.output)

    def test_options(self):
        m = machine(Chip(base=0x240))
        m.install(tail=" /a240 -H6 /S", qpiemu=True)
        self.assertEqual(m.exit[0], "tsr")
        self.assertIn("ES1869 at 240h, IRQ 5, DMA 1, the game's DMA 6 "
                      "through it", m.output)
        self.assertIn("set BLASTER=A240 I5 D1 H6", m.output)
        self.assertEqual(m.data8("b9"), 0)          # single transfers
        self.assertEqual(m.qpi_trapped, {0x240 + o for o in
                                         (4, 5, 6, 0xA, 0xC, 0xE, 0xF)}
                         | {0xC8, 0xCA, 0x89} | (DMA5_PORTS - {0xC4, 0xC6,
                                                               0x8B}))

    def test_dac_options(self):
        """/F and /O: mixer 71h's filters (bits 3 and 2 set bypass them)
        and Audio 2's 4x oversampling (bit 4), at the start and at every
        transfer; without them as they are. Bit 5 is always cleared"""
        for tail, keep, put, before, after in (
                ("", 0xDF, 0x00, 0x3F, 0x1F),
                ("/F0", 0xD3, 0x0C, 0x30, 0x1C),
                ("/F1 /O1", 0xC3, 0x10, 0x2C, 0x10),
                ("-f0 -o0", 0xC3, 0x0C, 0x3F, 0x0F)):
            chip = Chip()
            chip.mixer[0x71] = before
            m = machine(chip)
            m.install(tail=tail, qpiemu=True)
            self.assertEqual(m.exit[0], "tsr", tail)
            self.assertEqual((m.data8("keep71"), m.data8("set71")),
                             (keep, put), tail)
            self.assertEqual(m.chip.mixer[0x71], after, tail)

    def test_blaster(self):
        """BLASTER's A and H; I and D come from the card's PnP registers"""
        m = machine(Chip(base=0x240, irq=7, dma8=3),
                    env="PATH=C:\\DOS;C:\\GAMES\0BLASTER=A240 I10 D0 H7 T6")
        m.install(qpiemu=True)
        self.assertEqual(m.exit[0], "tsr")
        self.assertIn("ES1869 at 240h, IRQ 7, DMA 3, the game's DMA 7 "
                      "through it", m.output)
        self.assertIn("set BLASTER=A240 I7 D3 H7", m.output)
        self.assertEqual((m.data8("mix80"), m.data8("mix81")), (0xF4, 0x08))

    def test_card_registers(self):
        """mixer 80h and 81h's 8-bit channel, from the card's IRQ and DMA"""
        for irq, dma8, h, m80, m81 in ((7, 3, "/H6", 0xF4, 0x08),
                                       (10, 0, "/H7", 0xF8, 0x01),
                                       (9, 1, "/H5", 0xF1, 0x02)):
            m = machine(Chip(irq=irq, dma8=dma8))
            m.install(tail=h, qpiemu=True)
            self.assertEqual((m.data8("mix80"), m.data8("mix81")),
                             (m80, m81), irq)

    def test_no_blaster(self):
        m = machine(env="PATH=C:\\")
        m.install(qpiemu=True)
        self.assertEqual(m.exit[0], "tsr")
        self.assertIn("ES1869 at 220h, IRQ 5, DMA 1, the game's DMA 5",
                      m.output)

    def test_no_config_port(self):
        m = machine(NoConfig(), env="BLASTER=A220 I7 D3 H6")
        m.install(qpiemu=True)
        self.assertEqual(m.exit[0], "tsr")
        self.assertEqual(m.data16("cfg"), 0)
        self.assertIn("ES1869 at 220h, IRQ 7, DMA 3, the game's DMA 6",
                      m.output)
        cfg = m.chip.cfg
        self.assertEqual([e for e in m.chip.log if e[1] in (cfg, cfg + 1)], [])

    def test_no_16bit_channel(self):
        """/H0, or BLASTER's H equal to its D: 16-bit sound on the 8-bit
        channel, as a Sound Blaster 16 plays it then, and no DMA traps"""
        for tail, env in (("/H0", "BLASTER=A220 I5 D1 H5"),
                          ("", "BLASTER=A220 I5 D1 H1 T6")):
            m = machine(env=env)
            m.install(tail=tail, qpiemu=True)
            self.assertEqual(m.exit[0], "tsr")
            self.assertEqual(m.data8("dma16"), 0)
            self.assertIn("IRQ 5, DMA 1\r\n", m.output)
            self.assertIn("set BLASTER=A220 I5 D1 H1 P330 T6", m.output)
            self.assertEqual(m.qpi_trapped, SB_PORTS)
            self.assertEqual(vchan(m), [0xFF] * 4)

    def test_dpmi(self):
        """HDPMI: the switch with the host's data after the resident part,
        16-bit code on the host's first selectors, HDPMI32i's traps for
        them and the 16-bit channel's ports, the TSR through a real-mode
        int 21h that keeps the host's data; then a protected-mode game's
        accesses through the handler as the installer registered it"""
        m = machine()
        m.install(qpiemu=True, dpmi=True)
        self.assertEqual(m.stopped, "dpmi")
        self.assertEqual((m.resized, m.allocated), ([], []))
        self.assertEqual(m.dpmi_call[:2], (1, PSP_PARA + paragraphs(m.syms)))
        pm = PMMachine(m)
        rmcs = pm.install(m)
        self.assertEqual([c[0] for c in pm.dpmi_calls], [0x0300])
        traps = [(r["esi"], r["edi"], r["ecx"] & 0xFFFF, r["edx"],
                  r["ebx"] & 0xFFFF)
                 for fn, r in pm.vendor_calls if fn == 6]
        self.assertEqual(traps, [(p, n, SEL_CODE16, m.sym("hdpmi_shim"),
                                  SEL_DATA)
                                 for p, n in ((BASE + 4, 3), (BASE + 0xA, 1),
                                              (BASE + 0xC, 1),
                                              (BASE + 0xE, 2), (0xC4, 3),
                                              (0x8B, 1), (0xD4, 11))])
        self.assertIn("HDPMI32i for protected-mode games", pm.output)
        self.assertEqual(pm.data16("pm_data"), SEL_DATA)
        self.assertEqual(pm.mem(pm.addr("vendor"), 6),
                         (STUB32 + VENDOR_OFF).to_bytes(4, "little") +
                         SEL_CODE32.to_bytes(2, "little"))
        self.assertEqual(vchan(m), [0xFF, 5, 5, 5])
        eax = int.from_bytes(rmcs[0x1C:0x20], "little")
        edx = int.from_bytes(rmcs[0x14:0x18], "little")
        self.assertEqual((eax, edx), (0x3100, paragraphs(m.syms) + HOST))
        self.assertEqual(rmcs[0x2E:0x32], b"\0\0\0\0")      # SS:SP: the host's
        self.assertEqual(m.kept, paragraphs(m.syms) + HOST)
        # a protected-mode game, through what the installer registered
        m.chip.commands = []
        pm.hdpmi_io(BASE + 0xC, 0xE1)
        self.assertEqual((pm.hdpmi_io(BASE + 0xA), pm.hdpmi_io(BASE + 0xA)),
                         (4, 5))
        self.assertEqual(m.chip.commands, [])
        # and a real-mode game through QPI, on the same memory
        m.dsp(0xE1)
        self.assertEqual((m.dsp_read(), m.dsp_read()), (4, 5))

    def test_dpmi_without_hdpmi(self):
        """a host with HDPMI's logo that doesn't answer 168Ah: no traps, and
        the TSR still keeps the host's data"""
        m = machine()
        m.install(qpiemu=True, dpmi=True)
        pm = PMMachine(m)
        pm.hdpmi = False
        rmcs = pm.install(m)
        self.assertEqual([fn for fn, _r in pm.vendor_calls], [])
        self.assertIn("play 8-bit without HDPMI32i", pm.output)
        self.assertEqual(int.from_bytes(rmcs[0x1C:0x20], "little"), 0x3100)
        self.assertEqual(m.kept, paragraphs(m.syms) + HOST)

    def test_dpmi_not_hdpmi(self):
        """another DPMI host: no switch, and the TSR as without one"""
        m = machine()
        m.dpmi_logo = b"CWSDPMI\0"
        m.install(qpiemu=True, dpmi=True)
        self.assertIsNone(m.dpmi_call)
        self.assertEqual(m.exit, ("tsr", paragraphs(m.syms)))
        self.assertIn("play 8-bit without HDPMI32i", m.output)

    def test_dpmi_too_much_host_data(self):
        m = machine()
        m.dpmi_paras = HOST + 1
        m.install(qpiemu=True, dpmi=True)
        self.assertIsNone(m.dpmi_call)
        self.assertEqual(m.exit, ("tsr", paragraphs(m.syms)))

    def test_dpmi_switch_fails(self):
        """the switch fails after the host took its data: the installer
        goes on in real mode with nothing of what it overwrote"""
        m = machine()
        m.dpmi_fails = True
        m.install(qpiemu=True, dpmi=True)
        self.assertIsNotNone(m.dpmi_call)
        self.assertEqual(m.exit, ("tsr", paragraphs(m.syms)))
        self.assertIn("play 8-bit without HDPMI32i", m.output)
        m.dsp(0xE1)
        self.assertEqual((m.dsp_read(), m.dsp_read()), (4, 5))

    def test_hdpmi32_without_i(self):
        """HDPMI32 (IOPL 3) refuses the traps: none, a DPMI TSR still"""
        m = machine()
        m.install(qpiemu=True, dpmi=True)
        pm = PMMachine(m)
        pm.trap_fail = {0}
        rmcs = pm.install(m)
        self.assertEqual([fn for fn, _r in pm.vendor_calls], [6])
        self.assertIn("play 8-bit without HDPMI32i", pm.output)
        self.assertEqual(int.from_bytes(rmcs[0x14:0x18], "little"),
                         paragraphs(m.syms) + HOST)

    def test_hdpmi_traps_all_or_none(self):
        """a range refused after others: those are released again"""
        m = machine()
        m.install(qpiemu=True, dpmi=True)
        pm = PMMachine(m)
        pm.trap_fail = {2}
        pm.install(m)
        calls = [(fn, r["edx"]) for fn, r in pm.vendor_calls]
        self.assertEqual([fn for fn, _edx in calls], [6, 6, 6, 7, 7])
        self.assertEqual([edx for fn, edx in calls if fn == 7],
                         [0x1010, 0x1000])
        self.assertIn("play 8-bit without HDPMI32i", pm.output)

    def test_hdpmi_dma_traps_all_or_none(self):
        """the 16-bit channel's ranges too, without the Sound Blaster's"""
        m = machine()
        m.install(qpiemu=True, dpmi=True)
        pm = PMMachine(m)
        pm.trap_fail = {5}
        pm.install(m)
        calls = [(fn, r["edx"]) for fn, r in pm.vendor_calls]
        self.assertEqual([fn for fn, _edx in calls], [6] * 6 + [7])
        self.assertEqual(calls[-1], (7, 0x1040))
        self.assertIn("HDPMI32i for protected-mode games", pm.output)
        self.assertEqual(vchan(m), [0xFF, 0xFF, 5, 5])


def play16(c, cmd=0xB6, mode=0x30, samples=2048, rate=44100, ch=5,
           addr=0x24680, auto=True):
    """a game's 16-bit transfer: the channel, the rate, the command"""
    program_dma(c, ch, addr, samples * 2 * (2 if mode & 0x20 else 1),
                auto=auto)
    write(c, 0x41, rate >> 8, rate & 0xFF)
    write(c, cmd, mode, (samples - 1) & 0xFF, (samples - 1) >> 8)


@unittest.skipUnless(HAVE_UNICORN and have_nasm(), "needs nasm and unicorn")
class FrontTest(unittest.TestCase):
    """the chip behind the front, through QPI with a callback before ours"""

    path = "qpi"

    def card(self, **kw):
        return card(self.path, **kw)

    def setUp(self):
        self.c = self.card()
        self.chip = self.c.chip

    def assertClean(self):
        self.assertEqual(self.chip.violations, [])

    def test_the_front_answers(self):
        """the DSP's answers come from the front: the chip sees no command,
        and a reset of the front's DSP leaves the chip as it is"""
        c = self.c
        self.assertTrue(reset(c))
        write(c, 0xE1)
        self.assertEqual(read_all(c), [4, 5])
        write(c, 0xE3)
        self.assertEqual(bytes(read_all(c)),
                         b"COPYRIGHT (C) CREATIVE TECHNOLOGY LTD, 1992.\0")
        write(c, 0xE0, 0x5A, 0xE4, 0x77, 0xE8)
        self.assertEqual(read_all(c), [0xA5, 0x77])
        write(c, 0x40, 0xA5, 0x48, 0x00, 0x10, 0xD1, 0xD8)
        self.assertEqual(read_all(c), [0xFF])
        write(c, 0x0F, 0x81)
        self.assertEqual(read_all(c), [0xFF])
        write(c, 0xF8)
        self.assertEqual(read_all(c), [0x00])
        write(c, 0xE7)                      # no ESS chip on a Sound Blaster
        self.assertEqual(read_all(c), [])
        self.assertEqual(self.chip.commands, [])
        self.assertEqual(self.chip.resets, 0)
        self.assertClean()

    def test_16bit_stereo(self):
        """B6h, 44100 Hz: Extended mode once, the FIFO emptied for the first
        transfer, and the data sheet's order with only what changes; the
        game's channel 5 carried over to channel 1 in bytes"""
        c, chip = self.c, self.chip
        play16(c)
        cmds = chip.commands
        self.assertEqual(cmds[:2], [(0xC6,), (0xD1,)])
        w = written(chip)
        self.assertEqual((w[0xA1], w[0xA2]), clocks(248, True))
        self.assertEqual((w[0xA4], w[0xA5]), (0x00, 0xF0))      # 4096 bytes
        self.assertEqual((w[0xB6], w[0xB7]), (0x00, 0xBC))
        self.assertEqual((w[0xB9], chip.ctrl[0xA8] & 3), (0x02, 0x01))
        self.assertEqual(chip.ctrl[0xB8], 0x05)
        self.assertEqual(chip.ctrl[0xB1] & 0x50, 0x50)
        self.assertEqual(chip.ctrl[0xB2] & 0x50, 0x50)
        self.assertEqual(chip.mixer[0x71] & 0x20, 0)
        self.assertEqual([e[2] for e in chip.log if e[1] == BASE + 6][:2],
                         [0x02, 0x00])
        self.assertEqual(chip.dma[1], {"mask": False, "mode": 0x19,
                                       "addr": 0x4680, "count": 0x1FFF,
                                       "page": 0x02})
        self.assertEqual(self.c.m.flags(), F_EXT | F_RUN | F_16 | F_AUTO)
        c.wait(30000)
        self.assertGreater(len(chip.played), 1000)
        self.assertClean()

    def test_rates(self):
        """A1h and A2h for the Sound Blaster 16's rate, which 41h and 40h
        give through its rate generator"""
        for rate in (48000, 44100, 32000, 22050, 23000, 16000, 11025, 8000,
                     5000, 2000):
            c = self.card()
            play16(c, mode=0x10, samples=256, rate=rate)
            w = c.chip.ctrl
            self.assertEqual((w[0xA1], w[0xA2]),
                             clocks(rate_to_v(rate), False), rate)
            self.assertEqual(c.chip.violations, [])
        for tc in (0x06, 0x83, 0xA5, 0xC8, 0xD3, 0xEB, 0xFF):
            c = self.card()
            write(c, 0x40, tc, 0xC6, 0x30, 0xFF, 0x00)
            w = c.chip.ctrl
            self.assertEqual((w[0xA1], w[0xA2]), clocks(tc_to_v(tc), True),
                             hex(tc))

    def test_a_transfer_like_the_last(self):
        """the registers the chip has already aren't written again, and
        none is read twice until the chip's own reset"""
        c, chip = self.c, self.chip
        play16(c)
        write(c, 0xD9)
        c.wait(50000)
        chip.commands = []
        play16(c)
        self.assertEqual(chip.commands, [(0xB8, 0x04), (0xB8, 0x05)])
        chip.commands = []
        write(c, 0x41, 0x56, 0x22, 0xD9)
        c.wait(50000)
        chip.commands = []
        write(c, 0xB2, 0x10, 0xFF, 0x07)        # once, mono, 22050 Hz
        self.assertEqual([x for x in chip.commands if x[0] == 0xC0], [])
        self.assertEqual(written(chip)[0xB8], 0x01)
        self.assertEqual(chip.ctrl[0xA8] & 3, 0x02)
        self.assertEqual((chip.ctrl[0xA1], chip.ctrl[0xB7]), (0x6E, 0xF4))
        self.assertClean()

    def test_pause_continue_end(self):
        """D5h and D6h: Audio 1's DMA requests off and on (B2h bit 6); D9h:
        the block that plays is the last (B8h bit 2)"""
        c, chip = self.c, self.chip
        play16(c)
        chip.commands = []
        write(c, 0xD5)
        self.assertEqual(chip.ctrl[0xB2] & 0x40, 0)
        write(c, 0xD0)                      # the 8-bit kind: nothing
        write(c, 0xD6)
        self.assertEqual(chip.ctrl[0xB2] & 0x40, 0x40)
        self.assertEqual([x[0] for x in chip.commands], [0xB2, 0xB2])
        write(c, 0xD9)
        self.assertEqual(chip.ctrl[0xB8], 0x01)
        self.assertEqual(c.m.flags() & F_AUTO, 0)
        self.assertClean()

    def test_acknowledges(self):
        """82h says the kind of the chip's interrupt; +Fh and +Eh
        acknowledge it when it is theirs, through a read of the chip's
        +Eh"""
        c, chip = self.c, self.chip
        play16(c, samples=64)
        c.wait(2000)
        self.assertTrue(chip.a1_flags & 1)
        self.assertEqual(mixer(c, 0x82), 0x22)
        c.inp(BASE + 0x0E)                  # the 8-bit one: not this
        self.assertTrue(chip.a1_flags & 1)
        c.inp(BASE + 0x0F)
        self.assertFalse(chip.a1_flags & 1)
        self.assertEqual(mixer(c, 0x82), 0x20)
        self.assertClean()

    def test_reset_keeps_extended_mode(self):
        """the game's reset stops the front's transfer and leaves the chip
        in Extended mode, its registers known"""
        c, chip = self.c, self.chip
        play16(c)
        self.assertTrue(reset(c))
        self.assertEqual(chip.resets, 0)
        self.assertTrue(chip.ext_mode)
        self.assertEqual(chip.ctrl[0xB8] & 1, 0)
        self.assertEqual(c.m.flags() & (F_EXT | F_RUN | F_AUTO), F_EXT)
        chip.commands = []
        play16(c)
        self.assertNotIn((0xC6,), chip.commands)
        self.assertEqual([x for x in chip.commands if x[0] == 0xC0], [])
        self.assertClean()

    def test_firmware_commands(self):
        """the direct DAC, ADPCM and silence need Compatibility mode: the
        chip's own reset first, then the rate as a time constant; the next
        Extended transfer resets it again"""
        c, chip = self.c, self.chip
        play16(c)
        reset(c)
        write(c, 0x10, 0x80)
        self.assertEqual(chip.resets, 1)
        self.assertFalse(chip.ext_mode)
        self.assertEqual(chip.commands[-2:], [(0xD1,), (0x10, 0x80)])
        chip.commands = []
        # 4-bit ADPCM with its reference byte, at A5h's rate on a Sound
        # Blaster 16 (v 3Dh, 10847 Hz): 1 MHz over 92
        write(c, 0x40, 0xA5, 0x75, 0xFF, 0x01)
        self.assertEqual(chip.commands, [(0x40, 0xA4), (0x75, 0xFF, 0x01)])
        c.wait(20000)
        chip.commands = []
        write(c, 0x80, 0x10, 0x00)                  # silence
        self.assertEqual(chip.commands, [(0x40, 0xA4), (0x80, 0x10, 0x00)])
        c.wait(20000)
        reset(c)
        chip.commands = []
        write(c, 0xC6, 0x00, 0xFF, 0x03)
        self.assertEqual(chip.resets, 2)
        self.assertEqual(chip.commands[:2], [(0xC6,), (0xD1,)])
        self.assertClean()

    def test_midi(self):
        """MIDI goes through the chip in Compatibility mode: 38h a byte, 30h
        input until the next write, 35h the UART mode until a reset, which
        resets the chip too"""
        c, chip = self.c, self.chip
        write(c, 0x38, 0x90)
        self.assertEqual(chip.commands[-1], (0x38, 0x90))
        write(c, 0x35)
        self.assertEqual(chip.commands[-1], (0x35,))
        write(c, 0x90, 0x3C, 0x7F)
        self.assertEqual(chip.midi_out[-3:], [0x90, 0x3C, 0x7F])
        resets = chip.resets
        self.assertTrue(reset(c))
        self.assertEqual(chip.resets, resets + 1)
        write(c, 0xE1)
        self.assertEqual(read_all(c), [4, 5])
        self.assertClean()

    def test_a_program_that_knows_the_chip(self):
        """C6h with a register command after it is the chip's Extended
        mode: from then on every access is the chip's, until a reset"""
        c, chip = self.c, self.chip
        write(c, 0xC6, 0xA1, 0x6E)
        self.assertEqual(chip.commands, [(0xC6,), (0xA1, 0x6E)])
        self.assertEqual(c.m.flags(), F_ESS | F_EXT)
        write(c, 0xC0, 0xA1)
        self.assertEqual(read(c), 0x6E)
        mixer(c, 0x30, 0x55)                # the chip's mixer as it is
        self.assertEqual(chip.mixer[0x30], 0x55)
        self.assertEqual(mixer(c, 0x30), 0x55)
        self.assertTrue(reset(c))           # the chip's own reset
        self.assertEqual(chip.resets, 1)
        self.assertEqual(c.m.flags(), 0)
        write(c, 0xE1)
        self.assertEqual(read_all(c), [4, 5])
        self.assertClean()

    def test_masked_channel(self):
        """the game's channel masked at the command: Audio 1's waits for its
        unmasking; programmed anew, it is carried over again"""
        c, chip = self.c, self.chip
        program_dma(c, 5, 0x30000, 4096)
        c.out(0xD4, 0x05)                   # masked again
        write(c, 0x41, 0xAC, 0x44, 0xB6, 0x30, 0xFF, 0x03)
        self.assertTrue(chip.dma[1]["mask"])
        c.out(0xD4, 0x01)
        self.assertFalse(chip.dma[1]["mask"])
        program_dma(c, 5, 0x50000, 4096)
        self.assertEqual(chip.dma[1]["page"], 0x05)
        self.assertFalse(chip.dma[1]["mask"])
        self.assertClean()

    def test_mirror_above_64k(self):
        """a word address with bit 15 set: byte address bit 16, the page"""
        c, chip = self.c, self.chip
        play16(c, addr=0x32468)
        self.assertEqual((chip.dma[1]["addr"], chip.dma[1]["page"]),
                         (0x2468, 0x03))
        play16(c, addr=0x2A468)
        self.assertEqual((chip.dma[1]["addr"], chip.dma[1]["page"]),
                         (0xA468, 0x02))

    def test_single_transfers(self):
        """/S: single transfers on the card's channel and in B9h"""
        c = self.card(tail="/S")
        play16(c)
        self.assertEqual(c.chip.dma[1]["mode"], 0x59)
        self.assertEqual(c.chip.ctrl[0xB9], 0x00)

    def test_dac_options(self):
        """/F0 /O1: mixer 71h at each transfer, whatever changed it"""
        c = self.card(tail="/F0 /O1")
        c.chip.mixer[0x71] = 0x22
        play16(c)
        self.assertEqual(c.chip.mixer[0x71], 0x1E)

    def test_dma_select(self):
        """mixer 81h picks the 16-bit channel; one the front doesn't trap
        is read back at the command"""
        c, chip = self.c, self.chip
        mixer(c, 0x81, 0x42)                    # channel 6
        self.assertEqual(mixer(c, 0x81), 0x42)
        self.assertEqual(c.m.data8("dma16"), 6)
        play16(c, cmd=0xB2, ch=6, addr=0x51000, auto=False)
        self.assertEqual(chip.dma[1], {"mask": False, "mode": 0x09,
                                       "addr": 0x1000, "count": 0x1FFF,
                                       "page": 0x05})
        mixer(c, 0x81, 0x02)                    # none: on the 8-bit channel
        self.assertEqual(mixer(c, 0x81), 0x02)
        self.assertEqual(chip.mixer[0x81], 0)  # never the chip's
        self.assertClean()

    def test_mixer_levels(self):
        """the Sound Blaster 16's levels become the chip's"""
        c, chip = self.c, self.chip

        def hw(*regs):
            return [chip.mixer[r] for r in regs]
        mixer(c, 0x00, 0x00)
        mixer(c, 0x30, 0xF0)
        mixer(c, 0x31, 0x80)
        self.assertEqual(hw(0x60, 0x62), [0x3C, 0x20])
        mixer(c, 0x32, 0xA0)
        mixer(c, 0x33, 0x50)
        self.assertEqual(hw(0x14), [0xA5])
        mixer(c, 0x34, 0x70)
        mixer(c, 0x35, 0x30)
        self.assertEqual(hw(0x36), [0x73])
        mixer(c, 0x36, 0xF0)                    # CD: through the switches
        mixer(c, 0x37, 0xF0)
        self.assertEqual(hw(0x38), [0xFF])
        mixer(c, 0x3C, 0x1F & ~0x02)            # CD right off
        self.assertEqual(hw(0x38), [0xF0])
        mixer(c, 0x38, 0x80)                    # line
        mixer(c, 0x39, 0x40)
        self.assertEqual(hw(0x3E), [0x84])
        mixer(c, 0x3A, 0xE0)                    # mic
        self.assertEqual(hw(0x1A), [0xEE])
        mixer(c, 0x3C, 0x1F & ~0x01)            # mic off
        self.assertEqual(hw(0x1A), [0x00])
        mixer(c, 0x3B, 0xC0)                    # the PC speaker
        self.assertEqual(hw(0x3C), [0x06])
        chip.mixer[0x1C] = 0x08                 # the record source
        for left, right, src in ((0x01, 0x01, 0x08), (0x04, 0x02, 0x0A),
                                 (0x10, 0x08, 0x0E), (0x40, 0x20, 0x0D),
                                 (0x15, 0x00, 0x0D)):
            mixer(c, 0x3D, left)
            mixer(c, 0x3E, right)
            self.assertEqual(hw(0x1C), [src], hex(left))
        self.assertEqual(chip.commands, [])

    def test_sound_blaster_pro_registers(self):
        """the Sound Blaster Pro's levels are views of the Sound Blaster
        16's, as on the CT1745; 0Ch and 0Eh aren't there"""
        c, chip = self.c, self.chip
        mixer(c, 0x04, 0x99)                    # voice
        self.assertEqual(chip.mixer[0x14], 0x99)
        self.assertEqual((mixer(c, 0x32), mixer(c, 0x33)), (0x98, 0x98))
        mixer(c, 0x22, 0xF5)                    # master
        self.assertEqual((chip.mixer[0x60], chip.mixer[0x62]), (0x3E, 0x16))
        self.assertEqual(mixer(c, 0x22), 0xF5)
        mixer(c, 0x0A, 0x04)                    # mic
        self.assertEqual((mixer(c, 0x3A), chip.mixer[0x1A]), (0x98, 0x99))
        self.assertEqual(mixer(c, 0x0A), 0x04)
        hw = chip.mixer[0x0E]
        mixer(c, 0x0E, 0x02)
        self.assertEqual((chip.mixer[0x0E], mixer(c, 0x0E), mixer(c, 0x0C)),
                         (hw, 0x00, 0x00))

    def test_the_chips_own_registers(self):
        """the chip's own mixer registers go to it as they come; the view
        stays the game's"""
        c, chip = self.c, self.chip
        before = [mixer(c, r) for r in range(0x30, 0x48)]
        for reg, v in ((0x60, 0x20), (0x14, 0x5A), (0x1A, 0x99),
                       (0x1C, 0x12), (0x7C, 0x55)):
            mixer(c, reg, v)
            self.assertEqual((chip.mixer[reg], mixer(c, reg)), (v, v))
        self.assertEqual([mixer(c, r) for r in range(0x30, 0x48)], before)

    def test_card_registers_kept(self):
        c, chip = self.c, self.chip
        chip.log = []
        mixer(c, 0x80, 0x0F)
        mixer(c, 0x82, 0xFF)
        self.assertNotIn(("out", BASE + 5, 0x0F), chip.log)
        self.assertNotIn(("out", BASE + 5, 0xFF), chip.log)
        self.assertEqual((mixer(c, 0x80), mixer(c, 0x81), mixer(c, 0x82)),
                         (0xF2, 0x22, 0x20))

    def test_mixer_reset(self):
        """a game's mixer reset: the Sound Blaster 16's levels to the chip,
        its other registers left"""
        m = installed(chip=mixer_chip())
        m.mix_set(0x30, 0x10)
        m.mix_set(0x00, 0x00)
        self.assertEqual(m.chip.index, 0x00)        # the game's index kept
        hw = m.chip.mixer
        self.assertEqual(hw[0x00], 0x77)            # never the chip's reset
        self.assertEqual(m.mix_get(0x30), 0xC0)     # the view's reset
        self.assertEqual(m.mix_get(0x3C), 0x1F)
        self.assertEqual((hw[0x60], hw[0x62]), (0x30, 0x30))  # C0h, 6 bits
        self.assertEqual((hw[0x14], hw[0x36]), (0xCC, 0xCC))
        self.assertEqual((hw[0x38], hw[0x3E], hw[0x1A], hw[0x3C]),
                         (0, 0, 0, 0))
        self.assertEqual(hw[0x1C], 0x15)    # mic, CD and line: the record mix
        self.assertEqual(hw[0x0E], 0x20)    # the Sound Blaster Pro's stereo off
        for reg in (0x50, 0x52, 0x7C):
            self.assertEqual(hw[reg], DOS_MIXER[reg], hex(reg))

    def test_the_mixer_a_game_finds(self):
        """the view at the start shows the levels the chip has"""
        m = installed(chip=mixer_chip())
        view = [m.mix_get(r) for r in range(0x30, 0x48)]
        self.assertEqual(view, [
            0xE8, 0x00,                 # master, the right muted
            0xA0, 0xB0, 0xC0, 0xD0,     # voice, MIDI
            0x10, 0x20, 0x30, 0x40,     # CD, line
            0x50, 0xC0,                 # mic, PC speaker
            0x1F, 0x10, 0x08,           # the switches: line records
            0, 0, 0, 0, 0, 0x80, 0x80, 0x80, 0x80])
        # writing back what it read changes nothing
        before = list(m.chip.mixer)
        for r, v in zip(range(0x30, 0x48), view):
            if r != 0x31:               # the muted master can't come back
                m.mix_set(r, v)         # muted
        self.assertEqual([(r, v) for r, v in enumerate(m.chip.mixer)
                          if v != before[r]], [])

    def test_other_ports(self):
        """a port the front's traps don't have goes to the callback before
        ours; the trapped ports it doesn't answer go to the chip"""
        c = self.c
        m = c.m
        self.assertEqual(m.qpi_io(0x330), 0x5A)
        self.assertEqual(m.chained, [(0x330, 0, 0xA5)])
        m.chained = []
        c.chip.ports[6] = 0x33
        self.assertEqual(c.inp(BASE + 6), 0x33)
        c.out(BASE + 0x0A, 0x12)
        program_dma(c, 5, 0x20000, 512)
        mixer(c, 0x81, 0x42)                    # channel 6: 5's ports pass
        c.out(0xD4, 0x05)
        self.assertEqual(m.chained, [])
        self.assertTrue(c.chip.dma[5]["mask"])

    def test_stack(self):
        """the front's stack, the PSP's last 116 bytes, has room for its
        deepest calls: a transfer, a mixer reset, the chip's own reset"""
        c = self.c
        m = c.m
        lo, top = m.addr("stack"), m.addr("stack_top")
        poison_stack(m)
        play16(c)
        mixer(c, 0x00, 0x00)
        write(c, 0x10, 0x80)
        write(c, 0xC6, 0x00, 0xFF, 0x03)
        write(c, 0xE3)
        read_all(c)
        depth = stack_depth(m)
        self.assertGreater(depth, 0)
        self.assertLessEqual(depth, top - lo - 16)  # 16 bytes to spare


@unittest.skipUnless(HAVE_UNICORN and have_nasm(), "needs nasm and unicorn")
class QPIFrontTest(FrontTest):
    """the same without a callback before ours: QPI's untrapped I/O for
    the ports the front doesn't answer"""

    def card(self, **kw):
        return card("qpi", old_callback=False, **kw)

    def test_other_ports(self):
        m = self.c.m
        m.qpi_accesses = []
        self.chip.ports[6] = 0x33
        self.assertEqual(m.qpi_io(BASE + 6), 0x33)
        m.qpi_io(0x330)
        self.assertEqual(m.chained, [])
        self.assertEqual([a[:2] for a in m.qpi_accesses],
                         [("in", BASE + 6), ("in", 0x330)])


@unittest.skipUnless(HAVE_UNICORN and have_nasm(), "needs nasm and unicorn")
class EMM386FrontTest(unittest.TestCase):
    """the front through EMM386's handler at ring 0, which has no traps of
    the DMA controller: the game's channel is read back at the command"""

    def setUp(self):
        self.c = card("emm")
        self.chip = self.c.chip

    def test_readback(self):
        c, chip = self.c, self.chip
        play16(c, addr=0x32468)
        self.assertEqual(chip.dma[1], {"mask": False, "mode": 0x19,
                                       "addr": 0x2468, "count": 0x1FFF,
                                       "page": 0x03})
        self.assertEqual(chip.ff, [0, 0])
        c.wait(30000)
        self.assertGreater(len(chip.played), 1000)
        self.assertEqual(chip.violations, [])

    def test_ports(self):
        """+7h to +9h, +Bh and +Dh are EMM386's to do; the front's ports
        come back done, the reset port's read too"""
        m = self.c.m
        self.chip.ports[6] = 0x33
        self.assertEqual(m.emm_io(BASE + 6), (0x33, False))
        for o in (7, 8, 9, 0xB, 0xD):               # in the range, passed
            self.assertEqual(m.emm_io(BASE + o, 0x12)[1], True)
            self.assertEqual(m.emm_io(BASE + o)[1], True)
        for o in (0xA, 0xE, 0xF):
            self.assertEqual(m.emm_io(BASE + o, 0x12)[1], False)
        self.assertEqual(m.emm_io(BASE + 0x10), (None, True))   # not trapped
        self.assertEqual(m.emm_io(0xC4), (None, True))

    def test_out_is_8(self):
        """EMM386's documented direction for a byte out, 8 (4 in 6.22)"""
        m = self.c.m
        m.emm_out = 8
        m.dsp(0xE1)
        self.assertEqual((m.dsp_read(), m.dsp_read()), (4, 5))
        m.mix_set(0x30, 0xF0)
        self.assertEqual(self.chip.mixer[0x60], 0x3C)

    def test_stack(self):
        """the front runs on EMM386's stack, not its own"""
        m = self.c.m
        poison_stack(m)
        play16(self.c)
        self.assertEqual(stack_depth(m), 0)


@unittest.skipUnless(HAVE_UNICORN and have_nasm(), "needs nasm and unicorn")
class HDPMITest(unittest.TestCase):
    """the front through HDPMI32i's handler at ring 3, the chip and the DMA
    controllers through HDPMI's untrapped I/O"""

    def setUp(self):
        self.c = card("pm")
        self.chip = self.c.chip
        self.pm = self.c.pm

    def test_version(self):
        c, pm = self.c, self.pm
        pm.vendor_io = []
        write(c, 0xE1)
        self.assertEqual(read_all(c), [4, 5])
        self.assertEqual([a for a in pm.vendor_io if a[0] == "out"], [])

    def test_16bit_transfer(self):
        c, chip, pm = self.c, self.chip, self.pm
        pm.vendor_io = []
        play16(c)
        self.assertEqual((chip.ctrl[0xA1], chip.ctrl[0xB7], chip.ctrl[0xB8]),
                         (0xEE, 0xBC, 0x05))
        self.assertEqual(chip.dma[1], {"mask": False, "mode": 0x19,
                                       "addr": 0x4680, "count": 0x1FFF,
                                       "page": 0x02})
        self.assertIn(("out", 0x0B, 0x19), pm.vendor_io)
        self.assertEqual(pm.ports, [])
        c.wait(30000)
        self.assertGreater(len(chip.played), 1000)
        self.assertEqual(chip.violations, [])

    def test_other_ports(self):
        """the odd ports of the traps' ranges go to the chip as they are"""
        c, pm = self.c, self.pm
        pm.vendor_io = []
        c.out(0xC5, 0x12)
        self.assertEqual(pm.vendor_io, [("out", 0xC5, 0x12)])
        self.chip.ports[6] = 0x33
        self.assertEqual(c.inp(BASE + 6), 0x33)

    def test_stack(self):
        c = self.c
        m = c.m
        lo, top = m.addr("stack"), m.addr("stack_top")
        m.put(lo, b"\x5A" * (top - lo))
        play16(c)
        mixer(c, 0x00, 0x00)
        write(c, 0x10, 0x80)
        write(c, 0xC6, 0x00, 0xFF, 0x03)
        depth = stack_depth(m)
        self.assertGreater(depth, 0)
        self.assertLessEqual(depth, top - lo - 16)


if __name__ == "__main__":
    unittest.main()
