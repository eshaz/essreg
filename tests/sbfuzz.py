# (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
#
# Licensed under GPL Version 3.0
"""Random games for the Sound Blaster 16 fronts. A seed gives a list of
steps such as a game's sound driver takes: DSP resets, commands with
their operands (transfers, rates, pauses, the speaker, the version),
mixer writes and reads, the DMA channels programmed, unmasked and read,
the status ports polled, waits with the game's interrupt handler on or
off, and now and then a byte nobody sends. They are plausible enough to
reach deep into a front's states and random enough to go where no scene
in tests/sbscenes.py goes. play() runs them on a card and gives what the
game read; tests/test_sbfsm.py plays each on esssb16.com and on the
reference (tests/sbfsm.py), which have to reach the chip alike, access
for access.
"""

import random

from sbgame import BASE, dma_count, mixer, program_dma, read, reset, \
    sb16_isr, write

# where the steps' DMA buffers go: the 8-bit channel's 64 KB page and the
# 16-bit channel's 128 KB one
BUF8, BUF16 = 0x60000, 0x40000

# the DSP commands of a game, each with its operands: None for random
# bytes, else a function of the generator giving them
LEN = "len"                     # a transfer's length less one, two bytes


def _len(rng):
    n = rng.choice((15, 63, 255, 1023, 4095, rng.randrange(1, 8192)))
    return [n & 0xFF, n >> 8]


def _mode(rng):
    """a DSP 4.x transfer's mode byte: stereo (5) and signed (4), now and
    then other bits"""
    m = rng.choice((0x00, 0x10, 0x20, 0x30))
    if rng.random() < 0.05:
        m = rng.randrange(256)
    return [m]


COMMANDS = (
    (0x10, lambda r: [r.randrange(256)]),       # the direct DAC
    (0x14, _len),                               # 8-bit, one block
    (0x1C, None),                               # 8-bit auto
    (0x16, _len), (0x17, _len),                 # 2-bit ADPCM
    (0x24, _len), (0x2C, None),                 # 8-bit input
    (0x40, lambda r: [r.choice((0x83, 0xA5, 0xD2, r.randrange(256)))]),
    (0x41, lambda r: list(r.choice(((0xAC, 0x44), (0x56, 0x22),
                                    (0x2B, 0x11), (0x1F, 0x40))))),
    (0x42, lambda r: [0x56, 0x22]),
    (0x48, _len),                               # the block size
    (0x74, _len), (0x75, _len), (0x76, _len), (0x77, _len), (0x7D, None),
    (0x80, _len),                               # silence
    (0x90, None), (0x91, None),                 # high-speed 8-bit
    (0xD0, None), (0xD4, None), (0xD5, None), (0xD6, None),
    (0xD1, None), (0xD3, None), (0xD8, None),   # the speaker
    (0xD9, None), (0xDA, None),                 # auto-init ends
    (0xE0, lambda r: [r.randrange(256)]), (0xE1, None), (0xE3, None),
    (0xE4, lambda r: [r.randrange(256)]), (0xE8, None),
    (0xF2, None), (0xF3, None), (0xF8, None), (0xFA, lambda r: [0, 0]),
)
# the DSP 4.x transfers: B0h-BFh 16-bit, C0h-CFh 8-bit, the bits of the
# low nibble picking input (8), auto-init (4) and the FIFO (2)
TRANSFERS = (0xB0, 0xB2, 0xB4, 0xB6, 0xB8, 0xBC, 0xBE,
             0xC0, 0xC2, 0xC4, 0xC6, 0xC8, 0xCC, 0xCE)
# the mixer registers a game reaches, and now and then any
MIXER = (0x00, 0x04, 0x0A, 0x0C, 0x0E, 0x22, 0x26, 0x28, 0x2E,
         0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3A,
         0x3B, 0x3C, 0x3D, 0x3E, 0x3F, 0x40, 0x41, 0x42, 0x43, 0x44, 0x45,
         0x46, 0x47, 0x80, 0x81, 0x82)


