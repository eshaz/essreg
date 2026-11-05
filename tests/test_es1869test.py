# (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
#
# Licensed under GPL Version 3.0 or later
"""es1869test, the Linux driver's loopback suite (linux/test), on its
simulated chip: every case passes or skips on the healthy model, each of
the model's faults fails the cases that look for it, the self-test passes,
the options give their exit codes, and the GMCHECK.MID it writes is
tools/gmcheck.py's file.

usage: python3 -m unittest tests.test_es1869test   (needs gcc and alsa-lib)
"""

import glob
import os
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SUITE = os.path.join(ROOT, "linux", "test")
sys.path.insert(0, os.path.join(ROOT, "tools"))

import gmcheck  # noqa: E402

# hal.h's SIM_* faults, and a case that each must fail
FAULTS = {
    0x0001: "a1rec.fmt.s16",       # Audio 1 a byte late
    0x0002: "dig.fmt.s16",         # Audio 2 silent
    0x0004: "a1rec.fmt.s16",       # Audio 2's sides swapped
    0x0008: "buf.delay",           # the delay without the FIFO
    0x0010: "a1rec.rate.48000",    # the data sheet's clocks
    0x0020: "mix.a2",              # a level a step off
    0x0040: "mix.capture",         # B4h's sides swapped
    0x0080: "fm.opl3.sides",       # fm_sides ignored
    0x0100: "dig.fmt.s16",         # Audio 2 loses frames
    0x0200: "pm.suspend",          # a resume loses the 3-D level
    0x0400: "load.cycle",          # a reload ignores its parameters
    0x0800: "hwvol.buttons",       # the master's control misses the buttons
    0x1000: "probe.card",          # the driver logs an error
    0x2000: "both.a1play",         # Audio 1's close silences Audio 2
    0x4000: "dig.drain.full",      # a drain plays the ring's old frames
    0x8000: "dac.corner.11025",    # the filter's corner ignores the rate
    0x10000: "dig.two",            # two periods lose a buffer
    0x20000: "dig.runout",         # the core's silence misses the ring
    0x40000: "pm.a1",              # a resume leaves Audio 1 off the mixer
    0x80000: "a1play.drain",       # a drain drops Audio 1's FIFO
    0x100000: "a1play.pausedrain",  # a drain from a pause loses a buffer
    0x200000: "a1rec.dc",          # the ADC is off by more than half a step
    0x400000: "beep.out",          # the PC speaker's level is ignored
    0x800000: "dig.trim",          # the rate correction changes nothing
    0x1000000: "dig.clock",        # 768 kHz's offset the wrong way
    0x2000000: "rs.exact",         # a ring frame off by 1 at a chunk's seam
    0x4000000: "rs.band",          # the filter's images
    0x8000000: "rs.live",          # a change waits for the stream's end
    0x10000000: "rs.clock.44",     # the Clock mode misses the steered clock
    0x20000000: "rs.treble",       # the hold's correction does nothing
    0x40000000: "rs.over.2",       # the ring stays at the stream's rate
}

# the cases that run only when named, or with someone at the machine
OPT_IN = "hwvol,pm,load,stress,3d.full,rs.clock.long44,rs.clock.long48"

_BUILT = {}


def suite():
    if "exe" not in _BUILT:
        tmp = _BUILT["tmp"] = tempfile.TemporaryDirectory()
        exe = os.path.join(tmp.name, "es1869test")
        # with essreg's 3-D measurement and its simulated effect
        sources = sorted(glob.glob(os.path.join(SUITE, "*.c"))) + [
            os.path.join(ROOT, "src", "s3dmeas.c"),
            os.path.join(ROOT, "src", "s3dsim.c"),
            os.path.join(ROOT, "linux", "es1869", "resample.c")]
        subprocess.run(["gcc", "-std=gnu99", "-Wall", "-Wextra", "-Werror",
                        "-O2", "-fno-math-errno", "-DESS_HOST",
                        "-I" + os.path.join(ROOT, "src"), "-o", exe] +
                       sources + ["-lasound", "-lpthread", "-lm"],
                       check=True, capture_output=True)
        _BUILT["exe"] = exe
    return _BUILT["exe"]


def run(*args):
    with tempfile.TemporaryDirectory() as out:
        res = subprocess.run([suite(), "-o", out] + list(args),
                             capture_output=True, text=True, timeout=600)
        return res.returncode, res.stdout


