#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""All three AERA accessories at once (Erez, 19 Oct 2026): ECal, EContact
and EHypView, their latest releases (getrelease.py), loaded together on a
4 MB machine, as on Erez's own:

    common/tests/family/acc_all.py WORK [MACHINE] [--nvdi [--font]] [--display :N]

MACHINE is ST (4 MB, TOS 1.04: ECALST.ACC and ECONTST.ACC, the ST
editions) or Falcon-4 (4 MB, no TT RAM, TOS 4.04: ECAL.ACC and
ECONTACT.ACC), ST by default; EHYPVIEW.ACC runs on both. --nvdi boots
with Erez's NVDI (machines.py, m.nvdi()); --font gives EHypView one of
his Bitstream fonts (FONT=Courier 10 Pitch, 10 pt), loaded at its boot. The Atari boots to the desktop,
each accessory is opened from the Desk menu in turn, the three windows
left open over each other, then each opened again; the Desk menu and every
step are screenshots in WORK/shots. Checks: the desktop came, the Desk
menu lists three accessories, each opening changed the screen (its
window drew), Hatari still runs at the end, and no CRASH.LOG anywhere.
Each accessory's own log (LOG=1 is the apps' default where they have
one) is copied next to the shots. Run by the hub on Erez's TOS images;
on EmuTOS it runs too (AERA_TOS=emutos).
"""
import os
import shutil
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
TOOLS = os.path.join(HERE, "..", "..", "tools")
sys.path.insert(0, TOOLS)
import machines  # noqa: E402

args = [a for a in sys.argv[1:]]
nvdi = "--nvdi" in args
font = "--font" in args
display = ":90"
if "--display" in args:
    display = args[args.index("--display") + 1]
    args.remove(display)
rest = [a for a in args if not a.startswith("--")]
if not rest:
    sys.exit(__doc__)
work = os.path.abspath(rest[0])
machine = rest[1] if len(rest) > 1 else "ST"
st = machine.startswith("ST")

fails = 0


def check(ok, what):
    global fails
    print(("ok    " if ok else "FAIL  ") + what, flush=True)
    fails += not ok


shutil.rmtree(work, ignore_errors=True)
hd, shots, rel = (os.path.join(work, d) for d in ("hd", "shots", "rel"))
for d in (hd, shots, rel):
    os.makedirs(d)
m = machines.Machine(machine + ("" if ":" in machine or not st else ":mono"), hd)
if nvdi and not m.nvdi():
    sys.exit("acc_all: no NVDI here (machines.py, AERA_NVDI)")
want = {"ECal": "ECALST.ACC" if st else "ECAL.ACC",
        "EContact": "ECONTST.ACC" if st else "ECONTACT.ACC",
        "EHypView": "EHYPVIEW.ACC"}
for app, f in want.items():
    subprocess.run([sys.executable, os.path.join(TOOLS, "getrelease.py"), app, rel, f], check=True,
                   stdout=subprocess.DEVNULL)
    shutil.copy(os.path.join(rel, f), os.path.join(hd, f))
# ECal and EContact as set up already (their folders, named in C:\NAME.INF),
# so that no first-time question holds the Desk menu
for name in ("ECAL", "ECONTACT"):
    os.makedirs(os.path.join(hd, name))
    with open(os.path.join(hd, name + ".INF"), "w", newline="") as f:
        f.write("[OPTIONS]\r\nFOLDER=C:\\%s\r\n" % name)
if font:
    with open(os.path.join(hd, "EHYPVIEW.INF"), "w", newline="") as f:
        f.write("FOLDER=C:\\\r\nFONT=Courier 10 Pitch\r\nPOINT=10\r\n")
versions = ", ".join("%s %s" % (a, open(os.path.join(rel, a.upper() + ".VERSION")).read().strip()) for a in want)
print("on %s%s; %s" % (m.describe(), " with NVDI" if nvdi else "", versions), flush=True)

x = machines.xvfb(display)
env = m.env(dict(os.environ, DISPLAY=display, SDL_VIDEODRIVER="x11"))
hatari = subprocess.Popen(["hatari", "--statusbar", "no", "--borders", "no", "--zoom", "1",
                           "--fast-boot", "yes", "--natfeats", "yes", "--log-level", "warn"] + m.args(), env=env,
                          stdout=open(work + "/hatari.log", "w"), stderr=subprocess.STDOUT)


def xdo(*a):
    return subprocess.run(["xdotool", *a], env=env, capture_output=True, text=True).stdout


def win():
    for _ in range(100):
        w = (xdo("search", "--name", "Hatari") or xdo("search", "--class", "hatari")).split()
        if w:
            return w[0]
        time.sleep(0.2)
    sys.exit("acc_all: no Hatari window")


def shot(name):
    path = os.path.join(shots, name + ".png")
    subprocess.run(["import", "-display", display, "-window", win(), path], env=env)
    return path


def differs(a, b):
    """how many pixels two screenshots differ in"""
    r = subprocess.run(["compare", "-metric", "AE", a, b, "null:"], capture_output=True, text=True)
    try:
        return int(float(r.stderr.split()[0]))
    except (ValueError, IndexError):
        return -1


def click(x, y):
    g = dict(l.split("=") for l in xdo("getwindowgeometry", "--shell", win()).split())
    xdo("mousemove", str(int(g["X"]) + x), str(int(g["Y"]) + y))
    time.sleep(0.4)
    xdo("click", "1")
    time.sleep(0.6)


try:
    # the desktop: NVDI reads every font at boot (about 30 s on the Falcon)
    boot = (90 if nvdi else 45) * (2 if st else 1)
    prev = None
    for t in range(boot // 5):
        time.sleep(5)
        cur = shot("boot")
        if prev and differs(prev, cur) == 0 and t > 4:
            break
        prev = shutil.copy(cur, os.path.join(shots, "boot-prev.png"))
    desk = shot("0-desktop")
    check(hatari.poll() is None and os.path.getsize(desk) > 0, "the desktop came")
    click(18, 8)                                     # the Desk menu
    menu = shot("1-desk-menu")
    check(differs(desk, menu) > 500, "the Desk menu opens (see 1-desk-menu.png: three accessories)")
    click(300, 300)                                  # closed again
    time.sleep(1)
    for rnd in (1, 2):
        for i in range(len(want)):            # the Desk menu's order is the loading order
            before = shot("%d-%d-before" % (rnd, i))
            click(18, 8)
            click(60, 64 + 16 * i)                   # its row, under About and the line
            time.sleep(8 if st else 5)
            after = shot("%d-%d-opened" % (rnd, i))
            n = differs(before, after)
            if rnd == 1:            # a first start's welcome (EContact's) waits for OK,
                xdo("key", "Return")  # then offers to add an account: Esc declines
                time.sleep(2)
                xdo("key", "Escape")
                time.sleep(2)
                shot("%d-%d-welcomed" % (rnd, i))
            check(n > 2000, "round %d: the Desk menu's accessory %d drew its window (%d pixels)" % (rnd, i + 1, n))
    check(hatari.poll() is None, "Hatari still runs")
    crashes = [os.path.join(d, f) for d, _, fs in os.walk(hd) for f in fs if f.upper() == "CRASH.LOG"]
    check(not crashes, "no CRASH.LOG" + ("" if not crashes else ": " + ", ".join(crashes)))
    for d, _, fs in os.walk(hd):
        for f in fs:
            if f.upper().endswith(".LOG") and f.upper() != "CRASH.LOG":
                shutil.copy(os.path.join(d, f), os.path.join(shots, os.path.basename(d) + "-" + f))
finally:
    hatari.kill()
    m.stop()
    if x:
        x.kill()
print("screenshots in", shots)
print("all passed" if not fails else "%d failed" % fails)
sys.exit(1 if fails else 0)
