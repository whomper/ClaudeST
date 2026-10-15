#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
# Every CLAUDEST.PRG that is committed must carry a new version number.
# Run before committing a new build: fails if binary/CLAUDEST.PRG differs
# from the last committed one but VERSION in st/claudest.c is the same.
cd "$(dirname "$0")/.." || exit 1
now=$(sed -n 's/^#define VERSION "\(.*\)"/\1/p' st/claudest.c)
was=$(git show HEAD:st/claudest.c 2>/dev/null | sed -n 's/^#define VERSION "\(.*\)"/\1/p')
# the last committed program, wherever it was kept then
old=$(git rev-parse -q --verify HEAD:binary/CLAUDEST.PRG || git rev-parse -q --verify HEAD:st/CLAUDEST.PRG)
new=$(git hash-object binary/CLAUDEST.PRG)
if [ "$old" != "$new" ] && [ "$now" = "$was" ]; then
  echo "CLAUDEST.PRG changed but VERSION is still $now: raise it in st/claudest.c" >&2
  exit 1
fi
echo "version $now"
