# (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
#
# Licensed under GPL Version 3.0
"""Tests of src/piix4.c, the Type F rule, built for the host and run on the
PCI configuration model (pcicfg): it finds only an Intel PIIX4, writes only
the MBDMA register that names the channel asked for, one byte, and puts
CF8h back each time. Then build/essreg.exe's tf in the emulator, in front
of the chip and the laptop's bus."""

import ctypes
import os
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from pcicfg import PciBus, laptop, no_pci, piix4, via  # noqa: E402
from test_vxdext import HAVE_UNICORN  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "src")
EXE = os.path.join(ROOT, "build", "essreg.exe")
LINE = "PIIX4 0:07.0: 76h=8Dh (Type F, DMA 5), 77h=%02Xh (%sDMA 1)"
NONE = 0xFF

u32 = ctypes.c_ulong                # the host's u32 (esstypes.h)
ADDR_GET = ctypes.CFUNCTYPE(u32)
ADDR_PUT = ctypes.CFUNCTYPE(None, u32)
DATA_PUT8 = ctypes.CFUNCTYPE(None, ctypes.c_ubyte, ctypes.c_ubyte)
VOID = ctypes.CFUNCTYPE(None)


class IO(ctypes.Structure):
    _fields_ = [("addr_get", ADDR_GET), ("addr_put", ADDR_PUT),
                ("data_get", ADDR_GET), ("data_put8", DATA_PUT8),
                ("lock", VOID), ("unlock", VOID)]


_LIB = {}


def library():
    """src/piix4.c built as a shared library, once"""
    if "lib" not in _LIB:
        tmp = _LIB["tmp"] = tempfile.TemporaryDirectory()
        out = os.path.join(tmp.name, "libpiix4.so")
        subprocess.run(["gcc", "-std=gnu99", "-Wall", "-Wextra", "-Werror",
                        "-DESS_HOST", "-O1", "-fPIC", "-shared", "-I" + SRC,
                        "-o", out, os.path.join(SRC, "piix4.c")],
                       check=True, capture_output=True)
        lib = ctypes.CDLL(out)
        io = ctypes.POINTER(IO)
        mbdma = ctypes.c_ubyte * 2
        lib.piix4_pci.argtypes = [io]
        lib.piix4_find.argtypes = [io]
        lib.piix4_find.restype = ctypes.c_ubyte
        lib.piix4_mbdma.argtypes = [io, ctypes.c_ubyte, mbdma]
        lib.piix4_reg_of.argtypes = [mbdma, ctypes.c_ubyte]
        lib.piix4_typef.argtypes = [io, ctypes.c_ubyte, ctypes.c_ubyte,
                                    ctypes.c_int, mbdma]
        lib.piix4_text.argtypes = [ctypes.c_char_p, ctypes.c_ubyte, mbdma]
        _LIB["lib"] = lib
    return _LIB["lib"]


class Bus:
    """the model behind the access struct, with the lock's depth checked"""

    def __init__(self, bus):
        self.bus = bus
        self.depth = 0
        self.max_depth = 0

        def lock():
            self.depth += 1
            self.max_depth = max(self.max_depth, self.depth)

        def unlock():
            self.depth -= 1

        self.io = IO(ADDR_GET(lambda: bus.read(0xCF8, 4)),
                     ADDR_PUT(lambda a: bus.write(0xCF8, 4, a)),
                     ADDR_GET(lambda: bus.read(0xCFC, 4)),
                     DATA_PUT8(lambda off, v: bus.write(0xCFC + off, 1, v)),
                     VOID(lock), VOID(unlock))

    def ref(self):
        return ctypes.byref(self.io)


def mbdma():
    return (ctypes.c_ubyte * 2)()


