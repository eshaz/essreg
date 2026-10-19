# (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
#
# Licensed under GPL Version 3.0
"""esssb16.com in a CPU emulator: the installer under QEMM, QPIEMU, EMM386
and a DPMI host, its refusals, and the Sound Blaster 16 front through the
QPI callback, EMM386's handler and HDPMI32i's handler against the chip
model: the DSP's answers, 16-bit transfers on Audio 2, 8-bit ones on
Audio 1, the mixer's view, the interrupts, resets, and the chip's own
commands going through.

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
                        PSP_PARA, ENV_PARA, STUB_PARA, HOST_PARA, OLD_OFF,
                        STUB32, VENDOR_OFF, SEL_DATA, SEL_CODE32, SEL_NEW)

E_USAGE, E_NOCARD, E_NOTRAP, E_WINDOWS, E_LOADED = 1, 2, 3, 4, 5
F_ESS, F_EXT, F_16, F_8, F_RUN, F_AUTO, F_A2, F_RESET = (
    1, 2, 4, 8, 0x10, 0x20, 0x40, 0x80)
BASE = 0x220
_BUILD = {}


def build():
    if "image" not in _BUILD:
        _BUILD["image"], _BUILD["syms"] = build_esssb16.build()
    return _BUILD["image"], _BUILD["syms"]


def paragraphs(syms):
    return (syms["resident_end"] + 15) >> 4


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
    m.chip.log, m.chip.commands, m.chained = [], [], []
    return m


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


class NoChannel(Chip):
    """an ES1869 that refuses the 16-bit channel for Audio 2"""

    def outb(self, port, v):
        if port == self.cfg + 1 and self.cfg_index == 0x75:
            return
        super().outb(port, v)


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


@unittest.skipUnless(HAVE_UNICORN and have_nasm(), "needs nasm and unicorn")
class InstallTest(unittest.TestCase):
    def test_qpiemu(self):
        m = machine()
        m.install(qpiemu=True)
        self.assertEqual(m.exit, ("tsr", paragraphs(m.syms)))
        self.assertIn("ES1869 at 220h, IRQ 5, DMA 1 and 5", m.output)
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
        # the card: Audio 2 on the 16-bit channel and the card's IRQ, its
        # DAC's mode, its volume as Audio 1's
        self.assertEqual(m.chip.pnp[(1, 0x75)], 5)
        self.assertEqual(m.chip.pnp[(1, 0x72)], 5)
        self.assertEqual(m.chip.mixer[0x71] & 0x1E, 0x0E)
        self.assertEqual(m.chip.mixer[0x7C], 0xCC)
        self.assertEqual((m.data16("base"), m.data16("cfg"), m.data8("irq"),
                          m.data8("dma8"), m.data8("dma16")),
                         (0x220, 0x250, 5, 1, 5))
        self.assertEqual(m.chip.commands, [(0xE7,)])

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
        self.assertIn("no port traps", m.output)

    def test_qpi_failure_falls_back(self):
        m = machine()
        m.qpi_fail = {0x1A09}
        m.install(qpiemu=True, emm386=(4, 49))
        self.assertEqual(m.exit[0], "tsr")
        self.assertIn("traps: EMM386", m.output)

    def test_emm386(self):
        m = machine()
        m.install(emm386=(4, 49))
        self.assertEqual(m.exit[0], "tsr")
        self.assertIn("traps: EMM386 for real-mode games", m.output)
        shim, skip = m.sym("emm_shim"), m.sym("emm_pass")
        self.assertEqual(len(m.emm_traps), 2)
        first, second = m.emm_traps
        self.assertEqual((first["first"], first["last"], first["count"]),
                         (BASE + 4, BASE + 6, 3))
        self.assertEqual(first["table"], [(BASE + 4, shim), (BASE + 5, shim),
                                          (BASE + 6, shim)])
        self.assertEqual((second["first"], second["last"], second["count"]),
                         (BASE + 0xA, BASE + 0xF, 6))
        self.assertEqual(second["table"],
                         [(BASE + 0xA, shim), (BASE + 0xB, skip),
                          (BASE + 0xC, shim), (BASE + 0xD, skip),
                          (BASE + 0xE, shim), (BASE + 0xF, shim)])
        self.assertEqual(first["size"], m.sym("resident_end"))

    def test_emm386_noems(self):
        """Windows 98's EMM386 with NOEMS: the device is EMMQXXX0"""
        m = machine()
        m.install(emm386=(4, 95), noems=True)
        self.assertEqual(m.exit[0], "tsr")
        self.assertIn("traps: EMM386 for real-mode games", m.output)
        self.assertEqual(len(m.emm_traps), 2)

    def test_emm386_one_table(self):
        m = machine()
        m.emm_fail = {1}
        m.install(emm386=(4, 49))
        self.assertEqual(m.exit[0], "tsr")
        self.assertEqual(len(m.emm_traps), 3)
        third = m.emm_traps[2]
        self.assertEqual((third["first"], third["last"], third["count"]),
                         (BASE + 4, BASE + 0xF, 12))
        self.assertEqual([p for p, _h in third["table"]],
                         list(range(BASE + 4, BASE + 0x10)))

    def test_emm386_too_old(self):
        m = machine()
        m.install(emm386=(4, 45))
        self.assertEqual(m.exit, ("exit", E_NOTRAP))
        self.assertEqual(m.emm_traps, [])

    def test_no_memory_manager(self):
        m = machine()
        m.install()
        self.assertEqual(m.exit, ("exit", E_NOTRAP))

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
        self.assertIn("ES1869 at 240h, IRQ 5, DMA 1 and 6", m.output)
        self.assertIn("set BLASTER=A240 I5 D1 H6", m.output)
        self.assertEqual(m.data8("single"), 1)
        self.assertEqual(m.chip.pnp[(1, 0x75)], 6)
        self.assertEqual(m.qpi_trapped, {0x240 + o for o in
                                         (4, 5, 6, 0xA, 0xC, 0xE, 0xF)})

    def test_blaster(self):
        """BLASTER's A and H; I and D come from the card's PnP registers"""
        m = machine(Chip(base=0x240, irq=7, dma8=3),
                    env="PATH=C:\\DOS;C:\\GAMES\0BLASTER=A240 I10 D0 H7 T6")
        m.install(qpiemu=True)
        self.assertEqual(m.exit[0], "tsr")
        self.assertIn("ES1869 at 240h, IRQ 7, DMA 3 and 7", m.output)
        self.assertIn("set BLASTER=A240 I7 D3 H7", m.output)

    def test_no_blaster(self):
        m = machine(env="PATH=C:\\")
        m.install(qpiemu=True)
        self.assertEqual(m.exit[0], "tsr")
        self.assertIn("ES1869 at 220h, IRQ 5, DMA 1 and 5", m.output)

    def test_no_config_port(self):
        m = machine(NoConfig(), env="BLASTER=A220 I7 D3 H6")
        m.install(qpiemu=True)
        self.assertEqual(m.exit[0], "tsr")
        self.assertEqual(m.data16("cfg"), 0)
        self.assertIn("ES1869 at 220h, IRQ 7, DMA 3 and 6", m.output)
        cfg = m.chip.cfg
        self.assertEqual([e for e in m.chip.log if e[1] in (cfg, cfg + 1)], [])

    def test_no_16bit_channel(self):
        for chip, tail in ((NoChannel(), ""), (Chip(), "/H0")):
            m = machine(chip)
            m.install(tail=tail, qpiemu=True)
            self.assertEqual(m.exit[0], "tsr")
            self.assertEqual(m.data8("dma16"), 0)
            self.assertIn("IRQ 5, DMA 1\r\n", m.output)
            self.assertIn("set BLASTER=A220 I5 D1 P330 T6", m.output)
            self.assertEqual(m.chip.pnp[(1, 0x75)], 4)

    def test_dpmi(self):
        """the DPMI host: the switch, the handler's selector, HDPMI32i's
        traps, then the TSR through a real-mode int 21h"""
        m = machine()
        m.install(qpiemu=True, dpmi=True)
        self.assertEqual(m.stopped, "dpmi")
        self.assertEqual(m.resized, [(PSP_PARA, 0x1000)])
        self.assertEqual(m.allocated, [m.dpmi_paras])
        self.assertEqual(m.dpmi_call[:2], (1, HOST_PARA))
        pm = PMMachine(m)
        rmcs = pm.install(m)
        fns = [c[0] for c in pm.dpmi_calls]
        self.assertEqual(fns, [0x0000, 0x0007, 0x0008, 0x0009, 0x0300])
        regs = dict(pm.dpmi_calls)
        self.assertEqual(regs[0x0000]["ecx"] & 0xFFFF, 1)
        for fn in (0x0007, 0x0008, 0x0009):
            self.assertEqual(regs[fn]["ebx"] & 0xFFFF, SEL_NEW)
        self.assertEqual((regs[0x0007]["ecx"] & 0xFFFF,
                          regs[0x0007]["edx"] & 0xFFFF),
                         (PSP_PARA * 16 >> 16, PSP_PARA * 16 & 0xFFFF))
        self.assertEqual((regs[0x0008]["ecx"] & 0xFFFF,
                          regs[0x0008]["edx"] & 0xFFFF), (0, 0xFFFF))
        self.assertEqual(regs[0x0009]["ecx"] & 0xFFFF, 0x00FA)
        traps = [(r["esi"] & 0xFFFF, r["edi"] & 0xFFFF, r["ecx"] & 0xFFFF,
                  r["edx"], r["ebx"] & 0xFFFF)
                 for fn, r in pm.vendor_calls if fn == 6]
        self.assertEqual(traps, [(BASE + o, n, SEL_NEW, m.sym("hdpmi_shim"),
                                  SEL_DATA)
                                 for o, n in ((4, 3), (0xA, 1), (0xC, 1),
                                              (0xE, 2))])
        self.assertIn("HDPMI32i for protected-mode games", pm.output)
        self.assertEqual(pm.data16("pm_data"), SEL_DATA)
        self.assertEqual(pm.mem(pm.addr("vendor"), 6),
                         (STUB32 + VENDOR_OFF).to_bytes(4, "little") +
                         SEL_CODE32.to_bytes(2, "little"))
        eax = int.from_bytes(rmcs[0x1C:0x20], "little")
        edx = int.from_bytes(rmcs[0x14:0x18], "little")
        self.assertEqual((eax, edx), (0x3100, paragraphs(m.syms)))
        self.assertEqual(rmcs[0x2E:0x32], b"\0\0\0\0")      # SS:SP: the host's

    def test_dpmi_without_hdpmi(self):
        m = machine()
        m.install(qpiemu=True, dpmi=True)
        pm = PMMachine(m)
        pm.hdpmi = False
        rmcs = pm.install(m)
        self.assertEqual([fn for fn, _r in pm.vendor_calls], [])
        self.assertIn("play 8-bit without HDPMI32i", pm.output)
        self.assertEqual(int.from_bytes(rmcs[0x1C:0x20], "little"), 0x3100)

    def test_dpmi_no_memory(self):
        m = machine()
        m.alloc_fail = True
        m.install(qpiemu=True, dpmi=True)
        self.assertEqual(m.exit, ("tsr", paragraphs(m.syms)))
        self.assertIn("play 8-bit without HDPMI32i", m.output)


