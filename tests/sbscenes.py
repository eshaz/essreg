# (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
#
# Licensed under GPL Version 3.0
"""What games do with a Sound Blaster 16, as scripts that run on any card
(sbgame.Card) and return what the game saw.

Each scene returns a dict: the DSP's answers, the mixer's values, what the
interrupt handler found in mixer 82h, the DMA counts it read, the frames
the DAC played, cut at the blocks the game counted, and whether a
recording holds what the ADC heard, none of it left out. The conformance
tests (test_sb16conf.py) run a scene on the Sound Blaster 16 model and on
a front with the chip model, and compare the two.
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from sb16 import encode  # noqa: E402
from sbgame import (BASE, reset, write, read, read_all, mixer,  # noqa: E402
                    program_dma, dma_count, samples16, words)

BUF = 0x40000               # the game's DMA buffer, below 1 MB
SCENES = {}


def scene(fn):
    SCENES[fn.__name__] = fn
    return fn


def blocks_isr(kind=16):
    """a driver's handler: 82h, then the acknowledge it says, counting the
    blocks"""
    def isr(card):
        card.marks.append(len(card.played))
        card.out(BASE + 4, 0x82)
        st = card.inp(BASE + 5)
        card.seen.append(st)
        if st & 2:
            card.inp(BASE + 0x0F)
        if st & 1:
            card.inp(BASE + 0x0E)
        card.blocks += 1
        if card.on_block:
            card.on_block(card)
    return isr


def prepare(card):
    card.seen, card.blocks, card.marks, card.on_block = [], 0, [], None
    card.isr = blocks_isr()


def wait_blocks(card, n, limit_us=400000):
    """time passes until the handler counted n blocks"""
    t = 0
    while card.blocks < n and t < limit_us:
        card.wait(250)
        t += 250
    return card.blocks >= n


def played(card, start=0):
    """the frames played from `start` up to the last block mark"""
    marks = card.marks
    end = marks[-1] if marks else len(card.played)
    return [tuple(f) for f in card.played[start:end]]


def per_block(card, start=0):
    """frames between the block marks, from `start`"""
    marks = [start] + card.marks
    return [b - a for a, b in zip(marks, marks[1:])]


# --- detection ----------------------------------------------------------------

@scene
def detect(card):
    """what setup programs check: the reset, the version, the copyright,
    E0h, the test register, the card's mixer registers"""
    prepare(card)
    out = {"reset": reset(card)}
    write(card, 0xE1)
    out["version"] = (read(card), read(card))
    write(card, 0xE3)
    out["copyright"] = bytes(read_all(card))
    write(card, 0xE0, 0x5A)
    out["e0"] = read(card)
    write(card, 0xE4, 0xA5, 0xE8)
    out["test"] = read(card)
    write(card, 0xD1, 0xD8)
    out["speaker"] = read(card)
    write(card, 0xD3, 0xD8)
    out["speaker_off"] = read(card)
    out["mixer"] = [mixer(card, r) for r in (0x80, 0x81, 0x82)]
    write(card, 0xE1)
    out["again"] = (read(card), read(card))
    return out


@scene
def ess_probe(card):
    """a library asks for an ESS chip (E7h), then goes on as a Sound
    Blaster 16 driver"""
    prepare(card)
    reset(card)
    write(card, 0xE7)
    out = {"e7": read_all(card)}
    reset(card)
    write(card, 0xE1)
    out["version"] = (read(card), read(card))
    out.update(_play16(card, 0xB6, 0x30, 1024, 3))
    return out


@scene
def irq_f2_f3(card):
    """the interrupt test of a setup program: F2h, then F3h"""
    prepare(card)
    reset(card)
    write(card, 0xF2)
    card.wait(100)
    write(card, 0xF3)
    card.wait(100)
    return {"seen": card.seen, "blocks": card.blocks,
            "after": mixer(card, 0x82)}


