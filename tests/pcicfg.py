# (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
#
# Licensed under GPL Version 3.0
"""PCI configuration mechanism 1 for the tests: CF8h takes a dword address
(bit 31 on, bus in 23:16, device in 15:11, function in 10:8, register in
7:2), and CFCh-CFFh give that dword's bytes, read or written a byte, a word
or a dword at a time. Only bus 0's function 0s are here.

The Armada's bus (essprobe's env line on the laptop): the 440BX host
bridge at 0, the Intel PIIX4 ISA bridge at 7 and a TI CardBus bridge at
17. The PIIX4's motherboard DMA registers 76h and 77h (MBDMA) hold the
Type F timing in bit 7 and a DMA channel in bits 2:0; its BIOS leaves 8Dh
(channel 5) and 89h (channel 1). Of them only bits 7 and 2:0 take a write
here, and nothing else in any device does. `log` holds every access as
("r" or "w", the CF8h address, the port, the size, the value)."""

PCI_ADDR, PCI_DATA = 0xCF8, 0xCFC

PIIX4_ID = (0x8086, 0x7110)


class Device:
    """function 0 of a device: 256 bytes of configuration space, and the
    bits of each register that a write changes"""

    def __init__(self, vendor, device, cls, regs=None, writable=None):
        self.cfg = bytearray(256)
        self.cfg[0:2] = vendor.to_bytes(2, "little")
        self.cfg[2:4] = device.to_bytes(2, "little")
        self.cfg[0x0A:0x0C] = cls.to_bytes(2, "little")
        self.cfg[0x0E] = 0x80 if cls == 0x0601 else 0x00   # multifunction
        for r, v in (regs or {}).items():
            self.cfg[r] = v
        self.writable = dict(writable or {})       # register -> bit mask


def piix4(r76=0x8D, r77=0x89):
    """the PIIX4's function 0 with its MBDMA registers"""
    return Device(*PIIX4_ID, 0x0601, {0x76: r76, 0x77: r77},
                  {0x76: 0x87, 0x77: 0x87})


class PciBus:
    """mechanism 1, or nothing that answers it (`mech1` False: CF8h keeps
    no address and everything reads FFh)"""

    def __init__(self, devices=None, mech1=True):
        self.devices = dict(devices or {})         # device number -> Device
        self.mech1 = mech1
        self.addr = 0
        self.log = []

    @staticmethod
    def handles(port):
        return PCI_ADDR <= port <= PCI_DATA + 3

    def _device(self):
        a = self.addr
        if not a & 0x80000000 or (a >> 16) & 0xFF or (a >> 8) & 7:
            return None
        return self.devices.get((a >> 11) & 0x1F)

    def read(self, port, size):
        mask = (1 << (8 * size)) - 1
        if port < PCI_DATA:
            v = (self.addr if self.mech1 else 0xFFFFFFFF) >> \
                (8 * (port - PCI_ADDR))
        else:
            d = self._device()
            off = (self.addr & 0xFC) + port - PCI_DATA
            v = int.from_bytes(d.cfg[off:off + size], "little") \
                if d is not None and off + size <= 256 else mask
        v &= mask
        self.log.append(("r", self.addr, port, size, v))
        return v

    def write(self, port, size, value):
        value &= (1 << (8 * size)) - 1
        self.log.append(("w", self.addr, port, size, value))
        if port < PCI_DATA:
            if self.mech1 and port == PCI_ADDR and size == 4:
                self.addr = value & 0x80FFFFFC
            return
        d = self._device()
        if d is None:
            return
        off = (self.addr & 0xFC) + port - PCI_DATA
        for i in range(size):
            m = d.writable.get(off + i, 0)
            b = (value >> (8 * i)) & 0xFF
            d.cfg[off + i] = (d.cfg[off + i] & ~m) | (b & m)

    def cfg(self, dev, reg):
        """a register as the device holds it, without a logged access"""
        return self.devices[dev].cfg[reg]

    def writes(self):
        """the data writes, as (device, register, value) of each byte"""
        out = []
        for kind, a, port, size, v in self.log:
            if kind == "w" and port >= PCI_DATA:
                for i in range(size):
                    out.append(((a >> 11) & 0x1F,
                                (a & 0xFC) + port - PCI_DATA + i,
                                (v >> (8 * i)) & 0xFF))
        return out


def laptop(r76=0x8D, r77=0x89):
    """the Armada 1500c's bus 0, with the BIOS's Type F on both of the
    ES1869's channels unless told otherwise"""
    return PciBus({0: Device(0x8086, 0x7192, 0x0600),
                   7: piix4(r76, r77),
                   17: Device(0x104C, 0xAC1C, 0x0607)})


def no_pci():
    """a machine where mechanism 1 doesn't answer"""
    return PciBus(mech1=False)


def via():
    """another ISA bridge at 7 (VIA VT82C586) with the same bytes at 76h and
    77h, which only an Intel PIIX4 means as MBDMA"""
    return PciBus({7: Device(0x1106, 0x0586, 0x0601, {0x76: 0x8D, 0x77: 0x89},
                             {0x76: 0xFF, 0x77: 0xFF})})
