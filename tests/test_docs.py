# SPDX-License-Identifier: GPL-3.0-or-later
"""Generated documentation is in sync with its sources."""

import os
import subprocess
import sys
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


class DocsTest(unittest.TestCase):
    def test_registers_md_current(self):
        res = subprocess.run([sys.executable,
                              os.path.join(ROOT, "tools", "regdoc.py"),
                              "--check"], capture_output=True, text=True)
        self.assertEqual(res.returncode, 0, res.stdout + res.stderr)


if __name__ == "__main__":
    unittest.main()