def results(out):
    """case id -> PASS, FAIL, KNOWN, SKIP or ERROR"""
    found, case = {}, None
    for line in out.splitlines():
        if line.startswith("case "):
            case = line[5:].split(":")[0]
        elif case and line.startswith("  ") and line.split()[0] in (
                "PASS", "FAIL", "KNOWN", "SKIP", "ERROR", "SKIP:"):
            found[case] = line.split()[0].rstrip(":")
            case = None
    return found


@unittest.skipUnless(shutil.which("gcc") and
                     os.path.exists("/usr/include/alsa/asoundlib.h"),
                     "needs gcc and alsa-lib")
class SimTest(unittest.TestCase):
    def test_healthy_chip(self):
        code, out = run("--sim")
        found = results(out)
        self.assertEqual(code, 0, out[-2000:])
        bad = {k: v for k, v in found.items() if v in ("FAIL", "ERROR")}
        self.assertEqual(bad, {})
        self.assertGreater(sum(v == "PASS" for v in found.values()), 50)

    def test_opt_in_cases(self):
        code, out = run("--sim", "-i", "--destructive", "-c", OPT_IN)
        found = results(out)
        self.assertEqual(code, 0, out[-2000:])
        for case in ("hwvol.buttons", "pm.suspend", "pm.stream", "pm.a1",
                     "load.cycle", "stress.reopen", "stress.all", "3d.full"):
            self.assertEqual(found.get(case), "PASS", out[-2000:])

    def test_each_fault_fails_its_case(self):
        for fault, case in FAULTS.items():
            with self.subTest(fault=hex(fault), case=case):
                code, out = run("--sim=%d" % fault, "-i", "--destructive",
                                "-c", case)
                self.assertEqual(code, 6, out[-1500:])
                self.assertEqual(results(out).get(case), "FAIL")

    def test_stock_skips_the_new_drivers_cases(self):
        code, out = run("--sim", "--stock", "-c",
                        "probe.card,dig.fmt.s16,3d.quick,a1play.fmt.s16")
        found = results(out)
        self.assertEqual(code, 0, out[-2000:])
        for case in ("probe.card", "dig.fmt.s16", "3d.quick"):
            self.assertEqual(found.get(case), "SKIP", case)
        self.assertEqual(found.get("cal.rec2"), "PASS")
        self.assertEqual(found.get("a1play.fmt.s16"), "PASS")

    def test_baseline(self):
        with tempfile.TemporaryDirectory() as base:
            res = subprocess.run([suite(), "--sim", "-q", "-o", base],
                                 capture_output=True, text=True, timeout=600)
            self.assertEqual(res.returncode, 0)
            tsv = os.path.join(base, "es1869test.tsv")
            # the same machine: nothing moves; a level a step off: it does
            code, out = run("--sim", "-q", "--baseline", tsv)
            self.assertIn("against the baseline: 0 of", out)
            code, out = run("--sim=%d" % 0x20, "-q", "--baseline", tsv)
            moved = [line for line in out.splitlines()
                     if line.startswith("against the baseline:")]
            self.assertTrue(moved and not moved[0].startswith(
                "against the baseline: 0 of"), out[-1500:])

    def test_self_test(self):
        res = subprocess.run([suite(), "--self-test"], capture_output=True,
                             text=True)
        self.assertEqual(res.returncode, 0, res.stdout)

    def test_options(self):
        self.assertEqual(subprocess.run([suite(), "--nonsense"],
                                        capture_output=True).returncode, 1)
        res = subprocess.run([suite(), "-l"], capture_output=True, text=True)
        self.assertEqual(res.returncode, 0)
        self.assertIn("dig.fmt.s16", res.stdout)
        res = subprocess.run([suite(), "--version"], capture_output=True,
                             text=True)
        self.assertTrue(res.stdout.startswith("es1869test "))

    def test_gmcheck_is_tools_file(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = os.path.join(tmp, "GMCHECK.MID")
            res = subprocess.run([suite(), "--gmcheck", path])
            self.assertEqual(res.returncode, 0)
            with open(path, "rb") as f:
                self.assertEqual(f.read(), gmcheck.smf(gmcheck.events()))


if __name__ == "__main__":
    unittest.main()
