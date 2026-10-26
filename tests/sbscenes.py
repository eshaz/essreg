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

The scenes of two transfers at once (DUAL) run an 8-bit and a 16-bit
transfer together, and look at what each kind played (Card.dac). A Sound
Blaster 16's DAC plays only one of the two, and both run at the DSP's one
rate, so these scenes give both the same rate.
"""

import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from sb16 import encode  # noqa: E402
from sbgame import (BASE, reset, write, read, read_all, mixer,  # noqa: E402
                    program_dma, unmask_dma, dma_count, samples16, words)

BUF = 0x40000               # the game's DMA buffer, below 1 MB
BUF8 = 0x50000              # and its 8-bit one, while both play
SCENES = {}
DUAL = set()                # the scenes of two transfers at once


def scene(fn):
    SCENES[fn.__name__] = fn
    return fn


def dual(fn):
    DUAL.add(fn.__name__)
    return scene(fn)


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


def careful_isr(card):
    """a careful driver's handler: 82h, then the acknowledge of each kind
    it names; one that names neither is no block of the game's"""
    card.out(BASE + 4, 0x82)
    st = card.inp(BASE + 5)
    if not st & 3:
        card.spurious += 1
        return
    card.marks.append(len(card.played))
    card.seen.append(st)
    if st & 2:
        card.inp(BASE + 0x0F)
    if st & 1:
        card.inp(BASE + 0x0E)
    card.blocks += 1


def _long16(card, cmd, blocks, d9_us=None):
    """a 16-bit block of 33000 samples, longer than the chip's counter
    holds in bytes, over an auto-initialized buffer of 16384 samples, as
    Pinball Fantasies' driver plays a long block over a short buffer;
    D9h after d9_us. The frames played in all count where the playback
    ends"""
    prepare(card)
    card.isr = careful_isr
    card.spurious = 0
    n, size = 33000, 16384
    data = words(samples16(size, start=0x0555))
    card.poke(BUF, data)
    program_dma(card, 5, BUF, len(data))
    write(card, 0x41, 0xAC, 0x44, cmd, 0x30, (n - 1) & 0xFF, (n - 1) >> 8)
    t = 0
    while card.blocks < blocks and t < 1600000:
        card.wait(2000)
        t += 2000
        if d9_us is not None and t >= d9_us:
            write(card, 0xD9)
            d9_us = None
    card.wait(30000)
    flat = [s for f in played(card) for s in f]
    out = {"blocks": card.blocks, "seen": card.seen,
           "per_block": per_block(card), "data": flat[:2 * n],
           "spurious": card.spurious}
    if cmd == 0xB0:
        out["frames"] = len(card.played)
    return out


@scene
def play16_long_single(card):
    """a single-cycle block of 33000 samples plays to its end, with one
    interrupt"""
    reset(card)
    return _long16(card, 0xB0, 1)


@scene
def play16_long_auto(card):
    """auto-initialized blocks of 33000 samples: an interrupt at the end
    of each"""
    reset(card)
    return _long16(card, 0xB6, 2)


@scene
def play16_long_exit(card):
    """D9h in the first half of a long block: the block plays to its end"""
    reset(card)
    out = _long16(card, 0xB6, 1, d9_us=100000)
    out["frames"] = len(card.played)
    return out


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


