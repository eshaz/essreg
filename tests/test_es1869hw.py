# (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
#
# Licensed under GPL Version 3.0 or later
"""The Linux driver's chip core (linux/es1869/hw.c) on the chip model.

hw.c is built for the host and called through ctypes, its struct es1869_io
on tests/linuxchip.py: the port accesses go to the model, and the 8237's
programming goes through the ports in the order asm/dma.h writes them, so
the model's DMA controllers see what the kernel would write.

What it checks: the identification and its refusals (no chip, a Sound
Blaster without ESS's answer), every clock and divisor the driver offers
against the rate the model plays, the filter values, Audio 1's start held
masked over the race that loses the first byte (with the race itself shown
when the channel is unmasked at once), Audio 2's bytes spread over the words
of DMA 5, the interrupt's mask dance, the DMA position, a recording, a
pause, and the DSP left out of step.

usage: python3 -m unittest tests.test_es1869hw   (needs gcc and unicorn)
"""

import ctypes
import os
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from test_vxdext import HAVE_UNICORN  # noqa: E402

if HAVE_UNICORN:
    from esschip import a1_hz, a2_hz, decode, RACE_US  # noqa: E402
    from linuxchip import BASE, CFG, LinuxChip, run  # noqa: E402
    from sbgame import samples16, words  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DRV = os.path.join(ROOT, "linux", "es1869")
HOST = os.path.join(ROOT, "tests", "host")
BUF = 0x40000                   # a DMA buffer, in one 128 KB page

u8, u16, u32, u64 = (ctypes.c_uint8, ctypes.c_uint16, ctypes.c_uint32,
                     ctypes.c_uint64)
vp = ctypes.c_void_p

INB = ctypes.CFUNCTYPE(u8, vp, u16)
OUTB = ctypes.CFUNCTYPE(None, vp, u16, u8)
UDELAY = ctypes.CFUNCTYPE(None, vp, ctypes.c_uint)
NOW = ctypes.CFUNCTYPE(u64, vp)
DMAPROG = ctypes.CFUNCTYPE(None, vp, ctypes.c_uint, u32, u32, u8)
DMAMASK = ctypes.CFUNCTYPE(None, vp, ctypes.c_uint, ctypes.c_bool)
DMARES = ctypes.CFUNCTYPE(u32, vp, ctypes.c_uint)

IRQ_A1, IRQ_A2, IRQ_HWV, IRQ_MPU = 0x01, 0x02, 0x04, 0x08


class IO(ctypes.Structure):
    _fields_ = [("ctx", vp), ("inb", INB), ("outb", OUTB),
                ("udelay", UDELAY), ("now_ns", NOW),
                ("dma_program", DMAPROG), ("dma_mask", DMAMASK),
                ("dma_residue", DMARES)]


class Stream(ctypes.Structure):
    _fields_ = [("rate_num", u32), ("rate_den", u32), ("bits", u8),
                ("is_signed", ctypes.c_bool), ("stereo", ctypes.c_bool),
                ("period", u32), ("ch", u8), ("dma_phys", u32),
                ("dma_bytes", u32), ("single", ctypes.c_bool),
                ("burst", u8), ("corner", u16), ("a8", u8),
                ("adc", ctypes.c_bool)]


class Ident(ctypes.Structure):
    _fields_ = [("dsp", u8 * 2), ("mixer", u8 * 2), ("cfg", u16)]


class Stats(ctypes.Structure):
    _fields_ = [("irq", u32 * 4), ("irq_none", u32), ("dsp_busy", u32),
                ("dsp_desync", u32), ("pos_retries", u32),
                ("settles", u32)]


_LIB = {}


