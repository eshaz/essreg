# (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
#
# Licensed under GPL Version 3.0
"""The side of the Sound Blaster 16 fronts that a game doesn't see, for
esssb16.com through QPI and for ES1869.VXD's front in a DOS box, each in
front of the ES1869 model (esschip): the chip's Extended mode registers
behind each transfer with the rates ES1869.DRV would pick, the game's
16-bit DMA channel carried over to Audio 1's, the chip's Compatibility mode
for the firmware's commands, and the chip's mixer behind the Sound Blaster
16's view. What the game sees is tests/test_sb16conf.py's, against the
Sound Blaster 16 model; what only one front does is tests/test_esssb16.py's
and tests/test_vxddos.py's.

usage: python3 -m unittest tests.test_sb16front   (needs nasm and unicorn)
"""

import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from test_vxdext import HAVE_UNICORN, have_nasm  # noqa: E402
from sb16 import rate_to_v, tc_to_v  # noqa: E402
from sbgame import (BASE, reset, write, read, read_all, mixer,  # noqa: E402
                    program_dma)

if HAVE_UNICORN:
    from sbfronts import (TSRCard, VxDCard, DOS_MIXER, F_ESS,  # noqa: E402
                          F_EXT, F_AUTO, F_RUN, F_16, F_LAST, F_PAUSE,
                          clocks, written, play16)


def fresh(c):
    """the card with the chip's logs emptied of what came before the game:
    the TSR's installer, or ESS's taking of the DSP for the VM"""
    c.chip.commands, c.chip.log, c.chip.dma_log = [], [], []
    c.chip.resets = 0
    return c


def mixer_writes(chip):
    """the chip's mixer registers in the order the log wrote them"""
    out, index = [], None
    for op, port, v in chip.log:
        if op != "out":
            continue
        if port == BASE + 4:
            index = v
        elif port == BASE + 5:
            out.append((index, v))
    return out


