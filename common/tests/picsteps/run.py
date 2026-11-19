#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""PICSTEPS.PRG (make; ../../picovl's make first) on one of the family's
machines: PIC.OVL's check (pic_ovl_step) and chunky8's copies a call at
a time, the one before (C:\\B, --before FILE: by default the committed
pic.ovl) against picovl/build/pic.ovl (C:\\A), with each call's time and
the photo's screen compared byte for byte. Exit 1 when a draw differs or
the program didn't end.

    run.py WORK MACHINE [--nvdi] [--before FILE]
"""
import os
import shutil
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "speed"))
import speed  # noqa: E402

a = sys.argv[1:]
before = os.path.join(HERE, "..", "..", "picovl", "pic.ovl")
if "--before" in a:
    i = a.index("--before")
    before = a[i + 1]
    del a[i:i + 2]
pos = [x for x in a if not x.startswith("--")]
if len(pos) < 2:
    sys.exit(__doc__)
r = speed.Rig(pos[0], pos[1], nvdi="--nvdi" in a, display=":95")
bad = 1
try:
    r.put(os.path.join(HERE, "build", "PICSTEPS.PRG"))
    for d, f in (("B", before), ("A", os.path.join(HERE, "..", "..", "picovl", "build", "pic.ovl"))):
        os.makedirs(os.path.join(r.hd, d), exist_ok=True)
        shutil.copy(f, os.path.join(r.hd, d, "PIC.OVL"))
    r.start("C:\\PICSTEPS.PRG", ready="PST done", boot=900)
    print("PIC.OVL in steps on", r.describe())
    out = [l for l in r.console().splitlines() if l.startswith("PST ")]
    for l in out:
        print("  " + l[4:])
    bad = 0 if "PST done" in out and not any("DIFFER" in l for l in out) else 1
finally:
    r.stop()
sys.exit(bad)