@unittest.skipUnless(HAVE_UNICORN and have_nasm(), "needs nasm and unicorn")
class FrontTest(unittest.TestCase):
    """the Sound Blaster 16 through the QPI callback"""

    def setUp(self):
        self.m = installed()

    def mixer(self, *regs):
        return [self.m.chip.mixer[r] for r in regs]

    def a2_regs(self):
        return self.mixer(0x70, 0x71, 0x72, 0x74, 0x76, 0x78, 0x7A, 0x7C)

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
        m = self.m
        m.dsp(0x41, 0xAC, 0x44)                 # 44100 Hz
        m.dsp(0xB6, 0x30, 0xFF, 0x07)           # 2048 samples, auto-init
        self.assertEqual(self.a2_regs(),
                         [0x6E, 0x0E, 0xFD, 0x00, 0xF0, 0x93, 0x47, 0xCC])
        self.assertEqual(m.flags(), F_A2 | F_16)
        self.assertEqual(m.chip.commands, [])
        self.assertEqual(m.mix_get(0x82), 0)

    def test_16bit_mono_unsigned_single(self):
        m = self.m
        m.dsp(0x42, 0x1F, 0x40)                 # 8000 Hz, the input rate
        m.dsp(0xB0, 0x00, 0x7F, 0x00)           # 128 samples, once
        self.assertEqual(self.a2_regs(),
                         [0xA0, 0x0E, 0xE7, 0x00, 0xFF, 0x83, 0x41, 0xCC])

    def test_rates(self):
        """70h and 72h as ES1869.DRV picks them: the closer clock"""
        m = self.m
        for rate, r70, r72 in ((48000, 0xF0, 0xFD), (44100, 0x6E, 0xFD),
                               (22050, 0x5C, 0xF7), (11025, 0x38, 0xEE),
                               (8000, 0xA0, 0xE7), (2000, 0x80, 0xCE),
                               (60000, 0xF0, 0xFD)):
            m.dsp(0x41, rate >> 8, rate & 0xFF)
            m.dsp(0xB6, 0x30, 0xFF, 0x00)
            self.assertEqual(self.mixer(0x70, 0x72), [r70, r72], rate)

    def test_time_constant(self):
        m = self.m
        m.dsp(0x40, 0xD3)                       # 22050 Hz
        m.dsp(0xB6, 0x30, 0xFF, 0x00)
        self.assertEqual(self.mixer(0x70), [0x5C])
        self.assertEqual(m.chip.commands, [(0x40, 0xD3)])
        m.dsp(0x41, 0xAC, 0x44)                 # 41h wins over 40h
        m.dsp(0xB6, 0x30, 0xFF, 0x00)
        self.assertEqual(self.mixer(0x70), [0x6E])

    def test_pause_continue_stop_16(self):
        m = self.m
        m.dsp(0xB6, 0x30, 0xFF, 0x07)
        m.dsp(0xD5)
        self.assertEqual(self.mixer(0x78), [0x00])
        m.dsp(0xD6)
        self.assertEqual(self.mixer(0x78), [0x93])
        m.dsp(0xDA)                             # the 8-bit stop: not Audio 2's
        self.assertEqual(self.mixer(0x78), [0x93])
        m.dsp(0xD9)                             # the block that plays is the last
        self.assertEqual(self.mixer(0x78), [0x83])
        self.assertEqual(m.chip.commands, [(0xDA,)])

    def test_irq16(self):
        m = self.m
        m.dsp(0xB6, 0x30, 0xFF, 0x07)
        m.chip.mixer[0x7A] |= 0x80              # the block ended
        self.assertEqual(m.mix_get(0x82), 0x02)
        self.assertEqual(m.inp(BASE + 0xF), 0xFF)
        self.assertEqual(self.mixer(0x7A), [0x47])
        self.assertEqual(m.mix_get(0x82), 0x00)
        m.dsp(0xF3)                             # the 16-bit interrupt asked
        self.assertEqual(self.mixer(0x7A), [0xC7])
        self.assertEqual(m.flags() & (F_16 | F_8), F_16)
        self.assertEqual(m.chip.commands, [])

    def test_irq8(self):
        m = self.m
        m.dsp(0xC6, 0x00, 0xFF, 0x03)
        m.chip.a1_irq = True
        self.assertEqual(m.mix_get(0x82), 0x01)
        self.assertEqual(m.inp(BASE + 0xC) & 0x01, 0x01)
        m.status()                              # acknowledged
        self.assertEqual(m.mix_get(0x82), 0x00)
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
            (0xB2, 0x50), (0xC0, 0xB8), (0xB8, 0x05), (0xD1,)])
        self.assertEqual(m.flags(), F_EXT | F_RUN | F_8 | F_AUTO)
        self.assertEqual(self.mixer(0x78), [0])

    def test_8bit_stereo_signed_once(self):
        m = self.m
        m.dsp(0x41, 0x56, 0x22)                 # 22050 Hz
        m.dsp(0xC0, 0x30, 0xFF, 0x01)           # 512 samples
        regs = {c[0]: c[1] for c in m.chip.commands if 0xA0 <= c[0] <= 0xBF}
        self.assertEqual(regs[0xB8], 0x01)      # no auto-init
        self.assertEqual(regs[0xA8], 0x01)      # stereo
        self.assertEqual((regs[0xA1], regs[0xA2]), (0x6E, 0xF7))
        self.assertEqual((regs[0xB6], regs[0xB7]), (0x00, 0xB8))
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
        self.assertEqual(m.chip.commands, [(0xC0, 0xB8), (0xB8, 0x01)])
        self.assertEqual(m.flags() & F_AUTO, 0)

    def test_recording(self):
        m = self.m
        m.dsp(0xCE, 0x00, 0xFF, 0x03)           # 8-bit input, auto-init
        regs = {c[0]: c[1] for c in m.chip.commands if 0xA0 <= c[0] <= 0xBF}
        self.assertEqual(regs[0xB8], 0x0F)
        self.assertNotIn((0xD1,), m.chip.commands)

    def test_16bit_on_audio1(self):
        """without a 16-bit channel, Audio 1 plays 16-bit on the 8-bit one"""
        m = installed(chip=Chip(), tail="/H0", qpiemu=True)
        m.dsp(0xB6, 0x30, 0xFF, 0x03)           # 1024 samples
        regs = {c[0]: c[1] for c in m.chip.commands if 0xA0 <= c[0] <= 0xBF}
        self.assertEqual((regs[0xA4], regs[0xA5]), (0x00, 0xF8))    # 2048 bytes
        self.assertEqual((regs[0xB6], regs[0xB7]), (0x00, 0xBC))
        self.assertEqual(m.flags(), F_EXT | F_RUN | F_16 | F_AUTO)
        self.assertEqual(m.chip.mixer[0x78], 0)
        self.assertEqual(m.mix_get(0x81), 0x02)

    def test_one_transfer_at_a_time(self):
        m = self.m
        m.dsp(0xB6, 0x30, 0xFF, 0x07)
        self.assertEqual(self.mixer(0x78), [0x93])
        m.dsp(0xC6, 0x00, 0xFF, 0x03)
        self.assertEqual(self.mixer(0x78, 0x7A), [0, 0])
        self.assertEqual(m.flags() & F_A2, 0)

    def test_reset(self):
        m = self.m
        m.dsp(0xB6, 0x30, 0xFF, 0x07)
        m.dsp(0xE1)
        m.reset()
        self.assertEqual(self.mixer(0x78, 0x7A), [0, 0])
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
        self.assertEqual(self.mixer(0x78), [0])
        m.mix_set(0x30, 0x55)                   # the chip's mixer as it is
        self.assertEqual(m.chip.mixer[0x30], 0x55)
        self.assertEqual(m.mix_get(0x30), 0x55)
        m.mix_set(0x7A, 0x80)
        self.assertEqual(m.chip.mixer[0x7A], 0x80)

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
        m = self.m
        self.assertEqual([m.mix_get(r) for r in range(0x30, 0x48)],
                         [0xC0] * 6 + [0] * 6 + [0x1F, 0x15, 0x0B] + [0] * 5
                         + [0x80] * 4)
        m.mix_set(0x30, 0xF0)
        m.mix_set(0x31, 0x80)
        self.assertEqual((m.mix_get(0x30), m.mix_get(0x31)), (0xF0, 0x80))
        self.assertEqual(self.mixer(0x60, 0x62), [0x3C, 0x20])
        m.mix_set(0x32, 0xA0)
        m.mix_set(0x33, 0x50)
        self.assertEqual(self.mixer(0x14, 0x7C), [0xA5, 0xA5])
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

    def test_mixer_reset(self):
        m = self.m
        m.mix_set(0x30, 0x00)
        m.mix_set(0x00, 0x00)
        self.assertEqual(m.chip.index, 0)
        self.assertEqual(m.chip.log[-1], ("out", BASE + 5, 0))
        self.assertEqual(m.mix_get(0x30), 0xC0)

    def test_mixer_passthrough(self):
        m = self.m
        m.mix_set(0x14, 0x77)
        self.assertEqual(m.chip.mixer[0x14], 0x77)
        self.assertEqual(m.mix_get(0x14), 0x77)
        self.assertEqual(m.inp(BASE + 4), 0x14)
        m.chip.log = []
        m.mix_set(0x80, 0x0F)                   # the card's: not writable
        m.mix_set(0x81, 0xFF)
        self.assertNotIn(("out", BASE + 5, 0x0F), m.chip.log)
        self.assertNotIn(("out", BASE + 5, 0xFF), m.chip.log)
        self.assertEqual((m.mix_get(0x80), m.mix_get(0x81)), (0x02, 0x22))

    def test_card_registers(self):
        m = installed(chip=Chip(irq=7, dma8=3), tail="/H6", qpiemu=True)
        self.assertEqual((m.mix_get(0x80), m.mix_get(0x81)), (0x04, 0x48))
        m = installed(chip=Chip(irq=10, dma8=0), tail="/H7", qpiemu=True)
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

    def test_single_mode(self):
        m = installed(tail="/S", qpiemu=True)
        m.dsp(0xB6, 0x30, 0xFF, 0x07)
        self.assertEqual(m.chip.mixer[0x78], 0x13)
        m.dsp(0xC6, 0x00, 0xFF, 0x03)
        self.assertIn((0xB9, 0x01), m.chip.commands)

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
        """the same front through EMM386's handler, which passes the ports
        it doesn't answer back to EMM386"""
        m = installed(emm386=(4, 49))
        m.path = "emm"
        m.dsp(0xE1)
        self.assertEqual((m.dsp_read(), m.dsp_read()), (4, 5))
        m.dsp(0x41, 0xAC, 0x44)
        m.dsp(0xB6, 0x30, 0xFF, 0x07)
        self.assertEqual([m.chip.mixer[r] for r in (0x70, 0x78, 0x7A)],
                         [0x6E, 0x93, 0x47])
        self.assertEqual(m.chained, [])
        self.assertEqual(m.emm_io(BASE + 6), (0xA5, True))
        self.assertEqual(m.chained, [])
        m._frame()
        m.w16("cs", PSP_PARA)
        m.w16("eip", m.sym("emm_pass"))
        m.w("eflags", 0x202)
        m.run(m.addr("emm_pass"))
        self.assertTrue(m.r("eflags") & 1)

    def test_stack(self):
        """the front runs on its own stack, below the game's"""
        m = self.m
        m.dsp(0x41, 0xAC, 0x44)
        m.dsp(0xB6, 0x30, 0xFF, 0x07)
        used = m.mem(m.addr("stack"), m.sym("stack_top") - m.sym("stack"))
        depth = len(used.lstrip(b"\0"))        # it grows down from the top
        self.assertGreater(depth, 0)
        self.assertLess(depth, 160)


