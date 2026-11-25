#!/usr/bin/env python3
# (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
#
# Licensed under GPL Version 3.0
"""Check that the working tree differs from a git revision only in comments
and docstrings.

usage: guard.py [REV] [paths...]   (REV: HEAD, paths: every changed file)

- C, H, TBL, RC: the token streams without comments must be equal
  (strings included)
- Python: ast.dump without docstrings must be equal
- ASM, INC: the lines without ';' comments must be equal
- mk1, Makefile, sh: the lines without '#' comments
- bat: the lines other than 'rem'
- Markdown, names.txt and other text files are listed as "free"
"""

import ast
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def git_show(rev, path):
    r = subprocess.run(["git", "-C", ROOT, "show", "%s:%s" % (rev, path)],
                       capture_output=True)
    return r.stdout if r.returncode == 0 else None


def c_tokens(src):
    out = []
    tok = re.compile(r"""
        (?P<ws>\s+)
      | (?P<lc>//[^\n]*)
      | (?P<bc>/\*.*?\*/)
      | (?P<str>"(?:\\.|[^"\\\n])*")
      | (?P<chr>'(?:\\.|[^'\\\n])*')
      | (?P<num>\.?\d[\w.]*)
      | (?P<id>[A-Za-z_$][\w$]*)
      | (?P<pp>\#\#|\#)
      | (?P<op>->|\+\+|--|<<=|>>=|<<|>>|<=|>=|==|!=|&&|\|\|
              |[-+*/%&|^!~<>=?:;,.()\[\]{}\\])
      | (?P<other>.)
    """, re.S | re.X)
    # line splices first (backslash-newline), as the preprocessor does
    src = src.replace("\\\r\n", "").replace("\\\n", "")
    for m in tok.finditer(src):
        kind = m.lastgroup
        if kind in ("ws", "lc", "bc"):
            continue
        out.append(m.group())
    return out


def strip_docstrings(tree):
    for node in ast.walk(tree):
        if isinstance(node, (ast.Module, ast.ClassDef, ast.FunctionDef,
                             ast.AsyncFunctionDef)):
            body = node.body
            if body and isinstance(body[0], ast.Expr) and \
                    isinstance(body[0].value, ast.Constant) and \
                    isinstance(body[0].value.value, str):
                node.body = body[1:] or [ast.Pass()]
    return tree


def asm_lines(src):
    out = []
    for line in src.splitlines():
        # strip ; comments outside quotes
        res, q = [], None
        for ch in line:
            if q:
                res.append(ch)
                if ch == q:
                    q = None
                continue
            if ch in "'\"`":
                q = ch
                res.append(ch)
                continue
            if ch == ";":
                break
            res.append(ch)
        s = "".join(res).rstrip()
        if s.strip():
            out.append(s)
    return out


def hash_lines(src):
    out = []
    for line in src.splitlines():
        s = line.split("#", 1)[0].rstrip() if not line.startswith("#!") \
            else line
        if s.strip():
            out.append(s)
    return out


def bat_lines(src):
    return [line.rstrip() for line in src.splitlines()
            if line.strip() and
            not re.match(r"\s*(rem\b|::)", line, re.I)]


def check(rev, path):
    new = open(os.path.join(ROOT, path), "rb").read()
    old = git_show(rev, path)
    if old is None:
        return "new file"
    if old == new:
        return None
    o = old.decode("latin-1").replace("\r\n", "\n")
    n = new.decode("latin-1").replace("\r\n", "\n")
    ext = os.path.splitext(path)[1].lower()
    base = os.path.basename(path)
    if ext in (".c", ".h", ".tbl", ".rc"):
        ok = c_tokens(o) == c_tokens(n)
    elif ext == ".py":
        ok = ast.dump(strip_docstrings(ast.parse(o))) == \
            ast.dump(strip_docstrings(ast.parse(n)))
    elif ext in (".asm", ".inc"):
        ok = asm_lines(o) == asm_lines(n)
    elif ext in (".mk1", ".sh") or base == "Makefile":
        ok = hash_lines(o) == hash_lines(n)
    elif ext == ".bat":
        ok = bat_lines(o) == bat_lines(n)
    else:
        return "free (%s)" % (ext or base)
    return None if ok else "CODE CHANGED"


def main():
    args = sys.argv[1:]
    rev = "HEAD"
    if args and not os.path.exists(os.path.join(ROOT, args[0])):
        rev = args.pop(0)
    if not args:
        out = subprocess.run(["git", "-C", ROOT, "diff", "--name-only", rev],
                             capture_output=True, text=True).stdout
        args = [p for p in out.split() if p]
    bad = 0
    for p in args:
        res = check(rev, p)
        if res and res.startswith("CODE"):
            bad += 1
        if res:
            print("%-50s %s" % (p, res))
    print("%d files with code changes" % bad)
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
