# (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
#
# Licensed under GPL Version 3.0
"""The Sound Blaster 16 front as a state machine, the executable reference
of docs/SB16_FRONT.md: the state the front keeps, the events of the
game's port accesses, and for each the operations it sends the chip, as
esssb16.com and ES1869.VXD's front run them.

FSMCard installs esssb16.com as sbfronts.TSRCard does, so that the chip
and the front's settings start as the installer leaves them, and then
answers the game's trapped accesses with Front, in Python, which reaches
the chip's ports and the DMA controllers as ring 0 does. The tests run
the scenes on it and compare what it sent the chip with what the fronts
sent (tests/sbops.py, tests/test_sbfsm.py).

The names follow esssb16.asm's, so that each transition can be found in
both, and so do the accesses: the polls before each byte, the register
reads through C0h, the mixer index put back after each access.
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from sbfronts import TSRCard  # noqa: E402

DSP_POLLS = 0x400
MIX_LEN = 24
VERSION = (4, 5)

# the state flags of Audio 1's engine (flags) and Audio 2's (flags2)
F_ESS, F_EXT, F_AUTO, F_RUN, F_16 = 0x01, 0x02, 0x04, 0x08, 0x10
F_LAST, F_PAUSE, F_RESET = 0x20, 0x40, 0x80
# the DSP's state
S_FULL, S_HELD, S_UART, S_MIDI = 0x01, 0x02, 0x04, 0x08
S_COMP, S_WARM, S_SPKR, S_T16 = 0x10, 0x20, 0x40, 0x80
# a 16-bit block of more than 32768 samples in halves
H_ON, H_SECOND, H_ONCE, H_A2 = 0x01, 0x02, 0x04, 4
H_ALL = H_ON | H_SECOND | H_ONCE
# the game's 16-bit channel
V_MASK, V_FF, V_NEW, V_LIVE = 0x01, 0x02, 0x04, 0x08

# the operands each DSP command takes, as DSP 4.05's firmware takes them
OPTABLE = bytes((0x10, 0x09, 0x40, 0x61, 0x55, 0xAA, 0x00, 0x00,
                 0x00, 0xAA, 0x00, 0x00, 0x00, 0x00, 0x55, 0x55,
                 0x29, 0x00, 0x02, 0xA6, 0x00, 0x00, 0x00, 0x00,
                 0x00, 0x00, 0x00, 0x00, 0x00, 0xAA, 0x00, 0x00,
                 0xAA, 0xAA, 0xAA, 0xAA, 0x00, 0x00, 0x00, 0x00,
                 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF,
                 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
                 0x11, 0x01, 0x00, 0x00, 0x00, 0x00, 0x24, 0x00))
COPYRIGHT = b"COPYRIGHT (C) CREATIVE TECHNOLOGY LTD, 1992.\0"
# the commands that answer bytes that never change
ANSWERS = {0x03: b"\xFF", 0x08: b"\xFF", 0x09: b"\0\0", 0x0A: b"\0",
           0x4D: b"\0", 0xF9: b"\0", 0xFB: b"\0", 0xFC: b"\0",
           0xE1: bytes(VERSION), 0xE3: COPYRIGHT, 0xF4: b"\x7D\x1B"}
# Dxh: 1 the speaker on, 2 off, 0 nothing; for the engines 80h, F_RUN and
# F_16 for the 16-bit one, and 40h pause, 20h continue, else the end of
# auto-initialize
DX_OPS = (0xC0 | F_RUN, 1, 0, 2, 0xA0 | F_RUN, 0xC0 | F_RUN | F_16,
          0xA0 | F_RUN | F_16, 0, 0, 0x80 | F_RUN | F_16, 0x80 | F_RUN,
          0, 0, 0, 0, 0)
FILTER_PLAY = (3, 5, 5, 5, 6, 6, 6, 7, 7, 7, 7, 8, 8, 8, 8, 9, 9, 9)
FILTER_REC = (3, 5, 5, 6, 6, 7, 7, 7, 7, 8, 8, 8, 8, 9, 9, 9, 10, 10)
PAGE8 = (0x87, 0x83, 0x81, 0x82)
PAGE16 = (0x8B, 0x89, 0x8A)
# the Sound Blaster Pro's registers, each the view's left level at twice
# its place: master, voice, MIDI, CD, line and the mic
PRO_REGS = (0x22, 0x04, 0x26, 0x28, 0x2E, 0x0A)
MIX_MASK = (0xF8,) * 11 + (0xC0, 0x1F, 0x7F, 0x7F, 0xC0, 0xC0, 0xC0,
                           0xC0, 0x01, 0xF0, 0xF0, 0xF0, 0xF0)
# the chip's level registers for the view's pairs: the register, the
# view's offset of the left level, the output switch bits of each side
PAIRS = ((0x14, 2, 0, 0), (0x36, 4, 0, 0), (0x38, 6, 0x04, 0x02),
         (0x3E, 8, 0x10, 0x08), (0x1A, 10, 0x01, 0x01), (0x7C, 2, 0, 0))
SOURCES = ((0x01, 0), (0x06, 2), (0x18, 6), (0x60, 5))
MIX_DEFAULT = (0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0x00, 0x00, 0x00, 0x00,
               0x00, 0x00, 0x1F, 0x15, 0x0B, 0x00, 0x00, 0x00, 0x00, 0x00,
               0x80, 0x80, 0x80, 0x80)


class Front:
    """the front's state and its transitions; `chip` gives inb and outb
    as ring 0 reaches the ports, `settings` the installer's values"""

    def __init__(self, chip, settings):
        self.chip = chip
        for k, v in settings.items():
            setattr(self, k, v)

    # --- the operations: the chip's ports ------------------------------------
    def chip_in(self, port):
        return self.chip.inb(port)

    def chip_out(self, port, v):
        self.chip.outb(port, v & 0xFF)

    def port_of(self, off):
        return self.base + off

    def dsp_put(self, v):
        """a byte to the command port once the chip takes one; a chip that
        takes none in DSP_POLLS polls is SICK until its own reset"""
        if self.sick:
            return
        port = self.port_of(0x0C)
        for _ in range(DSP_POLLS):
            if not self.chip_in(port) & 0x80:
                self.chip_out(port, v)
                return
        self.sick = 1

    def dsp_reg(self, reg, v):
        """R(reg, v): the chip's register reg = v"""
        self.dsp_put(reg)
        self.dsp_put(v)

    def chip_answer(self):
        """the chip's next answer, polled at +Ch bit 6; None if none"""
        if self.sick:
            return None
        port = self.port_of(0x0C)
        for _ in range(DSP_POLLS):
            if self.chip_in(port) & 0x40:
                return self.chip_in(port - 2)
        return None

    def dsp_read(self, reg):
        """Q(reg), after any byte nobody read"""
        port = self.port_of(0x0C)
        for _ in range(8):
            if not self.chip_in(port) & 0x40:
                break
            self.chip_in(port - 2)
        self.dsp_reg(0xC0, reg)
        return self.chip_answer()

    def reg_set(self, reg, v):
        if reg in self.known and self.ctrl[reg] == v:
            return
        self.reg_force(reg, v)

    def reg_force(self, reg, v):
        self.ctrl[reg] = v & 0xFF
        self.known.add(reg)
        self.dsp_reg(reg, v)

    def reg_get(self, reg):
        if reg in self.known:
            return self.ctrl[reg]
        v = self.dsp_read(reg)
        if v is None:
            return 0
        self.ctrl[reg] = v
        self.known.add(reg)
        return v

    def reg_or(self, reg, bits):
        self.reg_set(reg, self.reg_get(reg) | bits)

    def reg_and(self, reg, bits):
        self.reg_set(reg, self.reg_get(reg) & bits)

    def mix_at(self, reg):
        self.chip_out(self.base + 4, reg)
        return self.base + 5

    def mix_back(self):
        self.chip_out(self.base + 4, self.index)

    def mix_read(self, reg):
        v = self.chip_in(self.mix_at(reg))
        self.mix_back()
        return v

    def mix_write(self, reg, v):
        """M(reg, v), the chip's index put back to the game's"""
        self.chip_out(self.mix_at(reg), v)
        self.mix_back()

    def mix_or(self, reg, bits):
        self.mix_write(reg, self.mix_read(reg) | bits)

    def mix_and(self, reg, bits):
        self.mix_write(reg, self.mix_read(reg) & bits)

    def chip_reset(self):
        """RESET: +6 = 3, three reads, 0, the AAh: COMP, the registers
        unknown, Audio 1's transfer over; 7Ah and 7Ch back while Audio 2
        plays"""
        saved = None
        if self.flags2 & F_RUN:
            saved = (self.mix_read(0x7C), self.mix_read(0x7A))
        port = self.port_of(6)
        self.chip_out(port, 3)
        for _ in range(3):
            self.chip_in(port)
        self.chip_out(port, 0)
        self.sick = 0
        self.chip_answer()
        self.flags &= F_ESS | F_RESET
        self.known = set()
        if saved is not None:
            self.mix_write(0x7A, saved[1])
            self.mix_write(0x7C, saved[0])

    def ensure_ext(self):
        """EXT for a transfer: from COMP or SICK through RESET"""
        if self.state & S_COMP or self.sick:
            self.state &= ~S_COMP & 0xFF
            self.chip_reset()
        if not self.flags & F_EXT:
            self.flags |= F_EXT
            self.dsp_put(0xC6)
            self.dsp_put(0xD1)

    def compat(self):
        """COMP for a firmware command"""
        if self.state & S_COMP:
            return
        self.state |= S_COMP
        if self.flags & F_EXT:
            self.chip_reset()
        self.dsp_put(0xD1)

    def compat_tc(self):
        self.compat()
        tc = min(5624 // self.rate_v, 0xFF)
        self.dsp_reg(0x40, -tc & 0xFF)

    # --- the events: the ports ---------------------------------------------
    def port_in(self, port):
        bx = port - self.base
        if 0 <= bx <= 0x0F:
            return self.s16_in(bx, port)
        if self.dma_port(port):
            return self.dma_in(port)
        return self.chip_in(port)

    def port_out(self, port, v):
        bx = port - self.base
        if 0 <= bx <= 0x0F:
            self.s16_out(bx, port, v)
        elif self.dma_port(port):
            self.dma_out(port, v)
        else:
            self.chip_out(port, v)

    def s16_in(self, bx, port):
        if bx == 0x05:
            return self.mix_in(port)
        if bx == 0x04:
            return self.index
        if bx < 0x0A or self.flags & F_ESS or self.state & (S_UART | S_MIDI):
            return self.chip_in(port)
        if bx == 0x0C:
            return 0xFF if self.state & S_HELD else 0x7F
        if bx > 0x0C:
            self.ack(bx - 0x0D)
            if bx == 0x0F:
                return 0xFF
            return 0xFF if self.state & S_FULL else 0x7F
        v = self.latch
        if self.state & S_FULL:
            self.state &= ~S_FULL & 0xFF
            self.next_byte()
        return v

    def s16_out(self, bx, port, v):
        if bx == 0x0C:
            self.s16_write(port, v)
        elif bx == 0x05:
            self.mix_out(port, v)
        elif bx == 0x06:
            self.reset_port(port, v)
        elif bx == 0x04:
            self.index = v
            self.chip_out(port, v)
        elif self.flags & F_ESS:
            self.chip_out(port, v)

    # --- the DSP: the read latch and the answers -----------------------------
    def next_byte(self):
        if self.answer:
            self.latch = self.answer.pop(0)
            self.state |= S_FULL
            if self.answer:
                return
        if self.state & S_HELD:
            self.state &= ~S_HELD & 0xFF
            self.dsp_take(self.held)

    def give(self, data):
        """an answer: the first byte into the latch if it is free"""
        data = list(data)
        if not self.state & S_FULL:
            self.state |= S_FULL
            self.latch = data.pop(0)
        self.answer = data

    def put1(self, v):
        self.give([v & 0xFF])

    # --- the DSP: the reset ----------------------------------------------
    def reset_port(self, port, v):
        if self.flags & F_ESS:
            self.chip_out(port, v)
            if v & 1:
                return
            self.chip_answer()
            self.flags &= ~(F_ESS | F_EXT | F_RUN) & 0xFF
            self.known = set()
            self.boot()
            return
        if v & 1:
            was = self.flags & F_RESET
            self.flags |= F_RESET
            if not was:
                self.halt()
            return
        if not self.flags & F_RESET:
            return
        self.flags &= ~F_RESET & 0xFF
        if self.state & (S_UART | S_COMP):
            self.chip_reset()
        self.boot()

    def boot(self):
        """the DSP's start: cold, or warm after the high-speed and MIDI
        UART commands; AAh in the latch"""
        if not self.state & S_WARM:
            self.rate_v, self.block8 = 0x38, 0x07FF
            self.state &= ~S_SPKR & 0xFF
        self.state &= S_SPKR
        self.latch = 0xAA
        self.state |= S_FULL
        self.need, self.answer = 0, []

    def halt(self):
        """the DSP held in reset: both engines stop, the chip's interrupts
        are acknowledged, a SICK chip gets RESET, nothing waits"""
        self.a1_stop()
        self.a2_stop()
        if self.dma2 < 4:
            self.mix_and(0x7A, 0x07)
        self.chip_in(self.port_of(0x0E))
        if self.sick:
            self.chip_reset()
        self.state &= S_WARM | S_SPKR | S_UART | S_COMP
        self.need, self.answer = 0, []

    def a1_stop(self):
        """Audio 1 IDLE: B8h bit 0 off, then FIFO"""
        self.halves &= ~H_ALL & 0xFF
        if self.flags & F_RUN:
            self.flags &= ~(F_RUN | F_AUTO | F_LAST | F_PAUSE) & 0xFF
            self.reg_and(0xB8, 0xFE)
        self.a1_flush()

    def a1_flush(self):
        """FIFO: +6 = 2, then 0"""
        port = self.port_of(6)
        self.chip_out(port, 2)
        self.chip_out(port, 0)

    def a2_stop(self):
        """Audio 2 IDLE: 78h bits 4 and 1 off, its FIFO plays out"""
        self.halves &= ~(H_ALL << H_A2) & 0xFF
        if self.flags2 & F_RUN:
            self.flags2 = 0
            self.mix_and(0x78, 0xED)

    # --- the DSP: a command and its operands -------------------------------
    def s16_write(self, port, v):
        if self.flags & F_ESS:
            self.chip_out(port, v)
            return
        if self.state & (S_UART | S_MIDI):
            self.state &= ~S_MIDI & 0xFF
            self.chip_out(port, v)
            return
        if self.answer:
            self.held = v
            self.state |= S_HELD
            return
        self.dsp_take(v)

    def dsp_take(self, v):
        if not self.need:
            self.prev, self.cmd = self.cmd, v
            self.got = 0
            self.need = OPTABLE[v >> 2] >> ((v & 3) * 2) & 3
            if not self.need:
                self.command()
            return
        if not self.got and self.cmd == 0xC6 and v & 0xCF:
            # C6h with a register: the game knows the chip, PASS
            self.dsp_reg(0xC6, v)
            self.flags |= F_ESS | F_EXT
            self.known = set()
            self.need = 0
            return
        self.ops[self.got] = v
        self.got += 1
        self.need -= 1
        if not self.need:
            self.command()

    def command(self):
        if self.cmd in ANSWERS:
            self.give(ANSWERS[self.cmd])
            return
        lo = self.cmd & 0x0F
        handler = (self.g0, self.g1, self.g2, self.g3, self.g4, self.none,
                   self.none, self.g7, self.g8, self.g9, self.none,
                   self.gx, self.gx, self.gd, self.ge,
                   self.gf)[self.cmd >> 4]
        handler(lo)

    def none(self, lo):
        pass

    def g0(self, lo):
        """0Fh: an X-bus register of the CSP"""
        if lo == 0x0F:
            self.put1(0xFF if self.ops[0] & 0xFC == 0x80 else 0)

    def g1(self, lo):
        """1xh: the direct DAC, 8-bit DMA, 2-bit ADPCM"""
        if lo < 4:
            self.direct_dac()
            return
        if lo & 0x02:
            self.fw_xfer(0x7A | (lo & 1), lo >= 8)
            return
        self.old8(lo, 0)

    def g2(self, lo):
        """2xh: the direct ADC, 8-bit recordings"""
        if lo < 4:
            self.direct_adc()
            return
        self.old8(lo, 0x08)

    def old8(self, lo, bl):
        length = self.ops[0] | self.ops[1] << 8
        if lo >= 8:
            bl |= 0x04
            length = self.block8
        self.xfer(bl, 0, length)

    def g9(self, lo):
        """9xh: the high-speed commands of the 8-bit block, never queued"""
        self.state |= S_WARM
        bl = (lo & 0x08) | 0x05
        if lo & 1:
            bl &= ~0x04 & 0xFF
        self.xfer(bl, 0, self.block8)

    def g3(self, lo):
        """3xh: MIDI through the chip in Compatibility mode"""
        self.compat()
        if lo >= 8:
            self.dsp_reg(0x38, self.ops[0])
            return
        self.state |= S_MIDI if lo < 4 else S_UART | S_WARM
        self.dsp_put((lo & 0x05) | 0x30)

    def g4(self, lo):
        """4xh: the time constant, the rate, the 8-bit block"""
        if lo == 8:
            self.block8 = self.ops[0] | self.ops[1] << 8
        elif lo == 0:
            self.rate_v = tc_to_v(self.ops[0])
        elif lo <= 2:
            self.rate_v = rate_to_v(self.ops[0] << 8 | self.ops[1])

    def g7(self, lo):
        """7xh: ADPCM through the chip, single-cycle (74h-77h)"""
        self.fw_xfer(0x74 | (lo & 3), not 4 <= lo < 8)

    def g8(self, lo):
        """8xh: silence"""
        self.fw_xfer(0x80, False)

    def fw_xfer(self, c, auto):
        """a firmware command in COMP at the rate's time constant; an
        auto-initialized one, which the chip doesn't have, plays the 8-bit
        block once"""
        if auto:
            self.ops[0], self.ops[1] = self.block8 & 0xFF, self.block8 >> 8
        self.compat_tc()
        self.dsp_put(c)
        self.dsp_put(self.ops[0])
        self.dsp_put(self.ops[1])
        self.state &= ~S_T16 & 0xFF

    def direct_dac(self):
        self.compat()
        self.dsp_reg(0x10, self.ops[0])

    def direct_adc(self):
        self.compat()
        self.dsp_put(0x20)
        v = self.chip_answer()
        self.put1(0 if v is None else v)

    def gx(self, lo):
        """Bxh 16-bit and Cxh 8-bit: bit 3 records, bit 2 auto-initializes"""
        bl = self.cmd & 0x1C
        length = self.ops[1] | self.ops[2] << 8
        if not bl & 0x10:
            self.block8 = length
        self.xfer(bl, self.ops[0], length)

    def gd(self, lo):
        """Dxh: the speaker flag, pause, continue, the end of
        auto-initialize of the kind that runs"""
        if lo == 8:
            self.put1(0xFF if self.state & S_SPKR else 0)
            return
        op = DX_OPS[lo]
        if not op & 0x80:
            if op == 1:
                self.state |= S_SPKR
            elif op == 2:
                self.state &= ~S_SPKR & 0xFF
            return
        if op & F_16 and self.flags2 & F_RUN:
            self.a2_dx(op)
            return
        if (self.flags ^ op) & (F_RUN | F_16):
            return
        if op & 0x40:
            self.flags |= F_PAUSE           # Audio 1's DMA requests off
            self.reg_and(0xB2, 0xBF)
        elif op & 0x20:
            if self.flags & F_PAUSE:
                self.flags &= ~F_PAUSE & 0xFF
                self.a1_clocks()
                self.reg_or(0xB2, 0x40)
        elif self.flags & F_AUTO:
            self.flags &= ~(F_AUTO | F_LAST) & 0xFF
            if not self.half_last(0):
                self.reg_and(0xB8, 0xFB)

    def a2_dx(self, op):
        if op & 0x40:
            self.flags2 |= F_PAUSE          # the FIFO off the DAC
            self.mix_and(0x78, 0xFE)
        elif op & 0x20:
            if self.flags2 & F_PAUSE:
                self.flags2 &= ~F_PAUSE & 0xFF
                self.a2_rate()
                self.mix_or(0x78, 0x01)
        elif self.flags2 & F_AUTO:
            self.flags2 &= ~(F_AUTO | F_LAST) & 0xFF
            if not self.half_last(H_A2):
                self.mix_and(0x78, 0xEF)

    def ge(self, lo):
        """Exh: E0h the operand inverted, E4h and E8h the test register"""
        if lo == 0:
            self.put1(~self.ops[0])
        elif lo == 4:
            self.test_reg = self.ops[0]
        elif lo == 8:
            self.put1(self.test_reg)

    def gf(self, lo):
        """Fxh: F2h and F3h the interrupts, F8h a 0, FDh the command
        before"""
        if lo in (2, 3):
            if lo == 3:
                self.state |= S_T16
            else:
                self.state &= ~S_T16 & 0xFF
            self.dsp_put(0xF2)
        elif lo == 0x0D:
            self.put1(self.prev)
        elif lo == 8:
            self.latch = 0
            self.state |= S_FULL

    # --- a transfer ------------------------------------------------------------
    def xfer(self, bl, mode, length):
        """a transfer of the kind BL (bit 4 16-bit, 3 recording, 2
        auto-initialize, 0 never queued) and the mode (bit 4 signed, 5
        stereo), length + 1 samples"""
        si = (length + 1) & 0xFFFF
        if not bl & 0x05:
            if bl & F_16 and \
                    self.flags2 & (F_RUN | F_AUTO) == F_RUN | F_AUTO:
                self.flags2 |= F_LAST           # queued behind Audio 2's
                self.a2_count(si, bl, queued=True)
                return
            want = (bl & F_16) | F_RUN | F_AUTO
            if self.flags & (F_16 | F_RUN | F_AUTO) == want:
                self.flags |= F_LAST            # queued behind Audio 1's
                self.a1_count(si, bl, queued=True)
                return
        self.xbits = bl
        if bl & F_16:
            if not bl & 0x08 and self.dma16 and self.dma2 < 4:
                self.a2_xfer(si, bl, mode)
                return
            self.a2_stop()
        if self.flags & (F_RUN | F_AUTO) != F_RUN:
            self.a1_stop()
        self.ensure_ext()
        self.flags = ((bl | F_RUN) & (F_16 | F_RUN | F_AUTO)) | \
            (self.flags & (F_ESS | F_EXT | F_RESET))
        self.a1_count(si, bl)
        b8 = bl & 0x0C
        if self.halves & H_ON:
            b8 |= 0x04
        if b8 & 0x08:
            b8 |= 0x02
        self.reg_set(0xB8, b8)
        self.reg_set(0xB9, self.b9)
        a8 = (self.reg_get(0xA8) & 0xFC) | 0x02
        if mode & 0x20:
            a8 ^= 0x03
        self.reg_set(0xA8, a8)
        self.a1_clocks()
        b7, b6 = 0xD0, 0x80
        if mode & 0x10:
            b7, b6 = b7 ^ 0x20, b6 ^ 0x80
        if bl & 0x10:
            b7 ^= 0x04
        if mode & 0x20:
            b7 ^= 0x48
        self.a1_format(b6, b7)
        self.reg_or(0xB1, 0x50)
        self.reg_or(0xB2, 0x50)
        self.dac_options()
        self.xfer_dma(self.dma8, bl)
        self.reg_or(0xB8, 0x01)

    def a2_xfer(self, si, bl, mode):
        """a 16-bit playback on Audio 2, as ES1869.DRV starts Audio 2"""
        if self.flags & (F_RUN | F_16) == F_RUN | F_16:
            self.a1_stop()
        self.mix_write(0x78, 0x00)
        self.flags2 = (bl & F_AUTO) | F_RUN
        self.a2_count(si, bl)
        self.a2_rate()
        self.pair_out(5)
        r7a = 0x41
        if mode & 0x10:
            r7a |= 0x04
        if mode & 0x20:
            r7a |= 0x02
        self.mix_write(0x7A, r7a)
        self.dac_options()
        self.xfer_dma(self.dma2, bl)
        r78 = 0x03
        if bl & F_AUTO or self.halves & (H_ON << H_A2):
            r78 |= 0x10
        if self.b9:
            r78 |= 0x80
        self.mix_write(0x78, r78)

    def half_count(self, shift, si, bl):
        """the halves of a 16-bit block of si samples (0: 65536): more
        than 32768 run in halves, and the block is the last unless it
        auto-initializes; True in halves"""
        self.halves &= ~(H_ALL << shift) & 0xFF
        if (si - 1) & 0xFFFF < 0x8000:
            return False
        h = H_ON if bl & F_AUTO else H_ON | H_ONCE
        self.halves |= h << shift
        return True

    def half_last(self, shift):
        """D9h or DAh in a first half: the block's end ends it"""
        if (self.halves >> shift) & (H_ON | H_SECOND) != H_ON:
            return False
        self.halves |= H_ONCE << shift
        return True

    def a1_count(self, si, bl, queued=False):
        """A4h and A5h: minus the bytes, or half of them in halves"""
        if not queued and self.flags & F_16 and self.half_count(0, si, bl):
            n = si
        else:
            self.halves &= ~H_ALL & 0xFF
            n = si * 2 if self.flags & F_16 else si
        n = -n & 0xFFFF
        self.reg_set(0xA4, n & 0xFF)
        self.reg_set(0xA5, n >> 8)

    def a2_count(self, si, bl, queued=False):
        """74h and 76h: minus the bytes, or half of them in halves"""
        if not queued and self.half_count(H_A2, si, bl):
            n = si
        else:
            self.halves &= ~(H_ALL << H_A2) & 0xFF
            n = si * 2
        n = -n & 0xFFFF
        self.mix_write(0x74, n & 0xFF)
        self.mix_write(0x76, n >> 8)

    def a1_format(self, b6, b7):
        """B6h, then B7h twice; another format on an empty FIFO"""
        if (self.ctrl.get(0xB6), self.ctrl.get(0xB7)) == (b6, b7) and \
                {0xB6, 0xB7} <= self.known:
            return
        self.a1_flush()
        self.reg_force(0xB6, b6)
        self.dsp_reg(0xB7, 0x71 if b6 == 0 else 0x51)
        self.reg_force(0xB7, b7)

    def dac_options(self):
        """mixer 71h: bit 5 off, the filters and 4x as /F and /O set them"""
        v = self.mix_read(0x71)
        w = (v & self.keep71) | self.set71
        if w != v:
            self.mix_write(0x71, w)

    def a1_clocks(self):
        """A1h and A2h as ES1869.DRV picks them (6:1FF4), the filter
        table for the transfer's direction (6:2308)"""
        v = self.rate_v
        if v <= 124:
            d, top = half_round(4472, v), 128
        else:
            d, top = half_round(8947, v), 256
        self.reg_set(0xA1, (top - d) & 0xFF)
        if v > 124 and 18 <= d <= 35:
            table = FILTER_REC if self.xbits & 0x08 else FILTER_PLAY
            f = table[d - 18]
        else:
            f = half_round(2245, v)
        self.reg_set(0xA2, -f & 0xFF)

    def a2_rate(self):
        """70h and 72h as ES1869.DRV picks them (6:2436)"""
        v = self.rate_v
        d1, e1 = div_near(4464, v)
        d2, e2 = div_near(4319, v)
        if e2 < e1:
            d, hi = d2, 0x80
        else:
            d, hi = d1, 0
        d = min(d, 128)
        self.mix_write(0x70, ((-d) & 0x7F) | hi)
        if v >= 0xF8:
            f = 0xFD
        else:
            f = -half_round(2245, v) & 0xFF
        self.mix_write(0x72, f)

    # --- the 16-bit channel ------------------------------------------------
    def vtrapped(self):
        return self.vchan[self.how_io] == self.dma16

    def xfer_dma(self, ch, bl):
        """the channel ch plays the transfer: the game's own (8-bit), or
        the game's 16-bit one carried over to it"""
        if not bl & 0x10 or not self.dma16:
            if self.dma8 == self.mchan:
                self.vbits &= ~V_LIVE & 0xFF
            return
        if ch != self.mchan:
            self.mchan = ch
            self.vbits &= ~V_LIVE & 0xFF
        if not self.vtrapped():
            self.mirror(bl)
            return
        if self.vbits & V_MASK:
            self.ch8_set(4)
            return
        if self.vbits & (V_NEW | V_LIVE) != V_LIVE:
            self.mirror(bl)

    def mirror(self, bl):
        """the game's 16-bit channel carried over to mchan: read back
        without the traps, where a single-cycle block of the buffer
        carried over last goes on from it"""
        if not self.vtrapped():
            last = (self.vpage, self.vcount, self.vaddr)
            self.chip_out(0xD8, 0)
            self.vpage = self.chip_in(self.vpagep)
            self.vaddr = self.word_in(self.vport)
            self.vcount = self.word_in(self.vport + 2)
            self.chip_out(0xD8, 0)
            self.vmode = 0x10
            if not bl & F_AUTO and self.vbits & V_LIVE and \
                    last == (self.vpage, self.vcount, self.vaddr):
                return
        ah = (self.vmode & 0x10) | 0x08
        if self.xbits & 0x08:
            ah ^= 0x0C
        if not self.b9:
            ah |= 0x40
        ch = self.mchan
        self.chip_out(0x0A, ch | 0x04)
        self.chip_out(0x0B, ah | ch)
        self.chip_out(0x0C, ah | ch)
        addr = self.vaddr << 1
        page = (self.vpage & 0xFE) + (addr >> 16)
        self.mpage = page & 0xFF
        self.word_out(ch << 1, addr & 0xFFFF)
        self.word_out((ch << 1) + 1, ((self.vcount << 1) | 1) & 0xFFFF)
        self.chip_out(PAGE8[ch], page)
        self.vbits = (self.vbits & ~V_NEW & 0xFF) | V_LIVE
        if self.vtrapped() and self.vbits & V_MASK:
            self.ch8_set(4)
        else:
            self.ch8_set(0)

    def ch8_set(self, mask):
        self.chip_out(0x0A, mask | self.mchan)

    def word_out(self, port, v):
        self.chip_out(port, v & 0xFF)
        self.chip_out(port, v >> 8)

    def word_in(self, port):
        lo = self.chip_in(port)
        return lo | self.chip_in(port) << 8

    def dma_port(self, port):
        if port > 0xFF:
            return False
        return port in (self.vpagep, self.vport, self.vport + 2) or \
            (0xD4 <= port <= 0xDE and not port & 1)

    def dma_in(self, port):
        """the game's read of its 16-bit channel: the controller's, but the
        address and count of the channel that carries it"""
        v = self.chip_in(port)
        if not self.vtrapped():
            return v
        reg = port - self.vport
        if reg not in (0, 2):
            return v
        reg >>= 1
        self.vbits ^= V_FF
        if not self.vbits & V_LIVE:
            return v
        if not self.vbits & V_FF:
            return self.vhigh
        self.chip_out(0x0C, 0)
        x = self.word_in((self.mchan << 1) + reg)
        if reg:
            if x != 0xFFFF:
                x >>= 1
        else:
            x >>= 1
            if self.mpage & 1:
                x |= 0x8000
        self.vhigh = x >> 8
        return x & 0xFF

    def dma_out(self, port, v):
        """the game's write of its 16-bit channel: to the controller, and
        followed while this path traps it"""
        self.chip_out(port, v)
        if not self.vtrapped():
            return
        if port == self.vpagep:
            self.vpage = v
            self.dma_new()
            return
        if port in (self.vport, self.vport + 2):
            name = "vaddr" if port == self.vport else "vcount"
            self.vbits ^= V_FF
            if self.vbits & V_FF:
                setattr(self, name, (getattr(self, name) & 0xFF00) | v)
            else:
                setattr(self, name, (getattr(self, name) & 0xFF) | v << 8)
            self.dma_new()
            return
        own = not (v ^ self.dma16) & 3
        if port == 0xD4:
            if own:
                self.dma_mask(bool(v & 0x04))
        elif port == 0xD6:
            if own:
                self.vmode = v
                self.dma_new()
        elif port == 0xD8:
            self.vbits &= ~V_FF & 0xFF
        elif port == 0xDA:
            self.vbits &= ~V_FF & 0xFF
            self.dma_mask(True)
        elif port == 0xDC:
            self.dma_mask(False)
        elif port == 0xDE:
            self.dma_mask(bool(v >> (self.dma16 & 3) & 1))

    def dma_new(self):
        self.vbits = (self.vbits | V_NEW) & ~V_LIVE & 0xFF

    def dma_mask(self, masked):
        if masked:
            self.vbits |= V_MASK
        else:
            self.vbits &= ~V_MASK & 0xFF
        self.vmask_changed()

    def vmask_changed(self):
        """the game masked or unmasked its 16-bit channel: the carrying
        channel follows while a 16-bit transfer runs"""
        if not self.flags2 & F_RUN and \
                self.flags & (F_RUN | F_16) != F_RUN | F_16:
            return
        if self.vbits & V_MASK:
            self.ch8_set(4)
        elif self.vbits & (V_NEW | V_LIVE) != V_LIVE:
            self.mirror(self.xbits)
        else:
            self.ch8_set(0)

    # --- the interrupts --------------------------------------------------------
    def irq_src(self):
        """(Audio 1's kinds, Audio 2's): bit 0 8-bit, bit 1 16-bit; the
        first half's interrupt of a block in halves is the front's own"""
        st = self.chip_in(self.port_of(0x0C))
        a1 = 0
        if st & 0x01 and not self.a1_half():
            a1 = 2 if self.flags & F_16 else 1
        if st & 0x04:
            a1 |= 2 if self.state & S_T16 else 1
        a2 = 0
        if self.dma2 < 4 and self.mix_read(0x7A) & 0x80 and \
                not self.a2_half():
            a2 = 2
        return a1, a2

    def a1_half(self):
        if self.halves & (H_ON | H_SECOND) != H_ON:
            return False
        self.chip_in(self.port_of(0x0E))
        self.halves |= H_SECOND
        if self.halves & H_ONCE:
            self.halves &= ~H_ONCE & 0xFF
            self.reg_and(0xB8, 0xFB)
        return True

    def a2_half(self):
        if (self.halves >> H_A2) & (H_ON | H_SECOND) != H_ON:
            return False
        self.mix_and(0x7A, 0x7F)
        self.halves |= H_SECOND << H_A2
        if self.halves & (H_ONCE << H_A2):
            self.halves &= ~(H_ONCE << H_A2) & 0xFF
            self.mix_and(0x78, 0xEF)
        return True

    def ack(self, kind):
        """the acknowledge of kind (1 8-bit, 2 16-bit); a queued block's
        turn ends the auto-initialize"""
        a1, a2 = self.irq_src()
        if a1 & kind:
            self.chip_in(self.port_of(0x0E))
            self.halves &= ~H_SECOND & 0xFF
            if self.flags & F_LAST:
                self.flags &= ~(F_LAST | F_AUTO) & 0xFF
                self.reg_and(0xB8, 0xFB)
        if a2 & kind:
            self.mix_and(0x7A, 0x7F)
            self.halves &= ~(H_SECOND << H_A2) & 0xFF
            if self.flags2 & F_LAST:
                self.flags2 &= ~(F_LAST | F_AUTO) & 0xFF
                self.mix_and(0x78, 0xEF)

    # --- the mixer -------------------------------------------------------------
    def mix_out(self, port, v):
        reg = self.index
        if self.flags & F_ESS:
            self.chip_out(port, v)
        elif reg == 0:
            self.mix = list(MIX_DEFAULT)
            for r in range(0x30, 0x3F):
                self.mix_apply(r)
        elif reg < 0x30:
            at = self.pro_find(reg)
            if at is None:
                self.chip_out(port, v)
            elif at < 0:
                pass                            # 0Ch and 0Eh
            elif reg == 0x0A:
                self.mix[at] = ((v & 7) << 5) | 0x18
                self.mix_apply(0x3A)
            else:
                self.mix[at] = (v & 0xF0) | 0x08
                self.mix[at + 1] = ((v << 4) & 0xF0) | 0x08
                self.mix_apply(0x31 + at)
                self.mix_apply(0x30 + at)
        elif reg <= 0x47:
            self.mix[reg - 0x30] = v & MIX_MASK[reg - 0x30]
            self.mix_apply(reg)
        elif reg < 0x80 or reg > 0x82:
            self.chip_out(port, v)
        elif reg == 0x81:
            bits = v & 0xE0
            self.dma16 = (bits & -bits).bit_length() - 1 if bits else 0
            self.v_ports()

    def mix_in(self, port):
        reg = self.index
        if self.flags & F_ESS:
            return self.chip_in(port)
        if reg < 0x30:
            at = self.pro_find(reg)
            if at is None:
                return self.chip_in(port)
            if at < 0:
                return 0
            if reg == 0x0A:
                return self.mix[at] >> 5
            return (self.mix[at] & 0xF0) | self.mix[at + 1] >> 4
        if reg <= 0x47:
            return self.mix[reg - 0x30]
        if reg == 0x80:
            return self.mix80
        if reg == 0x81:
            v = self.mix81
            if self.dma16:
                v |= 1 << self.dma16
            return v
        if reg != 0x82:
            return self.chip_in(port)
        a1, a2 = self.irq_src()
        v = a1 | a2 | 0x20
        if self.cfg:
            v |= (self.chip_in(self.cfg + 6) & 0x08) >> 1
        return v

    def pro_find(self, reg):
        """the view's offset of a Sound Blaster Pro register's left level,
        -1 for 0Ch and 0Eh, None for the chip's own"""
        if reg in PRO_REGS:
            return PRO_REGS.index(reg) * 2
        if reg in (0x0C, 0x0E):
            return -1
        return None

    def mix_apply(self, reg):
        """the chip's level for the Sound Blaster 16's register reg"""
        i = reg - 0x30
        if i <= 1:
            self.mix_write(0x60 + 2 * i, self.mix[i] >> 2)
        elif i < 0x0B:
            pair = (i - 2) >> 1
            self.pair_out(pair)
            if not pair and self.dma2 < 4:
                self.pair_out(5)
        elif i == 0x0B:
            self.mix_write(0x3C, self.mix[0x0B] >> 5)
        elif i == 0x0C:
            for pair in (2, 3, 4):
                self.pair_out(pair)
        elif i <= 0x0E:
            on = self.mix[0x0D] | self.mix[0x0E]
            picked = [src for bits, src in SOURCES if on & bits]
            if not picked:
                return
            src = picked[0] if len(picked) == 1 else 5
            self.mix_write(0x1C, (self.mix_read(0x1C) & 0xF8) | src)

    def pair_out(self, pair):
        reg, at, lsw, rsw = PAIRS[pair]
        left, right = self.mix[at], self.mix[at + 1]
        if at == 10:
            right = left
        if lsw:
            if not self.mix[0x0C] & lsw:
                left = 0
            if not self.mix[0x0C] & rsw:
                right = 0
        self.mix_write(reg, (left & 0xF0) | (right >> 4))

    def v_ports(self):
        if self.dma16 >= 5:
            self.vport = 0xC0 + ((self.dma16 - 4) << 2)
            self.vpagep = PAGE16[self.dma16 - 5]


def half_round(x, v):
    """round(x / 2 / v), as the fronts divide"""
    return (x // v + 1) >> 1


def div_near(x, v):
    """(round(x / v), how far that times v is from x)"""
    q, r = divmod(x, v)
    if 2 * r >= v:
        q += 1
    return q, abs(x - q * v)


def tc_to_v(tc):
    """the rate generator's value for 40h's time constant"""
    tc = min(tc, 0xEA)
    if tc == 0x68:
        return 0x25
    if tc == 0x6C:
        return 0x26
    if tc == 0xC8:
        return 0xC8
    return min(5623 // (256 - tc), 0xFF)


def rate_to_v(rate):
    """the value for 41h's and 42h's rate, losing the carry as DSPs before
    4.13 did"""
    hi, lo = rate >> 8, rate & 0xFF
    if hi >= 0xB1:
        return 0xFF
    if hi < 0x13:
        return 0x1C
    x = ((23 * hi) & 0xFF00) | (((23 * hi) & 0xFF) + ((23 * lo) >> 8)) & 0xFF
    x &= 0xFFFF
    return ((x >> 3) + 1) >> 1


SETTINGS = ("base", "cfg", "mix80", "mix81", "dma8", "dma16", "dma2",
            "mchan", "b9", "keep71", "set71", "how_io", "rate_v",
            "test_reg", "index", "vbits", "vport", "vpagep", "mpage",
            "xbits", "halves", "sick", "flags", "flags2", "state", "cmd",
            "prev", "got", "need", "latch", "held")


class FSMCard(TSRCard):
    """the reference on the TSR's card: the installer as esssb16.com runs
    it, then the game's trapped accesses answered by Front"""

    def __init__(self, path="qpi", **kw):
        super().__init__(path, **kw)
        m = self.m
        s = {name: m.data8(name) for name in SETTINGS}
        for name in ("base", "cfg", "block8", "vaddr", "vcount"):
            s[name] = m.data16(name)
        s["vchan"] = list(m.mem(m.addr("vchan"), 4))
        s["vpage"] = m.data8("vpage")
        s["vmode"] = m.data8("vmode")
        s["vhigh"] = m.data8("vhigh")
        s["mix"] = list(m.mem(m.addr("mix"), MIX_LEN))
        s["ctrl"] = dict(zip(range(0xA1, 0xBA),
                             m.mem(m.addr("ctrl"), 25)))
        known = int.from_bytes(m.mem(m.addr("known"), 4), "little")
        s["known"] = {0xA1 + i for i in range(25) if known >> i & 1}
        s["ops"] = list(m.mem(m.addr("ops"), 3))
        s["answer"] = []
        self.fsm = Front(self.chip, s)
        self.fsm.how_io = {"qpi": 2, "pm": 1}.get(path, 0)

    def port_in(self, port):
        if not self.trapped(port):
            return super().port_in(port)
        return self.fsm.port_in(port)

    def port_out(self, port, v):
        if not self.trapped(port):
            super().port_out(port, v)
        else:
            self.fsm.port_out(port, v)

    def flags(self):
        return self.fsm.flags

    @property
    def audio2(self):
        return self.fsm.dma2 < 4
