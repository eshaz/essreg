# (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
#
# Licensed under GPL Version 3.0
"""What a DOS game does with a Sound Blaster 16, and the card it does it to.

A card has the game's view of the machine: out() and inp() on its ports
(each access takes a microsecond), the DMA controllers it programs through
those ports, the memory its buffers are in, and wait(), in which time
passes and the card's interrupt runs the game's handler (`isr`), once per
rising edge as an edge-triggered PIC would. `played` is what the card
played, and dac(8) and dac(16) what the 8-bit and the 16-bit transfers
played, which can run together. ModelCard is a real Sound Blaster 16
(sb16.SB16); the fronts' cards (test_sb16conf) put esssb16.com or
ES1869.VXD and the chip model behind the same calls.

The functions below are a game's sound driver: the reset handshake, DSP
writes and reads that poll the status bits with a time-out, the mixer, the
DMA controller's programming as Creative's guide gives it, and a
transfer's start.
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from dma8237 import DMA8237, Memory, PAGE_PORTS  # noqa: E402
from sb16 import SB16, Clock  # noqa: E402

BASE = 0x220
POLLS = 2000                # a driver's time-out, in status reads


class Card:
    """the game's side of a card; subclasses give the ports and time"""

    isr = None                  # the game's interrupt handler: isr(card)

    def __init__(self):
        self.edges = 0          # rising edges seen
        self.serviced = 0       # times the handler ran
        self.in_isr = False

    # the subclass's
    def port_in(self, port):
        raise NotImplementedError

    def port_out(self, port, v):
        raise NotImplementedError

    def tick(self, us):
        """up to us of time on the card, stopping at a rising edge of its
        interrupt; (the time that passed, the rising edges so far)"""
        raise NotImplementedError

    def record_from(self, source):
        """what the card's ADC hears: source(n) gives n frames"""
        raise NotImplementedError

    def dac(self, kind):
        """the frames the transfers of `kind` (8 or 16) played"""
        raise NotImplementedError

    def out(self, port, v):
        self.port_out(port, v & 0xFF)
        self._time(1)

    def inp(self, port):
        v = self.port_in(port)
        self._time(1)
        return v

    def wait(self, us):
        """us of time pass; the handler runs at each rising edge"""
        self._time(us)

    def _time(self, us):
        while us > 0:
            done, edges = self.tick(us)
            us -= done
            if edges > self.edges and not self.in_isr:
                self.edges = edges
                if self.isr is not None:
                    self.in_isr = True
                    try:
                        self.serviced += 1
                        self.isr(self)
                    finally:
                        self.in_isr = False
            elif not self.in_isr:
                self.edges = edges
            if done == 0:
                break


class ModelCard(Card):
    """a real Sound Blaster 16 and the PC's DMA controllers"""

    def __init__(self, irq=5, dma8=1, dma16=5, **kw):
        super().__init__()
        self.memory = Memory(0x1000000)
        self.dma = DMA8237(self.memory)
        self.clock = Clock(1000000)
        self.sb = SB16(self.dma, BASE, irq, dma8, dma16, self.clock, **kw)

    def port_in(self, port):
        if self.sb.handles(port):
            return self.sb.inb(port)
        if self.dma.handles(port):
            return self.dma.inb(port)
        return 0xFF

    def port_out(self, port, v):
        if self.sb.handles(port):
            self.sb.outb(port, v)
        elif self.dma.handles(port):
            self.dma.outb(port, v)

    def tick(self, us):
        return self.sb.advance(us), len(self.sb.irq_edges)

    def record_from(self, source):
        self.sb.source = source

    # what the tests compare
    @property
    def played(self):
        return self.sb.played

    def dac(self, kind):
        """each engine's output, heard or not: the DAC plays one of them"""
        return self.sb.out[kind]

    @property
    def midi_out(self):
        return self.sb.midi_out

    def poke(self, addr, data):
        self.memory.write(addr, data)

    def peek(self, addr, n):
        return self.memory.read(addr, n)


