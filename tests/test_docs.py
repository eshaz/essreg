# (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
#
# Licensed under GPL Version 3.0
"""Check that the generated docs are in sync with their sources, and that
the links between the docs work."""

import os
import re
import subprocess
import sys
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def markdown_files():
    out = []
    for top, dirs, files in os.walk(ROOT):
        dirs[:] = [d for d in dirs if not d.startswith(".") and
                   d not in ("out", "__pycache__")]
        out += [os.path.join(top, f) for f in files if f.endswith(".md")]
    return sorted(out)


def anchors(path):
    """The #anchors GitHub makes for the headings of a markdown file."""
    out, code = set(), False
    for line in open(path, encoding="utf-8"):
        if line.startswith("```"):
            code = not code
        m = None if code else re.match(r"^#+\s+(.*)", line)
        if m:
            # a link's text only, lowercase, spaces to -, no punctuation
            # other than - and _
            h = re.sub(r"\[([^\]]*)\]\([^)]*\)", r"\1", m.group(1).strip())
            h = re.sub(r"[`*]", "", h.lower())
            out.add(re.sub(r"[^\w\- ]", "", h).replace(" ", "-"))
    return out


class DocsTest(unittest.TestCase):
    def test_registers_md_current(self):
        res = subprocess.run([sys.executable,
                              os.path.join(ROOT, "tools", "regdoc.py"),
                              "--check"], capture_output=True, text=True)
        self.assertEqual(res.returncode, 0, res.stdout + res.stderr)

    def test_vxd_api_md_lists_every_function(self):
        """docs/VXD_API.md has exactly the functions of the dispatch tables"""
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
        self.assertEqual([len(g) for g in groups], [12, 4, 2, 4, 14])

        doc = open(os.path.join(ROOT, "docs", "VXD_API.md")).read()
        section = doc.split("## Functions", 1)[1].split("\n### ", 1)[0]
        listed = set(re.findall(r"^\| ([0-9A-F]{4}) \|", section, re.M))
        self.assertEqual(listed, codes)

    def test_notepad(self):
        """every markdown file reads in Windows 98's Notepad: CRLF, plain
        ASCII, 76 columns, under 60,000 bytes (tools/mdfmt.py)"""
        sys.path.insert(0, os.path.join(ROOT, "tools"))
        import mdfmt
        problems = []
        for path in markdown_files():
            name = os.path.relpath(path, ROOT)
            with open(path, "rb") as f:
                raw = f.read()
            text = raw.decode("ascii", "replace")
            new = mdfmt.format_text(text, name, problems)
            mdfmt.check_rules(name, new, problems)
            if new.replace("\n", "\r\n").encode("ascii", "replace") != raw:
                problems.append(name + ": run python3 tools/mdfmt.py")
        self.assertEqual(problems, [])

    def test_links(self):
        """relative links and #anchors in the markdown files lead somewhere"""
        bad = []
        for path in markdown_files():
            text = open(path, encoding="utf-8").read()
            text = re.sub(r"```.*?```", "", text, flags=re.S)
            for target in re.findall(r"\]\(([^)\s]+)\)", text):
                if re.match(r"https?:|mailto:", target):
                    continue
                name, _, frag = target.partition("#")
                dest = os.path.normpath(os.path.join(os.path.dirname(path),
                                                     name)) if name else path
                where = "%s: %s" % (os.path.relpath(path, ROOT), target)
                if not os.path.exists(dest):
                    bad.append(where + " (missing)")
                elif frag and frag not in anchors(dest):
                    bad.append(where + " (no such heading)")
        self.assertEqual(bad, [])


if __name__ == "__main__":
    unittest.main()
