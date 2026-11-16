#!/usr/bin/env python3
# (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
#
# Licensed under GPL Version 3.0
"""Format the markdown files so that Windows 98's Notepad can show them.

usage: mdfmt.py [--check] [FILE...]

Notepad needs CRLF line endings, shows only the ANSI code page, opens no
file of 64 KB or more, and doesn't wrap lines unless Word Wrap is on. So
every markdown file is written with CRLF, prose and list items wrap at 76
columns, and a table is padded into columns when it fits in 76. The file
has to be plain ASCII and under 60,000 bytes.

Fenced code blocks and headings stay as they are. A wrapped line never
starts with something that would begin a list, a heading, a quote or a
table, so the file renders the same on GitHub.

Without FILE, formats every markdown file that git tracks. --check
changes nothing and fails if a file isn't formatted or breaks a rule.
"""

import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
WIDTH = 76
MAX_BYTES = 60000
SPAN_MAX = 40           # a code span up to this long is never split

LIST_RE = re.compile(r"([*+-]|\d+[.)]) +")


def block_start(line):
    """a line that starts a new block rather than continuing a paragraph"""
    s = line.lstrip(" ")
    return (not s or s.startswith(("```", "#", "|", ">")) or
            LIST_RE.match(s) is not None)


def tokens(text):
    """the words of a paragraph: split at spaces, except inside a short code
    span, which stays one word"""
    out, cur, i = [], "", 0
    while i < len(text):
        c = text[i]
        if c == "`":
            end = text.find("`", i + 1)
            if end > i:
                span = text[i:end + 1]
                if end - i < SPAN_MAX:
                    cur += span
                else:
                    # a long span splits at its spaces, and its closing
                    # backtick stays its own
                    parts = span.split(" ")
                    cur += parts[0]
                    for part in parts[1:]:
                        if cur:
                            out.append(cur)
                        cur = part
                i = end + 1
                continue
        if c == " ":
            if cur:
                out.append(cur)
            cur = ""
        else:
            cur += c
        i += 1
    if cur:
        out.append(cur)
    return out


def bad_start(word):
    """a word that would change the markdown if a line started with it"""
    return (word in ("*", "+", "-") or re.fullmatch(r"\d+[.)]", word) or
            re.fullmatch(r"#{1,6}", word) or re.fullmatch(r"[=-]+", word) or
            word.startswith((">", "|", "```", "~~~", "<")))


def wrap(text, first, rest, width=WIDTH):
    """text in lines of at most width columns, the first after the prefix
    first and the others after rest"""
    lines = [[]]
    heads = [first]
    for w in tokens(text):
        cur = lines[-1]
        size = len(heads[-1]) + sum(len(x) + 1 for x in cur) + len(w)
        if not cur or size <= width:
            cur.append(w)
            continue
        if bad_start(w):
            # take the last word along, or run over if it's the only one
            if len(cur) > 1:
                lines.append([cur.pop(), w])
                heads.append(rest)
            else:
                cur.append(w)
            continue
        lines.append([w])
        heads.append(rest)
    return [h + " ".join(ws) for h, ws in zip(heads, lines)]


def split_row(line):
    s = line.strip()
    if s.startswith("|"):
        s = s[1:]
    if s.endswith("|") and not s.endswith("\\|"):
        s = s[:-1]
    return [c.strip() for c in re.split(r"(?<!\\)\|", s)]


def table(rows, indent, problems, where):
    """the rows padded into columns, or as they were if that's too wide"""
    cells = [split_row(r) for r in rows]
    delim = [all(re.fullmatch(r":?-+:?", c) for c in row) for row in cells]
    ncol = max(len(row) for row in cells)
    for row in cells:
        row += [""] * (ncol - len(row))
    widths = [max([3] + [len(row[j]) for row, d in zip(cells, delim)
                         if not d]) for j in range(ncol)]
    if indent + 1 + sum(w + 3 for w in widths) > WIDTH:
        problems.append("%s: table too wide for %d columns" % (where, WIDTH))
        return [r.rstrip() for r in rows]
    out = []
    for row, d in zip(cells, delim):
        if d:
            parts = []
            for c, w in zip(row, widths):
                dash = "-" * (w + 2)
                if c.startswith(":"):
                    dash = ":" + dash[1:]
                if c.endswith(":"):
                    dash = dash[:-1] + ":"
                parts.append(dash)
            out.append(" " * indent + "|" + "|".join(parts) + "|")
        else:
            out.append(" " * indent + "| " + " | ".join(
                c.ljust(w) for c, w in zip(row, widths)) + " |")
    return out


