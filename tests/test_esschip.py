# (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
#
# Licensed under GPL Version 3.0
"""Tests of the ES1869 audio model (esschip): what the data sheet says the
chip does with the sequences the fronts send."""

import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from dma8237 import DMA8237  # noqa: E402
from esschip import ES1869Audio, a1_hz, a2_hz  # noqa: E402
from vxdemu import FakeES1869  # noqa: E402
from sbgame import program_dma, words, samples16  # noqa: E402
from test_sb16 import adpcm_decode  # noqa: E402

BASE = 0x220
BUF = 0x20000


class Chip(ES1869Audio, FakeES1869):
    """the model with its own DMA controllers on the ports"""

    def __init__(self):
        FakeES1869.__init__(self, BASE, 0x250)
        self.dmac = DMA8237()
        self.audio_init(irq=5, dma1=1, dma2=0)

    def _inb(self, port):
        if self.dmac.handles(port):
            return self.dmac.inb(port)
        return super()._inb(port)

    def outb(self, port, v):
        if self.dmac.handles(port):
            self.catch_up()
            self.clock.advance(1)
            self.dmac.outb(port, v)
            return
        super().outb(port, v)


class Card:
    """sbgame's card calls on the bare chip"""

    def __init__(self, chip):
        self.chip = chip

    def out(self, port, v):
        self.chip.outb(port, v)

    def inp(self, port):
        return self.chip.inb(port)


def run(chip, us):
    """us of time, past the interrupts' edges"""
    while us > 0:
        us -= chip.advance(us)


def dsp(chip, *data):
    for b in data:
        chip.outb(BASE + 0x0C, b)


def reg(chip, r, v):
    dsp(chip, r, v)


def reg_read(chip, r):
    dsp(chip, 0xC0, r)
    return chip.inb(BASE + 0x0A)


def reset(chip, fifo=True):
    chip.outb(BASE + 6, 3 if fifo else 1)
    chip.outb(BASE + 6, 0)
    run(chip, 100)
    assert chip.inb(BASE + 0x0A) == 0xAA


def extended_play(chip, bits=16, stereo=True, signed=True, auto=True,
                  nbytes=1024, rate_a1=0xF0):
    """the data sheet's sequence (DS p.48-49, table 16)"""
    reset(chip)
    dsp(chip, 0xC6)
    reg(chip, 0xB8, 0x04 if auto else 0x00)
    a8 = reg_read(chip, 0xA8)
    reg(chip, 0xA8, (a8 & 0xFC) | (0x01 if stereo else 0x02))
    reg(chip, 0xB9, 0x02)
    reg(chip, 0xA1, rate_a1)
    reg(chip, 0xA2, 0xF0)
    count = (-nbytes) & 0xFFFF
    reg(chip, 0xA4, count & 0xFF)
    reg(chip, 0xA5, count >> 8)
    reg(chip, 0xB6, 0x00 if signed else 0x80)
    reg(chip, 0xB7, 0x71 if signed else 0x51)
    fmt = 0x90 | (0x20 if signed else 0) | (0x04 if bits == 16 else 0) | \
        (0x08 if stereo else 0x40)
    reg(chip, 0xB7, fmt)
    for r in (0xB1, 0xB2):
        reg(chip, r, reg_read(chip, r) | 0x50)
    reg(chip, 0xB8, reg_read(chip, 0xB8) | 0x01)
    dsp(chip, 0xD1)


