#!/usr/bin/env python3
# (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
#
# Licensed under GPL Version 3.0
"""Build src/essp0.h, essprobe's window on the DMA controllers under
EMM386, from src/essp0.asm.

usage: build_essp0.py [--check]

The block is assembled with NASM's bin format, and the header gives its
bytes and the offsets and constants essprobe needs. --check only says
whether src/essp0.h is current.

Needs NASM 2.14 or later and Python 3.
"""

import argparse
import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
SRC = os.path.join(ROOT, "src", "essp0.asm")
OUT = os.path.join(ROOT, "src", "essp0.h")
# the labels whose offsets the program uses, and the constants
OFFSETS = ("signature", "handle", "armed", "count", "table", "req", "handler")
CONSTS = ("ESSP0_NREQ", "ESSP0_ARMED", "ESSP0_DONE", "ESSP0_IN",
          "ESSP0_OUT", "ESSP0_SIZE")


def symbols(mapfile):
    """NASM's map: {name: value}"""
    syms = {}
    with open(mapfile) as f:
        for line in f:
            parts = line.split()
            try:
                values = [int(p, 16) for p in parts[:-1]]
            except ValueError:
                continue
            if len(parts) in (2, 3):
                syms[parts[-1]] = max(values)
    return syms


def build():
    """the block's bytes and {symbol: value}"""
    with tempfile.TemporaryDirectory() as tmp:
        out = os.path.join(tmp, "essp0.bin")
        mapfile = os.path.join(tmp, "essp0.map")
        wrapper = os.path.join(tmp, "map.asm")
        with open(wrapper, "w") as f:
            f.write("[map symbols %s]\n%%include \"%s\"\n"
                    % (mapfile.replace("\\", "/"), SRC.replace("\\", "/")))
        res = subprocess.run(["nasm", "-f", "bin", "-o", out, wrapper],
                             capture_output=True, text=True)
        if res.returncode != 0:
            sys.stderr.write(res.stderr)
            raise SystemExit("build_essp0: nasm failed")
        with open(out, "rb") as f:
            data = f.read()
        syms = symbols(mapfile)
    if syms.get("ESSP0_SIZE") != len(data):
        raise SystemExit("build_essp0: ESSP0_SIZE isn't the block's length")
    return data, syms


def header(data, syms):
    """src/essp0.h's text, with LF line endings"""
    lines = [
        "/*",
        " * essprobe's window on the DMA controllers under EMM386, built from",
        " * src/essp0.asm by tools/build_essp0.py, which says what it does.",
        " * Don't edit: build it again.",
        " *",
        " * (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>",
        " *",
        " * Licensed under GPL Version 3.0",
        " */",
        "",
        "#ifndef ESSP0_H",
        "#define ESSP0_H",
        "",
        "// where each part of the block is",
    ]
    for name in OFFSETS:
        lines.append("#define ESSP0_O_%s 0x%04X" % (name.upper(), syms[name]))
    lines += ["", "// the constants"]
    for name in CONSTS:
        lines.append("#define %s 0x%02X" % (name, syms[name]))
    lines += ["", "static const u8 essp0_code[ESSP0_SIZE] = {"]
    for i in range(0, len(data), 12):
        lines.append("    " + ", ".join("0x%02X" % b for b in data[i:i + 12])
                     + ",")
    lines += ["};", "", "#endif /* ESSP0_H */", ""]
    return "\n".join(lines)


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--check", action="store_true",
                    help="only check that src/essp0.h is current")
    args = ap.parse_args()
    text = header(*build()).replace("\n", "\r\n").encode("latin-1")
    if args.check:
        with open(OUT, "rb") as f:
            current = f.read() == text
        print("src/essp0.h is %s" % ("current" if current else "out of date"))
        return 0 if current else 1
    with open(OUT, "wb") as f:
        f.write(text)
    print("src/essp0.h: a block of %d bytes" % len(build()[0]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