# --- a game's sound driver ---------------------------------------------------

def reset(card, base=BASE):
    """the DSP reset of Creative's guide: 1, 3 us, 0, then AAh within
    100 us; True if it came"""
    card.out(base + 6, 1)
    card.wait(3)
    card.out(base + 6, 0)
    for _ in range(POLLS):
        if card.inp(base + 0x0E) & 0x80:
            return card.inp(base + 0x0A) == 0xAA
    return False


def write(card, *data, base=BASE):
    """bytes to the DSP, each after 2xC bit 7 reads clear; False on a
    time-out"""
    for b in data:
        for _ in range(POLLS):
            if not card.inp(base + 0x0C) & 0x80:
                break
        else:
            return False
        card.out(base + 0x0C, b)
    return True


def read(card, base=BASE):
    """a byte from the DSP once 2xE bit 7 says one is there, else None"""
    for _ in range(POLLS):
        if card.inp(base + 0x0E) & 0x80:
            return card.inp(base + 0x0A)
    return None


def read_all(card, base=BASE, limit=64):
    """every byte the DSP has, as a game drains it"""
    out = []
    while len(out) < limit:
        for _ in range(8):
            if card.inp(base + 0x0E) & 0x80:
                out.append(card.inp(base + 0x0A))
                break
        else:
            break
    return out


def mixer(card, reg, v=None, base=BASE):
    card.out(base + 4, reg)
    if v is None:
        return card.inp(base + 5)
    card.out(base + 5, v)
    return None


def program_dma(card, ch, addr, size, auto=True, record=False,
                single=True):
    """a channel for size bytes at physical addr, in Creative's order:
    mask, mode, flip-flop, address, page, count, unmask; a 16-bit
    channel's address and count in words"""
    if ch >= 4:
        mask, mode_p, ff, port = 0xD4, 0xD6, 0xD8, 0xC0 + 4 * (ch - 4)
        a, n = (addr >> 1) & 0xFFFF, (size >> 1) - 1
        page = (addr >> 16) & 0xFE
    else:
        mask, mode_p, ff, port = 0x0A, 0x0B, 0x0C, 2 * ch
        a, n, page = addr & 0xFFFF, size - 1, addr >> 16
    mode = (0x40 if single else 0x00) | (0x10 if auto else 0) | \
        (0x04 if record else 0x08) | (ch & 3)
    card.out(mask, 4 | (ch & 3))
    card.out(mode_p, mode)
    card.out(ff, 0)
    card.out(port, a & 0xFF)
    card.out(port, a >> 8)
    card.out(PAGE_PORTS[ch], page)
    card.out(port + (2 if ch >= 4 else 1), n & 0xFF)
    card.out(port + (2 if ch >= 4 else 1), n >> 8)
    card.out(mask, ch & 3)


def dma_count(card, ch):
    """the channel's current count, read twice as games read it"""
    ff = 0xD8 if ch >= 4 else 0x0C
    port = (0xC2 + 4 * (ch - 4)) if ch >= 4 else 2 * ch + 1
    card.out(ff, 0)
    return card.inp(port) | (card.inp(port) << 8)


def ack16(card, base=BASE):
    card.inp(base + 0x0F)


def ack8(card, base=BASE):
    card.inp(base + 0x0E)


def sb16_isr(card, base=BASE):
    """the interrupt handler of a Sound Blaster 16 driver: mixer 82h says
    which transfer interrupted, and each gets its acknowledge"""
    card.out(base + 4, 0x82)
    status = card.inp(base + 5)
    if status & 0x02:
        card.inp(base + 0x0F)
    if status & 0x01:
        card.inp(base + 0x0E)
    card.irq_status = getattr(card, "irq_status", []) + [status]


def samples16(n, start=0, step=257):
    """n 16-bit samples that differ from each other"""
    return [((start + i * step) & 0xFFFF) for i in range(n)]


def words(values):
    out = bytearray()
    for v in values:
        out += bytes((v & 0xFF, (v >> 8) & 0xFF))
    return bytes(out)
