#!/usr/bin/env python3
# (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
#
# Licensed under GPL Version 3.0 or later
"""snd-flyer as a kernel patch: linux/flyer/ as sound/drivers/flyer/, sourced
from sound/drivers/Kconfig after snd-portman2x4 and built from
sound/drivers/Makefile after its subdirectories, against a kernel source tree.
Both places are away from snd-mtpav's, where mqengine-driver.patch adds
snd-mqengine, so that the two patches apply in either order. The format is
celeronix's (a description, then a -p1 unified diff).

usage: flyerpatch.py --ksrc KERNEL_TREE [-o flyer-driver.patch]
"""

import argparse
import os
import sys

from es1869patch import ROOT, diff, read

DRV = os.path.join(ROOT, "linux", "flyer")
FILES = ("Kconfig", "Makefile", "flyer.c", "proto.h")

HEADER = """snd-flyer: a driver for MOTU's PC-MIDI Flyer

Adds sound/drivers/flyer, snd-flyer, from essreg's linux/flyer
(https://github.com/eshaz/essreg). The unit is MOTU's MIDI interface for the
parallel port, powered by the port, with two MIDI inputs and two outputs,
which appear as one rawmidi device with a subdevice for each. It takes the
port's I/O and IRQ itself, as snd-mtpav does, so parport_pc has to stay out
of it.

"""


def hooks(ksrc):
    kconfig = read(os.path.join(ksrc, "sound/drivers/Kconfig"))
    makefile = read(os.path.join(ksrc, "sound/drivers/Makefile"))
    anchor = "\t  will be called snd-portman2x4.\n"
    if anchor not in kconfig or "flyer" in kconfig:
        sys.exit("sound/drivers/Kconfig: no snd-portman2x4 entry, "
                 "or flyer there")
    new_kconfig = kconfig.replace(
        anchor, anchor + '\nsource "sound/drivers/flyer/Kconfig"\n', 1)
    sub = "obj-$(CONFIG_SND) += opl3/ opl4/ mpu401/ vx/ pcsp/\n"
    if sub not in makefile or "flyer" in makefile:
        sys.exit("sound/drivers/Makefile: no subdirectory line, "
                 "or flyer there")
    new_makefile = makefile.replace(
        sub, sub + "obj-$(CONFIG_SND_FLYER) += flyer/\n", 1)
    return [("sound/drivers/Kconfig", kconfig, new_kconfig),
            ("sound/drivers/Makefile", makefile, new_makefile)]


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("--ksrc", required=True)
    ap.add_argument("-o", "--out", default="flyer-driver.patch")
    a = ap.parse_args(argv)
    parts = [HEADER]
    for name, old, new in hooks(a.ksrc):
        parts.append(diff(name, old, new))
    for rel in FILES:
        text = read(os.path.join(DRV, rel))
        parts.append(diff("sound/drivers/flyer/" + rel, "", text))
    with open(a.out, "w", encoding="utf-8") as f:
        f.write("".join(parts))
    print("%s: %d driver files" % (a.out, len(FILES)))


if __name__ == "__main__":
    main(sys.argv[1:])