@scene
def dma_ident(card):
    """E2h through the 8-bit channel, four times"""
    prepare(card)
    reset(card)
    program_dma(card, 1, BUF, 4, auto=False, record=True)
    for x in (0x00, 0x00, 0x5A, 0xFF):
        write(card, 0xE2, x)
        card.wait(100)
    return {"bytes": card.peek(BUF, 4)}


# --- 16-bit playback ------------------------------------------------------------

def _play16(card, cmd, mode, block, blocks, rate=44100, dma_first=True,
            ch=5):
    """an auto-initializing buffer of two blocks; the handler counts"""
    n = 2 * block
    stereo = bool(mode & 0x20)
    start = len(card.played)
    data = words(samples16(n, start=0x1234))
    card.poke(BUF, data)
    if dma_first:
        program_dma(card, ch, BUF, len(data))
    write(card, 0x41, rate >> 8, rate & 0xFF)
    write(card, cmd, mode, (block - 1) & 0xFF, (block - 1) >> 8)
    if not dma_first:
        program_dma(card, ch, BUF, len(data))
    ok = wait_blocks(card, blocks)
    got = played(card, start)
    flat = [s for f in got for s in f]
    return {"ok": ok, "seen": card.seen[:blocks],
            "per_block": per_block(card, start)[:blocks],
            "width": all(len(f) == (2 if stereo else 1) for f in got),
            "data": flat[:n]}


@scene
def play16_stereo(card):
    prepare(card)
    reset(card)
    return _play16(card, 0xB6, 0x30, 1024, 4)


@scene
def play16_mono_unsigned(card):
    prepare(card)
    reset(card)
    return _play16(card, 0xB6, 0x00, 512, 4, rate=22050)


@scene
def play16_dma_after(card):
    """the DSP command before the DMA controller's programming"""
    prepare(card)
    reset(card)
    return _play16(card, 0xB6, 0x30, 1024, 3, dma_first=False)


@scene
def play16_dma_before_reset(card):
    """the 16-bit channel programmed before the DSP's reset"""
    prepare(card)
    block = 1024
    data = words(samples16(2 * block, start=0x0777))
    card.poke(BUF, data)
    program_dma(card, 5, BUF, len(data))
    reset(card)
    write(card, 0x41, 0xAC, 0x44, 0xB6, 0x30, (block - 1) & 0xFF,
          (block - 1) >> 8)
    ok = wait_blocks(card, 3)
    flat = [s for f in played(card) for s in f]
    return {"ok": ok, "seen": card.seen[:3], "data": flat[:2 * block]}


@scene
def play16_single_each_block(card):
    """the DMA runs over the buffer in auto-initialize; each block is a
    single-cycle B0h that the handler sends for the next"""
    prepare(card)
    reset(card)
    block = 512
    data = words(samples16(4 * block, start=0x0400))
    card.poke(BUF, data)
    program_dma(card, 5, BUF, len(data))

    def again(c):
        if c.blocks < 6:
            write(c, 0xB0, 0x10, (block - 1) & 0xFF, (block - 1) >> 8)
    card.on_block = again
    write(card, 0x41, 0x56, 0x22)
    write(card, 0xB0, 0x10, (block - 1) & 0xFF, (block - 1) >> 8)
    ok = wait_blocks(card, 6)
    flat = [s for f in played(card) for s in f]
    return {"ok": ok, "data": flat[:4 * block], "blocks": card.blocks}


@scene
def play16_odd_command(card):
    """B7h: bit 0 is ignored, so it is B6h"""
    prepare(card)
    reset(card)
    return _play16(card, 0xB7, 0x30, 512, 2)


@scene
def play16_8bit_channel(card):
    """mixer 81h without a 16-bit channel: 16-bit sound on the 8-bit one"""
    prepare(card)
    reset(card)
    mixer(card, 0x81, 0x02)
    out = _play16(card, 0xB6, 0x30, 512, 3, ch=1)
    mixer(card, 0x81, 0x22)
    return out


