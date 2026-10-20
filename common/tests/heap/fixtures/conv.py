# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
# conv.py hatari.log out.txt: the H trace as ops on block ids
import sys, re
live = {}; nid = 0; out = []; area = None
for line in open(sys.argv[1], errors="replace"):
    m = re.match(r"H([AMFR]) ([0-9a-f]{8}) ([0-9a-f]{8}) ([0-9a-f]{8})", line)
    if not m:
        if line.startswith("HX failure"):
            out.append("X")
        continue
    op, a, b, n = m.group(1), int(m.group(2), 16), int(m.group(3), 16), int(m.group(4), 16)
    if op == "A":
        area = n; out.append("A %d" % n)
    elif op == "M":
        if a:
            nid += 1; live[a] = nid; out.append("M %d %d" % (nid, n))
        else:
            out.append("M 0 %d" % n)
    elif op == "F":
        if a in live:
            out.append("F %d" % live.pop(a))
    elif op == "R":
        old = live.pop(a, 0) if a else 0
        if b:
            nid += 1; live[b] = nid
            out.append("R %d %d %d" % (old, nid, n))
        else:
            if a: live[a] = old
            out.append("R %d 0 %d" % (old, n))
open(sys.argv[2], "w").write("\n".join(out) + "\n")
print(len(out), "ops,", nid, "blocks")
