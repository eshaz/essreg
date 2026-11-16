# (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
#
# Licensed under GPL Version 3.0
"""The operations a front sends the chip, as the chip model (esschip) and
the DMA controllers (dma8237) record them once record() starts them, in
the order they happened:

    ("R", reg, v)       a register A1h-B9h written, v as the chip keeps it
    ("D", c, *ops)      a command of the chip's own, with its operands
    ("M", reg, v)       a mixer register written
    ("FIFO",)           +6 bit 1: Audio 1's FIFO emptied
    ("RESET",)          +6 bit 0 set: the chip's software reset
    ("ACK",)            +Eh read with Audio 1's interrupt pending
    ("DMA", n, mode, addr, count, page)
                        channel n's first cycle with this programming

A read through C0h, a poll of +Ch and a mixer read change nothing, so they
aren't operations. A write is one also with the value that the register
holds, because that value depends on who else wrote the register, such as
ESS's own code in the VxD (sbfronts.VxDCard keeps its accesses out). The
fronts' port traces are compared access for access as well
(tests/test_sbfsm.py), with the value of a write to the DMA controllers'
flip-flop, master clear and mask clear ports left out, since the
controllers ignore it.
"""

DONT_CARE = (0x0C, 0x0D, 0x0E, 0xD8, 0xDA, 0xDC)

# (a test_sb16conf class's name, scene): (port accesses, operations) of
# the runs there, for tests/test_sbfsm.py
RUNS = {}


def record(card):
    """the card's chip records its operations from now on, into the list
    returned"""
    trace = []
    card.chip.trace = trace
    card.chip.dmac.trace = trace
    card.chip.dmac.served = {}
    return trace


def ports(log):
    """a chip's port log (("in" or "out", port, v)) without the values
    the DMA controllers ignore"""
    return [(e[0], e[1], None) if e[0] == "out" and e[1] in DONT_CARE
            else e for e in log]


def transfers(trace):
    """the operations of the transfers: the levels and the record source,
    which the fronts keep in their views of the mixer in their own ways,
    left out"""
    return [op for op in trace if op[0] != "M" or 0x70 <= op[1] <= 0x7A]


def name(op):
    """an operation as text"""
    kind = op[0]
    if kind in ("R", "M"):
        return "%s(%02Xh, %02Xh)" % (kind, op[1], op[2])
    if kind == "D":
        return "D(%s)" % ", ".join("%02Xh" % v for v in op[1:])
    if kind == "DMA":
        return "DMA(%d: mode %02Xh, %04Xh, %04Xh, page %02Xh)" % op[1:]
    if kind in ("in", "out"):
        v = "-" if op[2] is None else "%02Xh" % op[2]
        return "%s %03Xh %s" % (kind, op[1], v)
    return kind


def divergence(want, got, context=6):
    """None if the two lists are the same, else the first difference and
    what comes before it, as text"""
    n = min(len(want), len(got))
    at = next((i for i in range(n) if want[i] != got[i]), None)
    if at is None:
        if len(want) == len(got):
            return None
        at = n
    lines = ["first difference at %d (%d against %d):" % (
        at, len(want), len(got))]
    end = min(at + 3, max(len(want), len(got)))
    for i in range(max(0, at - context), end):
        w = name(want[i]) if i < len(want) else ""
        g = name(got[i]) if i < len(got) else ""
        lines.append("%s %5d  %-40s %s" % ("  " if w == g else "**", i, w, g))
    return "\n".join(lines)
