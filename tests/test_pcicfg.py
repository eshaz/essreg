# (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
#
# Licensed under GPL Version 3.0
"""Tests of the PCI configuration model (pcicfg): mechanism 1 as the
programs reach it, and the PIIX4's MBDMA registers as the laptop's BIOS
leaves them."""

import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from pcicfg import laptop, no_pci, via  # noqa: E402


def addr(dev, reg):
    return 0x80000000 | dev << 11 | (reg & 0xFC)


class MechanismTest(unittest.TestCase):
    def test_address_latch(self):
        bus = laptop()
        bus.write(0xCF8, 4, 0x80000000)
        self.assertEqual(bus.read(0xCF8, 4), 0x80000000)
        bus.write(0xCF8, 1, 0x12)           # a byte isn't an address
        self.assertEqual(bus.read(0xCF8, 4), 0x80000000)

    def test_ids_by_width(self):
        bus = laptop()
        bus.write(0xCF8, 4, addr(7, 0))
        self.assertEqual(bus.read(0xCFC, 4), 0x71108086)
        self.assertEqual(bus.read(0xCFE, 2), 0x7110)
        self.assertEqual(bus.read(0xCFD, 1), 0x80)
        bus.write(0xCF8, 4, addr(7, 0x08))
        self.assertEqual(bus.read(0xCFC, 4) >> 16, 0x0601)

    def test_mbdma_as_the_bios_leaves_it(self):
        bus = laptop()
        bus.write(0xCF8, 4, addr(7, 0x74))
        self.assertEqual(bus.read(0xCFE, 1), 0x8D)
        self.assertEqual(bus.read(0xCFF, 1), 0x89)

    def test_only_mbdma_bits_take_a_write(self):
        bus = laptop()
        bus.write(0xCF8, 4, addr(7, 0x74))
        bus.write(0xCFF, 1, 0x09)
        self.assertEqual(bus.cfg(7, 0x77), 0x09)
        bus.write(0xCFF, 1, 0xF0)           # bits 6:3 stay as they were
        self.assertEqual(bus.cfg(7, 0x77), 0x88)
        bus.write(0xCF8, 4, addr(7, 0))
        bus.write(0xCFC, 4, 0)
        self.assertEqual(bus.cfg(7, 0), 0x86)
        self.assertEqual(bus.writes()[0], (7, 0x77, 0x09))

    def test_empty_slots_and_other_buses_float(self):
        bus = laptop()
        bus.write(0xCF8, 4, addr(3, 0))
        self.assertEqual(bus.read(0xCFC, 4), 0xFFFFFFFF)
        bus.write(0xCF8, 4, 0x80010000 | 7 << 11)      # bus 1
        self.assertEqual(bus.read(0xCFC, 2), 0xFFFF)

    def test_no_mechanism(self):
        bus = no_pci()
        bus.write(0xCF8, 4, 0x80000000)
        self.assertEqual(bus.read(0xCF8, 4), 0xFFFFFFFF)
        self.assertEqual(bus.read(0xCFC, 4), 0xFFFFFFFF)

    def test_another_bridge(self):
        bus = via()
        bus.write(0xCF8, 4, addr(7, 0))
        self.assertEqual(bus.read(0xCFC, 4), 0x05861106)
        bus.write(0xCF8, 4, addr(7, 0x74))
        self.assertEqual(bus.read(0xCFF, 1), 0x89)


if __name__ == "__main__":
    unittest.main()