def library():
    """hw.c and the shim, built once"""
    if "lib" not in _LIB:
        tmp = _LIB["tmp"] = tempfile.TemporaryDirectory()
        out = os.path.join(tmp.name, "libes1869hw.so")
        subprocess.run(["gcc", "-std=gnu11", "-Wall", "-Wextra", "-Werror",
                        "-O1", "-fPIC", "-shared", "-I" + DRV, "-o", out,
                        os.path.join(DRV, "hw.c"),
                        os.path.join(HOST, "lx_hw.c")],
                       check=True, capture_output=True)
        lib = ctypes.CDLL(out)
        P = vp
        sigs = {
            "lx_new": ([ctypes.POINTER(IO), u16, u16, u16], P),
            "lx_free": ([P], None),
            "lx_mix": ([P, u8], u8),
            "lx_ctrl": ([P, u8], u8),
            "lx_set_imr": ([P, u8], None),
            "lx_imr": ([P], u8),
            "lx_set_port7": ([P, u8], None),
            "lx_set_mpu": ([P, u16], None),
            "lx_desync": ([P], ctypes.c_int),
            "lx_stats": ([P], ctypes.POINTER(Stats)),
            "es1869_reset": ([P], ctypes.c_int),
            "es1869_dsp_reset": ([P], ctypes.c_int),
            "es1869_dsp_cmd": ([P, ctypes.c_char_p, ctypes.c_uint],
                               ctypes.c_int),
            "es1869_identify": ([P, ctypes.POINTER(Ident)], ctypes.c_int),
            "es1869_route": ([P, u8, ctypes.c_bool], None),
            "es1869_mix_read": ([P, u8], u8),
            "es1869_mix_write": ([P, u8, u8], None),
            "es1869_mix_update": ([P, u8, u8, u8], None),
            "es1869_ctrl_read": ([P, u8], ctypes.c_int),
            "es1869_ctrl_write": ([P, u8, u8], ctypes.c_int),
            "es1869_pnp_read": ([P, u8, u8], u8),
            "es1869_pnp_write": ([P, u8, u8, u8], None),
            "es1869_ldn": ([P, u8], ctypes.c_int),
            "es1869_a1_rate": ([u32, u32, ctypes.POINTER(u8),
                                ctypes.POINTER(ctypes.c_bool)],
                               ctypes.c_int),
            "es1869_a2_rate": ([u32, u32, ctypes.POINTER(u8)],
                               ctypes.c_int),
            "es1869_filter": ([u32, u16], u8),
            "es1869_best_rate": ([u32, ctypes.c_bool, ctypes.POINTER(u32),
                                  ctypes.POINTER(u32)], ctypes.c_int),
            "es1869_a1_prepare": ([P, ctypes.POINTER(Stream), ctypes.c_bool,
                                   ctypes.c_bool], ctypes.c_int),
            "es1869_a1_go": ([P, ctypes.c_bool], ctypes.c_int),
            "es1869_a1_dac": ([P, ctypes.c_bool], ctypes.c_int),
            "es1869_a1_stop": ([P, ctypes.c_bool, u8, ctypes.c_bool],
                               ctypes.c_int),
            "es1869_a1_pause": ([P, ctypes.c_bool], ctypes.c_int),
            "es1869_a2_prepare": ([P, ctypes.POINTER(Stream),
                                   ctypes.c_bool], ctypes.c_int),
            "es1869_a2_go": ([P, u8, ctypes.c_bool], None),
            "es1869_a2_stop": ([P, u8], None),
            "es1869_irq_enter": ([P], u8),
            "es1869_irq_poll": ([P], u8),
            "es1869_irq_take": ([P], u8),
            "es1869_irq_exit": ([P], None),
            "es1869_irq_storm": ([P, ctypes.c_ulong, ctypes.c_ulong],
                                 ctypes.c_bool),
            "es1869_dma_pos": ([P, u8, u32], u32),
            "es1869_spread": ([ctypes.c_char_p, ctypes.c_char_p, u32],
                              None),
        }
        for name, (args, res) in sigs.items():
            fn = getattr(lib, name)
            fn.argtypes, fn.restype = args, res
        _LIB["lib"] = lib
    return _LIB["lib"]


# asm/dma.h's ports
def dma_ports(ch):
    """mask, mode, flip-flop, address, count and page ports of a channel"""
    page = {0: 0x87, 1: 0x83, 2: 0x81, 3: 0x82, 5: 0x8B, 6: 0x89,
            7: 0x8A}[ch]
    if ch < 4:
        return 0x0A, 0x0B, 0x0C, 2 * ch, 2 * ch + 1, page
    return 0xD4, 0xD6, 0xD8, 0xC0 + 4 * (ch - 4), 0xC2 + 4 * (ch - 4), page


class Driver:
    """the chip core on a model, through struct es1869_io as the kernel
    fills it in"""

    def __init__(self, chip=None, cfg=None):
        self.chip = chip if chip is not None else LinuxChip()
        cfg = CFG if cfg is None else cfg
        self.errors = []
        self.residues = []          # readings that stand in for the 8237's
        lib = self.lib = library()
        self.io = IO(None, INB(self._inb), OUTB(self._outb),
                     UDELAY(self._udelay), NOW(self._now),
                     DMAPROG(self._dma_program), DMAMASK(self._dma_mask),
                     DMARES(self._dma_residue))
        self.hw = lib.lx_new(ctypes.byref(self.io), BASE, cfg, BASE)

    def close(self):
        self.lib.lx_free(self.hw)

    def _guard(fn):
        def call(self, *a):
            try:
                return fn(self, *a)
            except Exception as e:      # ctypes would swallow it
                self.errors.append("%s: %r" % (fn.__name__, e))
                return 0
        return call

    @_guard
    def _inb(self, _ctx, port):
        return self.chip.inb(port) & 0xFF

    @_guard
    def _outb(self, _ctx, port, v):
        self.chip.outb(port, v)

    @_guard
    def _udelay(self, _ctx, us):
        run(self.chip, us)

    @_guard
    def _now(self, _ctx):
        return self.chip.clock.us * 1000

    # snd_dma_program() with DMA_MODE_NO_ENABLE: disable_dma, clear_dma_ff,
    # set_dma_mode, set_dma_addr (the page first), set_dma_count
    @_guard
    def _dma_program(self, _ctx, ch, phys, size, mode):
        mask, mode_p, ff, addr, count, page = dma_ports(ch)
        out = self.chip.outb
        out(mask, 0x04 | (ch & 3))
        out(ff, 0)
        out(mode_p, (mode | (ch & 3)) & 0xFF)
        out(page, ((phys >> 16) & 0xFE) if ch >= 4 else (phys >> 16) & 0xFF)
        a = phys >> 1 if ch >= 4 else phys
        out(addr, a & 0xFF)
        out(addr, (a >> 8) & 0xFF)
        n = (size >> 1) - 1 if ch >= 4 else size - 1
        out(count, n & 0xFF)
        out(count, (n >> 8) & 0xFF)

    @_guard
    def _dma_mask(self, _ctx, ch, masked):
        mask = dma_ports(ch)[0]
        self.chip.outb(mask, (0x04 if masked else 0) | (ch & 3))

    # clear_dma_ff, then get_dma_residue: 1 + the count, as a 16-bit
    # number, doubled on a word channel
    @_guard
    def _dma_residue(self, _ctx, ch):
        if self.residues:
            return self.residues.pop(0)
        ff, count = dma_ports(ch)[2], dma_ports(ch)[4]
        self.chip.outb(ff, 0)
        c = self.chip.inb(count)
        c |= self.chip.inb(count) << 8
        c = (c + 1) & 0xFFFF
        return c << 1 if ch >= 4 else c

    # --- calls ---------------------------------------------------------------
    def __getattr__(self, name):
        fn = getattr(self.lib, "es1869_" + name)
        return lambda *a: fn(self.hw, *a)

    def stats(self):
        return self.lib.lx_stats(self.hw).contents

    def mix(self, reg):
        return self.lib.lx_mix(self.hw, reg)

    def ctrl(self, reg):
        return self.lib.lx_ctrl(self.hw, reg)