def a2_regs(v):
    """70h and 72h for the rate generator's value v: the divisor of the
    clock that comes nearer (793.8 kHz, or 768 kHz with bit 7), 128 at most,
    and the filter clock FDh from 44.1 kHz on, else as for A2h"""
    picks = []
    for k, bit in ((4464, 0x00), (4319, 0x80)):
        d = k // v + (1 if 2 * (k % v) >= v else 0)
        picks.append((abs(k - d * v), d, bit))
    (e1, d1, b1), (e2, d2, b2) = picks
    d, bit = (d2, b2) if e2 < e1 else (d1, b1)
    r70 = ((-min(d, 128)) & 0x7F) | bit
    r72 = 0xFD if v >= 0xF8 else (256 - ((2245 // v + 1) >> 1)) & 0xFF
    return r70, r72


class FrontChecks:
    """the checks; the subclass gives card(**kw), a game's card on its
    front with the chip's logs empty (mixer=: the chip's levels before)"""

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
        self.assertEqual((w[0xA1], w[0xA2]), clocks(248))
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
        self.assertEqual(c.flags(), F_EXT | F_RUN | F_16 | F_AUTO)
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
                             clocks(rate_to_v(rate)), rate)
            self.assertEqual(c.chip.violations, [])
        for tc in (0x06, 0x83, 0xA5, 0xC8, 0xD3, 0xEB, 0xFF):
            c = self.card()
            write(c, 0x40, tc, 0xC6, 0x30, 0xFF, 0x00)
            w = c.chip.ctrl
            self.assertEqual((w[0xA1], w[0xA2]), clocks(tc_to_v(tc)),
                             hex(tc))

    def test_long_block_in_halves(self):
        """a 16-bit block of 33000 samples, 66000 bytes, more than the
        chip's counter holds: the counter gets half of it and the chip
        auto-initializes, also for a single-cycle block; the first half's
        interrupt is the front's own, which 82h doesn't show, and a
        single-cycle block's auto-initialize ends with it"""
        for dma2, (lo, hi), on in ((5, (0xA4, 0xA5), "b8"),
                                   (3, (0x74, 0x76), "78")):
            c = self.card(dma2=dma2)
            chip = c.chip
            n = 33000
            program_dma(c, 5, 0x40000, 32768)
            write(c, 0x41, 0xAC, 0x44, 0xB0, 0x30, (n - 1) & 0xFF,
                  (n - 1) >> 8)
            regs = chip.ctrl if lo == 0xA4 else chip.mixer
            self.assertEqual(regs[lo] | regs[hi] << 8, -n & 0xFFFF)

            def auto():
                return bool(chip.ctrl[0xB8] & 0x04) if on == "b8" else \
                    bool(chip.mixer[0x78] & 0x10)
            self.assertTrue(auto())
            c.wait(250000)          # the first half's end: 16500 * 2 bytes
            self.assertEqual(mixer(c, 0x82) & 0x03, 0, dma2)
            self.assertFalse(auto())
            c.wait(300000)
            self.assertEqual(mixer(c, 0x82) & 0x03, 0x02, dma2)
            self.assertEqual(len(c.dac(16)), n // 2)
            self.assertClean()

    def test_sick_chip(self):
        """a chip that takes no byte: after the first one waits out its
        polls, nothing goes to the chip, and no answer is waited for;
        the game's next reset resets the chip, and the next transfer
        plays"""
        c, chip = self.c, self.chip
        chip.busy = True
        t = chip.clock.us
        play16(c)
        self.assertEqual(chip.commands, [])
        self.assertLess(chip.clock.us - t, 5000)    # one wait, not one a byte
        resets = chip.resets
        chip.busy = False
        self.assertTrue(reset(c))
        self.assertEqual(chip.resets, resets + 1)
        play16(c)
        self.assertEqual(chip.commands[:2], [(0xC6,), (0xD1,)])
        c.wait(30000)
        self.assertGreater(len(chip.played), 1000)
        self.assertClean()

    def test_filter_by_direction(self):
        """at 37800 Hz, the fast clock's divisor 21, where ES1869.DRV's two
        filter tables differ: playback takes its playback table, a
        recording the recording one (6:2308)"""
        v = rate_to_v(37800)
        self.assertNotEqual(clocks(v), clocks(v, record=True))
        c = self.card()
        play16(c, mode=0x10, samples=256, rate=37800)
        self.assertEqual((c.chip.ctrl[0xA1], c.chip.ctrl[0xA2]), clocks(v))
        c = self.card()
        program_dma(c, 5, 0x60000, 4096, record=True)
        write(c, 0x41, 37800 >> 8, 37800 & 0xFF, 0xBE, 0x10, 0xFF, 0x03)
        self.assertEqual((c.chip.ctrl[0xA1], c.chip.ctrl[0xA2]),
                         clocks(v, record=True))
        self.assertEqual(c.chip.violations, [])

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

    def test_a_transfer_of_another_kind(self):
        """a 16-bit transfer that ends a running 8-bit one on Audio 1 starts
        on an empty FIFO, however many 8-bit bytes it held (each 8-bit
        sample changes their number's parity): none of them plays as a
        16-bit sample, and the 16-bit samples play whole"""
        for delay in range(1000, 1100, 9):
            c = self.card()
            chip = c.chip
            c.poke(0x14000, b"\x55" * 4096)
            c.poke(0x24680, b"\x34\x12" * 2048)
            program_dma(c, 1, 0x14000, 4096)
            write(c, 0x41, 0x56, 0x22, 0xC6, 0x00, 0xFF, 0x07)
            c.wait(delay)
            mark = len(chip.played)
            play16(c, mode=0x10, samples=2048, rate=22050)
            c.wait(20000)
            after = chip.played[mark:]
            self.assertEqual(after.count((0x5555,)), 0, delay)
            self.assertEqual(set(after[-200:]), {(0x1234,)}, delay)
            self.assertClean()

    def stereo8(self, c):
        """an 8-bit stereo transfer on the game's own channel, unmasked
        before the command, left 10h and right F0h: the frames played"""
        c.poke(0x14000, b"\x10\xF0" * 2048)
        program_dma(c, 1, 0x14000, 4096)
        write(c, 0x41, 0x56, 0x22, 0xC6, 0x20, 0xFF, 0x07)
        c.wait(20000)
        return self.chip.played

    def test_start_holds_the_channel(self):
        """the channel masked over B8h bit 0 and unmasked once the start
        settled: no byte is lost, and the sides stay as written
        (fix.s8late against fix.s8base)"""
        played = self.stereo8(self.c)
        self.assertGreater(len(played), 100)
        self.assertEqual(set(played), {(-0x7000, 0x7000)})
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
        self.assertEqual(c.flags() & F_AUTO, 0)
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
        self.assertEqual(c.flags() & (F_EXT | F_RUN | F_AUTO), F_EXT)
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

    def test_queued_block_then_firmware(self):
        """a single-cycle block queued behind an auto-initialized
        transfer, which a pause holds, then a command of the firmware's:
        the chip's own reset ends the transfer and what was queued, so the
        acknowledge of the firmware's interrupt sends the chip nothing, and
        no Extended register in Compatibility mode"""
        c, chip = self.c, self.chip
        program_dma(c, 1, 0x14000, 4096)
        write(c, 0x41, 0x56, 0x22, 0xC6, 0x00, 0xFF, 0x07)
        write(c, 0xC0, 0x00, 0xFF, 0x03, 0xD0)
        self.assertEqual(c.flags() & (F_RUN | F_AUTO | F_LAST | F_PAUSE),
                         F_RUN | F_AUTO | F_LAST | F_PAUSE)
        program_dma(c, 1, 0x20000, 512, auto=False)
        write(c, 0x40, 0xA5, 0x75, 0xFF, 0x01)
        self.assertEqual(chip.resets, 1)
        self.assertFalse(chip.ext_mode)
        self.assertEqual(c.flags() & (F_EXT | F_RUN | F_AUTO | F_16 | F_LAST
                                      | F_PAUSE), 0)
        c.wait(120000)
        self.assertTrue(chip.a1_flags & 4)          # the firmware's
        chip.commands = []
        c.inp(BASE + 0x0E)
        self.assertFalse(chip.a1_flags & 4)
        self.assertEqual(chip.commands, [])
        write(c, 0xD4)                              # nothing to continue
        self.assertEqual(chip.commands, [])
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
        mode: from then on every access is the chip's, until a reset, whose
        AAh the front takes from the chip and answers itself"""
        c, chip = self.c, self.chip
        write(c, 0xC6, 0xA1, 0x6E)
        self.assertEqual(chip.commands, [(0xC6,), (0xA1, 0x6E)])
        self.assertEqual(c.flags(), F_ESS | F_EXT)
        write(c, 0xC0, 0xA1)
        self.assertEqual(read(c), 0x6E)
        mixer(c, 0x30, 0x55)                # the chip's mixer as it is
        self.assertEqual(chip.mixer[0x30], 0x55)
        self.assertEqual(mixer(c, 0x30), 0x55)
        self.assertTrue(reset(c))           # the chip's own reset
        self.assertEqual(chip.resets, 1)
        c.wait(100)
        self.assertEqual(chip.out, [])
        self.assertEqual(c.flags(), 0)
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

    # --- Audio 2 on channel 3: the 16-bit playback's -------------------------
    def a2(self, **kw):
        c = self.card(dma2=3, **kw)
        self.assertTrue(c.audio2)
        return c

    def test_audio2_16bit(self):
        """B6h, 44100 Hz, on Audio 2 as ES1869.DRV starts it: no DSP
        command, 78h stopped first and started last, with the rate, the
        bytes, the voice's volume, the format and its own rate (71h bit 1)
        between; the game's channel 5 carried over to channel 3"""
        c = self.a2()
        chip = c.chip
        play16(c)
        self.assertEqual(chip.commands, [])
        a2 = [w for w in mixer_writes(chip) if 0x70 <= w[0] <= 0x7C]
        self.assertEqual((a2[0], a2[-1]), ((0x78, 0x00), (0x78, 0x93)))
        m = chip.mixer
        self.assertEqual((m[0x70], m[0x72]), (0x6E, 0xFD))
        self.assertEqual((m[0x74], m[0x76]), (0x00, 0xF0))      # 4096 bytes
        self.assertEqual((m[0x7A], m[0x7C]), (0x47, 0x88))
        self.assertEqual(m[0x71] & 0x22, 0x02)
        self.assertEqual(chip.dma[3], {"mask": False, "mode": 0x1B,
                                       "addr": 0x4680, "count": 0x1FFF,
                                       "page": 0x02})
        c.wait(30000)
        self.assertGreater(len(chip.played2), 1000)
        self.assertEqual(chip.played, [])
        self.assertClean()

    def test_audio2_rates(self):
        """70h and 72h for the Sound Blaster 16's rate: the clock whose
        divisor comes nearer, as ES1869.DRV picks it, 6 kHz at the least"""
        for rate in (48000, 44100, 32000, 22050, 23000, 16000, 11025, 8000,
                     5000):
            c = self.a2()
            play16(c, mode=0x10, samples=256, rate=rate)
            m = c.chip.mixer
            self.assertEqual((m[0x70], m[0x72]), a2_regs(rate_to_v(rate)),
                             rate)

    def test_audio2_pause_continue_end(self):
        """D5h and D6h: Audio 2's FIFO off and on its DAC (78h bit 0), the
        DMA waiting in between; D9h: the block that plays is the last (78h
        bit 4)"""
        c = self.a2()
        chip = c.chip
        play16(c)
        c.wait(3000)
        write(c, 0xD5)
        self.assertEqual(chip.mixer[0x78], 0x92)
        n = len(chip.played2)
        c.wait(10000)
        self.assertEqual(len(chip.played2), n)
        write(c, 0xD0)                      # the 8-bit kind: nothing
        self.assertEqual(chip.mixer[0x78], 0x92)
        write(c, 0xD6)
        self.assertEqual(chip.mixer[0x78], 0x93)
        write(c, 0xD9)
        self.assertEqual(chip.mixer[0x78], 0x83)
        self.assertEqual(chip.commands, [])
        self.assertClean()

    def test_audio2_acknowledge(self):
        """82h bit 1 for Audio 2's latch (7Ah bit 7), which +Fh clears and
        +Eh doesn't"""
        c = self.a2()
        chip = c.chip
        play16(c, samples=64)
        c.wait(2000)
        self.assertTrue(chip.mixer[0x7A] & 0x80)
        self.assertEqual(mixer(c, 0x82), 0x22)
        c.inp(BASE + 0x0E)
        self.assertTrue(chip.mixer[0x7A] & 0x80)
        c.inp(BASE + 0x0F)
        self.assertEqual(chip.mixer[0x7A], 0x47)
        self.assertEqual(mixer(c, 0x82), 0x20)
        self.assertClean()

    def test_audio2_through_a_chip_reset(self):
        """a firmware command while Audio 2 plays and Audio 1 is in Extended
        mode: the chip's own reset clears 7Ah and 7Ch (DS p.51, p.65),
        which go back at once, and Audio 2 plays on; without Extended mode
        no reset is needed"""
        c = self.a2()
        chip = c.chip
        play16(c)
        write(c, 0x10, 0x80)                # the direct DAC: no reset
        self.assertEqual(chip.resets, 0)
        program_dma(c, 1, 0x60000, 256, auto=False)
        write(c, 0xC0, 0x00, 0xFF, 0x00)    # 8-bit on Audio 1, Extended
        c.wait(15000)
        write(c, 0x10, 0x80)                # the direct DAC: the reset
        self.assertEqual(chip.resets, 2)    # and the one into Extended mode
        self.assertEqual((chip.mixer[0x78], chip.mixer[0x7A],
                          chip.mixer[0x7C]), (0x93, 0x47, 0x88))
        n = len(chip.played2)
        c.wait(10000)
        self.assertGreater(len(chip.played2), n + 300)
        self.assertClean()

    def test_audio2_reset(self):
        """the game's reset stops Audio 2 (78h bits 4 and 1), turns its
        interrupt off and clears its latch (7Ah bits 6 and 7); what its
        FIFO holds plays out in the stream's format, whole frames, so that
        nothing is left to put the next stream's channels off by a byte"""
        c = self.a2()
        chip = c.chip
        play16(c)
        c.wait(3000)
        n = len(chip.played2)
        self.assertTrue(reset(c))
        self.assertEqual((chip.mixer[0x78], chip.mixer[0x7A]), (0x81, 0x07))
        c.wait(3000)
        self.assertEqual(len(chip.a2.fifo), 0)
        self.assertGreater(len(chip.played2), n)
        self.assertTrue(all(len(f) == 2 for f in chip.played2[n:]))
        self.assertClean()

    def test_audio2_one_16bit_transfer(self):
        """a 16-bit recording records on Audio 1, and Audio 2's playback
        ends: a card has one 16-bit transfer"""
        c = self.a2()
        chip = c.chip
        play16(c)
        program_dma(c, 5, 0x60000, 4096, record=True)
        write(c, 0xBE, 0x30, 0xFF, 0x03)
        self.assertEqual(chip.mixer[0x78] & 0x12, 0)
        self.assertEqual(chip.ctrl[0xB8] & 0x0F, 0x0F)
        self.assertEqual(chip.dma[1]["mode"] & 0x0C, 0x04)
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
        """the chip's own mixer registers go to it as they come (what the
        view shows then is each front's)"""
        c, chip = self.c, self.chip
        for reg, v in ((0x60, 0x20), (0x14, 0x5A), (0x1A, 0x99),
                       (0x1C, 0x12), (0x7C, 0x55)):
            mixer(c, reg, v)
            self.assertEqual((chip.mixer[reg], mixer(c, reg)), (v, v))

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
        its other registers left (Audio 2's volume follows the voice where
        Audio 2 plays the game's 16-bit sound)"""
        c = self.card(mixer=DOS_MIXER)
        mixer(c, 0x30, 0x10)
        keep = {r: c.chip.mixer[r] for r in (0x50, 0x52, 0x7C)}
        mixer(c, 0x00, 0x00)
        self.assertEqual(c.chip.index, 0x00)        # the game's index kept
        hw = c.chip.mixer
        self.assertEqual(hw[0x00], 0x77)            # never the chip's reset
        self.assertEqual(mixer(c, 0x30), 0xC0)      # the view's reset
        self.assertEqual(mixer(c, 0x3C), 0x1F)
        self.assertEqual((hw[0x60], hw[0x62]), (0x30, 0x30))  # C0h, 6 bits
        self.assertEqual((hw[0x14], hw[0x36]), (0xCC, 0xCC))
        self.assertEqual((hw[0x38], hw[0x3E], hw[0x1A], hw[0x3C]),
                         (0, 0, 0, 0))
        self.assertEqual(hw[0x1C], 0x15)    # mic, CD and line: the record mix
        self.assertEqual(hw[0x0E], 0x20)    # the Sound Blaster Pro's stereo off
        if c.audio2:
            keep[0x7C] = 0xCC
        for reg in (0x50, 0x52, 0x7C):
            self.assertEqual(hw[reg], keep[reg], hex(reg))
        three_d = DOS_MIXER[0x50]
        if isinstance(c, TSRCard):
            three_d &= ~0x08        # esssb16 bypasses the effect as it loads
        self.assertEqual((hw[0x50], hw[0x52]), (three_d, DOS_MIXER[0x52]))

    def test_the_mixer_a_game_finds(self):
        """the view at the start shows the levels the chip has"""
        c = self.card(mixer=DOS_MIXER)
        view = [mixer(c, r) for r in range(0x30, 0x48)]
        self.assertEqual(view, [
            0xE8, 0x00,                 # master, the right muted
            0xA0, 0xB0, 0xC0, 0xD0,     # voice, MIDI
            0x10, 0x20, 0x30, 0x40,     # CD, line
            0x50, 0xC0,                 # mic, PC speaker
            0x1F, 0x10, 0x08,           # the switches: line records
            0, 0, 0, 0, 0, 0x80, 0x80, 0x80, 0x80])
        # writing back what it read changes nothing
        before = list(c.chip.mixer)
        for r, v in zip(range(0x30, 0x48), view):
            if r != 0x31:               # the muted master can't come back
                mixer(c, r, v)          # muted
        self.assertEqual([(r, v) for r, v in enumerate(c.chip.mixer)
                          if v != before[r]], [])
        # the master's 6 bits in the 5 of the CT1745
        c = self.card(mixer={0x60: 0x2D, 0x62: 0x13})
        self.assertEqual((mixer(c, 0x30), mixer(c, 0x31)), (0xB0, 0x48))


@unittest.skipUnless(HAVE_UNICORN and have_nasm(), "needs nasm and unicorn")
class TSRFrontTest(FrontChecks, unittest.TestCase):
    """esssb16.com through QPI, with a callback before the program's"""

    def card(self, **kw):
        return fresh(TSRCard("qpi", **kw))

    def test_mask_readback(self):
        """the game's own channel masked and unmasked by the front only
        where the chipset gives the mask back: without it (0Fh and DEh read
        FFh) the channel is left alone, and 8-bit stereo plays a byte off,
        its sides swapped, as on such a machine (fix.s8base)"""
        for readback, writes, frame in ((True, [0x05, 0x01], (-0x7000, 0x7000)),
                                        (False, [], (0x7000, -0x7000))):
            c = self.card()
            dmac = c.chip.dmac
            if not readback:
                inb = dmac.inb
                dmac.inb = lambda port: 0xFF if port in (0x0F, 0xDE) \
                    else inb(port)
            c.poke(0x14000, b"\x10\xF0" * 2048)
            program_dma(c, 1, 0x14000, 4096)
            mark = len(dmac.log)
            write(c, 0x41, 0x56, 0x22, 0xC6, 0x20, 0xFF, 0x07)
            c.wait(20000)
            self.assertEqual([v for p, v in dmac.log[mark:] if p == 0x0A],
                             writes, readback)
            self.assertEqual(set(c.chip.played), {frame}, readback)


@unittest.skipUnless(HAVE_UNICORN and have_nasm(), "needs nasm and unicorn")
class TSRUntrappedFrontTest(FrontChecks, unittest.TestCase):
    """esssb16.com through QPI, without a callback before the program's:
    QPI's untrapped I/O for the ports the front passes on"""

    def card(self, **kw):
        return fresh(TSRCard("qpi", old_callback=False, **kw))


@unittest.skipUnless(HAVE_UNICORN and have_nasm(), "needs nasm and unicorn")
class TSRProtectedFrontTest(FrontChecks, unittest.TestCase):
    """esssb16.com through HDPMI32i's handler, for a protected-mode game"""

    def card(self, **kw):
        return fresh(TSRCard("pm", **kw))


@unittest.skipUnless(HAVE_UNICORN and have_nasm(), "needs nasm and unicorn")
class VxDFrontTest(FrontChecks, unittest.TestCase):
    """ES1869.VXD's front, for a game in a DOS box that has taken the DSP"""

    def card(self, **kw):
        c = VxDCard(**kw)
        c.inp(BASE + 0x0E)
        return fresh(c)


if __name__ == "__main__":
    unittest.main()
