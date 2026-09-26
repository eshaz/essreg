# SPDX-License-Identifier: GPL-3.0-or-later
"""Generated documentation is in sync with its sources."""

import os
import re
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


    def test_vxd_api_md_lists_every_function(self):
        """the function table of docs/VXD_API.md has exactly the functions
        of the dispatch tables in the VxD source"""
        def table(path, label, end_re):
            lines = open(os.path.join(ROOT, "src", "vxd", path)).read()
            body = lines.split(label + ":", 1)[1]
            body = re.split(end_re, body, maxsplit=1)[0]
            return re.findall(r"^\s*dd\s+(\w+)", body, re.M)

        groups = [table("pdat.asm", "API_Group%d_Funcs" % g,
                        r"\n(?:;|\w+:)") for g in range(4)]
        groups.append(table("essext.asm", "ESSREG_Group4_Funcs",
                            r"\n\w+ equ|\n\w+:"))
        codes = {"%02X%02X" % (g, i)
                 for g, funcs in enumerate(groups) for i in range(len(funcs))}
        self.assertEqual([len(g) for g in groups], [12, 4, 2, 4, 13])

        doc = open(os.path.join(ROOT, "docs", "VXD_API.md")).read()
        section = doc.split("## Functions", 1)[1].split("\n### ", 1)[0]
        listed = set(re.findall(r"^\| ([0-9A-F]{4}) \|", section, re.M))
        self.assertEqual(listed, codes)


if __name__ == "__main__":
    unittest.main()
