#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Run all host-side tests.

  python3 tests/run_tests.py

- Python tests (tests/test_*.py): LE tooling, byte-identical VxD rebuild,
  the VxD register API in a CPU emulator (needs nasm and unicorn).
- C tests (tests/host/t_*.c) built with gcc against the simulated ES1869:
  esshw protocols, the VxD API wrappers, the register catalog, profiles,
  ESFM patch banks and driver patching, and an old-versus-new port trace
  of essreg's register functions.
- With Open Watcom in $OW2: the 16-bit VxD call thunk in a CPU emulator.
"""

import os
import subprocess
import sys
import tempfile
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
HOST = os.path.join(ROOT, "tests", "host")
SRC = os.path.join(ROOT, "src")
SHIM = os.path.join(HOST, "shim")
BASELINE = "2f3cd6b"   # essreg before the esshw refactor

CFLAGS = ["-std=gnu99", "-Wall", "-Wextra", "-Werror", "-DESS_HOST"]


def cc(out, sources, extra=(), strict=True):
    cmd = ["gcc"] + (CFLAGS if strict else ["-std=gnu99", "-w", "-DESS_HOST"])
    cmd += list(extra) + ["-I" + SRC, "-I" + HOST, "-o", out] + sources
    res = subprocess.run(cmd, capture_output=True, text=True)
    if res.returncode:
        raise RuntimeError("gcc failed:\n%s\n%s" % (" ".join(cmd), res.stderr))


def run(cmd):
    if isinstance(cmd, str):
        cmd = [cmd]
    res = subprocess.run(cmd, capture_output=True, text=True)
    return res.returncode, res.stdout, res.stderr


def c_tests(tmp):
    failures = 0
    src = lambda *names: [os.path.join(SRC, n) for n in names]  # noqa: E731

    esfm_work = os.path.join(tmp, "esfm")
    os.makedirs(esfm_work)
    tests = [("t_esshw", src("esshw.c", "simhw.c"), []),
             ("t_vxdapi", src("vxdapi.c", "esshw.c"), []),
             ("t_esscat", src("esscat.c", "essio.c", "esshw.c", "simhw.c"),
              []),
             ("t_profile", src("esscat.c", "essio.c", "esshw.c", "simhw.c",
                               "profile.c"), []),
             ("t_esfm", src("esfmbank.c"),
              [os.path.join(ROOT, "driver", "ESFM.DRV"),
               os.path.join(ROOT, "esfm_patch_banks", "bnk_com.bin"),
               esfm_work])]
    for name, sources, args in tests:
        exe = os.path.join(tmp, name)
        cc(exe, [os.path.join(HOST, name + ".c")] + sources)
        code, out, err = run([exe] + args)
        sys.stdout.write(out + err)
        failures += code != 0

    # essreg: original regs.c versus the refactored one on esshw
    old = os.path.join(tmp, "old")
    os.makedirs(old, exist_ok=True)
    for name in ("regs.c", "regs.h", "debug.h"):
        data = subprocess.run(["git", "-C", ROOT, "show",
                               "%s:src/%s" % (BASELINE, name)],
                              capture_output=True, check=True).stdout
        with open(os.path.join(old, name), "wb") as f:
            f.write(data)
    with open(os.path.join(old, "trace_old.c"), "w") as f:
        f.write('#include "regs.c"\n#include "t_trace.c"\n')
    old_exe = os.path.join(tmp, "trace_old")
    new_exe = os.path.join(tmp, "trace_new")
    cc(old_exe, [os.path.join(old, "trace_old.c")] + src("simhw.c", "esshw.c"),
       ["-DTRACE_OLD", "-I" + old, "-I" + SHIM], strict=False)
    cc(new_exe, [os.path.join(HOST, "t_trace.c")] +
       src("regs.c", "esshw.c", "simhw.c"), ["-I" + SHIM])
    _c1, _o1, trace_old = run(old_exe)
    _c2, _o2, trace_new = run(new_exe)
    if trace_old != trace_new or not trace_old:
        failures += 1
        a, b = trace_old.splitlines(), trace_new.splitlines()
        first = next((i for i, (x, y) in enumerate(zip(a, b)) if x != y),
                     min(len(a), len(b)))
        print("t_trace: port traces differ at access %d (%d vs %d accesses)"
              % (first, len(a), len(b)))
    else:
        print("t_trace: %d port accesses identical to essreg %s" %
              (len(trace_old.splitlines()), BASELINE))
    return failures


def main():
    suite = unittest.defaultTestLoader.discover(
        os.path.join(ROOT, "tests"), pattern="test_*.py",
        top_level_dir=ROOT)
    result = unittest.TextTestRunner(verbosity=1).run(suite)
    with tempfile.TemporaryDirectory() as tmp:
        failures = c_tests(tmp)
    ok = result.wasSuccessful() and failures == 0
    print("ALL TESTS PASSED" if ok else "TESTS FAILED")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
