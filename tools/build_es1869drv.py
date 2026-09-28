#!/usr/bin/env python3
# (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
#
# Licensed under GPL Version 3.0
"""Build ES1869.DRV, ESS's wave, mixer and aux driver, from the source in
src/es1869.

usage: build_es1869drv.py [--stock [--verify]] [-o OUTPUT]

  (default)  ESS's driver with this repository's changes (ES1869_FIX=1),
             to build/ES1869.DRV
  --stock    the original driver (ES1869_FIX=0), to out/ES1869.DRV
  --verify   with --stock, fail unless it's byte-identical to
             driver/ES1869.DRV (4.04.00.1319)

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

SRC = os.path.join(ROOT, "src", "es1869")
ORIGINAL = os.path.join(ROOT, "driver", "ES1869.DRV")


def original():
    with open(ORIGINAL, "rb") as f:
        return f.read()


def build(fix=True, output=None, workdir=None, defines=None):
    workdir = workdir or os.path.join(ROOT, "out", "es1869")
    os.makedirs(workdir, exist_ok=True)
    binary = os.path.join(workdir, "es1869-%s.bin" %
                          ("fix" if fix else "stock"))
    cmd = ["nasm", "-f", "bin", "-I", SRC + os.sep,
           "-DES1869_FIX=%d" % (1 if fix else 0), "-o", binary]
    for k, v in (defines or {}).items():
        cmd.append("-D%s=%s" % (k, v))
    cmd.append(os.path.join(SRC, "es1869.asm"))
    res = subprocess.run(cmd, capture_output=True, text=True)
    if res.returncode != 0:
        sys.stderr.write(res.stderr)
        raise SystemExit("build_es1869drv: nasm failed")
    with open(os.path.join(SRC, "layout.json")) as f:
        layout = json.load(f)
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
                    help="build ESS's driver as it is (ES1869_FIX=0)")
    ap.add_argument("--verify", action="store_true",
                    help="with --stock: compare with driver/ES1869.DRV")
    ap.add_argument("-o", "--output")
    args = ap.parse_args()
    if args.verify and not args.stock:
        raise SystemExit("build_es1869drv: --verify needs --stock")
    out = args.output or os.path.join(
        ROOT, "out" if args.stock else "build", "ES1869.DRV")
    data = build(not args.stock, out)
    print("%s: %d bytes" % (os.path.relpath(out, ROOT), len(data)))
    if args.verify:
        if data != original():
            raise SystemExit("build_es1869drv: differs from "
                             "driver/ES1869.DRV")
        print("identical to driver/ES1869.DRV")


if __name__ == "__main__":
    main()
