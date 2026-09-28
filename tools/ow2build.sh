#!/bin/sh
# Builds the DOS and 16-bit Windows programs with Open Watcom v2 on Linux,
# into out/ow2/.  It also checks that the sources compile with a Watcom
# toolchain.  The programs in build/ were built this way, and build.bat
# builds the same programs with Watcom C 11.0.
#
# Usage:
#   `tools/ow2build.sh OPEN_WATCOM_DIR [wmake targets...]`
#
# (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
#
# Licensed under GPL Version 3.0
set -e
OW=${1:?usage: $0 OPEN_WATCOM_DIR [targets...]}
shift
ROOT=$(cd "$(dirname "$0")/.." && pwd)
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

cp -r "$ROOT/src" "$ROOT"/*.mk1 "$ROOT/Makefile" "$WORK/"
mkdir -p "$WORK/tools" "$WORK/bin"
cp "$ROOT/tools/nestamp.c" "$WORK/tools/"
# the makefile runs "nestamp essctl.exe": use a native build of it here
cc -O -o "$WORK/bin/nestamp" "$ROOT/tools/nestamp.c"
cd "$WORK"
# wmake on Linux wants forward slashes
# the DOS makefiles link Watcom 11's tiny-model startup (libf cstart_t) into
# small-model programs, and Open Watcom's won't start them, so they get the
# default startup here
sed -i -e 's#\\#/#g' -e 's/ libf cstart_t//' -e 's/\r$//' ./*.mk1 Makefile
export WATCOM="$OW" PATH="$WORK/bin:$OW/binl64:$PATH" INCLUDE="$OW/h"
wmake -h "$@"
mkdir -p "$ROOT/out/ow2"
for f in ./*.exe ./*.com; do
  [ -e "$f" ] && cp "$f" "$ROOT/out/ow2/"
done
ls -l "$ROOT/out/ow2"
