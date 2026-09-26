# SPDX-License-Identifier: GPL-3.0-or-later
"""ESFM.DRV rebuilt from src/esfm: the stock build must be byte-identical
to driver/ESFM.DRV, and the fixed build a valid driver with the same bank
loader (so that esfmpat and essctl keep working with it)."""

import os
import sys
import tempfile
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))

import build_esfm  # noqa: E402
from retools.ne import NEFile  # noqa: E402


class RebuildTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        cls.stock = build_esfm.build(False, workdir=cls.tmp.name)
        cls.fixed = build_esfm.build(True, workdir=cls.tmp.name)
        with open(build_esfm.ORIGINAL, "rb") as f:
            cls.orig = f.read()

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def test_stock_identical(self):
        self.assertEqual(self.stock, self.orig)

    def test_fixed_driver(self):
        ne, orig = NEFile(self.fixed), NEFile(self.orig)
        self.assertEqual(ne.module_name, "ESFM")
        self.assertEqual(ne.entries.keys(), orig.entries.keys())
        self.assertEqual(ne.modules, orig.modules)
        self.assertEqual(ne.resource_data(256, 1234),
                         orig.resource_data(256, 1234))
        # segment 3 (open, close and the bank loader) keeps its layout
        self.assertEqual(ne.segments[2].length, orig.segments[2].length)
        # DGROUP keeps its variables where they were
        self.assertEqual(ne.segment_data(4)[:0x1B2][0x3E:0x5B],
                         orig.segment_data(4)[0x3E:0x5B])


if __name__ == "__main__":
    unittest.main()
