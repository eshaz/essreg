# (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
#
# Licensed under GPL Version 3.0
"""The Sound Blaster 16 model (sb16.py) against what the real card does.

Each test holds one fact of the DSP's firmware or of Creative's guide, the
way a game would see it: the reset's handshake, the answers and their
latch, the operands of every command, the rates, the two DMA engines with
their blocks, interrupts and acknowledges, the queue, the stall, pause and
continue, the 8-bit format the older commands keep, E2h, the CSP commands
of a card without one, MIDI, the warm boot, and the CT1745 mixer. The
fronts' conformance tests (test_sb16conf.py) hold esssb16.com and
ES1869.VXD to this model.

usage: python3 -m unittest tests.test_sb16
"""

import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import sbgame  # noqa: E402
from sbgame import (BASE, ModelCard, reset, write, read, read_all,  # noqa
                    mixer, program_dma, dma_count, sb16_isr, words)
from sb16 import (COPYRIGHT, tc_to_v, rate_to_v, v_hz, SB16)  # noqa: E402

BUF = 0x20000               # the game's DMA buffer


def card(**kw):
    c = ModelCard(**kw)
    c.wait(100)
    assert reset(c)
    return c


class ResetTest(unittest.TestCase):
    def test_handshake(self):
        c = ModelCard()
        c.out(BASE + 6, 1)
        self.assertEqual(c.inp(BASE + 0x0C) & 0x80, 0x80)  # busy in reset
        c.wait(3)
        c.out(BASE + 6, 0)
        self.assertEqual(c.inp(BASE + 0x0E) & 0x80, 0)     # AAh not yet
        c.wait(60)
        self.assertEqual(c.inp(BASE + 0x0E), 0xFF)         # bits 6-0 are 1s
        self.assertEqual(c.inp(BASE + 0x0A), 0xAA)
        self.assertEqual(c.inp(BASE + 0x0E), 0x7F)
        self.assertEqual(c.inp(BASE + 0x0A), 0xAA)         # read again: same

    def test_what_a_reset_clears(self):
        c = card()
        write(c, 0xE4, 0x5A, 0xD1, 0x48, 0x34, 0x12, 0x40, 0xA5)
        reset(c)
        write(c, 0xE8)
        self.assertEqual(read(c), 0x5A)         # the test register stays
        write(c, 0xD8)
        self.assertEqual(read(c), 0x00)         # the speaker flag doesn't
        self.assertEqual(c.sb.block8, 0x07FF)
        self.assertEqual(c.sb.v, 0x38)          # 9958 Hz
        self.assertAlmostEqual(v_hz(0x38), 9958.06, places=1)

    def test_warm_boot_after_high_speed(self):
        """a reset that ends high-speed mode keeps the DSP's settings"""
        c = card()
        write(c, 0x40, 0xD3, 0x48, 0xFF, 0x00, 0x90)
        reset(c)
        self.assertEqual((c.sb.v, c.sb.block8), (0x7C, 0x00FF))
        reset(c)                                # a cold boot now
        self.assertEqual((c.sb.v, c.sb.block8), (0x38, 0x07FF))

    def test_the_transfer_stops(self):
        c = card()
        program_dma(c, 1, BUF, 0x1000)
        write(c, 0x1C)
        c.wait(1000)
        n = len(c.played)
        c.out(BASE + 6, 1)
        c.wait(3000)
        self.assertEqual(len(c.played), n)


