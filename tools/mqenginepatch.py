#!/usr/bin/env python3
# (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
#
# Licensed under GPL Version 3.0 or later
"""snd-mqengine as a kernel patch: linux/mqengine/ as sound/drivers/mqengine/,
sourced from sound/drivers/Kconfig after snd-mtpav and built from
sound/drivers/Makefile, against a kernel source tree. The format is celeronix's
(a description, then a -p1 unified diff).

usage: mqenginepatch.py --ksrc KERNEL_TREE [-o mqengine-driver.patch]
"""

import argparse
import os
import sys

from es1869patch import ROOT, diff, read

DRV = os.path.join(ROOT, "linux", "mqengine")
FILES = ("Kconfig", "Makefile", "mqengine.c", "proto.h")

HEADER = """snd-mqengine: a driver for the Opcode MIDIEngine 2Port/SE

Adds sound/drivers/mqengine, snd-mqengine, from essreg's linux/mqengine
(https://github.com/eshaz/essreg). The unit is a Music Quest design on the
parallel port with two MIDI inputs and two outputs, which appear as one
rawmidi device with a subdevice for each. It takes the port's I/O and IRQ
itself, as snd-mtpav does, so parport_pc has to stay out of it.

"""


def hooks(ksrc):
    kconfig = read(os.path.join(ksrc, "sound/drivers/Kconfig"))
    makefile = read(os.path.join(ksrc, "sound/drivers/Makefile"))
    anchor = "\t  will be called snd-mtpav.\n"
    if anchor not in kconfig or "mqengine" in kconfig:
        sys.exit("sound/drivers/Kconfig: no snd-mtpav entry, or mqengine there")
    new_kconfig = kconfig.replace(
        anchor, anchor + '\nsource "sound/drivers/mqengine/Kconfig"\n', 1)
    sub = "obj-$(CONFIG_SND_MTPAV) += snd-mtpav.o\n"
    if sub not in makefile or "mqengine" in makefile:
        sys.exit("sound/drivers/Makefile: no snd-mtpav entry, or mqengine there")
    new_makefile = makefile.replace(
        sub, sub + "obj-$(CONFIG_SND_MQENGINE) += mqengine/\n", 1)
    return [("sound/drivers/Kconfig", kconfig, new_kconfig),
            ("sound/drivers/Makefile", makefile, new_makefile)]


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("--ksrc", required=True)
    ap.add_argument("-o", "--out", default="mqengine-driver.patch")
    a = ap.parse_args(argv)
    parts = [HEADER]
    for name, old, new in hooks(a.ksrc):
        parts.append(diff(name, old, new))
    for rel in FILES:
        text = read(os.path.join(DRV, rel))
        parts.append(diff("sound/drivers/mqengine/" + rel, "", text))
    with open(a.out, "w", encoding="utf-8") as f:
        f.write("".join(parts))
    print("%s: %d driver files" % (a.out, len(FILES)))


if __name__ == "__main__":
    main(sys.argv[1:])
