#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
# The version in st/claudest.c, and whether a release may carry it: fails
# when a release (a tag vX.Y) with that number is out already, so a
# changed program gets a new number (tools/bump.py). The release workflow
# runs it; run it before releasing.
cd "$(dirname "$0")/.." || exit 1
now=$(sed -n 's/^#define VERSION "\(.*\)"/\1/p' st/claudest.c)
if [ -z "$now" ]; then
  echo "no #define VERSION in st/claudest.c" >&2
  exit 1
fi
if git ls-remote --exit-code --tags origin "refs/tags/v$now" >/dev/null 2>&1; then
  echo "v$now is out already: raise VERSION in st/claudest.c (tools/bump.py)" >&2
  exit 1
fi
echo "version $now"
