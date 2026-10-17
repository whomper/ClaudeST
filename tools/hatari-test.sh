#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
# Try ClaudeST in the Hatari emulator, with the bridge on this computer.
#
#   tools/hatari-test.sh                      demo chats, no account needed
#   tools/hatari-test.sh claudeai             your claude.ai account
#                                             (export CLAUDE_SESSION_KEY first)
#   MACHINE=Falcon tools/hatari-test.sh       one of the AERA test machines:
#                                             ST (the default), ST-1, TT, Falcon,
#                                             with a screen: ST:med, Falcon:256...
#                                             (common/tools/machines.py list)
#   LINK=network tools/hatari-test.sh         over STinG instead of the serial cable
#   HATARI=/path/to/hatari tools/hatari-test.sh   if Hatari isn't found by itself
#
# The machine, its TOS and its screen come from common/tools/machines.py:
# Erez's own TOS when ~/.aera/tos has it (AERA_TOS=folder, or emutos),
# else EmuTOS 1.4, fetched the first time.
#
# The emulated serial port is wired to the bridge through two named pipes.
# Hatari connects the ST-style MFP serial port this way: on the Falcon,
# ClaudeST is told to use it (SERIALPORT=6).
# Hatari has no network card: with LINK=network, common/tools/fakesting's
# FAKESTNG.PRG stands in for STinG and tunnels its TCP connections over
# that serial port to serial_bridge.py, which connects to the bridge on
# TCP port 2323 as the Atari would on the Pi. Building FAKESTNG needs the
# same cross-compiler as ClaudeST.
set -euo pipefail

BACKEND=${1:-demo}
ROOT=$(cd "$(dirname "$0")/.." && pwd)
WORK=${WORK:-$HOME/.claude-st-hatari}
MACHINE=${MACHINE:-ST}
LINK=${LINK:-serial}

# Find Hatari: $HATARI, a "hatari" command, or the macOS app bundle
find_hatari() {
  if [ -n "${HATARI:-}" ]; then
    echo "$HATARI"; return
  fi
  if command -v hatari >/dev/null; then
    command -v hatari; return
  fi
  local app bin
  for app in /Applications/Hatari*.app /Applications/*/Hatari*.app \
             "$HOME"/Applications/Hatari*.app "$HOME"/Applications/*/Hatari*.app \
             "$HOME"/Downloads/Hatari*.app "$HOME"/Downloads/*/Hatari*.app; do
    [ -d "$app/Contents/MacOS" ] || continue
    for bin in "$app"/Contents/MacOS/*; do
      [ -x "$bin" ] && { echo "$bin"; return; }
    done
  done
}
HATARI_BIN=$(find_hatari)
if [ -z "$HATARI_BIN" ]; then
  echo "Can't find Hatari. Install it from https://hatari.tuxfamily.org/download.html" >&2
  echo "  macOS: put Hatari.app in Applications (or run with HATARI=/path/to/Hatari.app/Contents/MacOS/Hatari)" >&2
  echo "  Linux: sudo apt install hatari" >&2
  exit 1
fi
echo "Using Hatari: $HATARI_BIN"

# the machine's Hatari arguments, one a line
ARGS=()
LINES=$(python3 "$ROOT/common/tools/machines.py" args "$MACHINE" "$WORK/drive") || exit 1
while IFS= read -r line; do ARGS+=("$line"); done <<< "$LINES"
case "$MACHINE" in
  Falcon*) EXTRA_INF="SERIALPORT=6\r\n${EXTRA_INF:-}" ;;
esac

mkdir -p "$WORK/drive/AUTO"
cp "$ROOT/binary/CLAUDEST.PRG" "$WORK/drive/"
rm -f "$WORK/drive/AUTO/FAKESTNG.PRG"
case "$LINK" in
  serial)
    printf "[OPTIONS]\r\nLINK=serial\r\n${EXTRA_INF:-}" > "$WORK/drive/CLAUDEST.INF" ;;
  network)
    make -s -C "$ROOT/common/tools/fakesting"
    cp "$ROOT/common/tools/fakesting/FAKESTNG.PRG" "$WORK/drive/AUTO/"
    printf "[OPTIONS]\r\nLINK=network\r\nHOST=127.0.0.1\r\nPORT=2323\r\n${EXTRA_INF:-}" \
      > "$WORK/drive/CLAUDEST.INF" ;;
  *)
    echo "LINK must be serial or network" >&2; exit 1 ;;
esac

if [ "$LINK" = network ]; then
  python3 "$ROOT/bridge/claude_bridge.py" --backend "$BACKEND" --tcp 127.0.0.1:2323 &
  BRIDGE=$!
  python3 "$ROOT/common/tools/fakesting/serial_bridge.py" "$WORK/st_out" "$WORK/st_in" &
  TUNNEL=$!
  trap 'kill $BRIDGE $TUNNEL 2>/dev/null' EXIT
else
  python3 "$ROOT/bridge/claude_bridge.py" --backend "$BACKEND" \
    --pipe "$WORK/st_out" "$WORK/st_in" &
  BRIDGE=$!
  trap 'kill $BRIDGE 2>/dev/null' EXIT
fi
sleep 1   # the bridge creates the pipes; Hatari must find them

# the machine's own HOME, so its NVRAM is its own (machines.py)
HOME="$WORK/drive.home" "$HATARI_BIN" "${ARGS[@]}" --auto 'C:\CLAUDEST.PRG' --fast-boot yes \
  --rs232-out "$WORK/st_out" --rs232-in "$WORK/st_in" "${@:2}"