def stream(rate_num, rate_den, bits=16, signed=True, stereo=True,
           period=1024, ch=1, size=4096, single=False, burst=2,
           corner=875, a8=0x00):
    return Stream(rate_num, rate_den, bits, signed, stereo, period, ch, BUF,
                  size, single, burst, corner, a8)


def frames16(data):
    """16-bit stereo frames of signed little-endian data"""
    return [tuple(decode(data[i:i + 4], 16, True, True))
            for i in range(0, len(data) - 3, 4)]


@unittest.skipUnless(HAVE_UNICORN, "needs unicorn")
class IdentifyTest(unittest.TestCase):
    def test_finds_the_chip(self):
        d = Driver()
        ident = Ident()
        self.assertEqual(d.identify(ctypes.byref(ident)), 0)
        self.assertEqual(ident.dsp[0], 0x68)
        self.assertEqual(ident.dsp[1] & 0xF0, 0x80)
        self.assertEqual((ident.mixer[0], ident.mixer[1]), (0x18, 0x69))
        self.assertEqual(ident.cfg, CFG)
        self.assertEqual(d.errors, [])

    def test_no_chip_fails_fast(self):
        """a bus with nothing on it reads FFh: busy, so the reset's AAh
        never comes, and the probe gives up within milliseconds, which a
        boot without the chip (QEMU) needs"""
        class Nothing:
            class Clock:
                us = 0
            clock = Clock()

            def inb(self, port):
                self.clock.us += 1
                return 0xFF

            def outb(self, port, v):
                self.clock.us += 1
        d = Driver(chip=Nothing())
        d.chip.advance = lambda us: us
        ident = Ident()
        self.assertEqual(d.identify(ctypes.byref(ident)), -19)   # ENODEV
        self.assertLess(d.chip.clock.us, 10000)

    def test_a_sound_blaster_without_ess_answer(self):
        """a chip that resets but doesn't answer E7h, as QEMU's sb16"""
        chip = LinuxChip()
        orig = chip._command

        def command(c, ops):
            if c == 0xE7:
                return              # no ESS answer
            orig(c, ops)
        chip._command = command
        d = Driver(chip)
        ident = Ident()
        self.assertEqual(d.identify(ctypes.byref(ident)), -19)

    def test_reset_sets_the_shadows_the_chip_has(self):
        d = Driver()
        self.assertEqual(d.reset(), 0)
        for reg in (0xA1, 0xA2, 0xA4, 0xA5, 0xA8, 0xB6, 0xB7, 0xB8, 0xB9):
            self.assertEqual(d.ctrl(reg), d.chip.ctrl[reg], hex(reg))
            self.assertEqual(d.ctrl_read(reg), d.chip.ctrl[reg], hex(reg))
        self.assertTrue(d.chip.ext_mode)
        self.assertEqual(d.chip.violations, [])

    def test_pnp_access_keeps_index_and_device(self):
        d = Driver()
        d.chip.outb(CFG, 0x23)
        d.chip.outb(CFG, 0x07)
        d.chip.outb(CFG + 1, 0x02)
        d.chip.outb(CFG, 0x23)
        self.assertEqual(d.pnp_read(0xFF, 0x25), 0x80)
        self.assertEqual(d.pnp_read(1, 0x60), 0x02)
        d.pnp_write(1, 0x72, 5)
        self.assertEqual(d.chip.pnp[(1, 0x72)], 5)
        self.assertEqual(d.chip.ldn, 2)
        self.assertEqual(d.chip.cfg_index, 0x23)

    def test_route_lets_the_dma_requests_out(self):
        """26h bits 3:2 set (a card left with them clear played nothing,
        under this driver and snd-es18xx alike), bit 4 as found, and every
        source on the audio IRQ"""
        d = Driver()
        d.chip.pnp[(None, 0x26)] = 0x10
        d.route(5, True)
        self.assertEqual(d.chip.pnp[(None, 0x26)], 0x1C)
        self.assertEqual(d.chip.pnp[(None, 0x27)], 5)
        self.assertEqual(d.chip.pnp[(None, 0x28)], 5)
        self.assertEqual(d.chip.pnp[(1, 0x72)], 5)
        d.chip.pnp[(None, 0x26)] = 0x00
        d.route(5, False)
        self.assertEqual(d.chip.pnp[(None, 0x26)], 0x0C)

    def test_optional_logical_devices(self):
        d = Driver()
        d.chip.pnp[(None, 0x25)] = 0x80     # MPU-401 in LDN 1: none
        self.assertEqual(d.ldn(0x10), -19)
        d.chip.pnp[(None, 0x25)] = 0x81     # MPU-401 as LDN 3
        self.assertEqual(d.ldn(0x10), 3)
        d.chip.pnp[(None, 0x25)] = 0x87     # MPU, CD-ROM, modem
        self.assertEqual(d.ldn(0x12), 5)
        self.assertEqual(d.ldn(0x13), -19)
        self.assertEqual(d.ldn(1), 1)


