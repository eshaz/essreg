# (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
#
# Licensed under GPL Version 3.0
"""The fronts as sbgame cards: esssb16.com in the CPU emulator, and later
ES1869.VXD's front, each in front of the ES1869 model (esschip).

TSRCard installs the TSR under QPIEMU, EMM386 or HDPMI32i, and sends the
game's accesses of the ports the memory manager traps through the front;
the others reach the chip and the DMA controllers directly, as on the
machine. Time passes on the chip, which stops at a rising edge of the
game's interrupt so that the game's handler runs then.
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))

import build_esssb16  # noqa: E402
from esschip import ES1869Audio  # noqa: E402
from sbgame import Card  # noqa: E402
from tsremu import Chip, Machine, PMMachine  # noqa: E402

_BUILD = {}


def tsr_build():
    if "image" not in _BUILD:
        _BUILD["image"], _BUILD["syms"] = build_esssb16.build()
    return _BUILD["image"], _BUILD["syms"]


class TSRChip(ES1869Audio, Chip):
    """the chip model with the TSR emulator's DMA controllers"""

    def __init__(self, irq=5, dma8=1, dma2=None):
        Chip.__init__(self, irq=irq, dma8=dma8)
        self.audio_init(irq=irq, dma1=dma8, dma2=dma2)

    def outb(self, port, v):
        if self.dmac.handles(port):     # an access takes its microsecond
            self.catch_up()
            self.clock.advance(1)
        super().outb(port, v)


class TSRCard(Card):
    """the game on esssb16.com: path "qpi" (QPIEMU), "emm" (EMM386 at ring
    0) or "pm" (a protected-mode game under HDPMI32i); `old_callback`: a
    QPI callback was there before the program's"""

    def __init__(self, path="qpi", env="BLASTER=A220 I5 D1 H5 P330 T6",
                 tail="", irq=5, dma8=1, dma2=None, old_callback=True):
        super().__init__()
        self.chip = TSRChip(irq, dma8, dma2)
        image, syms = tsr_build()
        self.m = Machine(image, syms, self.chip, env)
        self.path = path
        self.pm = None
        if path == "emm":
            self.m.install(tail=tail, emm386=(4, 95), noems=True)
            self.m.path = "emm"
        elif path == "pm":
            self.m.install(tail=tail, qpiemu=True, dpmi=True,
                           old_callback=old_callback)
            self.pm = PMMachine(self.m)
            self.pm.install(self.m)
            self.m.exit = ("tsr", self.m.kept)
        else:
            self.m.install(tail=tail, qpiemu=True, old_callback=old_callback)
        if not self.m.exit or self.m.exit[0] != "tsr":
            raise AssertionError("not resident: %r\n%s"
                                 % (self.m.exit, self.m.output))
        self.pm_ports = set()
        if self.pm is not None:         # the ranges of HDPMI32i's function 6
            for fn, r in self.pm.vendor_calls:
                if fn == 6:
                    first = r["esi"] & 0xFFFF
                    self.pm_ports.update(range(first, first + r["edi"]))

    def trapped(self, port):
        if self.path == "pm":
            return port in self.pm_ports
        if self.path == "qpi":
            return port in self.m.qpi_trapped
        return any(not t.get("failed") and t["first"] <= port <= t["last"]
                   for t in self.m.emm_traps)

    def port_in(self, port):
        if not self.trapped(port):
            return self.chip.inb(port)
        if self.pm is not None:
            return self.pm.hdpmi_io(port)
        return self.m.inp(port)

    def port_out(self, port, v):
        if not self.trapped(port):
            self.chip.outb(port, v)
        elif self.pm is not None:
            self.pm.hdpmi_io(port, v)
        else:
            self.m.out(port, v)

    def tick(self, us):
        return self.chip.advance(us), len(self.chip.irq_edges)

    @property
    def played(self):
        return self.chip.played

    def poke(self, addr, data):
        self.m.put(addr, data)

    def peek(self, addr, n):
        return self.m.mem(addr, n)
