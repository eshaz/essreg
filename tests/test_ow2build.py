# (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
#
# Licensed under GPL Version 3.0
"""Build every program with Open Watcom v2 (tools/ow2build.sh).

Then check the 16-bit Windows programs essctl.exe and ess3d.exe: expected
Windows version 4.0 (3-D look on Windows 95), a single data segment, one
for each instance (so "essctl /load" can run while the window is open, and
a second ess3d while the first one's display is up), discardable code, the
imports and the resources.

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
        cls.ne = cls.load("essctl.exe")
        cls.ne3d = cls.load("ess3d.exe")

    @classmethod
    def load(cls, name):
        with open(os.path.join(cls.out, name), "rb") as f:
            return NEFile(f.read())

    def check_windows_program(self, ne, module, imports, exports):
        self.assertEqual(ne.module_name, module)
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
        for mod in imports:
            self.assertIn(mod, ne.modules)
        exported = {name for name, _ in ne.resident[1:]}
        self.assertEqual(exported, exports)

    @staticmethod
    def resource_kinds(ne):
        kinds = {}
        for r in ne.resources:
            kinds.setdefault(r.type, set()).add(r.id)
        return kinds

    def test_all_programs(self):
        for name in ("essreg.exe", "esfmpat.exe", "1869opl3.com",
                     "essctl.exe", "ess3d.exe", "nestamp.exe"):
            self.assertTrue(os.path.exists(os.path.join(self.out, name)), name)

    def test_windows_program(self):
        self.check_windows_program(
            self.ne, "ESSCTL",
            ("KERNEL", "USER", "GDI", "COMMDLG", "TOOLHELP", "MMSYSTEM"),
            {"MAIN_DLG_PROC", "BIT_DLG_PROC"})

    def test_resources(self):
        kinds = self.resource_kinds(self.ne)
        self.assertIn("ESSCTL", kinds[14])             # icon group
        self.assertIn("ESSCTL", kinds[4])              # menu
        self.assertEqual(kinds[5], {"ESSCTL", "BITEDIT"})  # dialogs
        self.assertIn("ESSCTL", kinds[9])              # accelerators
        self.assertIn(1, kinds[16])                    # version

    def test_ess3d(self):
        # the display's window procedure is the only export
        self.check_windows_program(self.ne3d, "ESS3D",
                                   ("KERNEL", "USER", "GDI"), {"OSD_PROC"})
        kinds = self.resource_kinds(self.ne3d)
        self.assertEqual(set(kinds), {3, 14, 16})      # icons, version
        self.assertIn("ESS3D", kinds[14])
        self.assertIn(1, kinds[16])


if __name__ == "__main__":
    unittest.main()
