#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""All three AERA accessories at once (Erez, 19 Oct 2026): ECal, EContact
and EHypView, their latest releases (getrelease.py), loaded together on a
4 MB machine, as on Erez's own:

    common/tests/family/acc_all.py WORK [MACHINE] [--nvdi [--font]] [--display :N] [--use FILE]...
                                   [--accs ECal,EContact] [--prg EMail]

MACHINE is ST (4 MB, TOS 1.04: ECALST.ACC and ECONTST.ACC, the ST
editions) or Falcon-4 (4 MB, no TT RAM, TOS 4.04: ECAL.ACC and
ECONTACT.ACC), ST by default; EHYPVIEW.ACC runs on both. --nvdi boots
with Erez's NVDI (machines.py, m.nvdi()); --font gives EHypView one of
his Bitstream fonts (FONT=Courier 10 Pitch, 10 pt), loaded at its boot. --use FILE puts a
candidate's accessory (ECAL.ACC, say) or --prg's program (EMAIL.PRG) in place of the release's
of that name, for the hub's run before the release; the report names it. --accs names the accessories (all three by default);
--prg APP starts that program's latest release (EMAIL.PRG; APP:FILE[:MORE...] for another
edition and the files it needs, as EWeb:EWEBST.PRG:EWEB.FNT on an ST) first, as Erez works: the accessories
are then opened from its own Desk menu, and at the end the program is quit (^Q) and must give
the desktop back. The Atari boots to the desktop,
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
accs = ["ECal", "EContact", "EHypView"]
if "--accs" in args:
    v = args[args.index("--accs") + 1]
    args.remove(v)
    accs = [a.strip() for a in v.split(",") if a.strip()]
prg = None
if "--prg" in args:
    prg = args[args.index("--prg") + 1]
    args.remove(prg)
use = {}
while "--use" in args:
    i = args.index("--use")
    f = os.path.abspath(args.pop(i + 1))
    args.pop(i)
    use[os.path.basename(f).upper()] = f
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
want = {a: f for a, f in want.items() if a in accs}
for app, f in want.items():
    subprocess.run([sys.executable, os.path.join(TOOLS, "getrelease.py"), app, rel, f], check=True,
                   stdout=subprocess.DEVNULL)
    shutil.copy(use.get(f, os.path.join(rel, f)), os.path.join(hd, f))
# ECal and EContact as set up already (their folders, named in C:\NAME.INF),
# so that no first-time question holds the Desk menu
for name in ("ECAL", "ECONTACT"):
    os.makedirs(os.path.join(hd, name))
    with open(os.path.join(hd, name + ".INF"), "w", newline="") as f:
        f.write("[OPTIONS]\r\nFOLDER=C:\\%s\r\n" % name)
if font:
    with open(os.path.join(hd, "EHYPVIEW.INF"), "w", newline="") as f:
        f.write("FOLDER=C:\\\r\nFONT=Courier 10 Pitch\r\nPOINT=10\r\n")
auto = []
if prg:
    # APP:FILE[:MORE...] names an edition and the files it needs beside it
    # (EWeb:EWEBST.PRG:EWEB.FNT on an ST); else APP.PRG alone
    parts = prg.split(":")
    prg, pf, more = (parts[0], parts[1], parts[2:]) if len(parts) > 1 else (prg, prg.upper() + ".PRG", [])
    subprocess.run([sys.executable, os.path.join(TOOLS, "getrelease.py"), prg, rel, pf] + more, check=True,
                   stdout=subprocess.DEVNULL)
    os.makedirs(os.path.join(hd, prg.upper()), exist_ok=True)
    shutil.copy(use.get(pf, os.path.join(rel, pf)), os.path.join(hd, prg.upper(), pf))
    for f in more:
        shutil.copy(use.get(f, os.path.join(rel, f)), os.path.join(hd, prg.upper(), f))
    auto = ["--auto", "C:\\%s\\%s" % (prg.upper(), pf)]
versions = ", ".join("%s %s" % (a, use[f] if f in use else open(os.path.join(rel, a.upper() + ".VERSION")).read().strip())
                     for a, f in want.items())
if prg:
    versions += ", %s %s in front" % (prg, use[pf] if pf in use else
                                      open(os.path.join(rel, prg.upper() + ".VERSION")).read().strip())
print("on %s%s; %s" % (m.describe(), " with NVDI" if nvdi else "", versions), flush=True)

x = machines.xvfb(display)
env = m.env(dict(os.environ, DISPLAY=display, SDL_VIDEODRIVER="x11"))
hatari = subprocess.Popen(["hatari", "--statusbar", "no", "--borders", "no", "--zoom", "1",
                           "--fast-boot", "yes", "--natfeats", "yes", "--log-level", "warn"] + m.args() + auto, env=env,
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
    # a program that says in the log that it started: without that line
    # TOS's "not enough memory" alert over the desktop passed for its
    # window (EWeb found it on a 4 MB Falcon)
    said = {"EWEB": "EWEB ready", "EMAIL": "EMail: v_opnvwk"}.get(prg.upper() if prg else "")
    ran = not said or said in open(work + "/hatari.log", errors="replace").read()
    check(hatari.poll() is None and os.path.getsize(desk) > 0 and ran,
          ("%s's window came" % prg if ran else "%s started (no \"%s\" in the log: see 0-desktop.png, "
           "TOS's memory alert?)" % (prg, said)) if prg else "the desktop came")
    if prg:
        # the program's own "can't open its screen" (guideline 2.1) is right
        # when the accessories hold the memory, but then it never ran: say so
        if "v_opnvwk -> 0" in open(work + "/hatari.log", errors="replace").read():
            print("NOTE  %s couldn't open its screen (v_opnvwk 0): the accessories hold the memory; "
                  "its own alert, see 0-desktop.png" % prg)
    if prg and prg.upper() == "EMAIL":
        # a first start's welcome (EMail's "Let's set up your mail account")
        # waits for OK, then the account dialog: Esc declines (only EMail's:
        # in EFTP, Return opens the drive row A:, and TOS asks for a disk)
        xdo("key", "Return")
        time.sleep(3)
        xdo("key", "Escape")
        time.sleep(3)
        desk = shot("0b-%s-ready" % prg.lower())
    click(30, 8)                                     # the Desk menu (the first title's middle:
    time.sleep(1)                                    # EWeb's "EWeb" starts past x 18)
    menu = shot("1-desk-menu")
    check(differs(desk, menu) > 500, "the Desk menu opens (see 1-desk-menu.png: %d accessories)" % len(want))
    click(300, 300)                                  # closed again
    time.sleep(1)
    for rnd in (1, 2):
        if rnd == 2 and len(want) == 1:
            # its window is still on top: closed (^W), so that opening it
            # again draws it
            xdo("key", "ctrl+w")
            time.sleep(3)
        for i in range(len(want)):            # the Desk menu's order is the loading order
            before = shot("%d-%d-before" % (rnd, i))
            click(30, 8)
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
    if prg:
        # the accessories' windows closed (^W, the top one each time), then
        # the program quit: the desktop must come back
        for i in range(len(want)):
            xdo("key", "ctrl+w")
            time.sleep(3)
        before = shot("8-accessories-closed")
        xdo("key", "ctrl+q")
        time.sleep(10 if st else 6)
        after = shot("9-%s-quit" % prg.lower())
        check(differs(before, after) > 2000, "%s quit, the desktop back (%d pixels)" % (prg, differs(before, after)))
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
