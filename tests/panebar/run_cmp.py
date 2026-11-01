#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""PBCMP.PRG (make cmp) on one of the family's machines: pb_draw's fast way
(pb_vdi) against objc_draw, byte for byte, and their times. Exit 1 when any
move differs or the fast way didn't come on.

    run_cmp.py WORK MACHINE [--nvdi] [--quick]
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "speed"))
import speed  # noqa: E402

a = [x for x in sys.argv[1:] if not x.startswith("--")]
if len(a) < 2:
    sys.exit(__doc__)
r = speed.Rig(a[0], a[1], nvdi="--nvdi" in sys.argv, display=":92")
bad = 1
try:
    r.put(os.path.join(HERE, "build", "atari", "PBCMP.PRG"))
    if "--quick" in sys.argv:          # the whole bars and the times only
        open(os.path.join(r.hd, "QUICK"), "w").close()
    r.start("C:\\PBCMP.PRG", ready="PBC done", boot=1800)    # an ST takes some 15 minutes for the moves
    print("pane bars on", r.describe())
    for l in r.console().splitlines():
        if l.startswith("PBC ") and l != "PBC done":
            print("  " + l[4:])
            if l == "PBC all the same":
                bad = 0
finally:
    r.stop()
sys.exit(bad)
