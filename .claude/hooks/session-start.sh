#!/bin/bash
# Sets up a Claude Code on the web session with the tools essreg's builds
# and tests use:
# - nasm (the drivers), gcc (the host C tests) and curl
# - the unicorn Python module (the CPU emulator tests), flake8 and
#   clang-format (the linters)
# - Open Watcom v2 in /opt/open-watcom (the DOS and 16-bit Windows programs)
# - 32-bit Wine, Xvfb, xdotool and ImageMagick (tests/test_wine.py and
#   tools/wineshot.sh)
#
# Exports OW2 and ESSREG_WINE=1 for the session, so tests/run_tests.py runs
# every test.  Each step is skipped when its tool is already there, so the
# hook is safe to run again.  Only runs on the web (CLAUDE_CODE_REMOTE).
#
# Usage:
#   `CLAUDE_CODE_REMOTE=true .claude/hooks/session-start.sh`
#
# (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
#
# Licensed under GPL Version 3.0
set -euo pipefail

if [ "${CLAUDE_CODE_REMOTE:-}" != "true" ]; then
  exit 0
fi

OW2_DIR=${OW2_DIR:-/opt/open-watcom}
OW2_URL=${OW2_URL:-https://github.com/open-watcom/open-watcom-v2/releases/download/Current-build/ow-snapshot.tar.xz}
UNICORN=unicorn==2.1.4
LOG=${TMPDIR:-/tmp}/essreg-setup.log
export DEBIAN_FRONTEND=noninteractive
: > "$LOG"

# the steps' output goes to the log, stdout only gets the summary
step() {
  if ! "$@" >> "$LOG" 2>&1; then
    echo "essreg setup: '$*' failed, see $LOG" >&2
    tail -n 15 "$LOG" >&2
    return 1
  fi
}

# true when one of the packages isn't installed
missing() {
  local pkg
  for pkg in "$@"; do
    dpkg -s "$pkg" > /dev/null 2>&1 || return 0
  done
  return 1
}

# true when one of the commands isn't there
no_cmd() {
  local cmd
  for cmd in "$@"; do
    command -v "$cmd" > /dev/null || return 0
  done
  return 1
}

updated=0
apt_update() {
  if [ "$updated" = 0 ]; then
    # some preconfigured PPAs are unreachable, apt still updates the rest
    step apt-get update -q || true
    updated=1
  fi
}

apt_install() {
  step apt-get install -y -q --no-install-recommends "$@"
}

pip_install() {
  step python3 -m pip install -q "$@" ||
    step python3 -m pip install -q --break-system-packages "$@"
}

# the builds and tests need these, a failure stops here
if no_cmd nasm gcc curl xz clang-format; then
  apt_update
  apt_install nasm gcc curl xz-utils clang-format
fi
if ! python3 -c 'import unicorn' 2> /dev/null; then
  pip_install "$UNICORN"
fi
if no_cmd flake8; then
  pip_install flake8
fi

# Open Watcom and Wine are optional: their tests skip without them
ow2=0
if [ -x "$OW2_DIR/binl64/wcc" ]; then
  ow2=1
else
  tmp=$(mktemp -d)
  if step curl -fsSL --retry 4 -o "$tmp/ow.tar.xz" "$OW2_URL" &&
      step mkdir -p "$OW2_DIR" &&
      step tar -xJf "$tmp/ow.tar.xz" -C "$OW2_DIR" ./binl64 ./h ./lib286 \
        ./readme.txt; then
    ow2=1
  else
    echo "essreg setup: no Open Watcom, its tests will skip" >&2
  fi
  rm -rf "$tmp"
fi

wine=0
if ! missing wine32:i386 && ! no_cmd wine Xvfb xdotool import; then
  wine=1
else
  if ! dpkg --print-foreign-architectures | grep -qx i386; then
    step dpkg --add-architecture i386 || true
    updated=0
  fi
  apt_update
  # libgd3:i386 on its own first: it brings the amd64 libraries it shares
  # with the i386 ones to the same versions, else wine32 can't install
  if apt_install libgd3:i386 &&
      apt_install wine32:i386 wine xvfb xdotool imagemagick; then
    wine=1
  else
    echo "essreg setup: no Wine, its tests will skip" >&2
  fi
fi

if [ -n "${CLAUDE_ENV_FILE:-}" ]; then
  if [ "$ow2" = 1 ] && ! grep -qs "^export OW2=" "$CLAUDE_ENV_FILE"; then
    echo "export OW2=$OW2_DIR" >> "$CLAUDE_ENV_FILE"
  fi
  if [ "$ow2" = 1 ] && [ "$wine" = 1 ] &&
      ! grep -qs "^export ESSREG_WINE=" "$CLAUDE_ENV_FILE"; then
    echo "export ESSREG_WINE=1" >> "$CLAUDE_ENV_FILE"
  fi
fi

echo "essreg setup: $(nasm -v | cut -d' ' -f1-3)," \
  "unicorn $(python3 -c 'import unicorn; print(unicorn.__version__)')," \
  "Open Watcom: $([ "$ow2" = 1 ] && echo "$OW2_DIR" || echo none)," \
  "Wine: $([ "$wine" = 1 ] && wine --version 2> /dev/null || echo none)"
