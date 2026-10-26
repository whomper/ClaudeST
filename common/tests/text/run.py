#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""TXT.PRG (make prg) on one of the family's machines: ui/text.c's text
against the VDI's own, byte for byte, and their times. Exit 1 when any
check differs.

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
r = speed.Rig(a[0], a[1], nvdi="--nvdi" in sys.argv, display=":91")
bad = 1
try:
    r.put(os.path.join(HERE, "build", "atari", "TXT.PRG"))
    r.start("C:\\TXT.PRG", ready="TXT done")
    print("text on", r.describe())
    for l in r.console().splitlines():
        if l.startswith("TXT ") and l != "TXT done":
            print("  " + l[4:])
            if l.endswith(" 0 differ"):
                bad = 0
finally:
    r.stop()
sys.exit(bad)
