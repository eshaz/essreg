#!/bin/sh
# Runs a 16-bit Windows program under Wine on a virtual X display, to click
# through it and take screenshots without a screen.  Needs 32-bit Wine,
# Xvfb, xdotool and ImageMagick (.claude/hooks/session-start.sh installs
# them).
#
# Usage:
#   `tools/wineshot.sh start DIR PROGRAM [ARGS...]`  run PROGRAM in DIR
#   `tools/wineshot.sh click X Y`                    click at X,Y
#   `tools/wineshot.sh key KEY`                      press KEY (xdotool names)
#   `tools/wineshot.sh shot FILE.png`                screenshot of the desktop
#   `tools/wineshot.sh stop`
#
# Example:
#   `tools/wineshot.sh start out/ow2 essctl.exe /sim`
#
# Win16 wants 8.3 path names, so DIR is reached through the link
# /tmp/wshot.  The Wine prefix is $WINEPREFIX, /tmp/wshot-prefix if unset.
#
# (c) 2026 Ethan Halsall <ethan.s.halsall@gmail.com>
#
# Licensed under GPL Version 3.0
set -e
LINK=/tmp/wshot
W=640
H=470
export WINEPREFIX="${WINEPREFIX:-/tmp/wshot-prefix}" WINEARCH=win32
export WINEDEBUG=-all DISPLAY="${WSHOT_DISPLAY:-:85}"

case "$1" in
start)
  dir=$(cd "${2:?usage: $0 start DIR PROGRAM [ARGS...]}" && pwd)
  prog="${3:?usage: $0 start DIR PROGRAM [ARGS...]}"
  shift 3
  # explorer doesn't look for the program in the current directory
  case "$prog" in
  *:* | *\\*) ;;
  *) prog="Z:\\tmp\\wshot\\$prog" ;;
  esac
  rm -f "$LINK"
  ln -s "$dir" "$LINK"
  Xvfb "$DISPLAY" -screen 0 "$((W + 20))x$((H + 20))x24" -nolisten tcp \
    > /dev/null 2>&1 &
  sleep 1.5
  # the first run makes the prefix
  [ -d "$WINEPREFIX" ] || wineboot -i > /dev/null 2>&1
  cd "$LINK"
  wine explorer "/desktop=essreg,${W}x$H" "$prog" "$@" > /dev/null 2>&1 &
  sleep 9
  ;;
click)
  xdotool mousemove "$2" "$3" click 1
  sleep 1
  ;;
key)
  xdotool key "$2"
  sleep 1
  ;;
shot)
  import -window root -crop "${W}x$H+0+0" "${2:?usage: $0 shot FILE.png}"
  ;;
stop)
  wineserver -k 2> /dev/null || true
  pkill -f "Xvfb $DISPLAY" || true
  rm -f "$LINK"
  ;;
*)
  echo "usage: $0 start DIR PROGRAM [ARGS...] | click X Y | key KEY |" \
    "shot FILE.png | stop" >&2
  exit 1
  ;;
esac
