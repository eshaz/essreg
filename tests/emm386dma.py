# (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
#
# Licensed under GPL Version 3.0
"""EMM386's DMA translation between a V86 program and the 8237s, as the
research on it says (docs/SB16.md): EMM386 traps the DMA controllers'
ports, keeps what a program writes of a channel (its address, page, count
and mode) and programs the 8237 with it when the program unmasks the
channel; reads of the address, count and status are the 8237's. The
research has EMM386 copy a buffer that crosses a page through one of its
own, which isn't modelled: essprobe's h1.cross cases tell.

`at_unmask` False passes every write on at once instead, as when the
memory manager's translation is off or the program runs in real mode.
`virtual_ff` True keeps the byte flip-flop of the program's writes in
EMM386 (a clear goes to both, a read toggles the 8237's); False uses the
8237's for both. essprobe's h1 cases tell which of these the hardware does.

The program's accesses go through outb() and inb(); a ring 0 handler, or
the window, reaches the 8237 itself.
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from dma8237 import PAGE_PORTS, PORT_PAGE  # noqa: E402

FF_PORTS = {0x0C: 0, 0xD8: 4}
MASK_PORTS = {0x0A: 0, 0xD4: 4}
MODE_PORTS = {0x0B: 0, 0xD6: 4}
ALL_MASK_PORTS = {0x0F: 0, 0xDE: 4}


def channel_regs(port):
    """(channel, 'addr' or 'count') of an address or count port, else None"""
    if port < 0x08:
        return port >> 1, ("count" if port & 1 else "addr")
    if 0xC0 <= port < 0xD0 and not port & 1:
        n = (port - 0xC0) >> 1
        return 4 + (n >> 1), ("count" if n & 1 else "addr")
    return None


class EMM386DMA:
    def __init__(self, dmac, at_unmask=True, virtual_ff=False):
        self.dmac = dmac
        self.at_unmask = at_unmask
        self.virtual_ff = virtual_ff
        self.ff = [0, 0]                # EMM386's own, for the writes
        # what the program wrote of each channel, as bytes in order
        self.v = [{"addr": [0, 0], "count": [0, 0], "page": None,
                   "mode": None, "dirty": False} for _ in range(8)]
        self.programmed = []            # (channel, the virtual copy) at unmasks

    def handles(self, port):
        return self.dmac.handles(port)

    def _flip(self, ctl):
        if self.virtual_ff:
            b = self.ff[ctl]
            self.ff[ctl] ^= 1
            return b
        b = self.dmac.ff[ctl]
        self.dmac.ff[ctl] ^= 1
        return b

    def _program(self, ch):
        """the program's channel into the 8237, as its unmask finds it"""
        v = self.v[ch]
        if not v["dirty"]:
            return
        base = 0xC0 if ch >= 4 else 0x00
        step = 4 if ch >= 4 else 2
        apo = base + step * (ch & 3)
        cpo = apo + (2 if ch >= 4 else 1)
        ffp = 0xD8 if ch >= 4 else 0x0C
        self.dmac.outb(ffp, 0)
        self.dmac.outb(apo, v["addr"][0])
        self.dmac.outb(apo, v["addr"][1])
        self.dmac.outb(cpo, v["count"][0])
        self.dmac.outb(cpo, v["count"][1])
        if v["page"] is not None:
            self.dmac.outb(PAGE_PORTS[ch], v["page"])
        if v["mode"] is not None:
            self.dmac.outb(0xD6 if ch >= 4 else 0x0B, v["mode"])
        self.programmed.append((ch, dict(v)))
        v["dirty"] = False

    def outb(self, port, v):
        if not self.at_unmask:
            self.dmac.outb(port, v)
            return
        regs = channel_regs(port)
        if regs is not None:
            ch, what = regs
            ctl = 1 if ch >= 4 else 0
            self.v[ch][what][self._flip(ctl)] = v & 0xFF
            self.v[ch]["dirty"] = True
            return
        if port in PORT_PAGE:
            ch = PORT_PAGE[port]
            self.v[ch]["page"] = v & 0xFF
            self.v[ch]["dirty"] = True
            return
        if port in MODE_PORTS:
            ch = MODE_PORTS[port] + (v & 3)
            self.v[ch]["mode"] = v & 0xFF
            self.v[ch]["dirty"] = True
            return
        if port in FF_PORTS:
            self.ff[1 if port == 0xD8 else 0] = 0
            self.dmac.outb(port, v)
            return
        if port in MASK_PORTS and not v & 4:
            self._program(MASK_PORTS[port] + (v & 3))
        elif port in ALL_MASK_PORTS:
            for i in range(4):
                if not v >> i & 1:
                    self._program(ALL_MASK_PORTS[port] + i)
        self.dmac.outb(port, v)

    def inb(self, port):
        return self.dmac.inb(port)
