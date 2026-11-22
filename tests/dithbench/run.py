#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""DITHB.PRG (the pic.c before) and DITHA.PRG (this tree's), made by
make BEFORE=..., each on a boot of its own of one of the family's
machines: pic.c's dither on the CPU at 2, 4, 16 and 256 pens, each 3
times, and with this tree's at the shares 12 and 8 too. Prints them
side by side with the pens' sums; exit 1 when a sum at share 16 differs
(the same pens) or a program didn't end.

    run.py WORK MACHINE
"""
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "speed"))
import speed  # noqa: E402

pos = [x for x in sys.argv[1:] if not x.startswith("--")]
if len(pos) < 2:
    sys.exit(__doc__)
got = {}
desc = ""
for name in ("DITHB", "DITHA"):
    r = speed.Rig(os.path.join(pos[0], name), pos[1], display=os.environ.get("RIG_DISPLAY", ":95"))
    try:
        r.put(os.path.join(HERE, "build", name + ".PRG"))
        r.start("C:\\%s.PRG" % name, ready="DTB done", boot=900)
        desc = r.describe()
        got[name] = [l[4:] for l in r.console().splitlines() if l.startswith("DTB ")]
    finally:
        r.stop()
print("pic.c's dither on the CPU, on", desc)
rows = {}
bad = 0
for name in ("DITHB", "DITHA"):
    if "done" not in got.get(name, []):
        print("  %s didn't end" % name)
        bad = 1
    for l in got.get(name, []):
        m = re.match(r"(\d+) pens share (\d+): (\d+) (\d+) (\d+) ms, sum (\w+)", l)
        if m:
            rows.setdefault((int(m.group(1)), int(m.group(2))), {})[name] = (
                [int(m.group(i)) for i in (3, 4, 5)], m.group(6))
for (pens, share), v in sorted(rows.items()):
    b, a = v.get("DITHB"), v.get("DITHA")
    fmt = lambda t: "/".join(str(x) for x in t[0]) if t else "-"
    note = ""
    if share == 16 and b and a:
        note = "same pens" if a[1] == b[1] else "PENS DIFFER"
        bad |= a[1] != b[1]
    print("  %3d pens, share %2d: before %-14s after %-14s %s" % (pens, share, fmt(b) + " ms" if b else "-",
                                                               fmt(a) + " ms" if a else "-", note))
sys.exit(bad)
