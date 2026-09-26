# SPDX-License-Identifier: GPL-3.0-or-later
"""Build every program with Open Watcom v2 (tools/ow2build.sh) and check
the 16-bit Windows executable: expected Windows version 4.0 (3-D look on
Windows 95), a single data segment (so that "essctl /load" can run while
the window is open), discardable code, the imports and the resources.

Needs Open Watcom v2 in $OW2.
"""

import os
import subprocess
import sys
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))

from retools.ne import NEFile, SEG_DATA, SEG_DISCARDABLE  # noqa: E402

OW2 = os.environ.get("OW2", "")


@unittest.skipUnless(os.path.exists(os.path.join(OW2, "binl64", "wcc")),
                     "needs Open Watcom v2 in $OW2")
class OW2BuildTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        res = subprocess.run([os.path.join(ROOT, "tools", "ow2build.sh"), OW2],
                             capture_output=True, text=True)
        if res.returncode:
            raise AssertionError("ow2build.sh failed:\n" + res.stdout[-3000:] +
                                 res.stderr[-3000:])
        cls.out = os.path.join(ROOT, "out", "ow2")
        with open(os.path.join(cls.out, "essctl.exe"), "rb") as f:
            cls.ne = NEFile(f.read())

    def test_all_programs(self):
        for name in ("essreg.exe", "esfmpat.exe", "1869opl3.com",
                     "essctl.exe", "nestamp.exe"):
            self.assertTrue(os.path.exists(os.path.join(self.out, name)), name)

    def test_windows_program(self):
        ne = self.ne
        self.assertEqual(ne.module_name, "ESSCTL")
        self.assertEqual(ne.target_os, 2)
        self.assertEqual(ne.expected_version, (4, 0))
        self.assertEqual(ne.flags & 3, 2)  # MULTIPLEDATA: one per instance
        data = [s for s in ne.segments if s.is_data]
        self.assertEqual(len(data), 1)
        self.assertEqual(data[0].index, ne.autodata)
        self.assertFalse(data[0].flags & SEG_DISCARDABLE)
        for s in ne.segments:
            if not s.flags & SEG_DATA:
                self.assertTrue(s.flags & SEG_DISCARDABLE, s)
        for mod in ("KERNEL", "USER", "GDI", "COMMDLG", "TOOLHELP"):
            self.assertIn(mod, ne.modules)
        exported = {name for name, _ in ne.resident[1:]}
        self.assertEqual(exported, {"MAIN_DLG_PROC", "BIT_DLG_PROC"})

    def test_resources(self):
        kinds = {}
        for r in self.ne.resources:
            kinds.setdefault(r.type, set()).add(r.id)
        self.assertIn("ESSCTL", kinds[14])             # icon group
        self.assertIn("ESSCTL", kinds[4])              # menu
        self.assertEqual(kinds[5], {"ESSCTL", "BITEDIT"})  # dialogs
        self.assertIn("ESSCTL", kinds[9])              # accelerators
        self.assertIn(1, kinds[16])                    # version


if __name__ == "__main__":
    unittest.main()