@unittest.skipUnless(HAVE_UNICORN, "needs unicorn")
class RateTest(unittest.TestCase):
    def test_audio1_every_divisor(self):
        lib = library()
        r, new = u8(), ctypes.c_bool()
        for num in (793800, 768000, 396900):
            for den in range(1, 129):
                self.assertEqual(lib.es1869_a1_rate(num, den,
                                                    ctypes.byref(r),
                                                    ctypes.byref(new)), 0)
                got = a1_hz(r.value, new.value)
                self.assertAlmostEqual(got, num / den, places=6,
                                       msg="%d/%d" % (num, den))
                self.assertEqual(new.value, num == 768000)
        self.assertNotEqual(lib.es1869_a1_rate(793800, 129, ctypes.byref(r),
                                               ctypes.byref(new)), 0)
        self.assertNotEqual(lib.es1869_a1_rate(44100, 1, ctypes.byref(r),
                                               ctypes.byref(new)), 0)

    def test_audio2_every_divisor(self):
        lib = library()
        r = u8()
        for num in (793800, 768000):
            for den in range(1, 129):
                self.assertEqual(lib.es1869_a2_rate(num, den,
                                                    ctypes.byref(r)), 0)
                self.assertAlmostEqual(a2_hz(r.value), num / den, places=6)
        self.assertNotEqual(lib.es1869_a2_rate(396900, 64, ctypes.byref(r)),
                            0)

    def test_filter_as_ess_writes_it(self):
        """FDh from 44.1 kHz up, else 256 - 199582/rate at ESS's corner of
        87.5 % of half the rate"""
        lib = library()
        for rate in (44100, 48000, 96000):
            self.assertEqual(lib.es1869_filter(rate, 875), 0xFD)
        for rate in (8000, 11025, 22050, 32000, 3101):
            want = 256 - min(255, max(3, round(199582 / rate)))
            self.assertEqual(lib.es1869_filter(rate, 875), want, rate)

    def test_best_rate(self):
        lib = library()
        num, den = u32(), u32()
        lib.es1869_best_rate(44100, True, ctypes.byref(num),
                             ctypes.byref(den))
        self.assertEqual(num.value / den.value, 44100)
        lib.es1869_best_rate(48000, False, ctypes.byref(num),
                             ctypes.byref(den))
        self.assertEqual((num.value, den.value), (768000, 16))
        lib.es1869_best_rate(4000, True, ctypes.byref(num),
                             ctypes.byref(den))
        self.assertEqual(num.value, 396900)


def play_a1(d, data, settle_us, rate=(793800, 18), period=1024,
            run_us=40000):
    """Audio 1 playback of data (16-bit signed stereo) from the start, the
    channel unmasked settle_us after B8h bit 0 (None: unmasked at once, the
    race)"""
    chip = d.chip
    chip.dmac.memory.write(BUF, data)
    assert d.reset() == 0
    s = stream(*rate, period=period, size=len(data))
    assert d.a1_prepare(ctypes.byref(s), False, False) == 0
    assert d.a1_dac(True) == 0
    if settle_us is None:
        d.lib.es1869_a1_go(d.hw, False)
        d._dma_mask(None, 1, False)
    else:
        d.lib.es1869_a1_go(d.hw, False)
        run(chip, settle_us)
        d._dma_mask(None, 1, False)
    run(chip, run_us)


