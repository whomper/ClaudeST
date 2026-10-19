#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""Count ClaudeST's own GEMDOS file calls in a slice of Hatari's
--trace gemdos output, and check them against a budget (the guidelines,
6.5). Calls from TOS itself (its PC in ROM, such as the file selector's
Fsfirst) aren't ClaudeST's and aren't counted.

    disk_count.py TRACE FROM TO JOB Fopen=1 Fwrite<=4 ...

FROM and TO are line numbers (TO 0: the end). Exit 1 over budget."""
import re
import sys

CALLS = ("Fopen", "Fcreate", "Fsfirst", "Fsnext", "Fattrib", "Fread", "Fwrite",
         "Fdelete", "Frename", "Fseek", "Fclose")
LINE = re.compile(r"^GEMDOS 0x[0-9A-Fa-f]+ (\w+)\(.*at PC[ =]0x([0-9A-Fa-f]+)")


def count(lines):
    n = dict.fromkeys(CALLS, 0)
    for line in lines:
        m = LINE.match(line)
        if m and m.group(1) in n and int(m.group(2), 16) < 0xE00000:
            n[m.group(1)] += 1
    return n


def main(argv):
    trace, start, end, job = argv[1], int(argv[2]), int(argv[3]), argv[4]
    with open(trace, errors="replace") as f:
        lines = f.read().splitlines()
    n = count(lines[start:end or len(lines)])
    ok = True
    for rule in argv[5:]:
        m = re.match(r"(\w+)(<=|=)(\d+)$", rule)
        name, op, want = m.group(1), m.group(2), int(m.group(3))
        names = CALLS if name == "any" else (name,)
        got = sum(n[c] for c in names)
        if not (got <= want if op == "<=" else got == want):
            print("FAIL %s: %s %d, budget %s%d" % (job, name, got, op, want))
            ok = False
    print("%s %s: %s" % ("ok  " if ok else "FAIL", job,
                         ", ".join("%s %d" % (c, k) for c, k in n.items() if k) or "no file calls"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