class AnswerTest(unittest.TestCase):
    def test_version_and_copyright(self):
        c = card()
        write(c, 0xE1)
        self.assertEqual((read(c), read(c)), (4, 5))
        write(c, 0xE3)
        self.assertEqual(bytes(read_all(c)), COPYRIGHT)
        self.assertEqual(len(COPYRIGHT), 45)
        self.assertEqual(SB16(None, version=(4, 13)).version, (4, 13))

    def test_one_byte_at_a_time(self):
        """no queue: the firmware writes each byte once the last is read,
        and takes no command until then; an unread byte comes first"""
        c = card()
        write(c, 0xE0, 0x0F, 0xE1)
        self.assertEqual(read_all(c), [0xF0, 0x04, 0x05])
        write(c, 0xE1, 0xE0)                    # waits in the input latch
        self.assertEqual(c.inp(BASE + 0x0C) & 0x80, 0x80)
        self.assertFalse(write(c, 0x0F))        # a driver times out
        self.assertEqual(read_all(c), [0x04, 0x05])
        write(c, 0x0F)                          # E0h's operand now
        self.assertEqual(read_all(c), [0xF0])

    def test_f8h_overwrites(self):
        c = card()
        write(c, 0xE0, 0x0F, 0xF8)
        self.assertEqual(read_all(c), [0x00])

    def test_silent_commands(self):
        """E7h (ESS's identify), F1h and the no-ops answer nothing"""
        c = card()
        for cmd in (0xE7, 0xF1, 0x00, 0x43, 0x45, 0x47, 0x5F, 0xA8, 0xD2,
                    0xE5, 0xFE):
            write(c, cmd)
            self.assertEqual(read_all(c), [], hex(cmd))
        write(c, 0xE1)
        self.assertEqual(read_all(c), [4, 5])   # still in step

    def test_test_register_and_speaker(self):
        c = card()
        write(c, 0xE4, 0x96, 0xE8)
        self.assertEqual(read(c), 0x96)
        write(c, 0xD1, 0xD8)
        self.assertEqual(read(c), 0xFF)
        write(c, 0xD3, 0xD8)
        self.assertEqual(read(c), 0x00)

    def test_rom_id_and_status(self):
        c = card()
        write(c, 0xF4)
        self.assertEqual(read_all(c), [0x7D, 0x1B])
        write(c, 0xE1, 0xFD)
        read(c), read(c)
        self.assertEqual(read(c), 0xE1)          # FDh: the command before
        write(c, 0xF9, 0x37)
        self.assertEqual(read(c), 0x38)          # the rate in RAM 37h
        write(c, 0xFA, 0x37, 0x7C, 0xF9, 0x37)
        self.assertEqual(read(c), 0x7C)
        self.assertEqual(c.sb.v, 0x7C)


class OperandTest(unittest.TestCase):
    """each command takes its operands; a byte too many or too few would
    turn the rest of the stream into commands"""

    def counts(self, version=(4, 5)):
        sb = SB16(None, version=version)
        return [sb.operands(c) for c in range(256)]

    def test_the_firmware_map(self):
        n = self.counts()
        expect = {0x02: 1, 0x04: 1, 0x05: 2, 0x0B: 1, 0x0C: 1, 0x0E: 2,
                  0x0F: 1, 0x40: 1, 0x41: 2, 0x42: 2, 0x48: 2, 0x4C: 2,
                  0x4D: 1, 0x4E: 2, 0x4F: 2, 0xE0: 1, 0xE2: 1, 0xE4: 1,
                  0xF9: 1, 0xFA: 2}
        for c in range(0x10, 0x14):
            expect[c] = 1               # direct DAC and its aliases
        for c in list(range(0x14, 0x18)) + list(range(0x24, 0x28)) + \
                list(range(0x74, 0x78)) + list(range(0x80, 0x90)):
            expect[c] = 2
        for c in range(0x38, 0x40):
            expect[c] = 1
        for c in range(0xB0, 0xD0):
            expect[c] = 3
        self.assertEqual({c: v for c, v in enumerate(n) if v}, expect)
        self.assertEqual(n[0x08], 0)    # not "08h 03h"
        self.assertEqual(self.counts((4, 13))[0x43], 2)
        self.assertEqual(self.counts((4, 13))[0xDC], 1)

    def test_aliases_are_the_commands(self):
        """11h plays a sample like 10h, 25h records like 24h, 81h is a
        silence like 80h"""
        c = card()
        write(c, 0x11, 0xC0)
        self.assertEqual(c.played[-1], (0x40 << 8,))
        write(c, 0x40, 0xD3, 0x81, 0x09, 0x00)  # 10 sample periods
        c.wait(2000)
        self.assertEqual(c.sb.raised[-1][1], 8)

    def test_csp_commands_without_a_csp(self):
        c = card()
        write(c, 0x08)
        self.assertEqual(read_all(c), [0xFF])
        write(c, 0x03)
        self.assertEqual(read_all(c), [0xFF])
        write(c, 0x0F, 0x83)
        self.assertEqual(read_all(c), [0xFF])
        write(c, 0x04, 0x02, 0x05, 0x01, 0x02, 0x0E, 0x80, 0x00)
        write(c, 0x06, 0x0A)
        self.assertEqual(read_all(c), [1, 0xFF])    # the lock, then 82h
        write(c, 0x07, 0x09)
        self.assertEqual(read_all(c), [0, 0])
        write(c, 0x0B, 0x00)                        # waits for the CSP:
        write(c, 0xE1)                              # hung until a reset
        self.assertEqual(read_all(c), [])
        self.assertTrue(reset(c))
        write(c, 0xE1)
        self.assertEqual(read_all(c), [4, 5])


