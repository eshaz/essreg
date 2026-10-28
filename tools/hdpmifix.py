#!/usr/bin/env python3
# (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
#
# Licensed under GPL Version 3.0
"""Fix HDPMI32i's emulation of an STI that comes right before an IRETD.

usage: hdpmifix.py IN.EXE OUT.EXE
       hdpmifix.py --check FILE

HDPMI32i runs its clients with IOPL 0, so that it can trap their port
accesses, and an STI of theirs faults; the host emulates it (emuprivinstr in
Src/HDPMI/HDPMI.ASM of crazii's HX fork). An STI with interrupts off that
comes right before an IRETD, as at the end of a hardware interrupt's
handler, has the host do the IRETD too, from the client's stack. It pushes
EDI first, and then reaches the fault's frame at the offsets it has without
the push, 4 bytes short: it takes the client's EFLAGS and the low word of
its ESP for the stack's offset and selector, and faults at ring 0, which
stops the machine. Absolute Pinball's SETUP.EXE and ABS.EXE end their
handlers that way. The fix moves the four offsets by 4, as the push needs;
the instructions keep their lengths. The releases from v0.1-beta2 to
v0.1-beta4fix2 have the same code, which is found by its bytes.

--check says whether FILE has the fix (exit status 0), the release's code
(1), or neither (2).
"""

import argparse
import hashlib
import sys

# cmp al,0CFh; jne; push edi; lds edi,[esp+20h]; mov esi,[edi];
# mov eax,[edi+4]; mov [esp+18h],eax; mov eax,[edi+8]; and eax,not 1A3000h;
# mov [esp+1Ch],eax; add dword [esp+20h],12; pop edi
BROKEN = bytes.fromhex("3ccf7521" "57" "c57c2420" "8b37" "8b4704" "89442418"
                       "8b4708" "25ffcfe5ff" "8944241c" "834424200c" "5f")
# the same after the push: rSSSP, rCSd, rFL and rSP of the frame
FIXED = bytes.fromhex("3ccf7521" "57" "c57c2424" "8b37" "8b4704" "8944241c"
                      "8b4708" "25ffcfe5ff" "89442420" "834424240c" "5f")
CHANGED = tuple(i for i, (a, b) in enumerate(zip(BROKEN, FIXED)) if a != b)
# HDPMI32i.EXE of v0.1-beta4fix2, as released and fixed
RELEASE_SHA256 = ("f6913b490ef30b709d94323889e2f857"
                  "aa0d1bee0638e09ba6d9a917d3cb34e8")


def where(data, code):
    """the offsets of code in data"""
    found, at = [], data.find(code)
    while at >= 0:
        found.append(at)
        at = data.find(code, at + 1)
    return found


def state(data):
    """'fixed', 'broken' or None, with the code's offset"""
    fixed, broken = where(data, FIXED), where(data, BROKEN)
    if len(fixed) == 1 and not broken:
        return "fixed", fixed[0]
    if len(broken) == 1 and not fixed:
        return "broken", broken[0]
    return None, None


def fix(data):
    """data with the fix, and the code's offset; ValueError without the
    release's code"""
    what, at = state(data)
    if what == "fixed":
        return bytes(data), at
    if what != "broken":
        raise ValueError("HDPMI32i's STI emulation isn't there as released")
    out = bytearray(data)
    out[at:at + len(FIXED)] = FIXED
    return bytes(out), at


def unfix(data):
    """data as released, from data with the fix"""
    what, at = state(data)
    if what != "fixed":
        raise ValueError("no fix to undo")
    out = bytearray(data)
    out[at:at + len(BROKEN)] = BROKEN
    return bytes(out)


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def main():
    p = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    p.add_argument("--check", action="store_true",
                   help="only say whether the file has the fix")
    p.add_argument("files", nargs="+", metavar="FILE")
    a = p.parse_args()
    if a.check:
        if len(a.files) != 1:
            p.error("--check takes one file")
        with open(a.files[0], "rb") as f:
            data = f.read()
        what, at = state(data)
        if what is None:
            print("%s: HDPMI32i's STI emulation isn't there as released"
                  % a.files[0])
            return 2
        print("%s: %s at %05Xh, SHA-256 %s" % (
            a.files[0], "the fix" if what == "fixed" else "the release's code",
            at, sha256(data)))
        return 0 if what == "fixed" else 1
    if len(a.files) != 2:
        p.error("IN.EXE OUT.EXE")
    with open(a.files[0], "rb") as f:
        data = f.read()
    try:
        out, at = fix(data)
    except ValueError as e:
        print("%s: %s" % (a.files[0], e))
        return 2
    with open(a.files[1], "wb") as f:
        f.write(out)
    if out == data:
        print("%s has the fix already: copied" % a.files[0])
    else:
        bytes_at = ", ".join("%05Xh" % (at + i) for i in CHANGED)
        print("fixed at %s: %s" % (bytes_at, a.files[1]))
    print("SHA-256 %s" % sha256(out))
    return 0


if __name__ == "__main__":
    sys.exit(main())