def format_text(text, name="<text>", problems=None):
    """markdown text formatted for Notepad, with LF line endings"""
    if problems is None:
        problems = []
    lines = text.replace("\r\n", "\n").split("\n")
    out = []
    i, n = 0, len(lines)
    while i < n:
        line = lines[i]
        s = line.lstrip(" ")
        indent = len(line) - len(s)
        where = "%s:%d" % (name, i + 1)
        if s.startswith(("```", "~~~")):
            fence = s[:3]
            out.append(line.rstrip())
            i += 1
            while i < n and not lines[i].lstrip(" ").startswith(fence):
                out.append(lines[i])
                i += 1
            if i < n:
                out.append(lines[i].rstrip())
                i += 1
            continue
        if not s:
            if out and out[-1] != "":
                out.append("")
            i += 1
            continue
        if s.startswith("#"):
            out.append(line.rstrip())
            i += 1
            continue
        if s.startswith("|"):
            rows = []
            while i < n and lines[i].lstrip(" ").startswith("|"):
                rows.append(lines[i])
                i += 1
            out += table(rows, indent, problems, where)
            continue
        if s.startswith(">"):
            parts = []
            while i < n and lines[i].lstrip(" ").startswith(">"):
                parts.append(lines[i].lstrip(" ")[1:].strip())
                i += 1
            head = " " * indent + "> "
            out += wrap(" ".join(parts), head, head)
            continue
        m = LIST_RE.match(s)
        if m:
            marker = m.group(1)
            first = " " * indent + marker + " "
            rest = " " * len(first)
            parts = [s[m.end():].strip()]
        else:
            first = rest = " " * indent
            parts = [s.strip()]
        i += 1
        while i < n and not block_start(lines[i]):
            parts.append(lines[i].strip())
            i += 1
        out += wrap(" ".join(parts), first, rest)
    while out and out[-1] == "":
        out.pop()
    return "\n".join(out) + "\n"


def check_rules(name, text, problems):
    """ASCII, size and line length of formatted text (LF endings)"""
    for no, line in enumerate(text.split("\n"), 1):
        bad = [c for c in line if ord(c) > 126 or
               (ord(c) < 32 and c != "\t")]
        if bad:
            problems.append("%s:%d: not plain ASCII: %r" % (name, no,
                                                            "".join(bad)))
    size = len(text.replace("\n", "\r\n").encode("latin-1", "replace"))
    if size >= MAX_BYTES:
        problems.append("%s: %d bytes, Notepad needs under %d" %
                        (name, size, MAX_BYTES))
    code = False
    for no, line in enumerate(text.split("\n"), 1):
        if line.lstrip(" ").startswith(("```", "~~~")):
            code = not code
        if len(line) <= WIDTH:
            continue
        s = line.lstrip(" ")
        if s.startswith("|") and not code:
            continue    # reported by table()
        m = None if code else LIST_RE.match(s)
        # a single word too long for a line may run over
        if code or s.startswith("#") or \
                len(tokens(s[m.end():] if m else s)) > 1:
            problems.append("%s:%d: %d columns" % (name, no, len(line)))


def markdown_files():
    r = subprocess.run(["git", "ls-files", "*.md"], cwd=ROOT,
                       capture_output=True, text=True, check=True)
    return [os.path.join(ROOT, p) for p in r.stdout.split()]


def main(argv):
    check = "--check" in argv
    files = [a for a in argv if not a.startswith("--")] or markdown_files()
    problems, stale = [], []
    for path in files:
        name = os.path.relpath(path, ROOT)
        with open(path, "rb") as f:
            raw = f.read()
        text = raw.decode("utf-8", "replace")
        new = format_text(text, name, problems)
        check_rules(name, new, problems)
        data = new.replace("\n", "\r\n").encode("utf-8")
        if data != raw:
            stale.append(name)
            if not check:
                with open(path, "wb") as f:
                    f.write(data)
    for p in problems:
        print(p)
    if check:
        for name in stale:
            print("%s: not formatted, run python3 tools/mdfmt.py" % name)
        return 1 if problems or stale else 0
    print("%d files, %d reformatted" % (len(files), len(stale)))
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
