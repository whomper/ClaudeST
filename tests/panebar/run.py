#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""PBTEST.PRG (make prg) in Hatari, for a look at the pane bars as the
machine's AES draws them: run.py WORK [MACHINE] [--flat]. MACHINE is one of
common/tools/machines.py's (Falcon:16 by default); --flat puts FLAT beside
the program (DIALOGS3D=0). It waits for "PBT ready" and leaves
WORK/shots/pbtest.png. Clicks (each says "PBT top N") go through the XaAES
rig, whose --steps click and wait for lines:
  (in ../xaaes) run.py WORK EMUTOS.IMG ../panebar/build/atari/PBTEST.PRG \\
    --steps "until PBT 200; click 240 346; until PBT top 31 20; shot"
"""
import os, re, shutil, subprocess, sys, time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "tools"))
import machines  # noqa: E402

args = [a for a in sys.argv[1:] if not a.startswith("--")]
flat = "--flat" in sys.argv
work = os.path.abspath(args[0])
machine = args[1] if len(args) > 1 else "Falcon:16"
shutil.rmtree(work, ignore_errors=True)
hd, shots = os.path.join(work, "hd"), os.path.join(work, "shots")
os.makedirs(hd)
os.makedirs(shots)
shutil.copy(os.path.join(HERE, "build", "atari", "PBTEST.PRG"), hd)
if flat:
    open(os.path.join(hd, "FLAT"), "w").close()
m = machines.Machine(machine, hd)
display = ":93"
x = machines.xvfb(display)
env = m.env(dict(os.environ, DISPLAY=display, SDL_VIDEODRIVER="x11"))
log = os.path.join(work, "hatari.log")
hatari = subprocess.Popen(["hatari", "--statusbar", "no", "--borders", "no", "--zoom", "1", "--fast-boot", "yes",
                           "--natfeats", "yes", "--log-level", "warn"] + m.args() + ["--auto", "C:\\PBTEST.PRG"],
                          env=env, stdout=open(log, "w"), stderr=subprocess.STDOUT)


def xdo(*a):
    return subprocess.run(["xdotool", *a], env=env, capture_output=True, text=True).stdout


def win():
    for _ in range(100):
        w = (xdo("search", "--name", "Hatari") or xdo("search", "--class", "hatari")).split()
        if w:
            return w[0]
        time.sleep(0.2)
    sys.exit("run.py: no Hatari window")


def shot(name):
    subprocess.run(["import", "-display", display, "-window", win(), os.path.join(shots, name + ".png")], env=env)


def said(text, after=0):
    return text in open(log, errors="replace").read()[after:]


def click(px, py, hold=0.3):
    g = dict(l.split("=") for l in xdo("getwindowgeometry", "--shell", win()).split())
    xdo("mousemove", str(int(g["X"]) + px), str(int(g["Y"]) + py))
    time.sleep(0.4)
    xdo("mousedown", "1")
    time.sleep(hold)
    xdo("mouseup", "1")
    time.sleep(1.5)


fails = 0
try:
    for _ in range(120):
        if said("PBT ready"):
            break
        time.sleep(1)
    else:
        sys.exit("run.py: PBTEST didn't start (see hatari.log)")
    time.sleep(2)
    shot("pbtest")
    print("on %s%s: shots/pbtest.png" % (m.describe(), ", flat" if flat else ""))
    # the window opens at 60,60, 400 x 300; its work area's right half's
    # left edge is the left pane's bar (16 wide, at the pane's right)
    n = len(open(log, errors="replace").read())
    tops = lambda: [int(t) for t in re.findall(r"PBT top (-?\d+)", open(log, errors="replace").read()[n:])]
    # (where the bar is depends on the AES's frame: found by its slider's
    # first top, 30, and the window's work area; taken from WF_WORKXYWH
    # through the program would be cleaner, the shot shows it)
finally:
    hatari.kill()
    m.stop()
    if x:
        x.kill()