@scene
def pause_continue16(card):
    prepare(card)
    reset(card)
    block = 1024
    data = words(samples16(2 * block))
    card.poke(BUF, data)
    program_dma(card, 5, BUF, len(data))
    write(card, 0x41, 0xAC, 0x44, 0xB6, 0x30, (block - 1) & 0xFF,
          (block - 1) >> 8)
    card.wait(3000)
    write(card, 0xD5)
    n = len(card.played)
    card.wait(10000)
    held = len(card.played) - n
    write(card, 0xD6)
    ok = wait_blocks(card, 3)
    flat = [s for f in played(card) for s in f]
    return {"held": held, "ok": ok, "data": flat[:2 * block]}


@scene
def exit_auto16(card):
    prepare(card)
    reset(card)
    block = 512
    data = words(samples16(2 * block))
    card.poke(BUF, data)
    program_dma(card, 5, BUF, len(data))
    write(card, 0x41, 0xAC, 0x44, 0xB6, 0x30, (block - 1) & 0xFF,
          (block - 1) >> 8)
    card.wait(2000)
    write(card, 0xD9)
    card.wait(40000)
    return {"blocks": card.blocks, "frames": len(card.played),
            "seen": card.seen}


@scene
def position16(card):
    """a game that mixes ahead of the DMA position it reads from the
    16-bit channel's count"""
    prepare(card)
    reset(card)
    block = 2048
    data = words(samples16(2 * block))
    card.poke(BUF, data)
    program_dma(card, 5, BUF, len(data))
    write(card, 0x41, 0xAC, 0x44, 0xB6, 0x30, (block - 1) & 0xFF,
          (block - 1) >> 8)
    counts = []
    for _ in range(8):
        card.wait(5000)
        counts.append(dma_count(card, 5))
    moving = sum(1 for a, b in zip(counts, counts[1:]) if a != b)
    return {"moving": moving >= 6}


# --- 8-bit playback -----------------------------------------------------------

def _bytes(n, start=0x10):
    return bytes((start + 7 * i) & 0xFF for i in range(n))


@scene
def play8_cx(card):
    prepare(card)
    reset(card)
    block = 1024
    card.poke(BUF, _bytes(2 * block))
    program_dma(card, 1, BUF, 2 * block)
    write(card, 0x41, 0x56, 0x22, 0xC6, 0x00, (block - 1) & 0xFF,
          (block - 1) >> 8)
    ok = wait_blocks(card, 3)
    return {"ok": ok, "seen": card.seen[:3],
            "data": [f for f in played(card)][:2 * block]}


@scene
def play8_legacy_auto(card):
    """an SB 2.0 driver: the time constant, 48h and 1Ch"""
    prepare(card)
    reset(card)
    block = 1000
    card.poke(BUF, _bytes(2 * block, 0x33))
    program_dma(card, 1, BUF, 2 * block)
    write(card, 0xD1, 0x40, 0xA5, 0x48, (block - 1) & 0xFF,
          (block - 1) >> 8, 0x1C)
    ok = wait_blocks(card, 3)
    return {"ok": ok, "seen": card.seen[:3],
            "data": [f for f in played(card)][:2 * block]}


@scene
def play8_single_each_block(card):
    """an SB 1.x driver: 14h for each block from the handler"""
    prepare(card)
    reset(card)
    block = 800
    card.poke(BUF, _bytes(2 * block, 0x50))
    program_dma(card, 1, BUF, 2 * block)

    def again(c):
        if c.blocks < 4:
            write(c, 0x14, (block - 1) & 0xFF, (block - 1) >> 8)
    card.on_block = again
    write(card, 0xD1, 0x40, 0xA5, 0x14, (block - 1) & 0xFF,
          (block - 1) >> 8)
    ok = wait_blocks(card, 4)
    return {"ok": ok, "data": [f for f in played(card)][:2 * block]}


@scene
def play8_high_speed(card):
    prepare(card)
    reset(card)
    block = 2048
    card.poke(BUF, _bytes(2 * block, 0x77))
    program_dma(card, 1, BUF, 2 * block)
    write(card, 0x40, 0xE9, 0x48, (block - 1) & 0xFF, (block - 1) >> 8,
          0x90)
    ok = wait_blocks(card, 3)
    return {"ok": ok, "data": [f for f in played(card)][:2 * block]}


