#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
# ClaudeST's disk work against its budget (the guidelines, 6.5), counted
# from Hatari's GEMDOS trace:
#   start            the settings read once: 1 Fopen
#   20 replies       no file calls at all
#   idle             no file calls at all (IDLE seconds, 120 by default)
#   Save to disk     a 50 KB artifact: 1 Fcreate, written in one go
#                    (up to 4 Fwrites if only the 16 KB buffer is free)
#   Attach           a 40 KB file: 1 Fopen, read in 16 KB pieces (4 Freads
#                    with the end-of-file one)
#
#   tests/hatari/disk_budget.sh          the ST (4 MB); MACHINE=ST-1, TT...
# Needs binary/CLAUDEST.PRG (make) and Hatari; runs without a display.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
HERE=$ROOT/tests/hatari
MACHINE=${MACHINE:-ST}
IDLE=${IDLE:-120}
W=$(mktemp -d)
trap 'kill $(jobs -p) 2>/dev/null; rm -rf "$W"' EXIT

mkdir -p "$W/drive"
cp "$ROOT/binary/CLAUDEST.PRG" "$W/drive/"
printf '[OPTIONS]\r\nLINK=serial\r\n' > "$W/drive/CLAUDEST.INF"
case $MACHINE in Falcon*) printf 'SERIALPORT=6\r\n' >> "$W/drive/CLAUDEST.INF" ;; esac
head -c 40960 /dev/urandom > "$W/drive/BIG.TXT"
mkfifo "$W/out" "$W/in"
python3 "$HERE/disk_bridge.py" --backend demo --pipe "$W/out" "$W/in" > "$W/bridge.log" 2>&1 &
LINES=$(python3 "$ROOT/common/tools/machines.py" args "$MACHINE" "$W/drive")
ARGS=()
while IFS= read -r line; do ARGS+=("$line"); done <<< "$LINES"
HOME="$W/drive.home" SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy hatari "${ARGS[@]}" \
  --auto 'C:\CLAUDEST.PRG' --fast-boot yes --rs232-out "$W/out" --rs232-in "$W/in" \
  --cmd-fifo "$W/fifo" ${XARGS:-} --trace gemdos --trace-file "$W/trace" > "$W/hatari.log" 2>&1 &

key() { echo "hatari-event keypress $1" > "$W/fifo"; sleep "${2:-0.6}"; }
ctrl() { echo "hatari-event keydown 0x1d" > "$W/fifo"; sleep 0.3; key "$1" 0.3
         echo "hatari-event keyup 0x1d" > "$W/fifo"; sleep "${2:-2}"; }
mark() { wc -l < "$W/trace"; }
wait_for() {	# wait_for SECONDS COMMAND...
  local t=$1; shift
  until "$@"; do t=$((t - 1)); [ $t -gt 0 ] || return 1; sleep 1; done
}
count() { python3 "$HERE/disk_count.py" "$W/trace" "$@" || fail=1; }
fail=0

# online: the bridge has sent the usage after the Atari's HELLO
wait_for 180 grep -q "session usage" "$W/bridge.log" || { echo "ClaudeST didn't come online"; exit 1; }
sleep 3
a=$(mark); count 0 "$a" start Fopen=1 Fcreate=0 Fwrite=0

for i in $(seq 20); do key 0x23 0.3; key 0x17 0.3; key 0x1c 6; done	# "hi", Return
b=$(mark); count "$a" "$b" "20 replies" any=0

sleep "$IDLE"
c=$(mark); count "$b" "$c" "idle ${IDLE}s" any=0

# Artifacts (F4), the first one's menu (Tab, Insert), Save to disk..., OK
key 0x3e 8; key 0x0f; key 0x52 1.5; key 0x50; key 0x50; key 0x1c 4; key 0x1c 1
saved() { test "$(stat -c %s "$W/drive/BIG.BIN" 2>/dev/null)" = 51200; }
wait_for 120 saved || { echo "FAIL BIG.BIN not saved"; fail=1; }
python3 -c 'import sys; sys.stdout.buffer.write(bytes(i & 255 for i in range(51200)))' |
  cmp -s - "$W/drive/BIG.BIN" || { echo "FAIL BIG.BIN isn't what the bridge sent"; fail=1; }
sleep 3; key 0x1c 2		# the "Saved" alert
d=$(mark); count "$c" "$d" "Save to disk 50 KB" Fcreate=1 Fopen=0 Fwrite\<=4

# File > Attach file... (^O): the file selector, BIG.TXT typed in
ctrl 0x18 3; key 0x01; for k in 0x30 0x17 0x22 0x34 0x14 0x2d 0x14; do key $k 0.3; done
key 0x1c 1
wait_for 120 grep -q "attached BIG.TXT" "$W/bridge.log" || { echo "FAIL BIG.TXT not attached"; fail=1; }
sleep 2
e=$(mark); count "$d" "$e" "Attach 40 KB" Fopen=1 Fread\<=4 Fcreate=0

exit $fail
