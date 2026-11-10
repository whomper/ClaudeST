#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""speedcmp.py: compares speed-rig logs of a base build and a candidate and
says which rows are really slower (guidelines 1.2, "The speed rule in steps").

    speedcmp.py --base A.log [A2.log ...] --cand B.log [B2.log ...] [--step 10]

A log is the output of a speed.py run (any app's: "speed on M, TOS", the
"seen on" and "finished picture on" tables). Every row is keyed by its
section and its name; with several logs a side its value is their median.

A row is a REGRESSION when the candidate's median is at least --step ms
(default 10: two of the rig's 5 ms steps) above the base's AND every
candidate run is above every base run. A row that is only one 5 ms step up,
or whose runs overlap, is NOISE and is not reported as slower. A row is NEWLY
OVER when the candidate's median is over its limit and the base's was not.
Rows that are the same or faster are counted, not listed (-v lists them).

Exit status: 1 when there is a regression or a newly-over row, else 0.
"""
import argparse
import re
import statistics
import sys

ROW = re.compile(r"^  (\S.*?)\s{2,}(<?\d+|\?)(?:\s+(\d+))?(?:\s+(ok|OVER))?\s*(.*)$")
SECTION = re.compile(r"^(speed on|seen on|finished picture on)\s+(.*)$")


def parse(path):
    """{(section, name): (value, limit)}; '<20' is 20, '?' is skipped."""
    rows = {}
    section = ""
    for line in open(path, errors="replace"):
        line = line.rstrip("\n")
        m = SECTION.match(line)
        if m:
            section = m.group(1)
            continue
        m = ROW.match(line)
        if not m or not section:
            continue
        name, val, lim = m.group(1).strip(), m.group(2), m.group(3)
        if name == "action" or val == "?":
            continue
        v = int(val.lstrip("<"))
        # 'seen on' rows carry rates/notes in the rest of the line, not a limit
        limit = int(lim) if lim and section == "speed on" else None
        rows[(section, name)] = (v, limit)
    return rows


def side(paths):
    runs = [parse(p) for p in paths]
    keys = set()
    for r in runs:
        keys |= set(r)
    out = {}
    for k in keys:
        vals = [r[k][0] for r in runs if k in r]
        lim = next((r[k][1] for r in runs if k in r and r[k][1] is not None), None)
        out[k] = (vals, lim)
    return out


def compare(base, cand, step):
    res = {"regression": [], "newly_over": [], "noise": [], "same_or_faster": 0, "new_rows": [], "gone_rows": []}
    for k in sorted(set(base) | set(cand)):
        if k not in cand:
            res["gone_rows"].append(k)
            continue
        if k not in base:
            res["new_rows"].append((k, statistics.median(cand[k][0]), cand[k][1]))
            continue
        bv, bl = base[k]
        cv, cl = cand[k]
        bm, cm = statistics.median(bv), statistics.median(cv)
        lim = cl if cl is not None else bl
        over_now = lim is not None and cm > lim
        over_before = lim is not None and bm > lim
        d = cm - bm
        if d >= step and min(cv) > max(bv):
            res["regression"].append((k, bm, cm, lim))
        elif over_now and not over_before:
            res["newly_over"].append((k, bm, cm, lim))
        elif d > 0:
            res["noise"].append((k, bm, cm, lim))
        else:
            res["same_or_faster"] += 1
    return res


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--base", nargs="+", required=True)
    ap.add_argument("--cand", nargs="+", required=True)
    ap.add_argument("--step", type=int, default=10)
    ap.add_argument("-v", action="store_true")
    a = ap.parse_args()
    res = compare(side(a.base), side(a.cand), a.step)

    def show(title, items):
        if items:
            print(title)
            for k, b, c, lim in items:
                print("  %-12s %-52s %6g -> %-6g %s" % (k[0].split()[0], k[1][:52], b, c,
                                                         "(limit %d)" % lim if lim else ""))
    show("REGRESSION (>= %d ms up, in every run):" % a.step, res["regression"])
    show("NEWLY OVER its limit:", res["newly_over"])
    if a.v:
        show("noise (up, but under %d ms or runs overlap):" % a.step, res["noise"])
    print("%d rows the same or faster, %d slightly up (noise), %d new rows, %d gone"
          % (res["same_or_faster"], len(res["noise"]), len(res["new_rows"]), len(res["gone_rows"])))
    return 1 if res["regression"] or res["newly_over"] else 0


if __name__ == "__main__":
    sys.exit(main())
