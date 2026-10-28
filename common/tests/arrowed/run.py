#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""ARTEST.PRG (make) in Hatari: run.py WORK [MACHINE] [--busy MS] [--hold MS,...]
Clicks the window slider's track below the slider, once for each hold
time (the button held that long; a person's click is 50 to 150 ms), and
prints what the program was sent for each. A click held up to 200 ms
must give one WM_ARROWED (TOS 4.04 sent two until common's evnt_multi_
dropped the second); one held longer may give the AES's repeats.
  make && python3 run.py WORK Falcon-4:16 [--busy 300] [--hold 20,120,600]
  python3 run.py WORK ST:mono --y 300    (TOS 1.04: one a click, as ever)"""
import os, shutil, subprocess, sys, time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "tools"))
import machines  # noqa: E402

argv = sys.argv[1:]
def opt(name, default):
    if name in argv:
        i = argv.index(name); v = argv[i + 1]; del argv[i:i + 2]; return v
    return default
busy = opt("--busy", "0")
opt_y = opt("--y", "330")
holds = [int(v) for v in opt("--hold", "20,120").split(",")]
work = os.path.abspath(argv[0])
machine = argv[1] if len(argv) > 1 else "Falcon:16"
shutil.rmtree(work, ignore_errors=True)
hd = os.path.join(work, "hd")
os.makedirs(hd)
shutil.copy(os.path.join(HERE, "build", "ARTEST.PRG"), hd)
open(os.path.join(hd, "BUSY.TXT"), "w").write(busy)
m = machines.Machine(machine, hd)
display = ":94"
machines.xvfb(display)
env = m.env(dict(os.environ, DISPLAY=display, SDL_VIDEODRIVER="x11"))
log = os.path.join(work, "hatari.log")
hatari = subprocess.Popen(["hatari", "--statusbar", "no", "--borders", "no", "--zoom", "1", "--fast-boot", "yes",
                           "--natfeats", "yes", "--log-level", "warn"] + m.args() + ["--auto", "C:\\ARTEST.PRG"],
                          env=env, stdout=open(log, "w"), stderr=subprocess.STDOUT)

def xdo(*a):
    return subprocess.run(["xdotool", *a], env=env, capture_output=True, text=True).stdout

def lines():
    return [l.strip() for l in open(log, errors="replace") if l.startswith("ART ")]

failed = 0
try:
    for _ in range(300):
        time.sleep(0.5)
        if any(l.startswith("ART ready") for l in lines()):
            break
    time.sleep(3)
    w = (xdo("search", "--name", "Hatari") or xdo("search", "--class", "hatari")).split()[0]
    g = dict(l.split("=") for l in xdo("getwindowgeometry", "--shell", w).split())
    gw, gh = int(g["WIDTH"]), int(g["HEIGHT"])
    # the window: x 40..340 of the desktop, its slider at the right edge;
    # the track below the slider, a fifth of the way up from the bottom
    x = int(g["X"]) + 40 + 300 - 8
    y = int(g["Y"]) + int(opt_y)
    print("%s, busy %s ms: Hatari %dx%d, click at %d,%d of it" % (m.describe(), busy, gw, gh, x - int(g["X"]), y - int(g["Y"])), flush=True)
    for hold in holds:
        before = len(lines())
        xdo("mousemove", str(x), str(y))
        time.sleep(0.5)
        xdo("mousedown", "1")
        time.sleep(hold / 1000.0)
        xdo("mouseup", "1")
        time.sleep(2.5)
        got = lines()[before:]
        n = sum(1 for l in got if l.startswith("ART arrowed"))
        ok = n == 1 or hold > 200
        failed += not ok
        print("%s held %4d ms: %d WM_ARROWED  %s" % ("ok  " if ok else "FAIL", hold, n, " | ".join(got)), flush=True)
finally:
    hatari.kill()
print("all passed" if not failed else "%d failed" % failed)
sys.exit(1 if failed else 0)
