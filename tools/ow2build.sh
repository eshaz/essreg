#!/bin/sh
# Build the DOS and 16-bit Windows programs with Open Watcom v2 on Linux.
# This is a check that the sources compile with a Watcom toolchain; the
# release binaries in build/ are built with build.bat.
#
# usage: tools/ow2build.sh OPEN_WATCOM_DIR [wmake targets...]
# Output goes to out/ow2/.
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
# wmake on Linux wants forward slashes.  The DOS makefiles link Watcom 11's
# tiny-model startup (libf cstart_t) into small-model programs; Open
# Watcom's does not start them, so they get the default startup here.
sed -i -e 's#\\#/#g' -e 's/ libf cstart_t//' -e 's/\r$//' ./*.mk1 Makefile
export WATCOM="$OW" PATH="$WORK/bin:$OW/binl64:$PATH" INCLUDE="$OW/h"
wmake -h "$@"
mkdir -p "$ROOT/out/ow2"
for f in ./*.exe ./*.com; do
  [ -e "$f" ] && cp "$f" "$ROOT/out/ow2/"
done
ls -l "$ROOT/out/ow2"