def _mix16(card, again_us=None):
    """a game that mixes up to the position it reads from the 16-bit
    channel's count, as Pinball Fantasies' driver does: a buffer of two
    ticks of 10 ms, and at each tick a counter of the frames, which goes
    on, into the slots behind the position, each the frame it plays next
    time round. The DAC plays the counter in order while what it plays
    stays ahead of the position, or a tick behind it; else a slot plays
    before the game wrote it, or after it wrote it again, and the counter
    steps back. With again_us, B6h comes again then, the channel as it
    was"""
    prepare(card)
    reset(card)
    frames = 882                    # 16-bit stereo at 44.1 kHz
    card.poke(BUF, b"".join(struct.pack("<hh", f, f) for f in range(frames)))
    program_dma(card, 5, BUF, 4 * frames)
    write(card, 0x41, 0xAC, 0x44, 0xB6, 0x30, 0xFF, 0xFF)
    done = last = laps = 0          # done: the frames mixed for
    for t in range(5000, 150001, 5000):
        card.wait(5000)
        if t == again_us:
            write(card, 0xB6, 0x30, 0xFF, 0xFF)
        if t % 10000:
            continue
        moved = (2 * frames - 1 - dma_count(card, 5)) % (2 * frames)
        pos = moved // 2
        laps += pos < last
        last = pos
        for f in range(done, laps * frames + pos):
            card.poke(BUF + 4 * (f % frames),
                      struct.pack("<hh", f + frames, f + frames))
        done = max(done, laps * frames + pos)
    got = [f[0] for f in card.dac(16)]
    return {"back": sum(1 for a, b in zip(got, got[1:]) if b < a),
            "mixed": done > 4 * frames}


@scene
def mix16(card):
    return _mix16(card)


@scene
def mix16_again(card):
    """B6h again between two ticks, the 16-bit channel as it was: the DMA
    goes on where it is"""
    return _mix16(card, again_us=25000)


# --- Pinball Fantasies' drivers (FrontLine Design) ----------------------------

PF_TICK = 20000             # the game's timer, in microseconds


def pf_count(card, ch, top):
    """the channel's count as FrontLine Design's drivers read it: twice,
    until the second is at most 40h below the first; and whether it is
    beyond the buffer's `top`, where the driver writes "dma count=" and
    the count into segment B000h"""
    for _ in range(100):
        first, second = dma_count(card, ch), dma_count(card, ch)
        if (first - second) & 0xFFFF <= 0x40:
            break
    return second, second > top


