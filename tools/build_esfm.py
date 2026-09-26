#!/usr/bin/env python3
# (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
#
# Licensed under GPL Version 3.0
"""Build ESFM.DRV from the source in src/esfm.

usage: build_esfm.py [--stock] [--verify] [--bank FILE] [-o OUTPUT]

  (default)  the ESS driver with the stuck-note fixes (ESFM_FIX=1),
             to build/ESFM.DRV
  --stock    the original driver (ESFM_FIX=0), to out/ESFM.DRV
  --verify   with --stock, fail unless it's byte-identical to
             driver/ESFM.DRV
  --bank     put another patch bank (raw, as in esfm_patch_banks/) in
             the driver instead of the stock one

Needs NASM 2.14 or later and Python 3.
"""

import argparse
import json
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)

import nelink  # noqa: E402

SRC = os.path.join(ROOT, "src", "esfm")
ORIGINAL = os.path.join(ROOT, "driver", "ESFM.DRV")
BANK = (256, 1234)


def build(fix, output=None, workdir=None, bank=None, defines=None):
    workdir = workdir or os.path.join(ROOT, "out", "esfm")
    os.makedirs(workdir, exist_ok=True)
    binary = os.path.join(workdir, "esfm-%s.bin" % ("fix" if fix else "stock"))
    cmd = ["nasm", "-f", "bin", "-I", SRC + os.sep,
           "-DESFM_FIX=%d" % (1 if fix else 0), "-o", binary]
    for k, v in (defines or {}).items():
        cmd.append("-D%s=%s" % (k, v))
    cmd.append(os.path.join(SRC, "esfm.asm"))
    res = subprocess.run(cmd, capture_output=True, text=True)
    if res.returncode != 0:
        sys.stderr.write(res.stderr)
        raise SystemExit("build_esfm: nasm failed")
    with open(os.path.join(SRC, "layout.json")) as f:
        layout = json.load(f)
    if bank:
        for t in layout["resources"]:
            for r in t["entries"]:
                if (t["type"], r["id"]) == BANK:
                    r["file"] = os.path.relpath(os.path.abspath(bank), SRC)
    with open(binary, "rb") as f:
        data = nelink.link(f.read(), layout, SRC)
    if output:
        os.makedirs(os.path.dirname(os.path.abspath(output)), exist_ok=True)
        with open(output, "wb") as f:
            f.write(data)
    return data


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--stock", action="store_true",
                    help="build the unmodified ESS driver (ESFM_FIX=0)")
    ap.add_argument("--verify", action="store_true",
                    help="with --stock: compare with driver/ESFM.DRV")
    ap.add_argument("--bank", help="patch bank to put in the driver")
    ap.add_argument("-o", "--output")
    args = ap.parse_args()
    out = args.output or os.path.join(
        ROOT, "out" if args.stock else "build", "ESFM.DRV")
    try:
        data = build(not args.stock, out, bank=args.bank)
    except nelink.LinkError as e:
        raise SystemExit("build_esfm: link error: %s" % e)
    print("%s: %d bytes" % (os.path.relpath(out, ROOT), len(data)))
    if args.verify:
        if not args.stock:
            raise SystemExit("build_esfm: --verify needs --stock")
        with open(ORIGINAL, "rb") as f:
            if f.read() != data:
                raise SystemExit("build_esfm: differs from driver/ESFM.DRV")
        print("identical to driver/ESFM.DRV")


if __name__ == "__main__":
    main()
