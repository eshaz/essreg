#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
#
# MOTU's PC-MIDI Flyer on a parallel port that the BIOS leaves off: the port on
# through its ACPI node, until the next boot, then snd-flyer. As root. Never
# "auto" on the node, which picks resources of its own. The unit runs off the
# port, so the port goes back off with the driver, and when the driver doesn't
# load.
#
#   flyer-up.sh [MODULE.ko] [PARAMETER=VALUE...]   on, and the driver
#   flyer-up.sh off                                the driver out, port off
#
# (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>

set -u
OTHERS='^(parport_pc|snd_mtpav|snd_mqengine) '
NODE=
for d in /sys/bus/pnp/devices/*; do
	if grep -q PNP0401 "$d/id" 2>/dev/null; then
		NODE=$d
		break
	fi
done
if [ -z "$NODE" ]; then
	echo "flyer-up: no parallel port on PnP" >&2
	exit 2
fi

if [ "${1:-}" = off ]; then
	if grep -q '^snd_flyer ' /proc/modules; then
		rmmod snd_flyer || exit 1
	fi
	if grep -q -E "$OTHERS" /proc/modules; then
		echo "flyer-up: another driver has the port, which stays on" >&2
		exit 2
	fi
	echo disable > "$NODE/resources"
	exit 0
fi

if grep -q -E "$OTHERS" /proc/modules; then
	echo "flyer-up: parport_pc, snd-mtpav or snd-mqengine has the port" >&2
	exit 2
fi
if grep -q '^snd_flyer ' /proc/modules; then
	exit 0
fi
TURNED=
if ! grep -q '^state = active' "$NODE/resources" ||
   ! grep -q '^io 0x378' "$NODE/resources"; then
	echo disable > "$NODE/resources"
	echo clear > "$NODE/resources"
	echo "set io 0x378-0x37f io 0x778-0x77a irq 7 dma 3" > "$NODE/resources"
	echo activate > "$NODE/resources" || exit 1
	TURNED=1
fi
case "${1:-}" in
*.ko)
	insmod "$@"
	;;
*)
	modprobe snd-flyer "$@"
	;;
esac
ERR=$?
if [ "$ERR" -ne 0 ] && [ -n "$TURNED" ]; then
	echo disable > "$NODE/resources"
fi
exit "$ERR"
