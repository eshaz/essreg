# (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
#
# Licensed under GPL Version 3.0
"""esssb16.com in a CPU emulator: the installer under QEMM, QPIEMU, EMM386
and a DPMI host, its refusals and what it says about them, and the Sound
Blaster 16 front through the QPI callback, EMM386's handler at ring 0 and
HDPMI32i's handler at ring 3 against the chip model: the DSP's answers,
16-bit and 8-bit transfers on Audio 1 with the rates ES1869.DRV would
pick, the game's 16-bit DMA channel carried over to the card's, the
mixer's view, the interrupts, resets, and the chip's own commands going
through. Every resident test runs after DOS has reused the memory the
TSR gave back.

usage: python3 -m unittest tests.test_esssb16   (needs nasm and unicorn)
"""

import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))

from test_vxdext import HAVE_UNICORN, have_nasm  # noqa: E402

import build_esssb16  # noqa: E402

if HAVE_UNICORN:
    from tsremu import (Machine, PMMachine, Chip, DeadChip,  # noqa: E402
                        PAGE_PORTS, PSP_PARA, ENV_PARA, STUB_PARA, OLD_OFF,
                        STUB32, VENDOR_OFF, SEL_CODE16, SEL_DATA, SEL_CODE32,
                        POISON)

E_USAGE, E_NOCARD, E_NOTRAP, E_WINDOWS, E_LOADED = 1, 2, 3, 4, 5
F_ESS, F_EXT, F_16, F_8, F_RUN, F_AUTO, F_RESET = 1, 2, 4, 8, 0x10, 0x20, 0x80
BASE = 0x220
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


def game_dma(chip, ch, addr, count, page, mode):
    """a game programs its 16-bit channel on the second controller: masked,
    the flip-flop cleared, the word address and count, the page, the mode,
    unmasked"""
    port = 0xC0 + 4 * (ch - 4)
    chip.outb(0xD4, 4 | (ch & 3))
    chip.outb(0xD8, 0)
    chip.outb(port, addr & 0xFF)
    chip.outb(port, addr >> 8)
    chip.outb(port + 2, count & 0xFF)
    chip.outb(port + 2, count >> 8)
    chip.outb(PAGE_PORTS[ch], page)
    chip.outb(0xD6, mode | (ch & 3))
    chip.outb(0xD4, ch & 3)
    chip.dma_log = []


def regs(m):
    """the chip's controller registers the last commands wrote"""
    return {c[0]: c[1] for c in m.chip.commands if 0xA0 <= c[0] <= 0xBF}


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
# voice ABh, FM CDh, CD 12h, line 34h, mic 50h, PC speaker 5, the record
# source line; and the chip's own registers a mixer reset would clear
DOS_MIXER = {0x60: 0x3A, 0x62: 0x7A, 0x14: 0xAB, 0x36: 0xCD, 0x38: 0x12,
             0x3E: 0x34, 0x1A: 0x55, 0x3C: 0x05, 0x1C: 0x16, 0x50: 0x0C,
             0x52: 0x2A, 0x71: 0x2E, 0x7C: 0x99, 0x0E: 0x22, 0x00: 0x77}


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
        image, syms = build()
        self.assertLess(len(image), 8192)
        self.assertLess(paragraphs(syms), 256)       # under 4 KB resident
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
                     "rmcs", "msg_nohdpmi", "msg_hdpmi", "hdpmi_name"):
            self.assertGreaterEqual(syms[name], syms["host_code_end"], name)


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
        self.assertEqual(fns, [0x1A06, 0x1A07] + [0x1A09] * 7)
        self.assertEqual(m.qpi_callback, (PSP_PARA, m.sym("qpi_shim")))
        self.assertEqual(m.qpi_trapped, {BASE + o for o in
                                         (4, 5, 6, 0xA, 0xC, 0xE, 0xF)})
        self.assertEqual(m.mem(m.addr("qpi_old"), 4),
                         bytes([OLD_OFF, 0, STUB_PARA & 0xFF, STUB_PARA >> 8]))
        self.assertEqual(m.freed, [ENV_PARA])
        self.assertEqual(m.umb_links, [1, 0])
        # the card is read, not set up: its channels and mixer stay
        self.assertEqual(m.chip.pnp[(1, 0x75)], 4)
        self.assertEqual((m.chip.mixer[0x71], m.chip.mixer[0x7C]), (0, 0))
        self.assertEqual((m.data16("base"), m.data16("cfg"), m.data8("irq"),
                          m.data8("dma8"), m.data8("dma16")),
                         (0x220, 0x250, 5, 1, 5))
        self.assertEqual(m.chip.commands, [(0xE7,)])
        self.assertEqual(m.chip.dma_log, [])

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
        self.assertLessEqual(call["si"] + 4 * 12, m.sym("resident_end"))

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
        for tail in ("/X", "/?", "-h9", "/A"):
            m = machine()
            m.install(tail=tail, qpiemu=True)
            self.assertEqual(m.exit, ("exit", E_USAGE), tail)
            self.assertIn("usage: esssb16", m.output)

    def test_options(self):
        m = machine(Chip(base=0x240))
        m.install(tail=" /a240 -H6 /S", qpiemu=True)
        self.assertEqual(m.exit[0], "tsr")
        self.assertIn("ES1869 at 240h, IRQ 5, DMA 1, the game's DMA 6 "
                      "through it", m.output)
        self.assertIn("set BLASTER=A240 I5 D1 H6", m.output)
        self.assertEqual(m.data8("single"), 1)
        self.assertEqual(m.qpi_trapped, {0x240 + o for o in
                                         (4, 5, 6, 0xA, 0xC, 0xE, 0xF)})

    def test_blaster(self):
        """BLASTER's A and H; I and D come from the card's PnP registers"""
        m = machine(Chip(base=0x240, irq=7, dma8=3),
                    env="PATH=C:\\DOS;C:\\GAMES\0BLASTER=A240 I10 D0 H7 T6")
        m.install(qpiemu=True)
        self.assertEqual(m.exit[0], "tsr")
        self.assertIn("ES1869 at 240h, IRQ 7, DMA 3, the game's DMA 7 "
                      "through it", m.output)
        self.assertIn("set BLASTER=A240 I7 D3 H7", m.output)

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
        m = machine()
        m.install(tail="/H0", qpiemu=True)
        self.assertEqual(m.exit[0], "tsr")
        self.assertEqual(m.data8("dma16"), 0)
        self.assertIn("IRQ 5, DMA 1\r\n", m.output)
        self.assertIn("set BLASTER=A220 I5 D1 P330 T6", m.output)

    def test_dpmi(self):
        """HDPMI: the switch with the host's data after the resident part,
        16-bit code on the host's first selectors, HDPMI32i's traps for
        them, the TSR through a real-mode int 21h that keeps the host's
        data; then a protected-mode game's accesses through the handler
        as the installer registered it"""
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
        self.assertEqual(traps, [(BASE + o, n, SEL_CODE16,
                                  m.sym("hdpmi_shim"), SEL_DATA)
                                 for o, n in ((4, 3), (0xA, 1), (0xC, 1),
                                              (0xE, 2))])
        self.assertIn("HDPMI32i for protected-mode games", pm.output)
        self.assertEqual(pm.data16("pm_data"), SEL_DATA)
        self.assertEqual(pm.mem(pm.addr("vendor"), 6),
                         (STUB32 + VENDOR_OFF).to_bytes(4, "little") +
                         SEL_CODE32.to_bytes(2, "little"))
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
        game_dma(m.chip, 5, 0x1234, 0x07FF, 0x03, SINGLE | AUTO | PLAY)
        for b in (0x41, 0xAC, 0x44, 0xB6, 0x30, 0xFF, 0x07):
            pm.hdpmi_io(BASE + 0xC, b)
        self.assertEqual(regs(m)[0xB7], 0xBC)
        self.assertEqual(m.chip.dma[1]["addr"], 0x2468)
        # and a real-mode game through QPI, on the same memory
        m.chip.commands = []
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