class ExtendedTest(unittest.TestCase):
    def test_rates(self):
        """A1h's clocks as the card of the first report measured them, not
        the data sheet's 795.5 and 397.7 kHz: 44101 Hz for EEh"""
        self.assertAlmostEqual(a1_hz(0xF0), 793800 / 16)
        self.assertAlmostEqual(a1_hz(0x40), 396900 / 64)
        self.assertAlmostEqual(a1_hz(0xEE), 44100)
        self.assertAlmostEqual(a2_hz(0xF0), 768000 / 16)
        self.assertAlmostEqual(a2_hz(0x6E), 793800 / 18)

    def test_sixteen_bit_stereo_auto(self):
        chip = Chip()
        data = words(samples16(1024, start=0x1111))
        chip.dmac.memory.write(BUF, data)
        program_dma(Card(chip), 1, BUF, len(data))
        extended_play(chip, nbytes=1024)
        edges = []
        for _ in range(4):
            run(chip, 5200)
            edges.append(len(chip.irq_edges))
            chip.inb(BASE + 0x0E)
        self.assertEqual(edges, [1, 2, 3, 4])   # 1024 bytes, 5.1 ms a block
        self.assertEqual(chip.violations, [])
        got = [s for f in chip.played for s in f][:1024]
        want = [s - 0x10000 if s >= 0x8000 else s
                for s in samples16(1024, start=0x1111)][:len(got)]
        self.assertEqual(got, want)
        self.assertTrue(all(len(f) == 2 for f in chip.played))

    def test_interrupt_leads_the_dac_by_the_fifo(self):
        chip = Chip()
        chip.dmac.memory.write(BUF, bytes(4096))
        program_dma(Card(chip), 1, BUF, 4096)
        extended_play(chip, nbytes=1024)
        chip.advance(30000)             # to the first interrupt
        frames = len(chip.played) + len(chip.muted)
        self.assertLess(frames, 256)            # 1024 bytes are 256 frames
        self.assertGreaterEqual(frames, 256 - FIFO_FRAMES - 4)

    def test_normal_mode_halts_at_the_count(self):
        chip = Chip()
        chip.dmac.memory.write(BUF, bytes(4096))
        program_dma(Card(chip), 1, BUF, 4096)
        extended_play(chip, auto=False, nbytes=512)
        run(chip, 100000)
        self.assertEqual(len(chip.irq_edges), 1)
        self.assertEqual(chip.dmac.left(1), 4096 - 512)

    def test_ack_is_any_read_of_e(self):
        chip = Chip()
        chip.dmac.memory.write(BUF, bytes(4096))
        program_dma(Card(chip), 1, BUF, 4096)
        extended_play(chip, nbytes=256)
        run(chip, 30000)
        self.assertTrue(chip.inb(BASE + 0x0C) & 0x01)
        self.assertTrue(chip.irq_line)
        chip.inb(BASE + 0x0E)
        self.assertFalse(chip.irq_line)
        self.assertFalse(chip.inb(BASE + 0x0C) & 0x01)

    def test_drq_off_holds_the_dma(self):
        chip = Chip()
        chip.dmac.memory.write(BUF, bytes(4096))
        program_dma(Card(chip), 1, BUF, 4096)
        extended_play(chip, nbytes=1024)
        run(chip, 2000)
        reg(chip, 0xB2, reg_read(chip, 0xB2) & ~0x40)
        left = chip.dmac.left(1)
        run(chip, 20000)
        self.assertEqual(chip.dmac.left(1), left)
        reg(chip, 0xB2, reg_read(chip, 0xB2) | 0x40)
        run(chip, 2000)
        self.assertLess(chip.dmac.left(1), left)

    def test_reset_stops_and_leaves_extended_mode(self):
        chip = Chip()
        chip.dmac.memory.write(BUF, bytes(4096))
        program_dma(Card(chip), 1, BUF, 4096)
        extended_play(chip)
        run(chip, 1000)
        reset(chip)
        self.assertFalse(chip.ext_mode)
        self.assertEqual(chip.ctrl[0xB8], 0)
        left = chip.dmac.left(1)
        run(chip, 5000)
        self.assertEqual(chip.dmac.left(1), left)
        self.assertFalse(chip.dac_on)

    def test_the_low_nibbles_of_b1_and_b2_are_the_chips(self):
        chip = Chip()
        reset(chip)
        dsp(chip, 0xC6)
        reg(chip, 0xB1, 0x50)
        reg(chip, 0xB2, 0x5F)
        self.assertEqual(reg_read(chip, 0xB1), 0x55)    # IRQ 5
        self.assertEqual(reg_read(chip, 0xB2), 0x5A)    # DMA 1


FIFO_FRAMES = 256 // 4


