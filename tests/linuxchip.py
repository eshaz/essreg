# (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
#
# Licensed under GPL Version 3.0 or later
"""The ES1869 as the Linux driver finds it on the Armada, for the driver's
tests: esschip's audio engines with their own DMA controllers, and what the
driver uses beyond the fronts.

LinuxChip adds:
* Config_Base+6's status of the four sources the driver takes (Audio 1,
  Audio 2, the hardware volume and the MPU-401), read before the mask, and
  the mask in Config_Base+7 gating the line, so that a restored mask with a
  source still pending makes a new edge (DS p.18);
* the hardware volume's request (mixer 64h bit 4, its mask bit 1), which a
  write to 66h clears, and press(), a button;
* the MPU-401's request (64h bit 6) for bytes waiting at 330h;
* the card registers the driver reads or fixes up: 25h (the DRQ latch, with
  the MPU-401 in logical device 1), 26h to 28h, and LDN 1's base and
  activation;
* card register 26h bits 3:2 gating Audio 2's and Audio 1's DMA requests:
  the data sheet calls them masks (DS p.15, p.31), but on the Armada with
  them clear neither engine's request reached the 8237, and set, both did.
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from dma8237 import DMA8237  # noqa: E402
from esschip import ES1869Audio  # noqa: E402
from vxdemu import FakeES1869  # noqa: E402

BASE = 0x220
CFG = 0x250
MPU = 0x330


class GatedDMA(DMA8237):
    """the DMA controllers, which see an engine's requests only while card
    register 26h lets them out"""

    def __init__(self, chip):
        super().__init__()
        self.chip = chip

    def _gated(self, n):
        chip = self.chip
        r26 = chip.pnp.get((None, 0x26), 0x1C)
        if n == chip.a1_channel() and not r26 & 0x04:
            return True
        return n == chip.a2_channel() and not r26 & 0x08

    def read(self, n, units):
        return b"" if self._gated(n) else super().read(n, units)

    def write(self, n, data):
        return 0 if self._gated(n) else super().write(n, data)


class LinuxChip(ES1869Audio, FakeES1869):
    """the chip, its DMA controllers on the ports"""

    def __init__(self, dma1=1, dma2=5, cfg=CFG):
        FakeES1869.__init__(self, BASE, cfg)
        self.dmac = GatedDMA(self)
        self.audio_init(irq=5, dma1=dma1, dma2=dma2)
        # a hardware reset enables every source (DS p.18)
        self.cfg_ports[7] = 0x7F
        self.pnp[(None, 0x25)] = 0x80
        self.pnp[(None, 0x26)] = 0x1C
        for reg in (0x27, 0x28):
            self.pnp[(None, reg)] = 0x00
        self.pnp[(1, 0x30)] = 0x01
        self.pnp[(1, 0x60)], self.pnp[(1, 0x61)] = BASE >> 8, BASE & 0xFF
        self.hwv_req = False
        self.mpu_in = []            # bytes the MPU-401 has for the host
        self.mpu_out = []
        self.mask_writes = []       # (us, value) of Config_Base+7's writes

    # --- the sources and the line --------------------------------------------
    def status(self):
        """Config_Base+6: the requests, before the mask"""
        st = 0
        if self.a1_request():
            st |= 0x01
        if self.a2_request():
            st |= 0x02
        if self.hwv_req and self.mixer[0x64] & 0x02:
            st |= 0x04
        if self.mpu_in and self.mixer[0x64] & 0x40:
            st |= 0x08
        return st

    def _line(self):
        line = bool(self.status() & self.cfg_ports[7])
        if line and not self.irq_line:
            self.irq_edges.append(self.clock.us)
        self.irq_line = line

    def press(self):
        """a hardware volume button: the request (64h bit 4)"""
        self.hwv_req = True
        self.mixer[0x64] |= 0x10
        self._line()

    def mpu_receive(self, data):
        self.mpu_in += list(data)
        self._line()

    # --- ports ---------------------------------------------------------------
    def _inb(self, port):
        if self.dmac.handles(port):
            return self.dmac.inb(port)
        if port == self.cfg + 6:
            return self.status()
        if port == MPU:
            v = self.mpu_in.pop(0) if self.mpu_in else 0xFF
            self._line()
            return v
        if port == MPU + 1:
            return 0x00 if self.mpu_in else 0x80
        return super()._inb(port)

    def outb(self, port, v):
        if self.dmac.handles(port):
            self.catch_up()
            self.clock.advance(1)
            self.dmac.outb(port, v)
            return
        if port in (MPU, MPU + 1):
            self.catch_up()
            self.clock.advance(1)
            if port == MPU + 1 and v in (0xFF, 0x3F):
                self.mpu_in.append(0xFE)
            elif port == MPU:
                self.mpu_out.append(v)
            self._line()
            return
        super().outb(port, v)
        if port == self.cfg + 7:
            self.mask_writes.append((self.clock.us, v & 0xFF))
            self._line()
        elif port == self.base + 5 and self.index == 0x66:
            self.hwv_req = False
            self.mixer[0x64] &= ~0x10
            self._line()


def run(chip, us):
    """us of time, past the interrupts' edges"""
    while us > 0:
        us -= chip.advance(us)