@unittest.skipUnless(HAVE_UNICORN and have_nasm(), "needs nasm and unicorn")
class FrontTest(unittest.TestCase):
    """the Sound Blaster 16 through the QPI callback"""

    def setUp(self):
        self.m = self.install()

    def install(self, **kw):
        """the program resident, the front reached as this class reaches it"""
        return installed(qpiemu=True, **kw)

    def mixer(self, *regs_):
        return [self.m.chip.mixer[r] for r in regs_]

    def test_version(self):
        m = self.m
        self.assertEqual(m.status() & 0x80, 0)
        m.dsp(0xE1)
        self.assertEqual(m.status() & 0x80, 0x80)
        self.assertEqual((m.dsp_read(), m.dsp_read()), (4, 5))
        self.assertEqual(m.status() & 0x80, 0)
        self.assertEqual(m.chip.commands, [])

    def test_copyright(self):
        m = self.m
        m.dsp(0xE3)
        s = b""
        while m.status() & 0x80:
            s += bytes([m.dsp_read()])
        self.assertEqual(s, b"COPYRIGHT (C) CREATIVE TECHNOLOGY LTD, 1992.\0")

    def test_16bit_stereo_signed(self):
        """a 16-bit stereo transfer on the game's channel 5: Audio 1 in
        16-bit stereo, the channel's programming on channel 1 in bytes"""
        m = self.m
        game_dma(m.chip, 5, 0x1234, 0x07FF, 0x03, SINGLE | AUTO | PLAY)
        m.dsp(0x41, 0xAC, 0x44)                 # 44100 Hz
        m.dsp(0xB6, 0x30, 0xFF, 0x07)           # 2048 samples, auto-init
        r = regs(m)
        self.assertEqual((r[0xA1], r[0xA2]), (0xEE, 0xFD))
        self.assertEqual((r[0xA4], r[0xA5]), (0x00, 0xF0))      # 4096 bytes
        self.assertEqual((r[0xB6], r[0xB7]), (0x00, 0xBC))
        self.assertEqual((r[0xA8], r[0xB8]), (0x01, 0x05))
        self.assertEqual(r[0xB9], 0x02)         # demand, 2 bytes: ESS's
        self.assertEqual(m.chip.commands[-1], (0xD1,))
        self.assertEqual(m.chip.dma[1], {"mask": False, "mode": 0x19,
                                         "addr": 0x2468, "count": 0x0FFF,
                                         "page": 0x02})
        self.assertEqual(m.chip.dma[5], {"mask": False, "mode": 0x59,
                                         "addr": 0x1234, "count": 0x07FF,
                                         "page": 0x03})
        self.assertEqual(m.flags(), F_EXT | F_RUN | F_16 | F_AUTO)
        self.assertEqual(m.mix_get(0x82), 0x02)
        self.assertEqual(m.chip.ff, [0, 0])

    def test_16bit_mono_unsigned_once(self):
        m = self.m
        game_dma(m.chip, 5, 0x0000, 0x007F, 0x00, SINGLE | PLAY)
        m.dsp(0x42, 0x1F, 0x40)                 # 8000 Hz, the input rate
        m.dsp(0xB0, 0x00, 0x7F, 0x00)           # 128 samples, once
        r = regs(m)
        self.assertEqual((r[0xA1], r[0xA2]), (0x4E, 0xE7))
        self.assertEqual((r[0xA4], r[0xA5]), (0x00, 0xFF))      # 256 bytes
        self.assertEqual((r[0xB6], r[0xB7]), (0x80, 0xD4))
        self.assertEqual((r[0xA8], r[0xB8]), (0x02, 0x01))
        self.assertEqual(m.chip.dma[1], {"mask": False, "mode": 0x09,
                                         "addr": 0, "count": 0x00FF,
                                         "page": 0})
        self.assertEqual(m.flags(), F_EXT | F_RUN | F_16)

    def test_rates(self):
        """A1h and A2h as ES1869.DRV picks them"""
        m = self.m
        for rate, a1, a2 in ((48000, 0xEF, 0xFC), (44100, 0xEE, 0xFD),
                             (22050, 0x6E, 0xF7), (11025, 0x5C, 0xEE),
                             (8000, 0x4E, 0xE7), (2000, 0x1D, 0xCE),
                             (60000, 0xEF, 0xFC)):
            m.chip.commands = []
            m.dsp(0x41, rate >> 8, rate & 0xFF)
            m.dsp(0xB6, 0x30, 0xFF, 0x00)
            r = regs(m)
            self.assertEqual((r[0xA1], r[0xA2]), (a1, a2), rate)

    def test_time_constant(self):
        m = self.m
        m.dsp(0x40, 0xD3)                       # 1 MHz / 45 = 22222 Hz
        m.dsp(0xB6, 0x30, 0xFF, 0x00)
        self.assertEqual(regs(m)[0xA1], 0xDC)
        self.assertEqual(m.chip.commands[0], (0x40, 0xD3))
        m.chip.commands = []
        m.dsp(0x41, 0xAC, 0x44)                 # 41h wins over 40h
        m.dsp(0xB6, 0x30, 0xFF, 0x00)
        self.assertEqual(regs(m)[0xA1], 0xEE)

    def test_pause_continue_stop_16(self):
        m = self.m
        m.dsp(0xB6, 0x30, 0xFF, 0x07)
        m.chip.commands = []
        m.dsp(0xD5)
        m.dsp(0xD6)
        self.assertEqual(m.chip.commands, [(0xD0,), (0xD4,)])
        m.chip.commands = []
        m.dsp(0xD9)                             # the block that plays is the
        self.assertEqual(m.chip.commands, [(0xB8, 0x01)])       # last
        self.assertEqual(m.flags() & F_AUTO, 0)
        m.chip.commands = []
        m.dsp(0xDA)                             # the 8-bit stop does the same
        self.assertEqual(m.chip.commands, [(0xB8, 0x01)])

    def test_irq16(self):
        m = self.m
        m.dsp(0xB6, 0x30, 0xFF, 0x07)
        m.chip.a1_irq = True
        self.assertEqual(m.mix_get(0x82), 0x02)
        m.chip.log = []
        m.inp(BASE + 0xF)                       # the 16-bit acknowledge
        self.assertEqual([e for e in m.chip.log if e[0] == "in"],
                         [("in", BASE + 0xE, 0)])
        self.assertFalse(m.chip.a1_irq)
        m.chip.commands = []
        m.dsp(0xF3)                             # the 16-bit interrupt asked
        self.assertEqual(m.chip.commands, [(0xF2,)])
        self.assertEqual(m.flags() & (F_16 | F_8), F_16)

    def test_irq8(self):
        m = self.m
        m.dsp(0xC6, 0x00, 0xFF, 0x03)
        m.chip.a1_irq = True
        self.assertEqual(m.mix_get(0x82), 0x01)
        self.assertEqual(m.inp(BASE + 0xC) & 0x01, 0x01)
        m.status()                              # acknowledged
        self.assertFalse(m.chip.a1_irq)
        m.chip.commands = []
        m.dsp(0xF2)
        self.assertEqual(m.chip.commands, [(0xF2,)])
        self.assertEqual(m.flags() & (F_16 | F_8), F_8)

    def test_8bit_audio1(self):
        m = self.m
        m.dsp(0x40, 0xA6)                       # 11111 Hz
        m.dsp(0xC6, 0x00, 0xFF, 0x03)           # 1024 samples, auto-init
        self.assertEqual(m.chip.commands, [
            (0x40, 0xA6), (0xC6,), (0xC0, 0xB8), (0xB8, 0x04), (0xB9, 0x02),
            (0xC0, 0xA8), (0xA8, 0x02), (0xA1, 0x5C), (0xA2, 0xEE),
            (0xA4, 0x00), (0xA5, 0xFC), (0xB6, 0x80), (0xB7, 0x51),
            (0xB7, 0xD0), (0xC0, 0xB1), (0xB1, 0x50), (0xC0, 0xB2),
            (0xB2, 0x50), (0xB8, 0x05), (0xD1,)])      # B8h known by now
        self.assertEqual(m.flags(), F_EXT | F_RUN | F_8 | F_AUTO)
        self.assertEqual(m.chip.dma_log, [])    # the game's own channel
        self.assertEqual(m.mix_get(0x82), 0x01)

    def test_8bit_stereo_signed_once(self):
        m = self.m
        m.dsp(0x41, 0x56, 0x22)                 # 22050 Hz
        m.dsp(0xC0, 0x30, 0xFF, 0x01)           # 512 samples
        r = regs(m)
        self.assertEqual(r[0xB8], 0x01)         # no auto-init
        self.assertEqual(r[0xA8], 0x01)         # stereo
        self.assertEqual((r[0xA1], r[0xA2]), (0x6E, 0xF7))
        self.assertEqual((r[0xB6], r[0xB7]), (0x00, 0xB8))
        self.assertEqual(m.chip.commands[0], (0xC6,))
        self.assertEqual(m.flags(), F_EXT | F_RUN | F_8)
        m.chip.commands = []
        m.dsp(0xC0, 0x30, 0xFF, 0x01)           # Extended mode set once
        self.assertNotIn((0xC6,), m.chip.commands)

    def test_8bit_pause_continue_stop(self):
        m = self.m
        m.dsp(0xC6, 0x00, 0xFF, 0x03)
        m.chip.commands = []
        m.dsp(0xD0)
        m.dsp(0xD4)
        self.assertEqual(m.chip.commands, [(0xD0,), (0xD4,)])
        m.chip.commands = []
        m.dsp(0xDA)                             # auto-init off: the last block
        self.assertEqual(m.chip.commands, [(0xB8, 0x01)])
        self.assertEqual(m.flags() & F_AUTO, 0)

    def test_recording(self):
        m = self.m
        m.dsp(0xCE, 0x00, 0xFF, 0x03)           # 8-bit input, auto-init
        self.assertEqual(regs(m)[0xB8], 0x0F)
        self.assertNotIn((0xD1,), m.chip.commands)
        self.assertEqual(m.chip.dma_log, [])
        m.chip.commands = []
        game_dma(m.chip, 5, 0x4000, 0x03FF, 0x01, SINGLE | AUTO | RECORD)
        m.dsp(0xBE, 0x10, 0xFF, 0x03)           # 16-bit input on channel 5
        self.assertEqual(regs(m)[0xB8], 0x0F)
        self.assertEqual(regs(m)[0xB7], 0xF4)   # 16-bit mono signed
        self.assertNotIn((0xD1,), m.chip.commands)
        self.assertEqual(m.chip.dma[1], {"mask": False, "mode": 0x15,
                                         "addr": 0x8000, "count": 0x07FF,
                                         "page": 0x00})

    def test_16bit_without_channel(self):
        """without a 16-bit channel the game programs the 8-bit one, as a
        Sound Blaster 16 plays 16-bit without an H"""
        m = self.install(tail="/H0")
        m.dsp(0xB6, 0x30, 0xFF, 0x03)           # 1024 samples
        r = regs(m)
        self.assertEqual((r[0xA4], r[0xA5]), (0x00, 0xF8))      # 2048 bytes
        self.assertEqual((r[0xB6], r[0xB7]), (0x00, 0xBC))
        self.assertEqual(m.flags(), F_EXT | F_RUN | F_16 | F_AUTO)
        self.assertEqual(m.chip.dma_log, [])
        self.assertEqual(m.mix_get(0x81), 0x02)

    def test_dma_select(self):
        """mixer 81h picks the 16-bit channel, as on a Sound Blaster 16"""
        m = self.m
        self.assertEqual(m.mix_get(0x81), 0x22)
        m.mix_set(0x81, 0x02)                   # none: 16-bit on the 8-bit one
        self.assertEqual(m.mix_get(0x81), 0x02)
        m.dsp(0xB6, 0x30, 0xFF, 0x03)
        self.assertEqual(m.chip.dma_log, [])
        m.mix_set(0x81, 0x42)                   # channel 6
        self.assertEqual(m.mix_get(0x81), 0x42)
        game_dma(m.chip, 6, 0x0800, 0x00FF, 0x05, SINGLE | PLAY)
        m.dsp(0xB2, 0x30, 0xFF, 0x03)           # once
        self.assertEqual(m.chip.dma[1], {"mask": False, "mode": 0x09,
                                         "addr": 0x1000, "count": 0x01FF,
                                         "page": 0x04})
        m.mix_set(0x81, 0x82)
        self.assertEqual(m.data8("dma16"), 7)
        self.assertEqual(m.chip.mixer[0x81], 0)  # never the chip's

    def test_mirror_above_64k(self):
        """a word address with bit 15 set: byte address bit 16, the page"""
        m = self.m
        game_dma(m.chip, 5, 0x9234, 0x0FFF, 0x02, SINGLE | AUTO | PLAY)
        m.dsp(0xB6, 0x30, 0xFF, 0x1F)
        self.assertEqual(m.chip.dma[1], {"mask": False, "mode": 0x19,
                                         "addr": 0x2468, "count": 0x1FFF,
                                         "page": 0x03})

    def test_mirror_single_mode(self):
        """/S: single transfers on the card's channel and on the chip's
        side (B9h 01h, as the data sheet's register page has it)"""
        m = self.install(tail="/S")
        game_dma(m.chip, 5, 0x0100, 0x00FF, 0x00, SINGLE | AUTO | PLAY)
        m.dsp(0xB6, 0x30, 0xFF, 0x01)
        self.assertEqual(m.chip.dma[1]["mode"], 0x59)
        self.assertEqual(regs(m)[0xB9], 0x01)
        m.chip.commands = []
        m.dsp(0xC6, 0x00, 0xFF, 0x03)
        self.assertNotIn(0xB9, regs(m))         # the chip has it already

    def test_mirror_order(self):
        """the card's channel is masked while it is programmed, and the
        game's flip-flop is left cleared"""
        m = self.m
        game_dma(m.chip, 5, 0x1234, 0x07FF, 0x03, SINGLE | AUTO | PLAY)
        m.chip.ff = [1, 1]
        m.dsp(0xB6, 0x30, 0xFF, 0x07)
        log = m.chip.dma_log
        self.assertEqual(log[0], (0xD8, log[0][1]))
        self.assertEqual([p for p, _v in log if p == 0x0A], [0x0A, 0x0A])
        self.assertEqual([v for p, v in log if p == 0x0A], [0x05, 0x01])
        self.assertEqual(log[-1], (0x0A, 0x01))
        self.assertEqual(m.chip.ff, [0, 0])

    def test_reset(self):
        m = self.m
        m.dsp(0xB6, 0x30, 0xFF, 0x07)
        m.dsp(0xE1)
        m.reset()
        self.assertEqual(m.flags(), 0)
        self.assertEqual(m.chip.ports[6], 0)
        self.assertEqual(m.status() & 0x80, 0x80)
        self.assertEqual(m.dsp_read(), 0xAA)    # the chip's, not 04h
        self.assertEqual(m.status() & 0x80, 0)

    def test_reset_in_a_command(self):
        m = self.m
        m.dsp(0xB6, 0x30)                       # a command cut short
        m.reset()
        m.dsp(0xE1)
        self.assertEqual((m.dsp_read(), m.dsp_read()), (4, 5))

    def test_chip_commands_pass(self):
        m = self.m
        m.dsp(0x14, 0xFF, 0x03)
        m.dsp(0x10, 0x80)
        m.dsp(0xD1)
        m.dsp(0xE0, 0x55)
        m.dsp(0x48, 0x00, 0x04)
        m.dsp(0x1C)
        self.assertEqual(m.chip.commands, [(0x14, 0xFF, 0x03), (0x10, 0x80),
                                           (0xD1,), (0xE0, 0x55),
                                           (0x48, 0x00, 0x04), (0x1C,)])
        self.assertEqual(m.flags(), F_8)
        self.assertEqual(m.dsp_read(), 0xAA)    # E0h's answer from the chip

    def test_ess_program(self):
        """a program that knows the chip gets the chip"""
        m = self.m
        m.dsp(0xE7)
        self.assertEqual((m.dsp_read(), m.dsp_read()), (0x68, 0x89))
        self.assertEqual(m.flags(), F_ESS)
        m.chip.commands = []
        m.dsp(0xC6)
        m.dsp(0xA1, 0x6E)
        m.dsp(0x41, 0x12)                       # 41h: the chip's, 1 operand
        m.dsp(0xC0, 0xA1)
        self.assertEqual(m.dsp_read(), 0x6E)
        m.dsp(0xB6, 0x80)
        m.dsp(0xC7)
        self.assertEqual(m.chip.commands, [(0xC6,), (0xA1, 0x6E), (0x41, 0x12),
                                           (0xC0, 0xA1), (0xB6, 0x80), (0xC7,)])
        self.assertEqual(m.flags(), F_ESS)
        m.mix_set(0x30, 0x55)                   # the chip's mixer as it is
        self.assertEqual(m.chip.mixer[0x30], 0x55)
        self.assertEqual(m.mix_get(0x30), 0x55)
        m.mix_set(0x7A, 0x80)
        self.assertEqual(m.chip.mixer[0x7A], 0x80)
        self.assertEqual(m.chip.dma_log, [])

    def test_c6_is_the_chips(self):
        """C6h followed by a command byte: Extended mode, not a transfer"""
        m = self.m
        m.dsp(0xC6, 0xA1, 0x6E)
        self.assertEqual(m.chip.commands, [(0xC6,), (0xA1, 0x6E)])
        self.assertEqual(m.flags(), F_ESS | F_EXT)
        m.dsp(0xC7)
        self.assertEqual(m.chip.commands[-1], (0xC7,))
        self.assertEqual(m.flags(), F_ESS)

    def test_mixer_view(self):
        """the Sound Blaster 16's volumes become the chip's, from the
        reset's"""
        m = self.m
        m.mix_set(0x00, 0x00)
        self.assertEqual([m.mix_get(r) for r in range(0x30, 0x48)],
                         [0xC0] * 6 + [0] * 6 + [0x1F, 0x15, 0x0B] + [0] * 5
                         + [0x80] * 4)
        m.mix_set(0x30, 0xF0)
        m.mix_set(0x31, 0x80)
        self.assertEqual((m.mix_get(0x30), m.mix_get(0x31)), (0xF0, 0x80))
        self.assertEqual(self.mixer(0x60, 0x62), [0x3C, 0x20])
        m.mix_set(0x32, 0xA0)
        m.mix_set(0x33, 0x50)
        self.assertEqual(self.mixer(0x14, 0x7C), [0xA5, 0x00])
        m.mix_set(0x34, 0x70)
        m.mix_set(0x35, 0x30)
        self.assertEqual(self.mixer(0x36), [0x73])
        m.mix_set(0x36, 0xF0)                   # CD: through the switches
        m.mix_set(0x37, 0xF0)
        self.assertEqual(self.mixer(0x38), [0xFF])
        m.mix_set(0x3C, 0x1F & ~0x02)           # CD right off
        self.assertEqual(self.mixer(0x38), [0xF0])
        m.mix_set(0x38, 0x80)                   # line
        m.mix_set(0x39, 0x40)
        self.assertEqual(self.mixer(0x3E), [0x84])
        m.mix_set(0x3A, 0xE0)                   # mic
        self.assertEqual(self.mixer(0x1A), [0xEE])
        m.mix_set(0x3C, 0x1F & ~0x01)           # mic off
        self.assertEqual(self.mixer(0x1A), [0x00])
        m.mix_set(0x3B, 0xC0)                   # the PC speaker
        self.assertEqual(self.mixer(0x3C), [0x06])
        self.assertEqual(m.chip.commands, [])

    def test_record_source(self):
        m = self.m
        m.chip.mixer[0x1C] = 0x08
        m.mix_set(0x3D, 0x01)                   # the mic, alone
        m.mix_set(0x3E, 0x01)
        self.assertEqual(self.mixer(0x1C), [0x08])
        m.mix_set(0x3D, 0x04)                   # CD
        m.mix_set(0x3E, 0x02)
        self.assertEqual(self.mixer(0x1C), [0x0A])
        m.mix_set(0x3D, 0x10)                   # line
        m.mix_set(0x3E, 0x08)
        self.assertEqual(self.mixer(0x1C), [0x0E])
        m.mix_set(0x3D, 0x40)                   # the music
        m.mix_set(0x3E, 0x20)
        self.assertEqual(self.mixer(0x1C), [0x0D])
        m.mix_set(0x3D, 0x15)                   # several: the record mixer
        self.assertEqual(self.mixer(0x1C), [0x0D])

    def test_the_mixer_a_game_finds(self):
        """a Sound Blaster 16 shows the levels it has: the chip's"""
        m = self.install(chip=mixer_chip())
        view = [m.mix_get(r) for r in range(0x30, 0x48)]
        self.assertEqual(view, [
            0xE8, 0x00,                 # master, the right muted
            0xA0, 0xB0, 0xC0, 0xD0,     # voice, MIDI
            0x10, 0x20, 0x30, 0x40,     # CD, line
            0x50, 0xA0,                 # mic, PC speaker
            0x1F, 0x10, 0x08,           # the switches: line records
            0, 0, 0, 0, 0, 0x80, 0x80, 0x80, 0x80])
        # writing back what it read changes nothing; each register goes to
        # its own, so the muted right master stays muted
        before = list(m.chip.mixer)
        for r, v in zip(range(0x30, 0x48), view):
            if r != 0x31:               # the muted master can't come back
                m.mix_set(r, v)         # muted
        self.assertEqual([(r, v) for r, v in enumerate(m.chip.mixer)
                          if v != before[r]], [])

    def test_mixer_reset(self):
        """a game's mixer reset gives the chip the Sound Blaster 16's reset
        levels and leaves the chip's other registers"""
        m = self.install(chip=mixer_chip())
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
        for reg in (0x50, 0x52, 0x71, 0x7C):
            self.assertEqual(hw[reg], DOS_MIXER[reg], hex(reg))

    def test_the_sound_blaster_pros_volumes(self):
        """the Sound Blaster Pro's registers are the chip's, as they come,
        and the Sound Blaster 16's view follows them when the game uses
        it"""
        m = self.install(chip=mixer_chip())
        m.chip.sbpro_alias = True
        m.chip.log = []
        m.mix_set(0x04, 0x99)                       # voice
        self.assertEqual(m.chip.log, [("out", BASE + 4, 0x04),
                                      ("out", BASE + 5, 0x99)])
        self.assertEqual(m.chip.mixer[0x14], 0x88)
        self.assertEqual((m.mix_get(0x32), m.mix_get(0x33)), (0x80, 0x80))
        m.mix_set(0x22, 0xF5)                       # master
        self.assertEqual((m.chip.mixer[0x60], m.chip.mixer[0x62]),
                         (0x3F, 0x15))
        self.assertEqual((m.mix_get(0x30), m.mix_get(0x31)), (0xFC, 0x54))
        m.mix_set(0x3C, 0x1F & ~0x04)               # CD left off
        m.mix_set(0x28, 0xEE)                       # CD: to the chip
        self.assertEqual(m.chip.mixer[0x38], 0xEE)  # as it comes
        self.assertEqual((m.mix_get(0x36), m.mix_get(0x37)), (0xE0, 0xE0))
        m.mix_set(0x3C, 0x1F & ~0x04)               # gated again by
        self.assertEqual(m.chip.mixer[0x38], 0x0E)  # the next write
        m.mix_set(0x0A, 0x04)                       # mic: AAh
        self.assertEqual(m.mix_get(0x3A), 0xA0)
        self.assertEqual(m.chip.mixer[0x1A], 0xAA)

    def test_the_chips_own_volumes(self):
        """a mixer program sets the chip's own registers between games, or
        a game that knows the chip does: the next view shows them"""
        m = self.install(chip=mixer_chip())
        m.chip.log = []
        for reg, v in ((0x60, 0x20), (0x14, 0x5A), (0x1A, 0x99),
                       (0x1C, 0x12)):
            m.mix_set(reg, v)
            self.assertEqual(m.chip.mixer[reg], v)
        self.assertEqual([e for e in m.chip.log if e[0] == "in"], [])
        self.assertEqual([m.mix_get(r) for r in (0x30, 0x31, 0x32, 0x33,
                                                 0x3A, 0x3D, 0x3E)],
                         [0x80, 0x00, 0x50, 0xA0, 0x90, 0x04, 0x02])
        m.dsp(0xE7)
        self.assertEqual((m.dsp_read(), m.dsp_read()), (0x68, 0x89))
        m.chip.sbpro_alias = True
        m.mix_set(0x36, 0x77)                       # the chip's FM volume
        self.assertEqual(m.chip.mixer[0x36], 0x77)
        m.mix_set(0x32, 0x88)                       # and its master
        self.assertEqual(m.chip.mixer[0x60], 0x22)
        m.reset()                                   # the next game
        self.assertEqual(m.dsp_read(), 0xAA)
        self.assertEqual([m.mix_get(r) for r in (0x30, 0x34, 0x35)],
                         [0x88, 0x70, 0x70])
        m.dsp(0xE7)                                 # and one that knows the
        m.mix_set(0x00, 0x00)                       # chip resets the mixer,
        self.assertEqual(m.chip.mixer[0x00], 0x00)  # which gives the chip
        m.chip.mixer[0x14] = 0x88                   # its power-on levels
        m.chip.mixer[0x3C] = 0x04
        m.reset()
        self.assertEqual(m.dsp_read(), 0xAA)
        self.assertEqual([m.mix_get(r) for r in (0x32, 0x3B)], [0x80, 0x80])

    def test_a_transfer_like_the_last(self):
        """the registers the chip has already aren't written again, and
        none is read again until a reset or a command of the chip's that
        may change them"""
        m = self.m
        m.dsp(0x41, 0xAC, 0x44)
        m.dsp(0xB6, 0x30, 0xFF, 0x0F)
        m.chip.commands = []
        m.dsp(0xB6, 0x30, 0xFF, 0x0F)
        self.assertEqual(m.chip.commands,
                         [(0xB8, 0x04), (0xB8, 0x05), (0xD1,)])
        m.chip.commands = []
        m.dsp(0x41, 0x56, 0x22)                 # 22050 Hz, 2048 samples
        m.dsp(0xB6, 0x30, 0xFF, 0x07)
        self.assertEqual(m.chip.commands, [
            (0xB8, 0x04), (0xA1, 0x6E), (0xA2, 0xF7), (0xA5, 0xF0),
            (0xB8, 0x05), (0xD1,)])
        m.chip.commands = []
        m.dsp(0xB2, 0x10, 0xFF, 0x07)           # once, mono: B8h, A8h, the
        self.assertEqual(m.chip.commands, [     # format
            (0xB8, 0x00), (0xA8, 0x02), (0xB6, 0x00), (0xB7, 0x71),
            (0xB7, 0xF4), (0xB8, 0x01), (0xD1,)])
        self.assertEqual(m.chip.ctrl, {**m.chip.ctrl, 0xA1: 0x6E,
                                       0xA2: 0xF7, 0xA4: 0x00, 0xA5: 0xF0,
                                       0xB6: 0x00, 0xB7: 0xF4, 0xA8: 0x02,
                                       0xB8: 0x01})
        # the speaker and pause go through and change nothing the front
        # knows; the time constant may: the next transfer reads again
        m.dsp(0xD1, 0xD0, 0xD4)
        m.chip.commands = []
        m.dsp(0xB2, 0x10, 0xFF, 0x07)
        self.assertNotIn(0xC0, [c[0] for c in m.chip.commands])
        m.dsp(0x40, 0xA5)
        m.chip.commands = []
        m.dsp(0xB2, 0x10, 0xFF, 0x07)
        self.assertEqual(m.chip.commands[0], (0xC0, 0xB8))
        # a reset: the chip's registers are its reset values again
        m.reset()
        self.assertEqual(m.dsp_read(), 0xAA)
        m.chip.commands = []
        m.dsp(0xB2, 0x10, 0xFF, 0x07)
        cmds = [c[0] for c in m.chip.commands]
        self.assertEqual(cmds[:2], [0xC6, 0xC0])
        self.assertEqual(cmds.count(0xC0), 4)   # B8h, A8h, B1h, B2h
        # the stop of an auto-initializing transfer writes B8h, no read
        m.dsp(0xB6, 0x30, 0xFF, 0x0F)
        m.chip.commands = []
        m.dsp(0xD9)
        self.assertEqual(m.chip.commands, [(0xB8, 0x01)])

    def test_mixer_passthrough(self):
        m = self.m
        m.mix_set(0x14, 0x77)
        self.assertEqual(m.chip.mixer[0x14], 0x77)
        self.assertEqual(m.mix_get(0x14), 0x77)
        self.assertEqual(m.inp(BASE + 4), 0x14)
        m.chip.log = []
        m.mix_set(0x80, 0x0F)                   # the card's IRQ: not writable
        m.mix_set(0x82, 0xFF)
        self.assertNotIn(("out", BASE + 5, 0x0F), m.chip.log)
        self.assertNotIn(("out", BASE + 5, 0xFF), m.chip.log)
        self.assertEqual((m.mix_get(0x80), m.mix_get(0x82)), (0x02, 0x00))

    def test_card_registers(self):
        m = self.install(chip=Chip(irq=7, dma8=3), tail="/H6")
        self.assertEqual((m.mix_get(0x80), m.mix_get(0x81)), (0x04, 0x48))
        m = self.install(chip=Chip(irq=10, dma8=0), tail="/H7")
        self.assertEqual((m.mix_get(0x80), m.mix_get(0x81)), (0x08, 0x81))

    def test_other_ports(self):
        """ports of the range that aren't the front's go on"""
        m = self.m
        self.assertEqual(m.inp(BASE + 6), 0x5A)
        self.assertEqual(m.chained, [(BASE + 6, 0, 0xA5)])
        m = installed(old_callback=False, qpiemu=True)
        m.chip.ports[6] = 0x33
        self.assertEqual(m.inp(BASE + 6), 0x33)
        self.assertEqual(m.qpi_accesses, [("in", BASE + 6, 0x33)])
        self.assertEqual(m.inp(BASE + 0xC), 0x00)
        m.chip.out = [0x12]
        self.assertEqual(m.inp(BASE + 0xC), 0x40)
        self.assertEqual(m.chained, [])

    def test_queue_full(self):
        m = self.m
        m.dsp(0xE3)
        m.dsp(0xE3)
        m.dsp(0xE1)
        n = 0
        while m.status() & 0x80:
            m.dsp_read()
            n += 1
        self.assertEqual(n, 48)

    def test_emm386_handler(self):
        """the same front through EMM386's handler at ring 0, which passes
        the ports it doesn't answer back to EMM386"""
        m = installed(emm386=(4, 95), noems=True)
        m.path = "emm"
        m.dsp(0xE1)
        self.assertEqual((m.dsp_read(), m.dsp_read()), (4, 5))
        game_dma(m.chip, 5, 0x1234, 0x07FF, 0x03, SINGLE | AUTO | PLAY)
        m.dsp(0x41, 0xAC, 0x44)
        m.dsp(0xB6, 0x30, 0xFF, 0x07)
        r = regs(m)
        self.assertEqual((r[0xA1], r[0xB7]), (0xEE, 0xBC))
        self.assertEqual(m.chip.dma[1]["addr"], 0x2468)
        self.assertEqual(m.chained, [])
        self.assertEqual(m.emm_io(BASE + 6), (0xA5, True))
        for o in (7, 8, 9, 0xB, 0xD):               # in the range, passed
            self.assertEqual(m.emm_io(BASE + o, 0x12)[1], True)
            self.assertEqual(m.emm_io(BASE + o)[1], True)
        self.assertEqual(m.emm_io(BASE + 0x10), (None, True))   # not trapped
        self.assertEqual(m.chained, [])

    def test_emm386_out_is_8(self):
        """EMM386's documented direction for a byte out, 8 (4 in 6.22)"""
        m = installed(emm386=(4, 95), noems=True)
        m.path, m.emm_out = "emm", 8
        m.dsp(0xE1)
        self.assertEqual((m.dsp_read(), m.dsp_read()), (4, 5))
        m.mix_set(0x30, 0xF0)
        self.assertEqual(m.chip.mixer[0x60], 0x3C)

    def test_stack(self):
        """the front runs on its own stack, below the game's"""
        m = self.m
        game_dma(m.chip, 5, 0x1234, 0x07FF, 0x03, SINGLE | AUTO | PLAY)
        m.dsp(0x41, 0xAC, 0x44)
        m.dsp(0xB6, 0x30, 0xFF, 0x07)
        used = m.mem(m.addr("stack"), m.sym("stack_top") - m.sym("stack"))
        depth = len(used.lstrip(b"\0"))        # it grows down from the top
        self.assertGreater(depth, 0)
        self.assertLess(depth, 160)