@unittest.skipUnless(HAVE_UNICORN, "needs unicorn")
class Audio1Test(unittest.TestCase):
    def setUp(self):
        self.data = words(samples16(4096, start=0x1234))

    def test_settle_keeps_the_first_byte(self):
        """the channel unmasked after the settle: every frame in step"""
        d = Driver()
        play_a1(d, self.data, settle_us=46)
        got = d.chip.played[:200]
        self.assertGreater(len(got), 100)
        self.assertEqual(got, frames16(self.data)[:len(got)])
        self.assertEqual(d.chip.violations, [])
        self.assertEqual(d.errors, [])

    def test_unmasked_at_once_loses_the_first_byte(self):
        """the race itself, which the settle avoids (fix.late0)"""
        d = Driver()
        play_a1(d, self.data, settle_us=None)
        got = d.chip.played[:200]
        self.assertNotEqual(got, frames16(self.data)[:len(got)])
        self.assertEqual(got[:50], frames16(self.data[1:])[:50])

    def test_settle_beyond_the_race_window(self):
        d = Driver()
        play_a1(d, self.data, settle_us=RACE_US + 22)
        got = d.chip.played[:100]
        self.assertEqual(got, frames16(self.data)[:len(got)])

    def test_registers_as_planned(self):
        d = Driver()
        play_a1(d, self.data, settle_us=46, run_us=100)
        c = d.chip.ctrl
        self.assertEqual(c[0xA1], 256 - 18)
        self.assertEqual(c[0xA2], 0xFD)
        self.assertEqual(c[0xA4] | c[0xA5] << 8, 0x10000 - 1024)
        self.assertEqual(c[0xA8], 0x01)         # as ESS's player leaves it
        self.assertEqual(c[0xB9], 0x02)
        self.assertEqual(c[0xB7], 0xBC)         # signed 16-bit stereo
        self.assertEqual(c[0xB8], 0x05)
        self.assertEqual(c[0xB1] & 0xF0, 0x50)
        self.assertEqual(c[0xB2] & 0xF0, 0x50)
        # demand, auto-initialize, read
        self.assertEqual(d.chip.dmac.ch[1].mode, 0x18 | 1)
        self.assertFalse(d.chip.mixer[0x71] & 0x20)

    def test_768_clock_uses_the_new_mode(self):
        d = Driver()
        play_a1(d, self.data, settle_us=46, rate=(768000, 16), run_us=100)
        self.assertTrue(d.chip.mixer[0x71] & 0x20)
        self.assertEqual(d.chip.a1_rate(), 48000)

    def test_dma_requests_held_without_26h(self):
        """the model: with 26h bit 2 clear, Audio 1's requests never reach
        the 8237, as on the Armada"""
        d = Driver()
        d.chip.pnp[(None, 0x26)] = 0x00
        play_a1(d, self.data, settle_us=46, run_us=20000)
        self.assertEqual(d.chip.played, [])
        self.assertEqual(d.chip.dmac.left(1), len(self.data))

    def test_low_clock(self):
        d = Driver()
        play_a1(d, self.data, settle_us=600, rate=(396900, 128),
                run_us=20000)
        self.assertAlmostEqual(d.chip.a1_rate(), 396900 / 128)
        got = d.chip.played[:40]
        self.assertEqual(got, frames16(self.data)[:len(got)])

    def test_period_interrupts_and_mask_dance(self):
        """an edge per period; the handler's ack lets the next come"""
        d = Driver()
        d.lib.lx_set_imr(d.hw, IRQ_A1 | IRQ_A2)
        chip = d.chip
        chip.dmac.memory.write(BUF, self.data)
        d.reset()
        s = stream(793800, 18, period=1024, size=8192)
        d.a1_prepare(ctypes.byref(s), False, False)
        d.a1_dac(True)
        d.lib.es1869_a1_go(d.hw, False)
        run(chip, 46)
        d._dma_mask(None, 1, False)
        seen = []
        for _ in range(4):
            edges = len(chip.irq_edges)
            for _ in range(200):
                run(chip, 100)
                if len(chip.irq_edges) > edges:
                    break
            seen.append(d.irq_enter())
            d.irq_exit()
        self.assertEqual(seen, [IRQ_A1] * 4)
        self.assertEqual(d.stats().irq[0], 4)
        self.assertEqual(chip.mask_writes[-1][1], IRQ_A1 | IRQ_A2)

    def test_polled_sources_acknowledged_where_they_live(self):
        """ESS's way (ES1869.DRV 3:01B9): +Ch bits 2:0 and a read of +Eh,
        7Ah bit 7 written 0, 64h bit 4 and a write to 66h, the MPU-401's
        status"""
        d = Driver()
        d.lib.lx_set_imr(d.hw, IRQ_A1 | IRQ_A2 | IRQ_HWV | IRQ_MPU)
        d.lib.lx_set_mpu(d.hw, 0x330)
        chip = d.chip
        d.mix_write(0x64, 0x42)
        self.assertEqual(d.irq_poll(), 0)
        chip.press()
        chip.mixer[0x7A] |= 0xC0
        chip.a1_flags |= 0x01
        chip.ctrl[0xB1] |= 0x40
        chip.mpu_receive(b"\x90")
        self.assertEqual(d.irq_poll(), IRQ_A1 | IRQ_A2 | IRQ_HWV | IRQ_MPU)
        self.assertFalse(chip.hwv_req)
        self.assertFalse(chip.mixer[0x7A] & 0x80)
        self.assertEqual(chip.a1_flags, 0)
        self.assertEqual(chip.mpu_in, [0x90])   # the caller reads it
        self.assertEqual(chip.status() & 0x07, 0)
        self.assertEqual(chip.mask_writes, [])  # Config_Base+7 untouched

    def test_take_reads_the_status_once(self):
        """Config_Base+6 shows the sources: each is taken back where it
        lives, Audio 2's latch from the shadow, and Config_Base+7 is left
        alone"""
        d = Driver()
        d.lib.lx_set_imr(d.hw, IRQ_A1 | IRQ_A2 | IRQ_HWV | IRQ_MPU)
        d.lib.lx_set_mpu(d.hw, 0x330)
        chip = d.chip
        d.mix_write(0x64, 0x42)
        d.mix_write(0x7A, 0x47)
        self.assertEqual(d.irq_take(), 0)
        chip.press()
        chip.mixer[0x7A] |= 0x80
        chip.a1_flags |= 0x01
        chip.ctrl[0xB1] |= 0x40
        chip.mpu_receive(b"\x90")
        self.assertEqual(d.irq_take(), IRQ_A1 | IRQ_A2 | IRQ_HWV | IRQ_MPU)
        self.assertFalse(chip.hwv_req)
        self.assertEqual(chip.mixer[0x7A], 0x47)    # the format kept
        self.assertEqual(chip.a1_flags, 0)
        self.assertEqual(chip.mpu_in, [0x90])
        self.assertEqual(chip.status() & 0x07, 0)
        self.assertEqual(chip.mask_writes, [])
        self.assertEqual(d.irq_take(), IRQ_MPU)     # until it is read

    def test_poll_finds_a_source_that_came_late(self):
        """a source that comes while another holds the line raises no
        edge: the next pass finds it"""
        d = Driver()
        d.lib.lx_set_imr(d.hw, IRQ_A1 | IRQ_A2)
        chip = d.chip
        chip.mixer[0x7A] |= 0xC0
        self.assertEqual(d.irq_poll(), IRQ_A2)
        chip.a1_flags |= 0x01
        chip.ctrl[0xB1] |= 0x40
        self.assertEqual(d.irq_poll(), IRQ_A1)
        self.assertEqual(d.irq_poll(), 0)

    def test_pending_source_at_exit_makes_a_new_edge(self):
        """a source that comes while the mask is clear raises the line
        when the mask comes back (DS p.18)"""
        d = Driver()
        d.lib.lx_set_imr(d.hw, IRQ_A1 | IRQ_HWV)
        d.mix_write(0x64, 0x02)             # the button's interrupt on
        chip = d.chip
        st = d.irq_enter()
        self.assertEqual(st, 0)
        chip.press()                        # comes with the mask clear
        self.assertFalse(chip.irq_line)
        edges = len(chip.irq_edges)
        d.irq_exit()
        self.assertTrue(chip.irq_line)
        self.assertEqual(len(chip.irq_edges), edges + 1)
        self.assertEqual(d.irq_enter(), IRQ_HWV)
        self.assertFalse(chip.hwv_req)
        d.irq_exit()
        self.assertFalse(chip.irq_line)

    def test_position_follows_the_dma(self):
        d = Driver()
        play_a1(d, self.data, settle_us=46, run_us=2000)
        size = len(self.data)
        pos = d.dma_pos(1, size)
        self.assertAlmostEqual(pos, size - d.chip.dmac.left(1), delta=4)
        run(d.chip, 3000)
        self.assertGreater(d.dma_pos(1, size), pos)

    def test_pause_resumes_in_step(self):
        d = Driver()
        play_a1(d, self.data, settle_us=46, run_us=3000)
        self.assertEqual(d.a1_pause(True), 0)
        run(d.chip, 3000)
        n = len(d.chip.played)
        run(d.chip, 3000)
        self.assertEqual(len(d.chip.played), n)     # still
        self.assertEqual(d.a1_pause(False), 0)
        run(d.chip, 5000)
        got = d.chip.played[:len(frames16(self.data))]
        self.assertEqual(got, frames16(self.data)[:len(got)])

    def test_stop(self):
        d = Driver()
        play_a1(d, self.data, settle_us=46, run_us=2000)
        self.assertEqual(d.a1_stop(False, 1, False), 0)
        n = len(d.chip.played)
        run(d.chip, 5000)
        self.assertEqual(len(d.chip.played), n)
        self.assertTrue(d.chip.dmac.ch[1].masked)

    def test_capture(self):
        d = Driver()
        chip = d.chip
        src = [(i * 37 - 2000, -i * 11) for i in range(3000)]
        it = iter(src)
        chip.source = lambda n: [next(it) for _ in range(n)]
        d.reset()
        s = stream(793800, 18, period=1024, size=4096, a8=0xF4)
        self.assertEqual(d.a1_prepare(ctypes.byref(s), True, True), 0)
        # stopped as ESS's driver leaves it; the start turns the ADC on
        self.assertEqual(chip.ctrl[0xB8], 0x00)
        self.assertEqual(chip.ctrl[0xA8], 0xFD)     # ESS's F5h, the monitor
        self.assertEqual(chip.dmac.ch[1].mode, 0x14 | 1)
        d._dma_mask(None, 1, False)
        self.assertEqual(d.a1_go(True), 0)
        self.assertEqual(chip.ctrl[0xB8], 0x0F)
        run(chip, 20000)
        got = frames16(chip.dmac.memory.read(BUF, 2048))
        self.assertEqual(got, src[:len(got)])
        self.assertEqual(chip.violations, [])

    def test_capture_turns_to_the_adc_at_prepare(self):
        """a1c_settle_ms: the codec on the ADC from the prepare and between
        recordings, so that its input settles before a start"""
        d = Driver()
        chip = d.chip
        chip.source = lambda n: [(100, -100)] * n
        d.reset()
        s = stream(793800, 18, period=1024, size=4096, a8=0xF4)
        s.adc = True
        self.assertEqual(d.a1_prepare(ctypes.byref(s), True, True), 0)
        self.assertEqual(chip.ctrl[0xB8], 0x08)
        d._dma_mask(None, 1, False)
        self.assertEqual(d.a1_go(True), 0)
        self.assertEqual(chip.ctrl[0xB8], 0x0F)
        run(chip, 5000)
        self.assertEqual(d.a1_stop(True, 1, True), 0)
        self.assertEqual(chip.ctrl[0xB8], 0x08)
        self.assertTrue(chip.dmac.ch[1].masked)
        self.assertEqual(d.a1_stop(True, 1, False), 0)
        self.assertEqual(chip.ctrl[0xB8], 0x00)
        self.assertEqual(chip.violations, [])


