#!/usr/bin/env python3
# (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
#
# Licensed under GPL Version 3.0
"""Build ESFM.DRV from the source in src/esfm.

usage: build_esfm.py [--stock] [--verify] [--bank FILE] [-o OUTPUT]

  (default)  the ESS driver with the stuck-note fixes (ESFM_FIX=1),
             to build/ESFM.DRV, and ESFMOPL3.DRV next to it, the OPL3
             device's driver for the Multimedia control panel
             (src/esfm/esfmopl3.asm)
  --stock    the original driver (ESFM_FIX=0), to out/ESFM.DRV
  --verify   with --stock, fail unless it's byte-identical to
             driver/ESFM.DRV
  --bank     put another patch bank (raw, as in esfm_patch_banks/) in
             the driver instead of bnk_com_better_square_wave.bin (or,
             with --stock, ESS's bnk_com.bin). The fixed driver also
             carries ESS's bnk_com.bin as resource 1235, for
             BetterSquareWave=0 in SYSTEM.INI

Needs NASM 2.14 or later and Python 3.
"""

import argparse
import json
import os
import struct
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)

import nelink  # noqa: E402

SRC = os.path.join(ROOT, "src", "esfm")
ORIGINAL = os.path.join(ROOT, "driver", "ESFM.DRV")
BANK = (256, 1234)
# the bank built into the fixed driver, the stock build keeps ESS's
FIX_BANK = os.path.join(ROOT, "esfm_patch_banks",
                        "bnk_com_better_square_wave.bin")
# and the fixed driver keeps ESS's as well, for BetterSquareWave=0
ESS_BANK_ID = 1235
ESS_BANK = os.path.join(ROOT, "esfm_patch_banks", "bnk_com.bin")


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
    if bank is None and fix:
        bank = FIX_BANK
    if bank:
        for t in layout["resources"]:
            for r in t["entries"]:
                if (t["type"], r["id"]) == BANK:
                    r["file"] = os.path.relpath(os.path.abspath(bank), SRC)
    if fix:
        # ESS's bank after the rest of the file, loaded only when asked for
        for t in layout["resources"]:
            if t["type"] == BANK[0]:
                t["entries"].append({
                    "id": ESS_BANK_ID, "flags": 0x1C30,
                    "file": os.path.relpath(ESS_BANK, SRC)})
        layout["order"].append({"item": "res:%d:%d" % (BANK[0], ESS_BANK_ID),
                                "offset": 0})
    with open(binary, "rb") as f:
        data = nelink.link(f.read(), layout, SRC)
    if output:
        os.makedirs(os.path.dirname(os.path.abspath(output)), exist_ok=True)
        with open(output, "wb") as f:
            f.write(data)
    return data


def mz_stub():
    """the DOS stub of ESFMOPL3.DRV: says what the file is, 128 bytes"""
    # push cs, pop ds, mov dx,msg, mov ah,9, int 21h, mov ax,4C01h, int 21h
    code = bytes([0x0E, 0x1F, 0xBA, 0x0E, 0x00, 0xB4, 0x09, 0xCD, 0x21,
                  0xB8, 0x01, 0x4C, 0xCD, 0x21])
    body = code + b"This is a Windows MIDI driver.\r\n$"
    size = 0x80
    header = struct.pack("<2sHHHHHHHHHHHHH", b"MZ", size % 512,
                         (size + 511) // 512, 0, 4, 0, 0xFFFF, 0, 0xB8, 0,
                         0, 0, 0x40, 0)
    header += b"\0" * (0x3C - len(header)) + struct.pack("<I", size)
    return (header + body).ljust(size, b"\0")


def build_opl3(output=None, workdir=None):
    """ESFMOPL3.DRV, which passes its one device on to ESFM.DRV's device 1"""
    workdir = workdir or os.path.join(ROOT, "out", "esfm")
    os.makedirs(workdir, exist_ok=True)
    binary = os.path.join(workdir, "esfmopl3.bin")
    res = subprocess.run(["nasm", "-f", "bin", "-I", SRC + os.sep, "-o",
                          binary, os.path.join(SRC, "esfmopl3.asm")],
                         capture_output=True, text=True)
    if res.returncode != 0:
        sys.stderr.write(res.stderr)
        raise SystemExit("build_esfm: nasm failed on esfmopl3.asm")
    with open(os.path.join(SRC, "esfmopl3.json")) as f:
        layout = json.load(f)
    stub = os.path.join(workdir, "esfmopl3.stub")
    with open(stub, "wb") as f:
        f.write(mz_stub())
    layout["stub"] = stub
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
    if not args.stock:
        opl3 = os.path.join(os.path.dirname(os.path.abspath(out)),
                            "ESFMOPL3.DRV")
        try:
            size = len(build_opl3(opl3))
        except nelink.LinkError as e:
            raise SystemExit("build_esfm: link error: %s" % e)
        print("%s: %d bytes" % (os.path.relpath(opl3, ROOT), size))
    if args.verify:
        if not args.stock:
            raise SystemExit("build_esfm: --verify needs --stock")
        with open(ORIGINAL, "rb") as f:
            if f.read() != data:
                raise SystemExit("build_esfm: differs from driver/ESFM.DRV")
        print("identical to driver/ESFM.DRV")


if __name__ == "__main__":
    main()
