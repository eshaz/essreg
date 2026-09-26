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
cd "$WORK"
# wmake on Linux wants forward slashes and explicit object extensions
sed -i -e 's#\\#/#g' -e 's/libf cstart_t\([^.]\)/libf cstart_t.obj\1/' \
    -e 's/\r$//' ./*.mk1 Makefile
export WATCOM="$OW" PATH="$OW/binl64:$PATH" INCLUDE="$OW/h"
wmake -h "$@"
mkdir -p "$ROOT/out/ow2"
for f in ./*.exe ./*.com; do
  [ -e "$f" ] && cp "$f" "$ROOT/out/ow2/"
done
ls -l "$ROOT/out/ow2"
