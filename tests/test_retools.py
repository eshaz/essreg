# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for the reverse-engineering tools and the reassembled VxD.

Run: python3 -m unittest discover -s tests -p "test_*.py"
"""

import os
import struct
import subprocess
import sys
import tempfile
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))

from retools.le import read_le  # noqa: E402

VXD = os.path.join(ROOT, "driver", "ES1869.VXD")


def have_nasm():
    try:
        subprocess.run(["nasm", "-v"], capture_output=True, check=True)
        return True
    except (OSError, subprocess.CalledProcessError):
        return False


class LEFileTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.le = read_le(VXD)

    def test_round_trip_is_identical(self):
        with open(VXD, "rb") as f:
            self.assertEqual(self.le.to_bytes(), f.read())

    def test_objects(self):
        names = [o.name_str for o in self.le.objects]
        self.assertEqual(names, ["LCOD", "MCOD", "RARE", "PNP", "PCOD",
                                 "PDAT", "ICOD"])

    def test_fixups(self):
        types = [f.type for f in self.le.fixups]
        self.assertEqual((types.count(7), types.count(8)), (334, 241))

    def test_ddb(self):
        obj, off = self.le.ddb()
        self.assertEqual((obj, off), (1, 0x258))
        img = self.le.objects[0].data
        self.assertEqual(bytes(img[off + 0x0C:off + 0x14]), b"AUDDRV  ")
        sdk, dev, major, minor = struct.unpack_from("<HHBB", img, off + 4)
        self.assertEqual((sdk, dev, major, minor), (0x400, 0x3B07, 4, 4))
        fix = self.le.fixup_map()
        control = fix[(1, off + 0x18)]
        v86 = fix[(1, off + 0x1C)]
        pm = fix[(1, off + 0x20)]
        self.assertEqual((control.tobj, control.toff), (1, 0x3E0))
        self.assertEqual((v86.tobj, v86.toff), (5, 0x135C))
        self.assertEqual((pm.tobj, pm.toff), (5, 0x135C))
        for field in (0x30, 0x34, 0x38):  # no service or Win32 service table
            self.assertEqual(struct.unpack_from("<I", img, off + field)[0], 0)
            self.assertNotIn((1, off + field), fix)

    def test_control_messages(self):
        # Control_Proc: cmp eax, imm8 / jz near ... ; no W32_DEVICEIOCONTROL
        img = self.le.objects[0].data
        msgs = []
        pos = 0x3E0
        while img[pos] == 0x83 and img[pos + 1] == 0xF8:
            msgs.append(img[pos + 2])
            pos += 9
        self.assertEqual(msgs, [0x08, 0x0B, 0x03, 0x1B, 0x1C, 0x22])
        self.assertNotIn(0x23, msgs)

    def test_api_dispatch_table(self):
        fix = self.le.fixup_map()
        img = self.le.objects[5].data
        groups = []
        for g in range(4):
            count = struct.unpack_from("<I", img, 0x21C + 8 * g)[0]
            table = fix[(6, 0x21C + 8 * g + 4)]
            funcs = [fix[(table.tobj, table.toff + 4 * i)].toff
                     for i in range(count)]
            groups.append(funcs)
        self.assertEqual([len(g) for g in groups], [12, 4, 2, 4])
        self.assertEqual(groups[0][:6], [0x14F2, 0x15DA, 0x14FD, 0x155D,
                                         0x148B, 0x138C])
        self.assertEqual(groups[1], [0x14F2, 0x16D1, 0x1654, 0x1691])
        self.assertEqual(groups[3], [0x14F2, 0x1893, 0x1816, 0x1853])


@unittest.skipUnless(have_nasm(), "nasm not installed")
class RebuildTest(unittest.TestCase):
    def test_stock_rebuild_is_identical(self):
        import build_vxd
        with tempfile.TemporaryDirectory() as tmp:
            out = os.path.join(tmp, "ES1869.VXD")
            data, _o, _f = build_vxd.build(False, out, tmp)
        with open(VXD, "rb") as f:
            self.assertEqual(data, f.read())


if __name__ == "__main__":
    unittest.main()
