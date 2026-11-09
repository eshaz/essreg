#!/usr/bin/env python3
# (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
#
# Licensed under GPL Version 3.0 or later
"""The ES1869 driver as a kernel patch: linux/es1869/ as sound/isa/es1869/,
sourced from sound/isa/Kconfig after snd-es18xx and built from
sound/isa/Makefile, against a kernel source tree. The format is celeronix's
(a description, then a -p1 unified diff).

usage: es1869patch.py --ksrc KERNEL_TREE [-o es1869-driver.patch]
"""

import argparse
import difflib
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DRV = os.path.join(ROOT, "linux", "es1869")
SKIP_EXT = (".o", ".ko", ".mod", ".cmd", ".symvers", ".order", ".a")

HEADER = """snd-es1869: a driver for the ESS ES1869 AudioDrive, every feature

Adds sound/isa/es1869, snd-es1869 and snd-es1869-esfm, from essreg's
linux/es1869 (https://github.com/eshaz/essreg). It takes the devices of
snd-es18xx, which stays built and is blacklisted where snd-es1869 is used:
* both DACs, Audio 2 on a 16-bit DMA channel through a ring of words, as
  ESS's drivers play it, where snd-es18xx's is silent;
* full duplex, every rate of the chip's clocks, the record mixer, the 3-D
  effect, the DAC stages, the hardware volume, power save;
* the music DAC recorded digitally (PCM device 1), dual playback (device
  2), what Audio 2's DAC took (device 3), OPL3 and a native ESFM GM synth
  with ESS's bank as firmware;
* Audio 2 resampled in the driver: its rate locked to the clock NTP steers
  (the Clock mode, the default), its output at 2x or 3x, and its DAC's
  treble taken back by a table that loads under a playing stream; Audio
  1's recordings through a correction filter whose taps load the same way;
* on PIIX4 machines whose BIOS sets Type F DMA timing on Audio 1's channel,
  that timing turned off, since a recording then hangs the machine;
* every setting a module parameter writable at runtime, an ALSA control
  where it is chip state, and a Kconfig default.

"""


def driver_files():
    out = []
    for base, dirs, files in os.walk(DRV):
        dirs[:] = sorted(d for d in dirs if not d.startswith("."))
        for name in sorted(files):
            if name.startswith(".") or name.endswith(SKIP_EXT) or \
                    name.endswith(".mod.c"):
                continue
            out.append(os.path.relpath(os.path.join(base, name), DRV))
    return out


def read(path):
    with open(path, encoding="utf-8") as f:
        return f.read()


def diff(name, old, new):
    lines = difflib.unified_diff(old.splitlines(True), new.splitlines(True),
                                 "a/" + name if old else "/dev/null",
                                 "b/" + name)
    return "".join(lines)


def hooks(ksrc):
    kconfig = read(os.path.join(ksrc, "sound/isa/Kconfig"))
    makefile = read(os.path.join(ksrc, "sound/isa/Makefile"))
    anchor = "\t  will be called snd-es18xx.\n"
    if anchor not in kconfig or "es1869" in kconfig:
        sys.exit("sound/isa/Kconfig: no snd-es18xx entry, or es1869 there")
    new_kconfig = kconfig.replace(
        anchor, anchor + '\nsource "sound/isa/es1869/Kconfig"\n', 1)
    sub = "obj-$(CONFIG_SND) += ad1816a/ ad1848/ cs423x/ es1688/ "
    if sub not in makefile or "es1869" in makefile:
        sys.exit("sound/isa/Makefile: no subdirectory list, or es1869 there")
    new_makefile = makefile.replace(sub, sub + "es1869/ ", 1)
    return [("sound/isa/Kconfig", kconfig, new_kconfig),
            ("sound/isa/Makefile", makefile, new_makefile)]


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("--ksrc", required=True)
    ap.add_argument("-o", "--out", default="es1869-driver.patch")
    a = ap.parse_args(argv)
    parts = [HEADER]
    for name, old, new in hooks(a.ksrc):
        parts.append(diff(name, old, new))
    for rel in driver_files():
        text = read(os.path.join(DRV, rel))
        parts.append(diff("sound/isa/es1869/" + rel, "", text))
    with open(a.out, "w", encoding="utf-8") as f:
        f.write("".join(parts))
    print("%s: %d driver files" % (a.out, len(driver_files())))


if __name__ == "__main__":
    main(sys.argv[1:])