@unittest.skipUnless(HAVE_UNICORN and have_nasm(), "needs nasm and unicorn")
class EMM386FrontTest(FrontTest):
    """the Sound Blaster 16 through EMM386's handler at ring 0"""

    def install(self, **kw):
        m = installed(emm386=(4, 95), noems=True, **kw)
        m.path = "emm"
        return m

    def test_other_ports(self):
        """the ports QPI chains are EMM386's to do"""
        m = self.m
        m.chip.ports[6] = 0x33
        self.assertEqual(m.inp(BASE + 6), 0x33)
        self.assertEqual(m.chained, [(BASE + 6, 0, 0x33)])

    def test_stack(self):
        """the front runs on EMM386's stack, not its own"""
        m = self.m
        m.dsp(0xB6, 0x30, 0xFF, 0x07)
        used = m.mem(m.addr("stack"), m.sym("stack_top") - m.sym("stack"))
        self.assertEqual(used.lstrip(b"\0"), b"")


@unittest.skipUnless(HAVE_UNICORN and have_nasm(), "needs nasm and unicorn")
class HDPMITest(unittest.TestCase):
    """the front through HDPMI32i's handler at ring 3"""

    def setUp(self):
        self.m = installed()
        self.pm = PMMachine(self.m)
        self.pm.fake_install()

    def test_version(self):
        pm = self.pm
        pm.hdpmi_io(BASE + 0xC, 0xE1)
        self.assertEqual(pm.hdpmi_io(BASE + 0xE) & 0x80, 0x80)
        self.assertEqual((pm.hdpmi_io(BASE + 0xA), pm.hdpmi_io(BASE + 0xA)),
                         (4, 5))
        self.assertEqual(pm.vendor_io, [("in", BASE + 0xE, 0)])

    def test_16bit_transfer(self):
        """the chip and the DMA controllers through HDPMI's untrapped I/O"""
        pm = self.pm
        game_dma(self.m.chip, 5, 0x1234, 0x07FF, 0x03, SINGLE | AUTO | PLAY)
        for b in (0x41, 0xAC, 0x44, 0xB6, 0x30, 0xFF, 0x07):
            pm.hdpmi_io(BASE + 0xC, b)
        r = regs(self.m)
        self.assertEqual((r[0xA1], r[0xB7], r[0xB8]), (0xEE, 0xBC, 0x05))
        self.assertEqual(self.m.chip.dma[1], {"mask": False, "mode": 0x19,
                                              "addr": 0x2468, "count": 0x0FFF,
                                              "page": 0x02})
        self.assertEqual(pm.data8("how_io"), 0)
        self.assertIn(("out", 0x0B, 0x19), pm.vendor_io)
        self.assertIn(("in", 0x8B, 0x03), pm.vendor_io)
        self.assertEqual(pm.ports, [])

    def test_other_ports(self):
        pm = self.pm
        self.m.chip.ports[6] = 0x33
        self.assertEqual(pm.hdpmi_io(BASE + 6), 0x33)
        self.assertEqual(pm.vendor_io, [("in", BASE + 6, 0x33)])
        pm.hdpmi_io(BASE + 4, 0x14)
        self.assertEqual(self.m.chip.index, 0x14)
        self.assertEqual(pm.vendor_io[-1], ("out", BASE + 4, 0x14))

    def test_mixer(self):
        pm = self.pm
        pm.hdpmi_io(BASE + 4, 0x30)
        pm.hdpmi_io(BASE + 5, 0xF0)
        self.assertEqual(self.m.chip.mixer[0x60], 0x3C)
        pm.hdpmi_io(BASE + 4, 0x82)
        self.assertEqual(pm.hdpmi_io(BASE + 5), 0)
        self.assertEqual(self.m.chip.index, 0x82)


if __name__ == "__main__":
    unittest.main()
