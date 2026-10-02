#!/bin/bash
# Try Claude ST in the Hatari emulator, with the bridge on this computer.
#
#   tools/hatari-test.sh                      demo chats, no account needed
#   tools/hatari-test.sh claudeai             your claude.ai account
#                                             (export CLAUDE_SESSION_KEY first)
#   TOS=/path/to/tos.img tools/hatari-test.sh use your own TOS/EmuTOS image
#   MACHINE=ste tools/hatari-test.sh          st (default), ste or tt
#   HATARI=/path/to/hatari tools/hatari-test.sh   if Hatari isn't found by itself
#
# The emulated serial port is wired to the bridge through two named pipes.
# Hatari only connects the ST/STE/TT serial port this way, not the Falcon's.
set -euo pipefail

BACKEND=${1:-demo}
ROOT=$(cd "$(dirname "$0")/.." && pwd)
WORK=${WORK:-$HOME/.claude-st-hatari}
MACHINE=${MACHINE:-st}

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

mkdir -p "$WORK/drive"
cp "$ROOT/st/CLAUDE.PRG" "$WORK/drive/"
# serial mode: the drive must not carry the network CLAUDE.INF
printf 'serial\r\n' > "$WORK/drive/CLAUDE.INF"

if [ -z "${TOS:-}" ]; then
  TOS="$WORK/etos512us.img"
  if [ ! -f "$TOS" ]; then
    echo "Downloading EmuTOS (free TOS replacement)..."
    curl -fsSL -o "$WORK/emutos.zip" \
      "https://downloads.sourceforge.net/project/emutos/emutos/1.3/emutos-512k-1.3.zip"
    unzip -o -q -j "$WORK/emutos.zip" "*/etos512us.img" -d "$WORK"
  fi
fi

python3 "$ROOT/bridge/claude_bridge.py" --backend "$BACKEND" \
  --pipe "$WORK/st_out" "$WORK/st_in" &
BRIDGE=$!
trap 'kill $BRIDGE 2>/dev/null' EXIT
sleep 1   # the bridge creates the pipes; Hatari must find them

case "$MACHINE" in
  st)  DISPLAY_OPTS=(--mono) ;;
  *)   DISPLAY_OPTS=(--monitor rgb --tos-res med) ;;
esac

"$HATARI_BIN" --machine "$MACHINE" "${DISPLAY_OPTS[@]}" --tos "$TOS" \
  --harddrive "$WORK/drive" --auto 'C:\CLAUDE.PRG' --fast-boot yes \
  --rs232-out "$WORK/st_out" --rs232-in "$WORK/st_in" "${@:2}"
