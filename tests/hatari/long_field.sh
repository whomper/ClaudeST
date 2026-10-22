#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
# A text field holds more than it shows (the guidelines, 5.4): Rename
# shows 43 characters of a name that holds 71. The rig types 60, goes
# to the start (Shift+Left) for an x and to the end (Shift+Right) for a
# y, and the bridge must get all 62.
#
#   tests/hatari/long_field.sh          the ST (4 MB); MACHINE=ST-1, Falcon...
# Needs binary/CLAUDEST.PRG (make) and Hatari; runs without a display.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
HERE=$ROOT/tests/hatari
MACHINE=${MACHINE:-ST}
W=$(mktemp -d)
fail=1
trap 'kill $(jobs -p) 2>/dev/null; wait 2>/dev/null; if [ $fail = 0 ]; then rm -rf "$W"; else echo "logs in $W"; fi' EXIT

mkdir -p "$W/drive"
cp "$ROOT/binary/CLAUDEST.PRG" "$W/drive/"
printf '[OPTIONS]\r\nLINK=serial\r\n' > "$W/drive/CLAUDEST.INF"
case $MACHINE in Falcon*) printf 'SERIALPORT=6\r\n' >> "$W/drive/CLAUDEST.INF" ;; esac
mkfifo "$W/out" "$W/in"
python3 "$HERE/long_field_bridge.py" --backend demo --pipe "$W/out" "$W/in" > "$W/bridge.log" 2>&1 &
LINES=$(python3 "$ROOT/common/tools/machines.py" args "$MACHINE" "$W/drive")
ARGS=()
while IFS= read -r line; do ARGS+=("$line"); done <<< "$LINES"
HOME="$W/drive.home" SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy hatari "${ARGS[@]}" \
  --auto 'C:\CLAUDEST.PRG' --fast-boot yes --rs232-out "$W/out" --rs232-in "$W/in" \
  --cmd-fifo "$W/fifo" ${XARGS:-} > "$W/hatari.log" 2>&1 &

key() { echo "hatari-event keypress $1" > "$W/fifo"; sleep "${2:-0.6}"; }
shifted() { echo "hatari-event keydown 0x2a" > "$W/fifo"; sleep 0.3; key "$1" 0.3
            echo "hatari-event keyup 0x2a" > "$W/fifo"; sleep 0.6; }
wait_for() {	# wait_for SECONDS COMMAND...
  local t=$1; shift
  until "$@"; do t=$((t - 1)); [ $t -gt 0 ] || return 1; sleep 1; done
}

wait_for 180 grep -q "session usage" "$W/bridge.log" || { echo "FAIL ClaudeST didn't come online in 180 s"; exit 1; }
sleep 3
# Chats (F2), the first one's menu (Tab, Insert), Rename...
key 0x3c 6; key 0x0f; key 0x52 1.5; key 0x50; key 0x50; key 0x50; key 0x1c 2
key 0x47			# Clr/Home: the old name gone
# 60 letters: a to z, a to z, a to h (the US layout's scancodes)
abc=(0x1e 0x30 0x2e 0x20 0x12 0x21 0x22 0x23 0x17 0x24 0x25 0x26 0x32
     0x31 0x18 0x19 0x10 0x13 0x1f 0x14 0x16 0x2f 0x11 0x2d 0x15 0x2c)
for i in $(seq 0 59); do key "${abc[$((i % 26))]}" 0.15; done
shifted 0x4b; key 0x2d		# Shift+Left, x
shifted 0x4d; key 0x15		# Shift+Right, y
key 0x1c 3
want="x$(printf '%s' abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefgh)y"
if wait_for 30 grep -q "renamed CHAT to \[$want\]" "$W/bridge.log"; then
  echo "ok   Rename holds ${#want} characters in a field that shows 43"
  fail=0
else
  echo "FAIL the bridge got: $(grep -o 'renamed CHAT to .*' "$W/bridge.log" || echo nothing)"
fi
exit $fail
