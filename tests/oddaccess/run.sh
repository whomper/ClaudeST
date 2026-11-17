#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
# common/tests/oddaccess/run.sh: oddaccess.py against switch.s (2 expected).
set -e
d=$(dirname "$0"); t=$(mktemp -d)
m68k-linux-gnu-as -m68000 "$d/switch.s" -o "$t/s.o"
m68k-linux-gnu-ld -e f "$t/s.o" -o "$t/s.elf"
out=$(python3 "$d/../../tools/oddaccess.py" "$t/s.elf" || true)
rm -rf "$t"
echo "$out"
echo "$out" | grep -q "g+0x10" && echo "$out" | grep -q "^2 odd" && echo "ok" || { echo "FAILED"; exit 1; }
