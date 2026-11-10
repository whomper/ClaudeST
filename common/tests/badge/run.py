#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""BADGECMP.PRG (make prg) on one of the family's machines: badge_draw
against the VDI's own drawing of the same badge, byte for byte, and the
times. Exit 1 when a badge differs.

    run.py WORK MACHINE [--nvdi]
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "speed"))
import speed  # noqa: E402

a = [x for x in sys.argv[1:] if not x.startswith("--")]
if len(a) < 2:
    sys.exit(__doc__)
r = speed.Rig(a[0], a[1], nvdi="--nvdi" in sys.argv, display=":93")
bad = 1
try:
    r.put(os.path.join(HERE, "build", "atari", "BADGECMP.PRG"))
    r.start("C:\\BADGECMP.PRG", ready="BDG done", boot=900)
    print("badges on", r.describe())
    for l in r.console().splitlines():
        if l.startswith("BDG ") and l != "BDG done":
            print("  " + l[4:])
            if l == "BDG all the same":
                bad = 0
finally:
    r.stop()
sys.exit(bad)
