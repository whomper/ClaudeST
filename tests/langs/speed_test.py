#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""The Languages dialog against the guidelines' times (Speed: the stock machines set them): LANGT.PRG built
with -DAES_PROFILE=20 (make prof), its dialog opened over the network against fakegate.py, and the
user's actions done one at a time; each action's longest stretch between event calls (gem.c's AESGAP
lines, through common's tests/speed Rig) is set against its time:

    the dialog opened            150 ms; 300 on an 8 MHz ST (a dialog over half the screen: Settings')
    a key, a click, a tick       50 ms
    a line scrolled              50 ms
    a page scrolled              150 ms
    the dialog closed            300 ms

    speed_test.py WORK MACHINE [--nvdi]       MACHINE: ST:mono, ST:med, Falcon-4:16 (the stock machines)

The first stretch's number is the dialog's own: from the last event call of the list's request to the
dialog's first event call (the list read, sorted and drawn, the tree built and drawn).
"""
import os
import re
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "speed"))
sys.path.insert(0, HERE)
import speed  # noqa: E402
import fakegate  # noqa: E402

OPEN_ST = 300           # a dialog over half the screen on an 8 MHz ST (the guidelines' Speed, Settings)


def main():
    a = [x for x in sys.argv[1:] if not x.startswith("--")]
    if len(a) < 2:
        sys.exit(__doc__)
    work, machine, nvdi = os.path.abspath(a[0]), a[1], "--nvdi" in sys.argv
    prg = os.path.join(HERE, "build", "prof", "LANGT.PRG")
    if not os.path.exists(prg):
        sys.exit("speed_test.py: make prof first")
    gate = fakegate.Gate(port=0, cap_mb=600, installed=("he",))
    gate.start()
    r = speed.Rig(work, machine, nvdi=nvdi, display=":%d" % (100 + os.getpid() % 100))
    yscale = 2 if r.st and r.m.screen in ("med", "low") else 1
    r.extra = r.m.network()
    rc = 1
    try:
        r.put(prg)
        r.write("LANGT.CFG", "127.0.0.1 %d\n" % gate.port)
        r.start("C:\\LANGT.PRG", ready="LANGS geom", boot=400 if nvdi else 240)
        r.results.clear()               # (the start is the Gateway's list over a serial line, not the dialog's)
        con = r.console()
        m = re.search(r"LANGS geom rows (-?\d+) (-?\d+) (-?\d+) (-?\d+) bar (-?\d+) (-?\d+) (-?\d+) (-?\d+) ok (-?\d+) (-?\d+) (-?\d+) (-?\d+) "
                      r"cancel (-?\d+) (-?\d+) (-?\d+) (-?\d+) row (\d+)", con)
        if not m:
            sys.exit("speed_test.py: the dialog didn't open (see %s)" % r.log)
        v = [int(x) for x in m.groups()]
        rows, bar, rh = v[0:4], v[4:8], v[16]
        # the stretches from the list's request to the dialog's first event call: the last one before the
        # first key is the dialog's own
        seg = con[con.index("LANGT ask cancel"):]
        gaps = [int(g) for g in re.findall(r"AESGAP (\d+) ms", seg[seg.index("LANGS geom"):])]
        opened = gaps[0] if gaps else 0
        r.results.append(("the dialog opened (the first time)", opened, OPEN_ST if r.st else speed.DIALOG))

        def click(x, y):
            g = dict(l.split("=") for l in r.xdo("getwindowgeometry", "--shell", r.win()).split())
            r.xdo("mousemove", str(int(g["X"]) + x), str(int(g["Y"]) + y * yscale))
            time.sleep(0.4)
            r.xdo("click", "1")

        nrows = rows[3] // rh
        r.act("the cursor moved (Down)", speed.KEY, lambda: r.key("Down"))
        r.act("a row ticked (Space)", speed.KEY, lambda: r.key("space"))
        r.act("the cursor moved (Up)", speed.KEY, lambda: r.key("Up"))
        for _ in range(nrows - 2):
            r.key("Down")
            time.sleep(0.6)
        r.act("a row ticked (Space, one more)", speed.KEY, lambda: r.key("space"))
        r.act("a line scrolled (Down past the foot)", speed.LINE, lambda: r.key("Down"))
        r.act("a line scrolled (Up past the top, again)", speed.LINE, lambda: (r.key("Up"), r.key("Up"), r.key("Up")))
        r.act("a page scrolled (Shift+Down)", speed.PAGE, lambda: r.key("shift+Down"))
        r.act("a page scrolled (Shift+Up)", speed.PAGE, lambda: r.key("shift+Up"))
        r.act("a letter (S: a screen away)", speed.PAGE, lambda: r.key("s"))
        r.act("the last row (Shift+Home)", speed.PAGE, lambda: r.key("shift+Home"))
        r.act("the first row (Home)", speed.PAGE, lambda: r.key("Home"))
        r.act("a click on a row (press, release: ticked)", speed.KEY, lambda: click(rows[0] + rows[2] // 2, rows[1] + 2 * rh + rh // 2))
        r.act("a click on the bar's arrow (a row)", speed.LINE, lambda: click(bar[0] + bar[2] // 2, bar[1] + bar[3] - 8))
        r.act("a click in the bar's track (a page)", speed.PAGE, lambda: click(bar[0] + bar[2] // 2, bar[1] + bar[3] - bar[2] - 8))
        r.act("a click on the bar's up arrow (a row)", speed.LINE, lambda: click(bar[0] + bar[2] // 2, bar[1] + 8))
        r.act("the dialog closed (Esc)", speed.WINDOW, lambda: r.key("Escape"))
        # LANGT opens it again at once (its next scenario): the second time, the bar's first-draw check done
        end = time.time() + 90
        while time.time() < end and r.console().count("LANGS geom") < 2:
            time.sleep(1)
        r.still()
        con = r.console()
        if con.count("LANGS geom") >= 2:
            i = [m.start() for m in re.finditer("LANGS geom", con)][1]
            gaps = [int(g) for g in re.findall(r"AESGAP (\d+) ms", con[i:])]
            r.results.append(("the dialog opened again (second time)", gaps[0] if gaps else 0, OPEN_ST if r.st else speed.DIALOG))
        rc = r.report()
    finally:
        r.stop()
        gate.stop()
    return rc


if __name__ == "__main__":
    sys.exit(main())
