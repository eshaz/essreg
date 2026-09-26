#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Build ES1869.VXD from the reassemblable source in src/vxd.

usage: build_vxd.py [--stock] [--verify] [-o OUTPUT]

  (default)  ESSREG_EXT=1: the ESS driver plus the essreg register API,
             written to build/ES1869.VXD
  --stock    ESSREG_EXT=0: the original driver, written to out/ES1869.VXD
  --verify   with --stock: fail unless the result is byte-identical to
             driver/ES1869.VXD

Requires NASM 2.14 or later and Python 3.
"""

import argparse
import json
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)

import lelink  # noqa: E402
from retools import elf32  # noqa: E402

SRC = os.path.join(ROOT, "src", "vxd")
ORIGINAL = os.path.join(ROOT, "driver", "ES1869.VXD")


def build(ext, output, workdir=None):
    workdir = workdir or os.path.join(ROOT, "out", "vxd")
    os.makedirs(workdir, exist_ok=True)
    obj = os.path.join(workdir, "es1869-%s.o" % ("ext" if ext else "stock"))
    cmd = ["nasm", "-f", "elf32", "-I", SRC + os.sep,
           "-DESSREG_EXT=%d" % (1 if ext else 0), "-o", obj,
           os.path.join(SRC, "es1869.asm")]
    res = subprocess.run(cmd, capture_output=True, text=True)
    if res.returncode != 0:
        sys.stderr.write(res.stderr)
        raise SystemExit("build_vxd: nasm failed")
    with open(os.path.join(SRC, "layout.json")) as f:
        layout = json.load(f)
    try:
        data, objects, fixups = lelink.link(elf32.read_elf(obj), layout, SRC)
    except lelink.LinkError as e:
        raise SystemExit("build_vxd: link error: %s" % e)
    os.makedirs(os.path.dirname(os.path.abspath(output)), exist_ok=True)
    with open(output, "wb") as f:
        f.write(data)
    return data, objects, fixups


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--stock", action="store_true",
                    help="build the unmodified ESS driver (ESSREG_EXT=0)")
    ap.add_argument("--verify", action="store_true",
                    help="with --stock: compare with driver/ES1869.VXD")
    ap.add_argument("-o", "--output")
    args = ap.parse_args()
    if args.verify and not args.stock:
        ap.error("--verify requires --stock")
    output = args.output or (os.path.join(ROOT, "out", "ES1869.VXD")
                             if args.stock else
                             os.path.join(ROOT, "build", "ES1869.VXD"))
    data, objects, fixups = build(not args.stock, output)
    print("%s: %d bytes, %d objects, %d fixups" %
          (os.path.relpath(output, ROOT), len(data), len(objects), len(fixups)))
    for o in objects:
        print("  %-4s %6d bytes" % (o.name_str, o.vsize))
    if args.verify:
        with open(ORIGINAL, "rb") as f:
            orig = f.read()
        if data != orig:
            diff = next(i for i, (a, b) in enumerate(zip(data, orig)) if a != b) \
                if len(data) == len(orig) else min(len(data), len(orig))
            raise SystemExit("build_vxd: NOT identical to driver/ES1869.VXD "
                             "(first difference at file offset %#x)" % diff)
        print("identical to driver/ES1869.VXD")


if __name__ == "__main__":
    main()