class RateTest(unittest.TestCase):
    def test_the_generator(self):
        self.assertEqual(v_hz(0xF8), 44100)
        self.assertEqual(v_hz(0x7C), 22050)
        self.assertEqual(v_hz(0x3E), 11025)

    def test_time_constants(self):
        for tc, hz in ((0xA5, 10847), (0xD2, 21694), (0xD3, 22050),
                       (0xE9, 43389), (0xEA, 45345), (0xFF, 45345),
                       (0x83, 7824)):
            self.assertEqual(round(v_hz(tc_to_v(tc))), hz, hex(tc))
        self.assertEqual(tc_to_v(0xC8), 0xC8)      # 4.05: 35.6 kHz
        self.assertEqual(tc_to_v(0xC8, (4, 13)), 100)
        self.assertEqual(tc_to_v(0x83, (4, 13)), 0x2D)
        self.assertEqual((tc_to_v(0x68), tc_to_v(0x6C)), (0x25, 0x26))

    def test_rates(self):
        for rate, v in ((44100, 0xF8), (22050, 0x7C), (11025, 0x3E),
                        (48000, 0xFF), (45312, 0xFF), (4000, 0x1C),
                        (8000, 0x2D)):
            self.assertEqual(rate_to_v(rate), v, rate)
        # the carry 4.04-4.12 lose: 16 steps low
        self.assertEqual(round(v_hz(rate_to_v(23000))), 20094)
        self.assertEqual(rate_to_v(23000, (4, 13)), rate_to_v(23000) + 16)

    def test_rates_wait_for_the_next_start(self):
        c = card()
        program_dma(c, 1, BUF, 0x4000)
        write(c, 0x41, 0x56, 0x22, 0xC6, 0x00, 0xFF, 0x0F)
        self.assertEqual(c.sb.hw_v, 0x7C)
        write(c, 0x41, 0xAC, 0x44)
        self.assertEqual(c.sb.hw_v, 0x7C)          # still 22050 Hz
        write(c, 0xD0, 0xD4)
        self.assertEqual(c.sb.hw_v, 0xF8)          # the continue takes it


