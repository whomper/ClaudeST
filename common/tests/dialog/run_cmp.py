#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""DLGCMP.PRG (make cmp) on one of the family's machines: dlg_draw's fast
way (the root's fill, flat G_BOXTEXTs and plain G_STRINGs by the CPU)
against objc_draw, byte for byte, and their times. Exit 1 when any draw
differs or the fast way didn't come on.

    run_cmp.py WORK MACHINE [--nvdi] [--look]
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
    r.put(os.path.join(HERE, "build", "cmp", "DLGCMP.PRG"))
    if "--look" in sys.argv:           # the AES's drawing of the frames' tree, to WORK/hd/LOOK.BIN
        open(os.path.join(r.hd, "LOOK"), "w").close()
    r.start("C:\\DLGCMP.PRG", ready="DLC done", boot=1800)
    print("dialogs on", r.describe())
    for l in r.console().splitlines():
        if l.startswith("DLC ") and l != "DLC done":
            print("  " + l[4:])
            if l == "DLC all the same":
                bad = 0
finally:
    r.stop()
sys.exit(bad)