def pf_mix(card, ch, frames, size, value, ticks):
    """the game at each tick: the position from the count, and the frames
    behind it mixed for their next time round, `value(f)` the bytes of
    frame f; (the frames mixed, the counts beyond the buffer)"""
    unit = 2 if ch >= 4 else 1          # the count's bytes
    done = last = laps = wild = 0
    for _ in range(ticks):
        card.wait(PF_TICK)
        count, beyond = pf_count(card, ch, size // unit)
        wild += beyond
        if beyond:
            continue
        pos = (size // unit - count) * unit * frames // size
        laps += pos < last
        last = pos
        for f in range(done, laps * frames + pos):
            card.poke(BUF + size // frames * (f % frames), value(f + frames))
        done = max(done, laps * frames + pos)
    return done, wild


def pf_ack16(card):
    card.blocks += 1
    card.inp(BASE + 0x0F)


@scene
def resiplay(card):
    """Pinball Fantasies' Sound Blaster 16 driver (SB16.SDR): the highest
    channel of mixer 81h, programmed while it is masked for a buffer of
    two ticks of 16-bit stereo at 44000 Hz; the DSP's reset, D1h, 41h,
    mixer 0Eh, 04h and 22h, B6h for the most whole buffers that fit 65535
    samples, and the unmask after it. At each tick it reads the count and
    mixes up to it a counter of the frames, as mix16 does; its handler
    only acknowledges"""
    prepare(card)
    card.isr = pf_ack16
    bits = mixer(card, 0x81) & 0xEB
    ch = bits.bit_length() - 1
    frames, rate = 1760, 44000
    size = 4 * frames
    card.poke(BUF, b"".join(struct.pack("<hh", f, f) for f in range(frames)))
    program_dma(card, ch, BUF, size, unmask=False)
    reset(card)
    write(card, 0xD1, 0x41, rate >> 8, rate & 0xFF)
    for reg, v in ((0x0E, 0x02), (0x04, 0xFF), (0x22, 0xFF)):
        mixer(card, reg, v)
    n = 0xFFFF // size * size - 1
    write(card, 0xB6, 0x30, n & 0xFF, n >> 8)
    unmask_dma(card, ch)
    done, wild = pf_mix(card, ch, frames, size,
                        lambda f: struct.pack("<hh", f, f), 15)
    got = [f[0] for f in card.dac(16)]
    return {"channel": ch, "start": got[:32],
            "back": sum(1 for a, b in zip(got, got[1:]) if b < a),
            "mixed": done > 4 * frames, "wild": wild}


def pf_isr_pro(n):
    """SBPRO.SDR's handler: the acknowledge, then 48h and 91h again"""
    def isr(card):
        card.blocks += 1
        card.inp(BASE + 0x0E)
        write(card, 0x48, n & 0xFF, n >> 8, 0x91)
    return isr


@scene
def pinball_sbpro(card):
    """the game's Sound Blaster Pro driver (SBPRO.SDR), which plays on the
    ES1869: channel 1 programmed while it is masked for a buffer of two
    ticks of 8-bit stereo at 22 kHz; the DSP's reset, D1h, 40h, mixer 0Eh
    and 04h, 48h for the most whole buffers that fit 65535 bytes and 91h,
    and the unmask after them. Its handler acknowledges and sends 48h and
    91h again; at each tick it mixes up to the count a counter of the
    frames, its low byte left and its high byte right. 0Eh's stereo has no
    effect on a Sound Blaster 16, so each byte plays as a sample. The
    driver's position is up to a byte ahead of the DMA, so that where the
    DMA is the DAC's, as on the card, one byte plays a lap early: a spike,
    and no step back"""
    prepare(card)
    frames = 880
    size = 2 * frames
    n = 0xFFFF // size * size - 1
    card.isr = pf_isr_pro(n)
    card.poke(BUF, b"".join(struct.pack("<H", f) for f in range(frames)))
    program_dma(card, 1, BUF, size, unmask=False)
    reset(card)
    write(card, 0xD1, 0x40, 256 - 500 // 22)
    for reg, v in ((0x0E, 0x02), (0x04, 0xFF)):
        mixer(card, reg, v)
    write(card, 0x48, n & 0xFF, n >> 8, 0x91)
    unmask_dma(card, 1)
    done, wild = pf_mix(card, 1, frames, size,
                        lambda f: struct.pack("<H", f & 0xFFFF), 80)
    b = [((f[0] >> 8) + 128) & 0xFF for f in card.played]
    got = [lo | hi << 8 for lo, hi in zip(b[0::2], b[1::2])]
    return {"start": got[:32], "blocks": card.blocks,
            "back": sum(1 for a, v, c in zip(got, got[1:], got[2:])
                        if c < v and c < a),
            "mixed": done > 30 * frames, "wild": wild}


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


@scene
def play8_stereo(card):
    """C6h: 8-bit unsigned stereo, auto-initialized"""
    prepare(card)
    reset(card)
    block = 1024                    # samples, both channels counted
    card.poke(BUF, _bytes(2 * block, 0x21))
    program_dma(card, 1, BUF, 2 * block)
    write(card, 0x41, 0x56, 0x22, 0xC6, 0x20, (block - 1) & 0xFF,
          (block - 1) >> 8)
    ok = wait_blocks(card, 3)
    got = played(card)
    return {"ok": ok, "seen": card.seen[:3],
            "width": all(len(f) == 2 for f in got),
            "data": [s for f in got for s in f][:2 * block]}


@scene
def pause_continue8(card):
    prepare(card)
    reset(card)
    block = 2048
    card.poke(BUF, _bytes(2 * block, 0x44))
    program_dma(card, 1, BUF, 2 * block)
    write(card, 0x41, 0x2B, 0x11, 0xC6, 0x00, (block - 1) & 0xFF,
          (block - 1) >> 8)
    card.wait(5000)
    write(card, 0xD0)
    n = len(card.played)
    card.wait(20000)
    held = len(card.played) - n
    write(card, 0xD4)
    ok = wait_blocks(card, 2)
    return {"held8": held, "ok": ok, "data": played(card)[:2 * block]}


@scene
def exit_auto8(card):
    prepare(card)
    reset(card)
    block = 512
    card.poke(BUF, _bytes(2 * block, 0x66))
    program_dma(card, 1, BUF, 2 * block)
    write(card, 0x41, 0xAC, 0x44, 0xC6, 0x00, (block - 1) & 0xFF,
          (block - 1) >> 8)
    card.wait(2000)
    write(card, 0xDA)
    card.wait(40000)
    return {"blocks": card.blocks, "frames": len(card.played),
            "seen": card.seen}


@scene
def last_block16(card):
    """a single-cycle B2h while B6h runs auto-initialized: the block that
    plays ends, and the single one after it is the last"""
    prepare(card)
    reset(card)
    block = 512
    data = words(samples16(4 * block, start=0x0999))
    card.poke(BUF, data)
    program_dma(card, 5, BUF, len(data))
    write(card, 0x41, 0xAC, 0x44, 0xB6, 0x10, (block - 1) & 0xFF,
          (block - 1) >> 8)
    wait_blocks(card, 1)
    write(card, 0xB2, 0x10, (block - 1) & 0xFF, (block - 1) >> 8)
    card.wait(60000)
    return {"blocks": card.blocks, "frames": len(card.played),
            "seen": card.seen}


def _adpcm(card, first, then, n, fill):
    """two single-cycle ADPCM blocks of n bytes from one buffer: `first`
    with its reference byte, then `then`"""
    prepare(card)
    reset(card)
    card.poke(BUF, bytes((0x80,)) + _bytes(2 * n - 1, fill))
    program_dma(card, 1, BUF, 2 * n, auto=False)
    write(card, 0x40, 0xD3, first, (n - 1) & 0xFF, (n - 1) >> 8)
    ok = wait_blocks(card, 1)
    write(card, then, (n - 1) & 0xFF, (n - 1) >> 8)
    ok = wait_blocks(card, 2) and ok
    return {"ok": ok, "seen": card.seen[:2], "data": played(card)}


@scene
def adpcm4(card):
    """75h: 4-bit ADPCM with its reference byte, then 74h"""
    return _adpcm(card, 0x75, 0x74, 600, 0x13)


@scene
def adpcm2(card):
    """17h: 2-bit ADPCM with its reference byte, then 16h"""
    return _adpcm(card, 0x17, 0x16, 400, 0x5B)


@scene
def silence(card):
    """80h: silence for a length of sample periods, then the 8-bit
    interrupt"""
    prepare(card)
    reset(card)
    write(card, 0x40, 0xD3, 0x80, 0xFF, 0x03)
    ok = wait_blocks(card, 1)
    return {"ok": ok, "seen": card.seen[:1], "blocks": card.blocks}


@scene
def direct_dac(card):
    """10h, a sample at a time, as a game without DMA plays"""
    prepare(card)
    reset(card)
    for i in range(64):
        write(card, 0x10, (0x40 + 3 * i) & 0xFF)
        card.wait(45)
    return {"data": [tuple(f) for f in card.played]}


@scene
def midi_out(card):
    """38h a byte at a time, then the MIDI UART mode until a reset"""
    prepare(card)
    reset(card)
    write(card, 0x38, 0x90, 0x38, 0x3C, 0x38, 0x7F)
    write(card, 0x35)
    write(card, 0x80, 0x3C, 0x00)
    out = {"midi": list(card.midi_out), "reset": reset(card)}
    write(card, 0xE1)
    out["version"] = (read(card), read(card))
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


# --- two transfers at once -------------------------------------------------------

def dual_isr(card):
    """a driver that runs both kinds: 82h, then each kind's acknowledge,
    each kind's blocks counted where its own frames are"""
    card.out(BASE + 4, 0x82)
    st = card.inp(BASE + 5)
    card.seen.append(st)
    if st & 2:
        card.marks16.append(len(card.dac(16)))
        card.inp(BASE + 0x0F)
        card.blocks16 += 1
    if st & 1:
        card.marks8.append(len(card.dac(8)))
        card.inp(BASE + 0x0E)
        card.blocks8 += 1


def dual_prepare(card):
    prepare(card)
    card.marks8, card.marks16 = [], []
    card.blocks8 = card.blocks16 = 0
    card.isr = dual_isr


def wait_both(card, n8, n16, limit_us=600000):
    """time passes until the handler counted n8 8-bit and n16 16-bit
    blocks"""
    t = 0
    while (card.blocks8 < n8 or card.blocks16 < n16) and t < limit_us:
        card.wait(250)
        t += 250
    return card.blocks8 >= n8 and card.blocks16 >= n16


def kind_data(card, kind, n):
    """the samples the kind played up to its last block mark, n at most"""
    marks = card.marks16 if kind == 16 else card.marks8
    frames = card.dac(kind)
    end = marks[-1] if marks else len(frames)
    return [s for f in frames[:end] for s in f][:n]


def kind_blocks(card, kind):
    """the kind's frames between its block marks"""
    marks = [0] + (card.marks16 if kind == 16 else card.marks8)
    return [b - a for a, b in zip(marks, marks[1:])]


def _both(card, b16, b8, rate=44100, auto8=True, n8=None):
    """16-bit stereo music, auto-initialized over two blocks of b16 samples
    on the 16-bit channel, and 8-bit mono sound of b8 samples a block on
    the 8-bit channel, n8 bytes of it, at one rate"""
    music = words(samples16(2 * b16, start=0x1357))
    n8 = n8 or 2 * b8
    card.poke(BUF, music)
    card.poke(BUF8, _bytes(n8, 0x29))
    program_dma(card, 5, BUF, len(music))
    program_dma(card, 1, BUF8, n8, auto=auto8)
    write(card, 0x41, rate >> 8, rate & 0xFF)
    write(card, 0xB6, 0x30, (b16 - 1) & 0xFF, (b16 - 1) >> 8)
    write(card, 0xC6 if auto8 else 0xC0, 0x00, (b8 - 1) & 0xFF,
          (b8 - 1) >> 8)


@dual
def dual_play(card):
    """16-bit stereo music and 8-bit mono sound together, each
    auto-initialized on its own channel, each with its own interrupt"""
    dual_prepare(card)
    reset(card)
    _both(card, 1024, 600)
    ok = wait_both(card, 4, 4)
    return {"ok": ok, "spurious": [st for st in card.seen if not st & 3],
            "per_block16": kind_blocks(card, 16)[:4],
            "per_block8": kind_blocks(card, 8)[:4],
            "data16": kind_data(card, 16, 4096),
            "data8": kind_data(card, 8, 1200)}


@dual
def duplex_rec8(card):
    """full duplex: 16-bit stereo playback and an 8-bit mono recording at
    once, as a voice chat program plays and records"""
    dual_prepare(card)
    reset(card)
    b16, size = 1024, 1500
    music = words(samples16(2 * b16, start=0x2468))
    card.poke(BUF, music)
    card.poke(BUF8, b"\x5A" * size)
    card.record_from(ramp())
    program_dma(card, 5, BUF, len(music))
    program_dma(card, 1, BUF8, size, auto=False, record=True)
    write(card, 0x41, 0x56, 0x22)
    write(card, 0xB6, 0x30, (b16 - 1) & 0xFF, (b16 - 1) >> 8)
    write(card, 0xC8, 0x00, (size - 1) & 0xFF, (size - 1) >> 8)
    ok = wait_both(card, 1, 3)
    card.wait(2000)
    return {"ok": ok, "blocks8": card.blocks8,
            "per_block16": kind_blocks(card, 16)[:3],
            "data16": kind_data(card, 16, 3 * b16),
            "unbroken": unbroken(card.peek(BUF8, size), 8, False, False)}


@dual
def sfx8_over16(card):
    """a game's 16-bit music goes on while an 8-bit sound effect plays
    once on the 8-bit channel"""
    dual_prepare(card)
    reset(card)
    b16, sfx = 1024, 700
    music = words(samples16(2 * b16, start=0x0BAD))
    card.poke(BUF, music)
    card.poke(BUF8, _bytes(sfx, 0x3C))
    program_dma(card, 5, BUF, len(music))
    write(card, 0x41, 0xAC, 0x44, 0xB6, 0x30, (b16 - 1) & 0xFF,
          (b16 - 1) >> 8)
    wait_both(card, 0, 1)
    program_dma(card, 1, BUF8, sfx, auto=False)
    write(card, 0xC0, 0x00, (sfx - 1) & 0xFF, (sfx - 1) >> 8)
    ok = wait_both(card, 1, 5)
    return {"ok": ok, "blocks8": card.blocks8,
            "per_block16": kind_blocks(card, 16)[:5],
            "data16": kind_data(card, 16, 5 * b16),
            "data8": kind_data(card, 8, sfx)}


@dual
def pause16_keep8(card):
    """D5h pauses the 16-bit transfer while the 8-bit one plays on; D6h
    continues it"""
    dual_prepare(card)
    reset(card)
    _both(card, 1024, 1500)
    wait_both(card, 1, 1)
    write(card, 0xD5)
    n16, n8 = len(card.dac(16)), len(card.dac(8))
    card.wait(20000)
    held = len(card.dac(16)) - n16
    went8 = len(card.dac(8)) - n8
    write(card, 0xD6)
    ok = wait_both(card, card.blocks8 + 1, card.blocks16 + 2)
    return {"held": held, "went8": went8 > 600, "ok": ok,
            "data16": kind_data(card, 16, 4096)}


@dual
def exit16_keep8(card):
    """D9h ends the 16-bit transfer with the block that plays, and the
    8-bit one plays on"""
    dual_prepare(card)
    reset(card)
    _both(card, 512, 600)
    card.wait(2000)
    write(card, 0xD9)
    card.wait(40000)
    return {"blocks16": card.blocks16, "frames16": len(card.dac(16)),
            "more8": card.blocks8 >= 3}


@dual
def exit8_keep16(card):
    """DAh ends the 8-bit transfer with the block that plays, and the
    16-bit one plays on"""
    dual_prepare(card)
    reset(card)
    _both(card, 1024, 400)
    card.wait(2000)
    write(card, 0xDA)
    card.wait(40000)
    return {"blocks8": card.blocks8, "frames8": len(card.dac(8)),
            "more16": card.blocks16 >= 3}


@dual
def reset_stops_both(card):
    """the DSP's reset stops both transfers, and neither interrupts any
    more"""
    dual_prepare(card)
    reset(card)
    _both(card, 1024, 600)
    wait_both(card, 1, 1)
    out = {"reset": reset(card)}
    n16, n8 = len(card.dac(16)), len(card.dac(8))
    blocks = card.blocks8 + card.blocks16
    card.wait(30000)
    out.update({"held": len(card.dac(16)) - n16,
                "held8": len(card.dac(8)) - n8,
                "after": card.blocks8 + card.blocks16 - blocks})
    return out


@dual
def last_block16_with8(card):
    """a single-cycle B2h while B6h runs auto-initialized, with an 8-bit
    transfer running too: the 16-bit one ends after its queued block"""
    dual_prepare(card)
    reset(card)
    _both(card, 512, 700)
    wait_both(card, 0, 1)
    write(card, 0xB2, 0x10, 0xFF, 0x01)
    card.wait(60000)
    return {"blocks16": card.blocks16, "frames16": len(card.dac(16)),
            "more8": card.blocks8 >= 3}