class TransferTest(unittest.TestCase):
    def play16(self, c, data, length, cmd=0xB6, mode=0x30, rate=44100):
        c.poke(BUF, data)
        program_dma(c, 5, BUF, len(data))
        write(c, 0x41, rate >> 8, rate & 0xFF)
        write(c, cmd, mode, (length - 1) & 0xFF, (length - 1) >> 8)

    def test_16_bit_stereo_auto_init(self):
        c = card()
        c.isr = sb16_isr
        data = words(sbgame.samples16(2048))
        self.play16(c, data, 1024)              # 1024 samples a block
        c.wait(30000)                           # 1323 frames at 44.1 kHz
        self.assertGreaterEqual(c.serviced, 2)
        self.assertEqual(c.irq_status[0], 0x22)     # 16-bit, rev 2
        frames = c.played[:1024]
        self.assertEqual(frames[0], (0x0000, 0x0101))
        flat = [s & 0xFFFF for f in frames for s in f]
        self.assertEqual(flat, sbgame.samples16(2048))
        # each interrupt after 512 frames: 1024 samples, both counted
        times = [t for t, k in c.sb.raised]
        self.assertAlmostEqual((times[1] - times[0]) / 1e6 * 44100, 512,
                               delta=2)

    def test_16_bit_on_the_8_bit_channel(self):
        """without a 16-bit channel the 16-bit engine moves bytes on the
        8-bit one, and still raises the 16-bit interrupt"""
        c = card(dma16=0)
        data = words(sbgame.samples16(256))
        c.poke(BUF, data)
        program_dma(c, 1, BUF, len(data))
        write(c, 0x41, 0xAC, 0x44, 0xB2, 0x10, 0xFF, 0x00)
        c.wait(10000)
        self.assertEqual(c.sb.raised[0][1], 16)
        self.assertEqual(len(c.played), 256)
        self.assertEqual(mixer(c, 0x81), 0x02)

    def test_8_bit_signed_stereo(self):
        c = card()
        c.poke(BUF, bytes([0x10, 0xF0, 0x7F, 0x80]))
        program_dma(c, 1, BUF, 4)
        write(c, 0x41, 0x56, 0x22, 0xC0, 0x30, 0x03, 0x00)
        c.wait(1000)
        self.assertEqual(c.played, [(0x1000, -0x1000), (0x7F00, -0x8000)])
        self.assertEqual(c.sb.raised[0][1], 8)

    def test_the_acknowledges_are_separate(self):
        c = card()
        self.play16(c, bytes(4096), 256)
        c.wait(5000)
        self.assertEqual(mixer(c, 0x82) & 3, 2)
        c.inp(BASE + 0x0E)                      # the 8-bit acknowledge
        self.assertEqual(mixer(c, 0x82) & 3, 2)     # doesn't clear it
        c.inp(BASE + 0x0F)
        self.assertEqual(mixer(c, 0x82) & 3, 0)

    def test_an_unacknowledged_interrupt_stalls(self):
        c = card()
        self.play16(c, bytes(8192), 128)
        c.wait(20000)
        raised = len(c.sb.raised)
        self.assertEqual(raised, 1)             # no acknowledge: stalled
        n = len(c.played)
        c.wait(5000)
        self.assertEqual(len(c.played), n)
        c.inp(BASE + 0x0F)                      # the acknowledge resumes it
        self.assertEqual(len(c.sb.raised), 2)
        c.wait(2000)
        self.assertGreater(len(c.played), n)
        write(c, 0xE1)                          # so does any host byte
        read_all(c)

    def test_single_cycle_queued_after_auto_init(self):
        """B0h while B6h runs: its mode byte goes unused, its length plays
        once as the last block, after the block that runs"""
        c = card()
        c.isr = sb16_isr
        self.play16(c, words(sbgame.samples16(8192)), 512)
        c.wait(2000)
        write(c, 0xB0, 0x00, 0x7F, 0x00)        # 128 samples, unsigned mono
        self.assertTrue(c.sb.e16.switch)
        c.wait(50000)
        self.assertFalse(c.sb.e16.running)
        self.assertEqual(len(c.sb.raised), 2)
        self.assertEqual(len(c.played), (512 + 128) // 2)
        self.assertEqual(len(c.played[-1]), 2)  # still stereo, signed

    def test_two_engines(self):
        """an 8-bit transfer runs on while a 16-bit one starts; the DAC
        hears the one started last, both interrupt"""
        c = card()
        c.isr = sb16_isr
        program_dma(c, 1, BUF + 0x8000, 0x1000)
        write(c, 0x41, 0x56, 0x22, 0xC6, 0x00, 0xFF, 0x00)
        c.wait(2000)
        self.play16(c, bytes(4096), 256, rate=22050)
        c.wait(30000)
        kinds = {k for _t, k in c.sb.raised}
        self.assertEqual(kinds, {8, 16})
        self.assertTrue(c.sb.unheard)           # the 8-bit engine's

    def test_pause_holds_both(self):
        c = card()
        c.isr = sb16_isr
        self.play16(c, bytes(4096), 256)
        c.wait(1000)
        write(c, 0xD5)
        self.assertEqual(c.inp(BASE + 0x0C) & 0x80, 0x80)   # ~100 us busy
        n = len(c.played)
        c.wait(10000)
        self.assertEqual(len(c.played), n)
        write(c, 0xD6)
        c.wait(10000)
        self.assertGreater(len(c.played), n)

    def test_exit_auto_init(self):
        c = card()
        c.isr = sb16_isr
        self.play16(c, bytes(8192), 256)
        c.wait(1000)
        write(c, 0xD9)
        c.wait(30000)
        self.assertFalse(c.sb.e16.running)
        self.assertEqual(len(c.sb.raised), 1)
        self.assertEqual(len(c.played), 128)

    def test_old_commands_keep_the_8_bit_format(self):
        """1Ch after a signed stereo Cxh plays signed stereo; the Cxh's
        length is the block size"""
        c = card()
        c.poke(BUF, bytes([0x40, 0xC0] * 8))
        program_dma(c, 1, BUF, 16)
        write(c, 0xC0, 0x30, 0x01, 0x00)        # 2 bytes, signed stereo
        c.wait(500)
        write(c, 0x1C)
        self.assertEqual(c.sb.block8, 1)
        c.wait(500)
        self.assertEqual(c.played[1], (0x4000, -0x4000))
        reset(c)
        write(c, 0x48, 0x03, 0x00, 0x14, 0x03, 0x00)
        c.wait(500)
        self.assertEqual(len(c.played[-1]), 1)  # after reset: unsigned mono

    def test_e2h(self):
        """the DMA identification writes its byte through the 8-bit
        channel: from AAh and 96h, the key turning right by 2"""
        c = card()
        program_dma(c, 1, BUF, 4, auto=False, record=True)
        out = []
        for x in (0x00, 0x00, 0x5A, 0xFF):
            write(c, 0xE2, x)
            c.wait(50)
            out.append(c.peek(BUF + len(out), 1)[0])
        acc, key, expect = 0xAA, 0x96, []
        for x in (0x00, 0x00, 0x5A, 0xFF):
            acc = (acc + (x ^ key)) & 0xFF
            key = ((key >> 2) | (key << 6)) & 0xFF
            expect.append(acc)
        self.assertEqual(out, expect)
        self.assertEqual(expect[0], 0x40)       # AAh + 96h

    def test_silence(self):
        c = card()
        write(c, 0x40, 0xD3, 0x80, 0xFF, 0x00)  # 256 periods at 22050 Hz
        c.wait(10000)
        self.assertEqual(c.sb.raised, [])
        c.wait(3000)
        self.assertEqual(c.sb.raised[0][1], 8)

    def test_direct_dac(self):
        c = card()
        write(c, 0x10, 0x80, 0x10, 0xFF)
        self.assertEqual(c.played, [(0,), (0x7F00,)])

    def test_dma_count_moves(self):
        c = card()
        self.play16(c, bytes(4096), 1024)
        a = dma_count(c, 5)
        c.wait(2000)
        self.assertLess(dma_count(c, 5), a)


class MidiTest(unittest.TestCase):
    def test_uart_until_reset(self):
        c = card()
        write(c, 0x38, 0x90)
        write(c, 0x35, 0x3C, 0x7F, 0xE1)
        self.assertEqual(c.sb.midi_out, [0x90, 0x3C, 0x7F, 0xE1])
        self.assertEqual(read_all(c), [])
        self.assertTrue(reset(c))
        write(c, 0xE1)
        self.assertEqual(read_all(c), [4, 5])


class MixerTest(unittest.TestCase):
    def test_reset_values(self):
        c = card()
        mixer(c, 0x00, 0)
        got = {r: mixer(c, r) for r in range(0x30, 0x48)}
        self.assertEqual(got, {**{r: 0xC0 for r in range(0x30, 0x36)},
                               **{r: 0 for r in range(0x36, 0x3C)},
                               0x3C: 0x1F, 0x3D: 0x15, 0x3E: 0x0B,
                               **{r: 0 for r in range(0x3F, 0x44)},
                               **{r: 0x80 for r in range(0x44, 0x48)}})
        self.assertEqual([mixer(c, r) for r in (0x04, 0x22, 0x26, 0x28,
                                                0x2E, 0x0A)],
                         [0xCC, 0xCC, 0xCC, 0x00, 0x00, 0x00])

    def test_bits_kept(self):
        c = card()
        for reg, mask in ((0x30, 0xF8), (0x3B, 0xC0), (0x3C, 0x1F),
                          (0x3D, 0x7F), (0x41, 0xC0), (0x43, 0x01),
                          (0x44, 0xF0)):
            mixer(c, reg, 0xFF)
            self.assertEqual(mixer(c, reg), mask, hex(reg))

    def test_the_sound_blaster_pros_registers(self):
        c = card()
        mixer(c, 0x22, 0x9F)
        self.assertEqual((mixer(c, 0x30), mixer(c, 0x31)), (0x98, 0xF8))
        self.assertEqual(mixer(c, 0x22), 0x9F)
        mixer(c, 0x32, 0x50)
        self.assertEqual(mixer(c, 0x04) >> 4, 0x5)
        mixer(c, 0x0A, 0x06)
        self.assertEqual((mixer(c, 0x3A), mixer(c, 0x0A)), (0xD8, 0x06))
        self.assertEqual(mixer(c, 0x0E), 0x00)  # not on a Sound Blaster 16

    def test_card_registers(self):
        c = card()
        self.assertEqual(mixer(c, 0x80), 0xF2)
        self.assertEqual(mixer(c, 0x81), 0x22)
        self.assertEqual(mixer(c, 0x82), 0x20)
        write(c, 0xF2)
        self.assertEqual(mixer(c, 0x82), 0x21)
        write(c, 0xF3)
        self.assertEqual(mixer(c, 0x82), 0x23)
        c.inp(BASE + 0x0E)
        c.inp(BASE + 0x0F)
        self.assertEqual(mixer(c, 0x82), 0x20)
        mixer(c, 0x00, 0)
        self.assertEqual(mixer(c, 0x81), 0x22)  # the reset leaves it
        self.assertEqual(c.inp(BASE + 4), 0x81)


if __name__ == "__main__":
    unittest.main()
