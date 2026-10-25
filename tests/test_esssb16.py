# (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
#
# Licensed under GPL Version 3.0
"""esssb16.com in a CPU emulator: the installer under QEMM, QPIEMU, EMM386
and a DPMI host, its refusals and what it says about them, its options,
and what only this front does: its traps of the game's 16-bit channel on
each path, the ports it passes on, and its stack. What the fronts do with
the chip is tests/test_sb16front.py's, and what the game sees is
tests/test_sb16conf.py's, against the Sound Blaster 16 model. Every
resident test runs after DOS has reused the memory the TSR gave back.

usage: python3 -m unittest tests.test_esssb16   (needs nasm and unicorn)
"""

import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))

from test_vxdext import HAVE_UNICORN, have_nasm  # noqa: E402
from sb16 import tc_to_v  # noqa: E402
from sbgame import read_all, mixer, program_dma, write  # noqa: E402

import build_esssb16  # noqa: E402

if HAVE_UNICORN:
    from tsremu import (Machine, PMMachine, Chip, DeadChip,  # noqa: E402
                        PSP_PARA, ENV_PARA, STUB_PARA, OLD_OFF, STUB32,
                        VENDOR_OFF, SEL_CODE16, SEL_DATA, SEL_CODE32, POISON)
    from sbfronts import TSRCard, play16  # noqa: E402

E_USAGE, E_NOCARD, E_NOTRAP, E_WINDOWS, E_LOADED = 1, 2, 3, 4, 5
IO_DIRECT, IO_HDPMI, IO_QPI, IO_CHAIN = 0, 1, 2, 3
BASE = 0x220
SB_PORTS = {BASE + o for o in (4, 5, 6, 0xA, 0xC, 0xE, 0xF)}
DMA5_PORTS = {0xC4, 0xC6, 0x8B, 0xD4, 0xD6, 0xD8, 0xDA, 0xDC, 0xDE}
MAX_PARAS = 284         # the resident part with the PSP: 4.4 KB in MEM /C
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


def deep_calls(c):
    """the front's deepest calls with Audio 2 playing: the chip's own reset
    under a firmware command, with Audio 2's registers kept over it, and
    each of Audio 2's commands and acknowledges"""
    play16(c)
    mixer(c, 0x00, 0x00)
    program_dma(c, 1, 0x60000, 256, auto=False)
    write(c, 0xC0, 0x00, 0xFF, 0x00)
    c.wait(2000)
    write(c, 0x10, 0x80)
    write(c, 0xC6, 0x00, 0xFF, 0x03)
    mixer(c, 0x82)
    c.inp(BASE + 0x0F)
    write(c, 0xD5, 0xD6, 0xD9)
    write(c, 0xE3)
    read_all(c)


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


def half_up(x):
    return int(x + 0.5)


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


