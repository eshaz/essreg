# SPDX-License-Identifier: GPL-3.0-or-later
"""esfmpat on a copy of ESFM.DRV: in-place and relocated banks, backups,
and refusals.  The patched driver is checked with the NE reader."""

import os
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))

from retools.ne import NEFile  # noqa: E402

DRV = os.path.join(ROOT, "driver", "ESFM.DRV")
BANK = os.path.join(ROOT, "esfm_patch_banks", "bnk_com.bin")


def read(path):
    with open(path, "rb") as f:
        return f.read()


def bigger_bank():
    """bnk_com.bin with 40 one-voice patches added at entries 216-255"""
    bank = bytearray(read(BANK))
    size = len(bank)
    voice = bytearray(bank[0x200:0x200 + 36])
    voice[0] &= ~6
    for i in range(40):
        struct.pack_into("<H", bank, 2 * (216 + i), size + 36 * i)
        bank += voice
    return bytes(bank)


@unittest.skipUnless(shutil.which("gcc"), "needs gcc")
class EsfmpatTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmpdir = tempfile.TemporaryDirectory()
        cls.exe = os.path.join(cls.tmpdir.name, "esfmpat")
        subprocess.run(["gcc", "-std=gnu99", "-Wall", "-Werror", "-DESS_HOST",
                        "-I" + os.path.join(ROOT, "src"), "-o", cls.exe,
                        os.path.join(ROOT, "src", "esfmpat.c"),
                        os.path.join(ROOT, "src", "esfmbank.c")], check=True)

    @classmethod
    def tearDownClass(cls):
        cls.tmpdir.cleanup()

    def setUp(self):
        self.dir = tempfile.mkdtemp(dir=self.tmpdir.name)
        self.drv = os.path.join(self.dir, "ESFM.DRV")
        shutil.copy(DRV, self.drv)
        self.orig = read(DRV)

    def run_esfmpat(self, *args):
        return subprocess.run([self.exe, self.drv] + list(args),
                              capture_output=True, text=True)

    def write(self, name, data):
        path = os.path.join(self.dir, name)
        with open(path, "wb") as f:
            f.write(data)
        return path

    def test_show(self):
        res = self.run_esfmpat()
        self.assertEqual(res.returncode, 0, res.stdout)
        self.assertIn("8288 bytes at offset 0x2400", res.stdout)

    def test_same_size_in_place(self):
        bank = read(os.path.join(ROOT, "esfm_patch_banks",
                                 "bnk_com_better_square_wave.bin"))
        res = self.run_esfmpat(self.write("b.bin", bank))
        self.assertEqual(res.returncode, 0, res.stdout)
        new = read(self.drv)
        self.assertEqual(len(new), len(self.orig))
        self.assertEqual(new[0x2400:0x2400 + len(bank)], bank)
        self.assertEqual(new[:0x2400], self.orig[:0x2400])
        self.assertEqual(new[0x2400 + len(bank):],
                         self.orig[0x2400 + len(bank):])
        self.assertEqual(read(os.path.join(self.dir, "ESFM.BAK")),
                         self.orig)

    def test_larger_bank_is_moved(self):
        bank = bigger_bank()
        res = self.run_esfmpat(self.write("big.bin", bank))
        self.assertEqual(res.returncode, 0, res.stdout)
        self.assertIn("moved", res.stdout)
        old, new = NEFile(self.orig), NEFile(read(self.drv))
        res_new = new.resource(256, 1234)
        self.assertEqual(res_new.offset, (len(self.orig) + 15) & ~15)
        self.assertEqual(new.resource_data(256, 1234)[:len(bank)], bank)
        # every segment is unchanged except the four constants in seg 3
        for s_old, s_new in zip(old.segments, new.segments):
            a = old.segment_data(s_old.index)
            b = new.segment_data(s_new.index)
            changed = [i for i in range(len(a)) if a[i] != b[i]]
            if s_old.index == 3:
                self.assertTrue(set(changed) <= {0x670, 0x671, 0x6FC, 0x6FD,
                                                 0x77C, 0x77D, 0x783, 0x784})
                size = struct.unpack_from("<H", b, 0x670)[0]
                self.assertEqual(size, len(bank))
                self.assertEqual(struct.unpack_from("<H", b, 0x6FC)[0],
                                 len(bank) // 2)
            else:
                self.assertEqual(changed, [])
            self.assertEqual(s_old.relocs, s_new.relocs)
        self.assertEqual(old.entries, new.entries)
        for r in old.resources:
            if r.type != 256:
                self.assertEqual(old.resource_data(r.type, r.id),
                                 new.resource_data(r.type, r.id))
        # a second run keeps the first backup (the original driver)
        res = self.run_esfmpat(BANK)
        self.assertEqual(res.returncode, 0, res.stdout)
        self.assertIn("Keeping the existing backup", res.stdout)
        self.assertEqual(read(os.path.join(self.dir, "ESFM.BAK")),
                         self.orig)

    def test_riff_bank(self):
        bank = read(BANK)
        riff = (b"RIFF" + struct.pack("<I", 12 + len(bank)) + b"Ptch" +
                b"fm4 " + struct.pack("<I", len(bank)) + bank)
        res = self.run_esfmpat(self.write("b.fm4", riff))
        self.assertEqual(res.returncode, 0, res.stdout)
        self.assertEqual(read(self.drv), self.orig)

    def test_refusals_leave_the_driver_alone(self):
        bad = bytearray(read(BANK))
        struct.pack_into("<H", bad, 10, 0x10)  # entry inside the table
        res = self.run_esfmpat(self.write("bad.bin", bytes(bad)))
        self.assertEqual(res.returncode, 1)
        huge = self.write("huge.bin", bigger_bank() + bytes(0x6000))
        self.assertEqual(self.run_esfmpat(huge).returncode, 1)
        other = bytearray(self.orig)
        other[0x44A0 + 0x6FE] = 0x90
        with open(self.drv, "wb") as f:
            f.write(other)
        res = self.run_esfmpat(BANK)
        self.assertEqual(res.returncode, 1)
        self.assertIn("not the ESFM.DRV build", res.stdout)
        self.assertEqual(read(self.drv), bytes(other))
        self.assertFalse(os.path.exists(os.path.join(self.dir, "ESFM.BAK")))


if __name__ == "__main__":
    unittest.main()
