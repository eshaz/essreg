#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
#
# An Opcode MIDIEngine 2Port/SE on a parallel port that the BIOS leaves off:
# the port on through its ACPI node, until the next boot, then snd-mqengine. As
# root. Never "auto" on the node, which picks resources of its own.
#
#   mqengine-up.sh [MODULE.ko] [PARAMETER=VALUE...]   on, and the driver
#   mqengine-up.sh off                                the driver out, port off
#
# (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>

set -u
NODE=
for d in /sys/bus/pnp/devices/*; do
	if grep -q PNP0401 "$d/id" 2>/dev/null; then
		NODE=$d
		break
	fi
done
if [ -z "$NODE" ]; then
	echo "mqengine-up: no parallel port on PnP" >&2
	exit 2
fi

if [ "${1:-}" = off ]; then
	if grep -q '^snd_mqengine ' /proc/modules; then
		rmmod snd_mqengine || exit 1
	fi
	echo disable > "$NODE/resources"
	exit 0
fi

if grep -q -E '^(parport_pc|snd_mtpav) ' /proc/modules; then
	echo "mqengine-up: parport_pc or snd-mtpav has the port" >&2
	exit 2
fi
if ! grep -q '^state = active' "$NODE/resources" ||
   ! grep -q '^io 0x378' "$NODE/resources"; then
	echo disable > "$NODE/resources"
	echo clear > "$NODE/resources"
	echo "set io 0x378-0x37f io 0x778-0x77a irq 7 dma 3" > "$NODE/resources"
	echo activate > "$NODE/resources" || exit 1
fi
if grep -q '^snd_mqengine ' /proc/modules; then
	exit 0
fi
case "${1:-}" in
*.ko)
	exec insmod "$@"
	;;
esac
exec modprobe snd-mqengine "$@"