@unittest.skipUnless(have_nasm(), "needs nasm")
class BuildTest(unittest.TestCase):
    def test_build_is_current(self):
        with open(os.path.join(ROOT, "build", "esssb16.com"), "rb") as f:
            self.assertEqual(f.read(), build()[0],
                             "build/esssb16.com is out of date: run "
                             "tools/build_esssb16.py")

    def test_small(self):
        """the resident part, with the PSP, stays within its 280
        paragraphs, and the file, which LH loads whole into upper memory
        before it shrinks, within 9 KB"""
        image, syms = build()
        self.assertLess(len(image), 9216)
        self.assertLessEqual(paragraphs(syms), MAX_PARAS)
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
                     "/o5", "/2", "/22", "/24", "/2x"):
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

    def test_3d_off(self):
        """the 3-D effect bypassed at the start (mixer 50h bit 3), as after
        a hardware reset; /3 keeps it, and an effect that is off stays
        so"""
        for tail, before, after, said in (("", 0x0D, 0x05, True),
                                          ("/3", 0x0D, 0x0D, False),
                                          ("", 0x04, 0x04, False)):
            chip = Chip()
            chip.mixer[0x50] = chip.mixer[0x52] = before
            m = machine(chip)
            m.install(tail=tail, qpiemu=True)
            self.assertEqual(m.exit[0], "tsr", tail)
            self.assertEqual(m.chip.mixer[0x50], after, tail)
            self.assertEqual(m.chip.mixer[0x52], before, tail)  # the level
            self.assertEqual("3-D effect off" in m.output, said, tail)

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

    def audio2(self, dma2, tail="", irq2=0, pins=(0x10, 0x53)):
        """the program installed on a card with Audio 2 on channel dma2,
        its interrupt on irq2 and masked (Config_Base+7 bit 1 clear), its
        DMA pins wired as card registers 23h and 24h say"""
        chip = Chip()
        chip.pnp.update({(1, 0x75): dma2, (1, 0x72): irq2,
                         (None, 0x23): pins[0], (None, 0x24): pins[1]})
        chip.cfg_ports[7] = 0xFD
        chip.mixer[0x14] = 0x9A
        m = machine(chip)
        m.install(tail=tail, qpiemu=True)
        self.assertEqual(m.exit[0], "tsr", m.output)
        return m

    def test_audio2(self):
        """Audio 2 on an 8-bit channel plays the 16-bit sound: its
        interrupt goes to the card's, unmasked, it runs at its own rate
        (71h bit 1), stopped with its interrupt off, at the voice's
        volume"""
        m = self.audio2(3)
        self.assertEqual(m.data8("dma2"), 3)
        self.assertEqual(m.chip.pnp[(1, 0x72)], 5)
        self.assertEqual(m.chip.cfg_ports[7], 0xFF)
        mix = m.chip.mixer
        self.assertEqual((mix[0x71] & 0x22, mix[0x78], mix[0x7A], mix[0x7C]),
                         (0x02, 0x00, 0x00, 0x9A))
        self.assertIn("ES1869 at 220h, IRQ 5, DMA 1, the game's DMA 5 on "
                      "Audio 2's DMA 3\r\n", m.output)
        self.assertIn("set BLASTER=A220 I5 D1 H5 P330 T6", m.output)

    def test_audio2_not_its_own(self):
        """Audio 2 on a 16-bit channel, on Audio 1's, or on none: the
        16-bit sound plays on Audio 1, and the card is left as it is"""
        for dma2 in (5, 1, 4):
            m = self.audio2(dma2)
            self.assertEqual(m.data8("dma2"), 4, dma2)
            self.assertEqual(m.chip.pnp[(1, 0x72)], 0, dma2)
            self.assertEqual(m.chip.cfg_ports[7], 0xFD, dma2)
            self.assertEqual(m.chip.mixer[0x71], 0, dma2)
            self.assertIn("the game's DMA 5 through it\r\n16-bit sound plays "
                          "on Audio 1", m.output)

    def test_audio2_moved(self):
        """/23 puts Audio 2 on channel 3 when a DMA pin of the card is
        wired to it, else says that it can't"""
        m = self.audio2(5, "/23")
        self.assertEqual((m.chip.pnp[(1, 0x75)], m.data8("dma2")), (3, 3))
        m = self.audio2(5, "/20", pins=(0x11, 0x53))
        self.assertEqual((m.chip.pnp[(1, 0x75)], m.data8("dma2")), (5, 4))
        self.assertIn("Audio 2 can't have that DMA channel", m.output)

    def test_audio2_none(self):
        """/2-: no Audio 2, though it has a channel"""
        m = self.audio2(3, "/2-")
        self.assertEqual(m.data8("dma2"), 4)
        self.assertEqual(m.chip.pnp[(1, 0x72)], 0)
        self.assertEqual(m.chip.cfg_ports[7], 0xFD)
        self.assertIn("the game's DMA 5 through it\r\nset BLASTER", m.output)

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


@unittest.skipUnless(HAVE_UNICORN and have_nasm(), "needs nasm and unicorn")
class FrontTest(unittest.TestCase):
    """the front through QPI with a callback before ours"""

    path = "qpi"

    def card(self, **kw):
        return card(self.path, **kw)

    def setUp(self):
        self.c = self.card()
        self.chip = self.c.chip

    def assertClean(self):
        self.assertEqual(self.chip.violations, [])

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
        is read back at the command, and the channel that plays it
        auto-initializes, also for a single-cycle block"""
        c, chip = self.c, self.chip
        mixer(c, 0x81, 0x42)                    # channel 6
        self.assertEqual(mixer(c, 0x81), 0x42)
        self.assertEqual(c.m.data8("dma16"), 6)
        play16(c, cmd=0xB2, ch=6, addr=0x51000, auto=False)
        self.assertEqual(chip.dma[1], {"mask": False, "mode": 0x19,
                                       "addr": 0x1000, "count": 0x1FFF,
                                       "page": 0x05})
        mixer(c, 0x81, 0x02)                    # none: on the 8-bit channel
        self.assertEqual(mixer(c, 0x81), 0x02)
        self.assertEqual(chip.mixer[0x81], 0)  # never the chip's
        self.assertClean()

    def test_the_chips_own_registers(self):
        """the view stays the game's when it sets the chip's own levels"""
        c = self.c
        before = [mixer(c, r) for r in range(0x30, 0x48)]
        for reg, v in ((0x60, 0x20), (0x14, 0x5A), (0x1A, 0x99),
                       (0x1C, 0x12)):
            mixer(c, reg, v)
        self.assertEqual([mixer(c, r) for r in range(0x30, 0x48)], before)

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

    def test_stack_audio2(self):
        """the same with Audio 2 playing the 16-bit sound"""
        c = self.card(dma2=3)
        m = c.m
        lo, top = m.addr("stack"), m.addr("stack_top")
        poison_stack(m)
        deep_calls(c)
        self.assertLessEqual(stack_depth(m), top - lo - 16)
        self.assertEqual(c.chip.violations, [])


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

    def test_stack_audio2(self):
        """the deepest calls with Audio 2 playing, on HDPMI32i's frame"""
        c = card("pm", dma2=3)
        m = c.m
        lo, top = m.addr("stack"), m.addr("stack_top")
        poison_stack(m)
        deep_calls(c)
        self.assertLessEqual(stack_depth(m), top - lo - 16)
        self.assertEqual(c.chip.violations, [])


if __name__ == "__main__":
    unittest.main()
