#!/usr/bin/env python3
# (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
#
# Licensed under GPL Version 3.0
"""Build ES1869.DRV, ESS's wave, mixer and aux driver, with the Audio 2
DAC playing without 4x oversampling and with its filter bypassed.

usage: build_es1869drv.py [--stock [--verify]] [-o OUTPUT]

  (default)  the changed driver, to build/ES1869.DRV
  --stock    ESS's driver as it is, to out/ES1869.DRV
  --verify   with --stock: compare with driver/ES1869.DRV

ES1869.DRV isn't rebuilt from source like ESFM.DRV. The change is two
instructions of two bytes, where ESS's code makes the value it writes to
mixer 71h, the Audio 2 mode (docs/AUDIO_PIPELINE.md). The driver in driver/
must be ESS's 4.04.00.1319, by its SHA-256, and each instruction is
checked against ESS's bytes first.
"""

import argparse
import hashlib
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)

from retools.ne import NEFile  # noqa: E402

ORIGINAL = os.path.join(ROOT, "driver", "ES1869.DRV")
SHA256 = "95ec0acc2c0b2099628e9c827eb4701e003f28198cb4e0347a95ecb6d21aaaa0"

# (segment, offset, ESS's bytes, the new ones, what they do). Each follows
# a read of 71h (1:1294) and precedes its write (1:12B2), so AL holds 71h's
# value.
PATCHES = [
    (1, 0x115A, b"\x0C\x12", b"\x0C\x0A",
     "wave-out open, close and resume: or al,0Ah, the filter bypassed "
     "and asynchronous (ESS: or al,12h, 4x oversampling and "
     "asynchronous)"),
    (6, 0x2DEB, b"\x0C\x12", b"\x24\xEF",
     "each playback start: and al,0EFh, no 4x oversampling (ESS: "
     "or al,12h)"),
]


def original():
    with open(ORIGINAL, "rb") as f:
        return f.read()


def check(data):
    """raise unless data is the driver the changes are for"""
    if hashlib.sha256(data).hexdigest() != SHA256:
        raise SystemExit("build_es1869drv: %s isn't ESS's ES1869.DRV "
                         "4.04.00.1319" % ORIGINAL)


def file_offsets(data):
    """the file offset of each change"""
    ne = NEFile(data)
    return [ne.segments[seg - 1].offset + off for seg, off, _o, _n, _w
            in PATCHES]


def build(fix=True, data=None):
    data = original() if data is None else data
    check(data)
    if not fix:
        return data
    out = bytearray(data)
    for at, (seg, off, old, new, _what) in zip(file_offsets(data), PATCHES):
        if out[at:at + len(old)] != old:
            raise SystemExit("build_es1869drv: %d:%04X doesn't hold ESS's "
                             "bytes" % (seg, off))
        out[at:at + len(new)] = new
    return bytes(out)


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--stock", action="store_true",
                    help="ESS's driver as it is")
    ap.add_argument("--verify", action="store_true",
                    help="with --stock: compare with driver/ES1869.DRV")
    ap.add_argument("-o", "--output")
    args = ap.parse_args()
    if args.verify and not args.stock:
        raise SystemExit("build_es1869drv: --verify needs --stock")
    data = build(not args.stock)
    out = args.output or os.path.join(
        ROOT, "out" if args.stock else "build", "ES1869.DRV")
    os.makedirs(os.path.dirname(out), exist_ok=True)
    with open(out, "wb") as f:
        f.write(data)
    print("%s: %d bytes" % (os.path.relpath(out, ROOT), len(data)))
    if args.verify:
        with open(out, "rb") as f:
            if f.read() != original():
                raise SystemExit("build_es1869drv: differs from "
                                 "driver/ES1869.DRV")
        print("identical to driver/ES1869.DRV")


if __name__ == "__main__":
    main()