def steps(seed, n=60):
    """the steps of the random game `seed`"""
    rng = random.Random(seed)
    out = [("reset",)]
    for _ in range(n):
        x = rng.random()
        if x < 0.06:
            out.append(("reset",))
        elif x < 0.30:
            cmd, ops = rng.choice(COMMANDS)
            out.append(("dsp", cmd) + tuple(ops(rng) if ops else ()))
        elif x < 0.42:
            ch = rng.choice((1, 5))
            size = rng.choice((256, 1024, 4096, 16384, rng.randrange(2, 32768)))
            if ch == 5:
                size &= ~1
            out.append(("dma", ch, size, rng.random() < 0.8,
                        rng.random() < 0.85))
            cmd = rng.choice(TRANSFERS)
            out.append(("dsp", cmd) + tuple(_mode(rng) + _len(rng)))
        elif x < 0.52:
            reg = rng.choice(MIXER) if rng.random() < 0.9 else \
                rng.randrange(256)
            out.append(("mix", reg, rng.randrange(256)))
        elif x < 0.58:
            out.append(("mixr", rng.choice(MIXER)))
        elif x < 0.74:
            out.append(("wait", rng.choice((50, 500, 3000, 12000,
                                            rng.randrange(1, 40000)))))
        elif x < 0.78:
            out.append(("read",))
        elif x < 0.82:
            out.append(("in", rng.choice((0x0A, 0x0C, 0x0E, 0x0F, 0x04,
                                          0x05, 0x06, 0x01, 0x02, 0x03))))
        elif x < 0.85:
            out.append(("out", rng.choice((0x06, 0x0A, 0x0E, 0x0F, 0x00,
                                           0x02, 0x08, 0x09)),
                        rng.randrange(256)))
        elif x < 0.89:
            out.append(("count", rng.choice((1, 5))))
        elif x < 0.92:
            out.append(("unmask", rng.choice((1, 5))))
        elif x < 0.94:
            out.append(("mask", rng.choice((1, 5))))
        elif x < 0.97:
            out.append(("isr", rng.random() < 0.7))
        else:
            out.append(("byte", rng.randrange(256)))   # any byte to the DSP
    return out


def play(card, game, base=BASE):
    """the steps on the card, the game's interrupt handler on at first;
    what the game read, step by step"""
    seen = []
    card.isr = sb16_isr
    for step in game:
        kind, args = step[0], step[1:]
        if kind == "reset":
            seen.append(reset(card, base))
        elif kind == "dsp":
            seen.append(write(card, *args, base=base))
        elif kind == "byte":
            card.out(base + 0x0C, args[0])
        elif kind == "dma":
            ch, size, auto, unmask = args
            program_dma(card, ch, BUF16 if ch >= 4 else BUF8, size,
                        auto=auto, unmask=unmask)
        elif kind == "mix":
            mixer(card, args[0], args[1], base=base)
        elif kind == "mixr":
            seen.append(mixer(card, args[0], base=base))
        elif kind == "wait":
            card.wait(args[0])
        elif kind == "read":
            seen.append(read(card, base))
        elif kind == "in":
            seen.append(card.inp(base + args[0]))
        elif kind == "out":
            card.out(base + args[0], args[1])
        elif kind == "count":
            seen.append(dma_count(card, args[0]))
        elif kind == "unmask":
            card.out(0xD4 if args[0] >= 4 else 0x0A, args[0] & 3)
        elif kind == "mask":
            card.out(0xD4 if args[0] >= 4 else 0x0A, 4 | (args[0] & 3))
        elif kind == "isr":
            card.isr = sb16_isr if args[0] else None
    card.wait(20000)
    return seen


def text(game):
    """the steps as lines, for a report"""
    return "\n".join(" ".join(s if isinstance(s, str) else
                              ("%Xh" % s if isinstance(s, int) and
                               not isinstance(s, bool) else str(s))
                              for s in step) for step in game)