@unittest.skipUnless(HAVE_UNICORN and have_nasm(), "needs nasm and unicorn")
class HDPMITest(unittest.TestCase):
    """the front through HDPMI32i's handler at ring 3"""

    def setUp(self):
        self.m = installed()
        self.pm = PMMachine(self.m)

    def test_version(self):
        pm = self.pm
        pm.hdpmi_io(BASE + 0xC, 0xE1)
        self.assertEqual(pm.hdpmi_io(BASE + 0xE) & 0x80, 0x80)
        self.assertEqual((pm.hdpmi_io(BASE + 0xA), pm.hdpmi_io(BASE + 0xA)),
                         (4, 5))
        self.assertEqual(pm.vendor_io, [("in", BASE + 0xE, 0)])

    def test_16bit_transfer(self):
        pm = self.pm
        for b in (0x41, 0xAC, 0x44, 0xB6, 0x30, 0xFF, 0x07):
            pm.hdpmi_io(BASE + 0xC, b)
        chip = self.m.chip
        self.assertEqual([chip.mixer[r] for r in (0x70, 0x72, 0x78, 0x7A)],
                         [0x6E, 0xFD, 0x93, 0x47])
        self.assertEqual(pm.data8("how_io"), 0)
        self.assertIn(("out", BASE + 5, 0x93), pm.vendor_io)
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