@scene
def eight_then_sixteen(card):
    """a sound effect in 8-bit, the DSP reset, then 16-bit music"""
    prepare(card)
    reset(card)
    card.poke(BUF + 0x8000, _bytes(512))
    program_dma(card, 1, BUF + 0x8000, 512, auto=False)
    write(card, 0x40, 0xA5, 0x14, 0xFF, 0x01)
    wait_blocks(card, 1)
    first = len(card.played)
    reset(card)
    card.blocks, card.marks, card.seen = 0, [], []
    card.wait(100)
    out = _play16(card, 0xB6, 0x30, 1024, 3)
    out["first"] = first
    return out


# --- recording -------------------------------------------------------------------

RAMP = 4000                 # frames of the ADC's input to look for the data in


def ramp():
    """the ADC's input, a frame that differs from the last every time, the
    same for every card"""
    k = [0]

    def source(n):
        out = []
        for _ in range(n):
            v = (k[0] * 1031) % 0xF000 - 0x7800
            out.append((v, 0x10 - v))
            k[0] += 1
        return out
    return source


def unbroken(data, bits, signed, stereo):
    """True if data is the ADC's input from some frame on, none left out"""
    frame = (bits // 8) * (2 if stereo else 1)
    stream = encode(ramp()(RAMP), bits, signed, stereo)
    at = stream.find(data)
    while at >= 0 and at % frame:
        at = stream.find(data, at + 1)
    return at >= 0


def _record(card, cmd, ops, ch, size, fmt, rate=None, tc=None):
    """one single-cycle recording of size bytes into the buffer, in the
    format fmt (bits, signed, stereo)"""
    prepare(card)
    reset(card)
    card.poke(BUF, b"\x5A" * size)
    card.record_from(ramp())
    program_dma(card, ch, BUF, size, auto=False, record=True)
    if rate is not None:
        write(card, 0x42, rate >> 8, rate & 0xFF)
    if tc is not None:
        write(card, 0x40, tc)
    write(card, cmd, *ops)
    ok = wait_blocks(card, 1)
    card.wait(2000)
    return {"ok": ok, "seen": card.seen, "blocks": card.blocks,
            "unbroken": unbroken(card.peek(BUF, size), *fmt)}


@scene
def rec16_stereo(card):
    """BAh: 16-bit signed stereo, once, on the 16-bit channel"""
    return _record(card, 0xBA, (0x30, 0xFF, 0x07), 5, 4096,
                   (16, True, True), rate=22050)


@scene
def rec8_mono(card):
    """CAh: 8-bit unsigned mono, once"""
    return _record(card, 0xCA, (0x00, 0xCF, 0x07), 1, 2000,
                   (8, False, False), rate=11025)


@scene
def rec8_legacy(card):
    """an SB 2.0 driver's recording: the time constant, then 24h"""
    return _record(card, 0x24, (0xDB, 0x05), 1, 1500, (8, False, False),
                   tc=0xA5)


# --- the mixer -------------------------------------------------------------------

@scene
def mixer_sb16(card):
    prepare(card)
    reset(card)
    mixer(card, 0x00, 0)
    out = {"reset": [mixer(card, r) for r in range(0x30, 0x48)]}
    for r, v in ((0x30, 0xF8), (0x31, 0xA0), (0x32, 0x88), (0x34, 0x50),
                 (0x3C, 0x1E), (0x3D, 0x10), (0x3E, 0x08), (0x44, 0x90)):
        mixer(card, r, v)
    out["written"] = [mixer(card, r) for r in range(0x30, 0x48)]
    out["sbpro"] = [mixer(card, r)
                    for r in (0x04, 0x22, 0x26, 0x28, 0x2E, 0x0A)]
    mixer(card, 0x22, 0x9F)
    out["master"] = (mixer(card, 0x30), mixer(card, 0x31))
    mixer(card, 0x00, 0)
    out["again"] = [mixer(card, r) for r in (0x30, 0x3C, 0x3D)]
    return out
