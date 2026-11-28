#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""FPTEST.PRG in save mode (C:\\FPSAVE.TXT and C:\\FPAFTER.TXT): the dialog closed by Return (PLAYLIST.M3U
saved in C:\\MUSIC\\), then six presses on the desk, each of which must come to the program as a press
("FPT press N"): EWeb 1.27's gate lost presses after the dialog on TOS 4.04. RUNS times over.

    after.py WORK MACHINE [RUNS] [--nvdi] [--box] [--ewloop]

--box: first a box over the screen (dlg_open_area), three presses in it, as EWeb's download box
"""
import os
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import run  # noqa: E402

BOX = "--box" in sys.argv
EWLOOP = "--ewloop" in sys.argv


def session(work, machine, nvdi, prg, display):
    run.scale_for(machine)
    r = run.speed.Rig(work, machine, nvdi=nvdi, display=display)
    fails = []
    try:
        music = os.path.join(r.hd, "MUSIC")
        os.makedirs(music, exist_ok=True)
        open(os.path.join(r.hd, "FPSAVE.TXT"), "w").write("save\n")
        open(os.path.join(r.hd, "FPAFTER.TXT"), "w").write("after\n")
        if BOX:
            open(os.path.join(r.hd, "FPBOX.TXT"), "w").write("box\n")
        if EWLOOP:
            open(os.path.join(r.hd, "FPEWLOOP.TXT"), "w").write("ew\n")
        run.shutil.copy(prg, os.path.join(r.hd, "FPTEST.PRG"))
        r.start("C:\\FPTEST.PRG", ready="picture add", boot=900)
        if "picture add" not in r.console():
            return ["the dialog never came"]
        r.still(most=5)
        r.key("Return")
        if BOX:
            t_end = time.time() + 30
            while time.time() < t_end and "FPT box " not in r.console():
                time.sleep(0.5)
            time.sleep(1)
            for i in range(3):
                r.click((200 + 60 * i) * run.SX, 200 * run.SY)
                time.sleep(0.8)
            t_end = time.time() + 30
            while time.time() < t_end and "FPT box done" not in r.console():
                time.sleep(0.5)
            got = [l for l in r.console().splitlines() if l.startswith("FPT boxpress")]
            if len(got) != 3:
                fails.append("box: %d of 3 presses came" % len(got))
            print("  the box over the screen: %d of 3 presses" % len(got))
        t_end = time.time() + 30
        while time.time() < t_end and "FPT after ready" not in r.console():
            time.sleep(0.5)
        if "FPT saved 1 C:\\MUSIC\\PLAYLIST.M3U" not in r.console():
            fails.append("not saved: %s" % [l for l in r.console().splitlines() if l.startswith("FPT")])
        time.sleep(1)
        for i in range(6):
            r.click((100 + 40 * i) * run.SX, 200 * run.SY)
            time.sleep(0.8)
        t_end = time.time() + 40
        while time.time() < t_end and "FPT after " not in r.console().replace("FPT after ready", ""):
            time.sleep(0.5)
        got = [l for l in r.console().splitlines() if l.startswith("FPT press")]
        if len(got) != 6:
            fails.append("%d of 6 presses came: %s" % (len(got), got))
        print("  after the dialog on %s: %d of 6 presses" % (r.describe(), len(got)))
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
