#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""FPTEST.PRG in save mode with C:\\FPROUNDS.TXT: six rounds of filepick_save closed by Return, each followed
by a box over the screen (dlg_open_area, as EWeb's download box) and one press in it, the button then
watched with graf_mkstate until it is let go ("FPT round N press let go"; "STUCK" after 10 s). EWeb 1.27's
gate found TOS's button held down after a press on the box that came after the dialog, in about half its
runs, and never after the system's selector. RUNS boots.

    rounds.py WORK MACHINE [RUNS] [--nvdi] [--sys] [--pre] [--win]   (--sys: FPSYS.TXT, the system's selector, to
    compare; --pre: before each save a press taken with no wait for its release, as EWeb's link)
"""
import os
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import run  # noqa: E402

SYS = "--sys" in sys.argv
PRE = "--pre" in sys.argv
WIN = "--win" in sys.argv


def session(work, machine, nvdi, prg, display):
    run.scale_for(machine)
    r = run.speed.Rig(work, machine, nvdi=nvdi, display=display)
    fails = []
    try:
        os.makedirs(os.path.join(r.hd, "MUSIC"), exist_ok=True)
        for f in ("FPSAVE.TXT", "FPROUNDS.TXT") + (("FPSYS.TXT",) if SYS else ()) + (("FPPRE.TXT",) if PRE else ()) + (("FPWIN.TXT",) if WIN else ()):
            open(os.path.join(r.hd, f), "w").write("x\n")
        run.shutil.copy(prg, os.path.join(r.hd, "FPTEST.PRG"))
        r.start("C:\\FPTEST.PRG", ready="FPT open", boot=900)
        for k in range(6):
            if PRE:
                t_end = time.time() + 30
                while time.time() < t_end and r.console().count("FPT pre ready") <= k:
                    time.sleep(0.3)
                time.sleep(0.5)
                r.click(100 * run.SX, 300 * run.SY)	# the link's press, its release never waited for
            time.sleep(2.5)
            r.key("Return")
            t_end = time.time() + 30
            while time.time() < t_end and ("FPT round %d box" % k) not in r.console():
                time.sleep(0.3)
            time.sleep(0.6)
            r.click(320 * run.SX, 200 * run.SY)
            t_end = time.time() + 20
            while time.time() < t_end and ("FPT round %d press" % k) not in r.console() and \
                    ("FPT round %d no press" % k) not in r.console():
                time.sleep(0.3)
        con = r.console()
        rows = [l for l in con.splitlines() if l.startswith("FPT round") and "box" not in l]
        bad = [l for l in rows if "let go" not in l]
        if len(rows) != 6 or bad:
            fails.append("rounds: %s" % rows)
        print("  %s on %s: %d of 6 let go" % ("system selector" if SYS else "filepick", r.describe(),
                                              len([l for l in rows if "let go" in l])))
    finally:
        r.stop()
    return fails


def main():
    a = [x for x in sys.argv[1:] if not x.startswith("--")]
    if len(a) < 2:
        print(__doc__)
        return 2
    work, machine = a[0], a[1]
    runs = int(a[2]) if len(a) > 2 else 1
    prg = os.path.join(HERE, "build", "atari", "FPTEST.PRG")
    fails = []
    for i in range(runs):
        fails += session(os.path.join(work, str(i)), machine, "--nvdi" in sys.argv, prg, run.DISPLAY)
    for f in fails:
        print("FAIL  " + f)
    print("all passed" if not fails else "%d failed" % len(fails))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
