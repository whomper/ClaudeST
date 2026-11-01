#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""OFFCMP.PRG (make) on one of the family's machines: a piece of the
screen drawn through offdraw.c against the same on the screen, byte for
byte, the screen unchanged until off_end, and the times. Exit 1 when a
piece differs, showed before off_end, or the way off the screen didn't
come on.

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
r = speed.Rig(a[0], a[1], nvdi="--nvdi" in sys.argv, display=":94")
bad = 1
try:
    r.put(os.path.join(HERE, "build", "OFFCMP.PRG"))
    r.start("C:\\OFFCMP.PRG", ready="OFC done", boot=900)
    print("off the screen on", r.describe())
    for l in r.console().splitlines():
        if l.startswith("OFC ") and l != "OFC done":
            print("  " + l[4:])
            if l == "OFC all the same":
                bad = 0
finally:
    r.stop()
sys.exit(bad)
