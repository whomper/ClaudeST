#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""Each build names the other package (the guidelines, 7.4: two packages,
the same file names; common's cpucheck.c and cpucheck_cf.S):

  1. The standard CLAUDEST.PRG on the FireBee stand-in (_CF_ in the jar):
     its cpu_cf_alert, naming the FireBee package; OK, and the desktop
     again, ClaudeST never started (no first event call, the INF untouched).
  2. The FireBee's CLAUDEST.PRG on a Falcon (no _CF_; under NVDI where
     Erez's copy is here, the guidelines, 1.2): its cpu_alert, naming
     the standard package, the same way. A ColdFire program can't run here;
     its first code, cpucheck_cf.S, is the same bytes for the 68000 and the
     ColdFire, so the Falcon runs it up to the alert.
  3. The FireBee's CLAUDEST.PRG on an ST (TOS 1.04, a 68000): the same.

Each alert's picture is kept (WORK/*-alert.png), and is checked against the
desktop's after OK: it changed, then went. No CRASH.LOG anywhere.

    python3 tests/hatari/firebee_test.py WORK [--st PRG] [--fb PRG]   (--fb: binary/fb/CLAUDEST.PRG)
"""
import argparse
import os
import shutil
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, os.path.join(ROOT, "common", "tests", "speed"))
import speed  # noqa: E402

fails = 0


def check(ok, what, detail=""):
    global fails
    fails += not ok
    print("%s %s%s" % ("ok   " if ok else "FAIL ", what, ("  (" + detail + ")") if detail else ""), flush=True)


def differ(a, b):
    r = subprocess.run(["compare", "-metric", "AE", a, b, "null:"], capture_output=True, text=True)
    try:
        return int(float(r.stderr.split()[0])) > 0
    except (ValueError, IndexError):
        return False


def case(work, name, machine, prg, file_name, nvdi=False):
    w = os.path.join(work, name)
    shutil.rmtree(w, ignore_errors=True)
    r = speed.Rig(w, machine, nvdi=nvdi, extra=["--drive-led", "no"])
    p = os.path.join(w + "-p", file_name)
    os.makedirs(os.path.dirname(p), exist_ok=True)
    shutil.copy(prg, p)
    r.put(p)
    r.write("claudest.inf", "[OPTIONS]\nHOST=127.0.0.1\nPORT=2323\nNATFEATS=1\n")
    inf = os.path.join(r.hd, "claudest.inf")
    before = open(inf, "rb").read()
    try:
        r.start("C:\\" + file_name, ready="")
        time.sleep(75 if nvdi else 20)	# NVDI's fonts load behind a still screen
        r.still()
        alert = shutil.copy(r.shot("alert"), w + "-alert.png")
        r.key("Return")
        time.sleep(6)
        r.still()
        after = shutil.copy(r.shot("after"), w + "-after.png")
        con = r.console()
        named = r.describe()
    finally:
        r.stop()
    crashes = [f for d, _, fs in os.walk(r.hd) for f in fs if f.upper() == "CRASH.LOG"]
    print("on", named)
    check(differ(alert, after), "%s: an alert, gone after OK" % name)
    check("AESSTART" not in con, "%s: ClaudeST itself never started (no first event call)" % name)
    check(open(inf, "rb").read() == before, "%s: CLAUDEST.INF untouched" % name)
    check(not crashes, "%s: no CRASH.LOG" % name)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("work")
    ap.add_argument("--st", default=os.path.join(ROOT, "binary", "prof", "CLAUDEST.PRG"),
                    help="the 68000 program (default the timed build: its AESSTART shows a start)")
    ap.add_argument("--fb", default=os.path.join(ROOT, "binary", "fb", "CLAUDEST.PRG"))
    a = ap.parse_args()
    work = os.path.abspath(a.work)
    os.makedirs(work, exist_ok=True)
    case(work, "1-st-on-firebee", "FireBee:16", os.path.abspath(a.st), "CLAUDEST.PRG")
    case(work, "2-fb-on-falcon", "Falcon-4:16", os.path.abspath(a.fb), "CLAUDEST.PRG", nvdi=speed.machines.nvdi_here())
    case(work, "3-fb-on-st", "ST:mono", os.path.abspath(a.fb), "CLAUDEST.PRG")
    print("firebee_test: %s" % ("all ok" if not fails else "%d failed" % fails))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
