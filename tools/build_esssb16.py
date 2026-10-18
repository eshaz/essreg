#!/usr/bin/env python3
# (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
#
# Licensed under GPL Version 3.0
"""Build esssb16.com, the DOS Sound Blaster 16 front, from src/esssb16.asm.

usage: build_esssb16.py [-o OUTPUT]

The program is a .COM file, assembled with NASM's bin format, to
build/esssb16.com. A map of its symbols goes to out/esssb16/esssb16.map,
for the tests.

Needs NASM 2.14 or later and Python 3.
"""

import argparse
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
SRC = os.path.join(ROOT, "src", "esssb16.asm")


def build(output=None, workdir=None):
    """assemble the program: (its bytes, {symbol: offset})"""
    workdir = workdir or os.path.join(ROOT, "out", "esssb16")
    os.makedirs(workdir, exist_ok=True)
    output = output or os.path.join(workdir, "esssb16.com")
    mapfile = os.path.join(workdir, "esssb16.map")
    wrapper = os.path.join(workdir, "map.asm")
    with open(wrapper, "w") as f:
        f.write("[map symbols %s]\n%%include \"%s\"\n"
                % (mapfile.replace("\\", "/"), SRC.replace("\\", "/")))
    cmd = ["nasm", "-f", "bin", "-o", output, wrapper]
    res = subprocess.run(cmd, capture_output=True, text=True)
    if res.returncode != 0:
        sys.stderr.write(res.stderr)
        raise SystemExit("build_esssb16: nasm failed")
    with open(output, "rb") as f:
        data = f.read()
    return data, symbols(mapfile)


def symbols(mapfile):
    """the symbols of NASM's map: {name: offset in the segment}"""
    syms = {}
    with open(mapfile) as f:
        for line in f:
            parts = line.split()
            if len(parts) != 3:
                continue
            try:
                real, virtual = int(parts[0], 16), int(parts[1], 16)
            except ValueError:
                continue
            if parts[2] == "Name":
                continue
            syms[parts[2]] = virtual if virtual >= real else real
    return syms


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("-o", "--output")
    args = ap.parse_args()
    output = args.output or os.path.join(ROOT, "build", "esssb16.com")
    os.makedirs(os.path.dirname(os.path.abspath(output)), exist_ok=True)
    data, syms = build(output)
    resident = (syms["resident_end"] + 15) & ~15
    print("%s: %d bytes, %d resident (%d paragraphs with the PSP)"
          % (os.path.relpath(output, ROOT), len(data), resident,
             resident // 16))


if __name__ == "__main__":
    main()