class CompatibilityTest(unittest.TestCase):
    def test_14h_plays_mono_and_interrupts(self):
        chip = Chip()
        chip.dmac.memory.write(BUF, bytes(range(256)))
        program_dma(Card(chip), 1, BUF, 256, auto=False)
        reset(chip)
        dsp(chip, 0xD1, 0x40, 0x83, 0x14, 0xFF, 0x00)
        run(chip, 60000)
        self.assertEqual(len(chip.irq_edges), 1)
        self.assertTrue(chip.inb(BASE + 0x0C) & 0x04)
        self.assertEqual([f[0] for f in chip.played][:4],
                         [-0x8000, -0x7F00, -0x7E00, -0x7D00])
        self.assertEqual(chip.violations, [])

    def test_adpcm_blocks_go_on(self):
        """the block after the one with the reference byte goes on from its
        reference and step (DS p.44), and what the firmware's buffer held
        still plays"""
        for first, then, bits in ((0x75, 0x74, 4), (0x77, 0x76, 3),
                                  (0x7B, 0x7A, 2)):
            chip = Chip()
            data = bytes((0x80,)) + bytes((0x3B, 0xC4, 0x0F) * 133)
            chip.dmac.memory.write(BUF, data)
            program_dma(Card(chip), 1, BUF, len(data), auto=False)
            reset(chip)
            dsp(chip, 0xD1, 0x40, 0xD3, first, 199, 0)
            run(chip, 60000)
            dsp(chip, then, 199, 0)
            run(chip, 60000)
            self.assertEqual([f[0] for f in chip.played],
                             adpcm_decode(data, bits), hex(first))

    def test_stereo_starts_with_the_right_channel(self):
        chip = Chip()
        chip.dmac.memory.write(BUF, bytes((0x10, 0x90, 0x20, 0xA0)))
        program_dma(Card(chip), 1, BUF, 4, auto=False)
        reset(chip)
        chip.outb(BASE + 4, 0x0E)
        chip.outb(BASE + 5, 0x02)
        dsp(chip, 0xD1, 0x40, 0xD3, 0x14, 0x03, 0x00)
        run(chip, 5000)
        self.assertEqual(chip.played[0], ((0x90 - 128) << 8, (0x10 - 128) << 8))

    def test_undefined_uses_are_violations(self):
        chip = Chip()
        reset(chip)
        dsp(chip, 0xB6)                 # a register without C6h
        reset(chip)
        dsp(chip, 0xC6, 0x14, 0x00, 0x00)   # Compatibility in Extended
        reset(chip)
        dsp(chip, 0xC6, 0xD0)           # so is the pause
        reset(chip)
        dsp(chip, 0xE2)                 # not the chip's
        chip.inb(BASE + 0x0F)           # the FIFO's own port
        what = [v for _, v in chip.violations]
        self.assertEqual(len(what), 5, what)

    def test_d5_answers_a_byte(self):
        chip = Chip()
        reset(chip)
        dsp(chip, 0xD5)
        self.assertEqual(chip.inb(BASE + 0x0A), 0x01)


class Audio2Test(unittest.TestCase):
    def start(self, chip, ch=0, volume=0x88):
        chip.pnp[(1, 0x75)] = ch
        chip.dmac.memory.write(BUF, bytes(range(256)) * 4)
        program_dma(Card(chip), ch, BUF, 1024)
        mix = {0x71: 0x02, 0x70: 0xF0, 0x74: 0x00, 0x76: 0xFF, 0x7A: 0x40,
               0x7C: volume, 0x78: 0x93}
        for r, v in mix.items():
            chip.outb(BASE + 4, r)
            chip.outb(BASE + 5, v)

    def test_plays_and_latches_its_interrupt(self):
        chip = Chip()
        self.start(chip)
        run(chip, 30000)
        self.assertEqual(len(chip.irq_edges), 1)
        self.assertTrue(chip.mixer[0x7A] & 0x80)
        self.assertEqual(chip.inb(0x256) & 0x02, 0x02)
        chip.outb(BASE + 4, 0x7A)
        chip.outb(BASE + 5, 0x40)       # bit 7 = 0 acknowledges
        self.assertFalse(chip.irq_line)
        self.assertEqual([f[0] for f in chip.played2][:2], [-0x8000, -0x7F00])
        self.assertEqual(chip.heard, chip.played2)

    def test_its_volume_at_0_is_silence(self):
        chip = Chip()
        self.start(chip, volume=0)
        run(chip, 5000)
        self.assertEqual(chip.played2, [])
        self.assertNotEqual(chip.muted2, [])

    def test_a_word_channel_gives_it_the_low_bytes(self):
        """on a 16-bit channel a DMA cycle moves a word, and the chip keeps
        its low byte (docs/VXD_INTERNALS.md)"""
        chip = Chip()
        self.start(chip, ch=5)
        run(chip, 5000)
        self.assertEqual([f[0] for f in chip.played2][:3],
                         [-0x8000, -0x7E00, -0x7C00])

    def test_reset_keeps_the_mixer(self):
        """a reset keeps 7Ah and 7Ch, against the data sheet, as the first
        card measured it (rst.ext), and Audio 2's DMA goes on"""
        chip = Chip()
        for r, v in ((0x7A, 0x47), (0x7C, 0x88), (0x78, 0x93)):
            chip.outb(BASE + 4, r)
            chip.outb(BASE + 5, v)
        reset(chip)
        self.assertEqual((chip.mixer[0x7A], chip.mixer[0x7C]), (0x47, 0x88))
        self.assertEqual(chip.mixer[0x78], 0x93)

    def test_mixer_reset(self):
        """a write to mixer 00h sets the levels it measured and keeps 70h
        to 7Ch (rst.mixer)"""
        chip = Chip()
        for r, v in ((0x7A, 0x47), (0x14, 0x99), (0x38, 0x66)):
            chip.outb(BASE + 4, r)
            chip.outb(BASE + 5, v)
        chip.outb(BASE + 4, 0x00)
        chip.outb(BASE + 5, 0x00)
        self.assertEqual((chip.mixer[0x7A], chip.mixer[0x14],
                          chip.mixer[0x38]), (0x47, 0x88, 0x00))


if __name__ == "__main__":
    unittest.main()
