#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""speedcmp.py on made-up logs: a real regression, a one-step noise row, a row
newly over its limit, a faster row, a new row; and the median of two runs."""
import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
TOOL = os.path.join(HERE, "..", "..", "tools", "speedcmp.py")

BASE = """speed on Falcon-4:16, TOS 4.04
  action                                    longest     time
  started                                       450     2000  ok
  slow row                                      100      150  ok
  noise row                                      40       50  ok
  edge row                                      148      150  ok
  faster row                                     90      150  ok
seen on Falcon-4:16, TOS 4.04 (the window whole, ms from the key)
  a line scrolled                                65
"""
CAND = """speed on Falcon-4:16, TOS 4.04
  action                                    longest     time
  started                                       450     2000  ok
  slow row                                      125      150  ok
  noise row                                      45       50  ok
  edge row                                      152      150  OVER
  faster row                                     80      150  ok
  new row                                        10       50  ok
seen on Falcon-4:16, TOS 4.04 (the window whole, ms from the key)
  a line scrolled                                65
"""
fails = 0


def check(cond, what):
    global fails
    print("%s %s" % ("ok  " if cond else "FAIL", what))
    if not cond:
        fails += 1


def run(base, cand, *extra):
    d = tempfile.mkdtemp()
    bp, cp = [], []
    for i, t in enumerate(base):
        p = os.path.join(d, "b%d.log" % i)
        open(p, "w").write(t)
        bp.append(p)
    for i, t in enumerate(cand):
        p = os.path.join(d, "c%d.log" % i)
        open(p, "w").write(t)
        cp.append(p)
    r = subprocess.run([sys.executable, "-I", TOOL, "--base", *bp, "--cand", *cp, *extra],
                       capture_output=True, text=True)
    return r.returncode, r.stdout


rc, out = run([BASE], [CAND])
check(rc == 1, "a regression or a newly-over row gives status 1")
check("slow row" in out.split("NEWLY OVER")[0], "100 -> 125 is a regression")
check("edge row" in out.split("NEWLY OVER")[1], "148 -> 152 over 150 is newly over")
check("noise row" not in out, "40 -> 45 is one step: noise, not listed")
check("faster row" not in out and "  new row  " not in out, "faster and new rows are not listed")
check("1 new rows" in out, "the new row is counted")
rc, out = run([BASE], [BASE])
check(rc == 0 and "REGRESSION" not in out, "the same log twice is clean")
# two runs a side: the median decides, overlapping runs are noise
c2 = CAND.replace("slow row                                      125", "slow row                                       95")
rc, out = run([BASE, BASE], [CAND, c2])
check("slow row" not in out.split("NEWLY OVER")[0], "runs that overlap the base are not a regression")
print("speedcmp: %d failed" % fails)
sys.exit(1 if fails else 0)