@unittest.skipUnless(HAVE_UNICORN, "needs unicorn")
class Audio2Test(unittest.TestCase):
    def play(self, d, data, spread=True, rate=(793800, 18), ch=5,
             run_us=40000):
        chip = d.chip
        if spread:
            out = ctypes.create_string_buffer(2 * len(data))
            d.lib.es1869_spread(out, data, len(data))
            ring = out.raw
        else:
            ring = data
        chip.dmac.memory.write(BUF, ring)
        d.reset()
        d.mix_update(0x71, 0x02, 0x02)          # Audio 2 on its own clock
        d.mix_write(0x7C, 0xFF)
        s = stream(*rate, period=1024, ch=ch, size=len(ring))
        self.assertEqual(d.a2_prepare(ctypes.byref(s), True), 0)
        d.a2_go(2, False)
        run(chip, run_us)

    def test_dma5_spread_plays_every_byte(self):
        d = Driver()
        data = words(samples16(2048, start=0x2345))
        self.play(d, data)
        got = d.chip.played2[:300]
        self.assertGreater(len(got), 200)
        self.assertEqual(got, frames16(data)[:len(got)])
        self.assertEqual(d.chip.violations, [])     # never 78h bit 5
        self.assertEqual(d.chip.mixer[0x78], 0x93)
        self.assertEqual(d.chip.dmac.ch[5].mode, 0x18 | 1)

    def test_dma5_packed_buffer_plays_wrong(self):
        """what the 16-bit channel's words give the chip unspread: only the
        low bytes, which is why the driver spreads"""
        d = Driver()
        data = words(samples16(2048, start=0x2345))
        self.play(d, data, spread=False)
        got = d.chip.played2[:100]
        self.assertNotEqual(got, frames16(data)[:len(got)])

    def test_8bit_channel_direct(self):
        d = Driver(LinuxChip(dma2=0))
        data = words(samples16(2048, start=0x3456))
        self.play(d, data, spread=False, ch=0)
        got = d.chip.played2[:200]
        self.assertEqual(got, frames16(data)[:len(got)])

    def test_rate_768(self):
        d = Driver()
        self.play(d, words(samples16(2048)), rate=(768000, 16), run_us=100)
        self.assertEqual(d.chip.a2_rate(), 48000)
        self.assertEqual(d.chip.mixer[0x72], 0xFD)

    def test_interrupt_and_ack(self):
        d = Driver()
        d.lib.lx_set_imr(d.hw, IRQ_A1 | IRQ_A2)
        self.play(d, words(samples16(4096)), run_us=0)
        chip = d.chip
        for _ in range(3):
            edges = len(chip.irq_edges)
            for _ in range(300):
                run(chip, 100)
                if len(chip.irq_edges) > edges:
                    break
            self.assertEqual(d.irq_enter(), IRQ_A2)
            self.assertFalse(chip.mixer[0x7A] & 0x80)
            self.assertTrue(chip.mixer[0x7A] & 0x40)
            d.irq_exit()
        self.assertEqual(d.stats().irq[1], 3)

    def test_position_survives_a_torn_reading(self):
        # the count moves between the low and the high byte: 256 off, and
        # refused; readings 10 apart, as at 99 kHz, agree
        d = Driver()
        d.residues = [1000, 744, 1000, 990]
        self.assertEqual(d.dma_pos(5, 4096), 4096 - 1000)
        self.assertEqual(d.stats().pos_retries, 1)
        d.residues = [2000, 1990]
        self.assertEqual(d.dma_pos(1, 4096), 4096 - 2000)

    def test_position_in_words(self):
        d = Driver()
        data = words(samples16(4096))
        self.play(d, data, run_us=3000)
        ring = 2 * len(data)
        pos = d.dma_pos(5, ring)
        self.assertAlmostEqual(pos, ring - 2 * d.chip.dmac.left(5), delta=8)
        self.assertEqual(pos % 2, 0)

    def test_stop(self):
        d = Driver()
        self.play(d, words(samples16(4096)), run_us=3000)
        d.a2_stop(5)
        self.assertEqual(d.chip.mixer[0x78], 0)
        self.assertTrue(d.chip.dmac.ch[5].masked)
        n = len(d.chip.played2)
        run(d.chip, 5000)
        self.assertLessEqual(len(d.chip.played2) - n, 16)   # the FIFO


