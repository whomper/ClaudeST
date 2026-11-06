#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""PRBTEST.PRG (make prg) in Hatari, for a look at the family's progress
bar as the machine draws it: run.py WORK [MACHINE] [--nvdi]. MACHINE is one
of common/tools/machines.py's (Falcon:256 by default); --nvdi installs
Erez's NVDI (a Falcon). It waits for "PRB ready", prints the pens found
and leaves WORK/shots/progbar.png.
"""
import os, shutil, subprocess, sys, time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "tools"))
import machines  # noqa: E402

args = [a for a in sys.argv[1:] if not a.startswith("--")]
work = os.path.abspath(args[0])
machine = args[1] if len(args) > 1 else "Falcon:256"
shutil.rmtree(work, ignore_errors=True)
hd, shots = os.path.join(work, "hd"), os.path.join(work, "shots")
os.makedirs(hd)
os.makedirs(shots)
shutil.copy(os.path.join(HERE, "build", "atari", "PRBTEST.PRG"), hd)
m = machines.Machine(machine, hd)
if "--nvdi" in sys.argv and not m.nvdi():
    sys.exit("run.py: no NVDI to install (see machines.py)")
display = os.environ.get("XDISPLAY", ":93")
machines.xvfb(display)
env = m.env(dict(os.environ, DISPLAY=display, SDL_VIDEODRIVER="x11"))
log = os.path.join(work, "hatari.log")
hatari = subprocess.Popen(["hatari", "--statusbar", "no", "--borders", "no", "--zoom", "1", "--fast-boot", "yes",
                           "--natfeats", "yes", "--log-level", "warn"] + m.args() + ["--auto", "C:\\PRBTEST.PRG"],
                          env=env, stdout=open(log, "w"), stderr=subprocess.STDOUT)


def win():
    for _ in range(100):
        r = subprocess.run(["xdotool", "search", "--name", "Hatari"], env=env, capture_output=True, text=True)
        if r.stdout.split():
            return r.stdout.split()[0]
        time.sleep(0.2)
    sys.exit("run.py: no Hatari window")


try:
    for _ in range(180):
        if "PRB ready" in open(log, errors="replace").read():
            break
        time.sleep(1)
    else:
        sys.exit("run.py: PRBTEST didn't start (see hatari.log)")
    time.sleep(2)
    subprocess.run(["import", "-display", display, "-window", win(), os.path.join(shots, "progbar.png")], env=env)
    lines = [l for l in open(log, errors="replace").read().splitlines() if l.startswith("PRB ") and "ready" not in l]
    print("%s: %s" % (m.describe(), " ".join(l for l in lines if " rgb " not in l)))
    if "--rgb" in sys.argv:
        for l in lines:
            if " rgb " in l:
                print("   ", l[4:])
finally:
    hatari.kill()