class Piix4Test(unittest.TestCase):
    def setUp(self):
        self.lib = library()

    def test_finds_only_an_intel_piix4(self):
        for bus, dev in ((laptop(), 7), (no_pci(), NONE), (via(), NONE),
                         (PciBus({3: piix4()}), 3)):
            b = Bus(bus)
            self.assertEqual(self.lib.piix4_find(b.ref()), dev)
            self.assertEqual(bus.writes(), [])

    def test_reads_mbdma(self):
        b = Bus(laptop())
        r = mbdma()
        self.lib.piix4_mbdma(b.ref(), 7, r)
        self.assertEqual(list(r), [0x8D, 0x89])
        self.assertEqual(self.lib.piix4_reg_of(r, 1), 1)
        self.assertEqual(self.lib.piix4_reg_of(r, 5), 0)
        self.assertEqual(self.lib.piix4_reg_of(r, 0), -1)

    def test_off_for_the_record_channel_only(self):
        bus = laptop()
        bus.addr = 0x80003810               # someone's address in CF8h
        b = Bus(bus)
        r = mbdma()
        self.assertEqual(self.lib.piix4_typef(b.ref(), 7, 1, 0, r), 1)
        self.assertEqual(bus.writes(), [(7, 0x77, 0x09)])
        self.assertEqual(list(r), [0x8D, 0x09])
        self.assertEqual(bus.addr, 0x80003810)
        self.assertEqual((b.depth, b.max_depth), (0, 1))
        writes = [e for e in bus.log if e[0] == "w" and e[2] >= 0xCFC]
        self.assertEqual(writes, [("w", 0x80003874, 0xCFF, 1, 0x09)])

    def test_nothing_when_already_so(self):
        bus = laptop(r77=0x09)
        b = Bus(bus)
        r = mbdma()
        self.assertEqual(self.lib.piix4_typef(b.ref(), 7, 1, 0, r), 0)
        self.assertEqual(self.lib.piix4_typef(b.ref(), 7, 5, 1, r), 0)
        self.assertEqual(bus.writes(), [])

    def test_never_another_channel(self):
        for ch in (0, 2, 3, 6, 7):
            bus = laptop()
            b = Bus(bus)
            r = mbdma()
            self.assertEqual(self.lib.piix4_typef(b.ref(), 7, ch, 0, r), -1)
            self.assertEqual(bus.writes(), [])

    def test_back_on(self):
        bus = laptop(r77=0x09)
        b = Bus(bus)
        r = mbdma()
        self.assertEqual(self.lib.piix4_typef(b.ref(), 7, 1, 1, r), 1)
        self.assertEqual(list(r), [0x8D, 0x89])

    def test_text(self):
        for regs, text in (
                ((0x8D, 0x89), "PIIX4 0:07.0: 76h=8Dh (Type F, DMA 5), "
                               "77h=89h (Type F, DMA 1)"),
                ((0x8D, 0x09), "PIIX4 0:07.0: 76h=8Dh (Type F, DMA 5), "
                               "77h=09h (DMA 1)"),
                ((0x04, 0x84), "PIIX4 0:07.0: 76h=04h (no channel), "
                               "77h=84h (Type F, no channel)")):
            buf = ctypes.create_string_buffer(80)
            r = mbdma()
            r[0], r[1] = regs
            self.lib.piix4_text(buf, 7, r)
            self.assertEqual(buf.value.decode(), text)


@unittest.skipUnless(HAVE_UNICORN, "needs unicorn")
class EssregTest(unittest.TestCase):
    """essreg tf: the bridge's line and Audio 1's channel (PnP 74h, DMA 1
    on the chip model), and tf=0 or tf=1 at the DOS prompt only"""

    def essreg(self, tail, bus, vxd=None):
        from dosrun import Dos
        from sbfronts import ChipCard
        card = ChipCard(pci=bus)
        with open(EXE, "rb") as f:
            d = Dos(f.read(), card, vxd=vxd)
        return d, d.run(tail)

    def test_reads(self):
        bus = laptop()
        d, code = self.essreg("tf", bus)
        self.assertEqual(code, 0, d.output)
        self.assertIn(LINE % (0x89, "Type F, "), d.output)
        self.assertIn("Audio 1's DMA 1: Type F on", d.output)
        self.assertEqual(bus.writes(), [])
        self.assertEqual(bus.addr, 0)

    def test_off_then_on(self):
        """one byte at CFFh each way, 76h (DMA 5, Audio 2's) as it was"""
        bus = laptop()
        d, code = self.essreg("tf=0", bus)
        self.assertEqual(code, 0, d.output)
        self.assertIn(LINE % (0x09, ""), d.output)
        self.assertIn("Audio 1's DMA 1: Type F off", d.output)
        self.assertEqual(bus.writes(), [(7, 0x77, 0x09)])
        d, code = self.essreg("tf=1", bus)
        self.assertEqual(code, 0, d.output)
        self.assertIn("Audio 1's DMA 1: Type F on", d.output)
        self.assertEqual(bus.writes(), [(7, 0x77, 0x09), (7, 0x77, 0x89)])
        self.assertEqual((bus.cfg(7, 0x76), bus.addr), (0x8D, 0))

    def test_dos_box(self):
        """in a DOS box tf reads, even with the chip in a Windows
        program's hands, and tf=0 changes nothing"""
        vxd = {"adi": bytes(0xE9), "info": None}
        bus = laptop()
        d, code = self.essreg("tf", bus, vxd=vxd)
        self.assertEqual(code, 0, d.output)
        self.assertIn(LINE % (0x89, "Type F, "), d.output)
        d, code = self.essreg("tf=0", bus, vxd=vxd)
        self.assertEqual(code, 1, d.output)
        self.assertIn("tf=0: at the DOS prompt only, outside Windows",
                      d.output)
        self.assertEqual(bus.writes(), [])

    def test_no_piix4(self):
        """another bridge with the same bytes, or no PCI: nothing written"""
        for bus in (via(), no_pci()):
            d, code = self.essreg("tf=0", bus)
            self.assertEqual(code, 1, d.output)
            self.assertIn("Type F: no Intel PIIX4 on PCI bus 0", d.output)
            self.assertEqual(bus.writes(), [])

    def test_nothing_to_change(self):
        """Type F already off for DMA 1, or no MBDMA register that names
        DMA 1 (77h with no channel): tf=0 writes nothing"""
        bus = laptop(r76=0x84, r77=0x09)
        d, code = self.essreg("tf=0", bus)
        self.assertEqual(code, 0, d.output)
        self.assertEqual(bus.writes(), [])
        bus = laptop(r76=0x8D, r77=0x84)
        d, code = self.essreg("tf=0", bus)
        self.assertEqual(code, 1, d.output)
        self.assertIn("no MBDMA register names DMA 1", d.output)
        self.assertEqual(bus.writes(), [])


if __name__ == "__main__":
    unittest.main()