@unittest.skipUnless(HAVE_UNICORN, "needs unicorn")
class ProtocolTest(unittest.TestCase):
    def test_busy_dsp_refuses_the_first_byte(self):
        d = Driver()
        d.reset()
        d.chip.busy = True
        self.assertEqual(d.dsp_cmd(b"\xC6", 1), -16)        # EBUSY
        self.assertFalse(d.lib.lx_desync(d.hw))
        self.assertEqual(d.stats().dsp_busy, 1)

    def test_operand_timeout_leaves_it_out_of_step(self):
        d = Driver()
        d.reset()
        chip = d.chip
        orig = chip.outb

        def outb(port, v):
            orig(port, v)
            if port == BASE + 0x0C and v == 0xA1:
                chip.busy = True            # stuck after the first byte
        chip.outb = outb
        self.assertEqual(d.ctrl_write(0xA1, 0x80), -5)       # EIO
        self.assertTrue(d.lib.lx_desync(d.hw))
        self.assertEqual(d.dsp_cmd(b"\xC6", 1), -5)
        chip.busy = False
        chip.outb = orig
        self.assertEqual(d.reset(), 0)
        self.assertFalse(d.lib.lx_desync(d.hw))

    def test_mixer_update_writes_only_changes(self):
        d = Driver()
        d.mix_write(0x71, 0x12)
        before = len(d.chip.log)
        d.mix_update(0x71, 0x10, 0x10)
        self.assertEqual(len(d.chip.log), before)
        d.mix_update(0x71, 0x20, 0x20)
        self.assertEqual(d.chip.mixer[0x71], 0x32)


