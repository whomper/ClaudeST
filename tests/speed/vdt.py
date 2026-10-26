#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""What the VDI's drawing costs on one of the family's machines (the
guidelines' Speed): VDT.PRG (make prg) at boot, its "VDT" lines printed.

    vdt.py WORK MACHINE [--nvdi]
"""
import re
import sys
import time

import speed

a = [x for x in sys.argv[1:] if not x.startswith("--")]
if len(a) < 2:
    sys.exit(__doc__)
r = speed.Rig(a[0], a[1], nvdi="--nvdi" in sys.argv, display=":92")
try:
    r.put(speed.os.path.join(speed.HERE, "build", "atari", "VDT.PRG"))
    r.start("C:\\VDT.PRG", ready="VDT done")
    print("VDI on", r.describe())
    for l in r.console().splitlines():
        if l.startswith("VDT ") and l != "VDT done":
            print("  " + l[4:])
finally:
    r.stop()