class StormTest(unittest.TestCase):
    """the storm guard's second, on a clock of 250 ticks a second, from a
    start of 0 as at load"""

    HZ = 250
    LIMIT = 20000                   # ES_IRQ_STORM
    WRAP = 2 ** (8 * ctypes.sizeof(ctypes.c_ulong))

    def setUp(self):
        self.lib = library()
        self.hw = self.lib.lx_new(None, 0x220, 0x250, 0x388)
        self.addCleanup(self.lib.lx_free, self.hw)
        self.lib.lx_set_imr(self.hw, IRQ_A1 | IRQ_A2)

    def calls(self, now, n):
        """n calls of the handler at one tick: how many said storm"""
        fn = self.lib.es1869_irq_storm
        return sum(fn(self.hw, now % self.WRAP, self.HZ) for _ in range(n))

    def test_boot_clock(self):
        """a clock short of its wrap, as 32-bit jiffies are for a boot's
        first five minutes (INITIAL_JIFFIES): 1500 calls a second for 40 s
        across the wrap are no storm, where time_after() against the start
        of 0 took them all for one second's"""
        base = self.WRAP - 20 * self.HZ
        storms = sum(self.calls(base + t, 6) for t in range(40 * self.HZ))
        self.assertEqual(storms, 0)
        self.assertEqual(self.lib.lx_imr(self.hw), IRQ_A1 | IRQ_A2)

    def test_storm(self):
        """the call past the limit in one second is the storm, said once,
        and the mask stays clear in the seconds after"""
        self.assertEqual(self.calls(1000, self.LIMIT), 0)
        self.assertEqual(self.lib.lx_imr(self.hw), IRQ_A1 | IRQ_A2)
        self.assertEqual(self.calls(1000 + self.HZ - 1, 1), 1)
        self.assertEqual(self.lib.lx_imr(self.hw), 0)
        self.assertEqual(self.calls(1000 + self.HZ - 1, 5), 0)
        self.assertEqual(self.calls(1000 + 3 * self.HZ, self.LIMIT + 5), 0)
        self.assertEqual(self.lib.lx_imr(self.hw), 0)

    def test_second_ends(self):
        """the count starts again a second after the second's start"""
        self.assertEqual(self.calls(1000, self.LIMIT), 0)
        self.assertEqual(self.calls(1000 + self.HZ, self.LIMIT), 0)
        self.assertEqual(self.lib.lx_imr(self.hw), IRQ_A1 | IRQ_A2)

    def test_start_long_past(self):
        """a second that started more than half the clock's range ago, as
        after 99 days without an interrupt on 32-bit jiffies, is over"""
        self.assertEqual(self.calls(1000, self.LIMIT - 100), 0)
        later = 1000 + self.WRAP // 2 + 2 * self.HZ
        self.assertEqual(self.calls(later, self.LIMIT - 100), 0)
        self.assertEqual(self.lib.lx_imr(self.hw), IRQ_A1 | IRQ_A2)


if __name__ == "__main__":
    unittest.main()
