#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""FPTEST.PRG (make) on one of the family's machines: the family's file
dialog opened on a folder of music, a file, folders, a playlist and a text
file, with EPlay's types (Music files first). The keys select (Down,
Shift+Down), the mask narrows (*.W*) and is cleared, Control+A takes
every file, Return adds them. Checks: the dialog came, each step changed
the screen, the files taken were the music files in name order and not the
text file, the dialog called idle(), and no CRASH.LOG. A screenshot each
step in WORK/shots.

    run.py WORK MACHINE [--nvdi] [--old]

--old also runs FPOLD.PRG (make old EPLAY=...: EPlay's own Add files) the
same way and compares every screenshot, pixel for pixel: the move into
common must change none (the hub, 5 Nov 2026). On rows of 8 pixels (ST
low and medium) the folders' shapes are the one change since: they no
longer reach into the next row. There a difference within one column of
folder shapes (16 pixels wide at most) is a NOTE; anything more fails.
"""
import os
import re
import shutil
import subprocess
import sys
import time

DISPLAY = os.environ.get("FP_DISPLAY", ":165")
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "speed"))
import speed  # noqa: E402

# C:\MUSIC: (name, bytes); folders end in '\'. Fixed dates, so a shot is
# the same each run (3 Nov 2026, 14:00)
FILES = [("01_NIGHT.MP3", 6123456), ("02_RAIN.WAV", 52000000), ("03_ECHO.AIF", 9000), ("LIVE.M3U", 812),
         ("README.TXT", 1200), ("04_LAST.MP3", 700000), ("LIVE\\", 0), ("BONUS\\", 0), ("LIVE\\X.MP3", 1000)]
WHEN = time.mktime((2026, 11, 3, 14, 0, 0, 0, 0, -1))
WANT = ["C:\\MUSIC\\01_NIGHT.MP3", "C:\\MUSIC\\02_RAIN.WAV", "C:\\MUSIC\\03_ECHO.AIF", "C:\\MUSIC\\04_LAST.MP3",
        "C:\\MUSIC\\LIVE.M3U"]
# (name, keys, the screen stays as it was)
STEPS = [("1-open", [], False), ("2-select", ["Down", "shift+Down"], False), ("3-mask", ["type:*.W*"], False),
         ("4-esc", ["Escape"], False), ("5-esc-empty", ["Escape"], True), ("6-mask2", ["type:*.W*"], False),
         ("7-cleared", ["BackSpace", "BackSpace", "BackSpace", "BackSpace"], False), ("8-all", ["ctrl+a"], False)]


# the screenshot's pixels to the screen's: Hatari doubles ST medium's lines and ST low's both ways
SX = SY = 1


def scale_for(machine):
    global SX, SY
    tag = machine.split(":")[-1] if ":" in machine else ""
    SX = 2 if tag == "low" else 1
    SY = 2 if tag in ("low", "med") else 1


def geometry(con):
    """the buttons' places from the console: name -> (x, y, w, h)"""
    m = re.findall(r"FPT geom (.*)", con)
    g = {}
    if m:
        for part in m[-1].split():
            n, _, v = part.partition("=")
            g[n] = tuple(int(i) for i in v.split(","))
    return g


def move(r, x, y):
    g = dict(l.split("=") for l in r.xdo("getwindowgeometry", "--shell", r.win()).split())
    r.xdo("mousemove", str(int(g["X"]) + x * SX), str(int(g["Y"]) + y * SY))
    time.sleep(0.4)


def pop_open(r, geom, btn, name, fails, sel, rowh):
    """clicks the pop-up button, checks the list's top is `sel` rows above the button's (5.7, D27); returns the
    list's (x, y) or None"""
    x, y, w, h = geom[btn]
    move(r, x + w // 2, y + h // 2)
    r.still(most=5)
    before = r.shot(name + "-before")
    r.click(x * SX + w * SX // 2, y * SY + h * SY // 2)
    r.still(most=10)
    after = r.shot(name)
    box = diff_box(before, after)
    if not box:
        fails.append("%s: the pop-up didn't open" % name)
        return None
    bw, bh, bx, by = box[0] // SX, box[1] // SY, box[2] // SX, box[3] // SY
    want = y - 3 - (sel * (rowh + 2) if sel is not None else 0)
    if sel is None:		# the drive: whichever row is ticked, a whole number of rows above the button
        k = (y - 3 - by) / (rowh + 2.0)
        # (or the desktop's top held it down: the list is as high as the drives need, the menu bar above)
        ok = (abs(k - round(k)) * (rowh + 2) <= 2 or by <= 2 * rowh + 4) and by <= y and by + bh > y + h and \
            abs(bx - x) <= 8
    else:
        ok = abs(by - want) <= 2 and abs(bx - x) <= 8 and by + bh > y + h - 1
    print("  %s: pop-up %dx%d at %d,%d; the button at %d,%d, wanted top %s%s" %
          (name, bw, bh, bx, by, x, y, want if sel is not None else "a row multiple above",
           "" if ok else "  <- WRONG"))
    if not ok:
        fails.append("%s: the pop-up's top is %d, the button's %d (the ticked row %s over it)" % (name, by, y, sel))
    return bx, by


def pop_row(r, top, k, rowh):
    x, y = top
    r.click((x + 40) * SX, (y + 2 + k * (rowh + 2) + (rowh + 2) // 2) * SY)
    r.still(most=10)


def session(work, machine, nvdi, prg, display):
    scale_for(machine)
    r = speed.Rig(work, machine, nvdi=nvdi, display=display)
    fails, shots = [], {}
    try:
        music = os.path.join(r.hd, "MUSIC")
        for name, size in FILES:
            p = os.path.join(music, *name.rstrip("\\").split("\\"))
            if name.endswith("\\"):
                os.makedirs(p, exist_ok=True)
            else:
                os.makedirs(os.path.dirname(p), exist_ok=True)
                with open(p, "wb") as f:
                    f.truncate(size)
        for d, ds, fs in os.walk(music):
            for n in ds + fs:
                os.utime(os.path.join(d, n), (WHEN, WHEN))
        # under one name, FPTEST.PRG: the desktop shows it at the top
        shutil.copy(prg, os.path.join(r.hd, "FPTEST.PRG"))
        r.start("C:\\FPTEST.PRG", ready="picture add", boot=900)
        if "picture add" not in r.console():
            fails.append("the dialog never came")
            return fails, shots
        geom = geometry(r.console())
        if len(geom) < 7:
            fails.append("no button places on the console: %s" % geom)
            return fails, shots
        rowh = geom["type"][3]
        cx, cy, cw_, ch_ = geom["cancel"]
        ax, ay, aw, ah = geom["add"]
        cw = geom["drive"][2] // 5		# a cell's width
        if not (cw_ == aw == 10 * cw and ax - cx == 12 * cw and ch_ == ah == rowh):
            fails.append("Cancel and Add: %s, %s; 10 wide (%d px), Cancel left of Add 12 cells apart" %
                         (geom["cancel"], geom["add"], 10 * cw))
        # the pop-ups (C8, C9): the ticked row over the button
        move(r, 5, 5)
        r.still(most=5)
        shots["0-open"] = r.shot("0-open")
        top = pop_open(r, geom, "type", "p1-type", fails, 0, rowh)
        if top:
            pop_row(r, top, 2, rowh)			# WAV
            shots["p2-wav"] = r.shot("p2-wav")
            if same(shots["0-open"], shots["p2-wav"]):
                fails.append("choosing WAV changed nothing")
            top = pop_open(r, geom, "type", "p3-type-again", fails, 2, rowh)
            if top:
                pop_row(r, top, 0, rowh)		# Music files again
        for btn, name in (("sort", "p4-sort"), ("folders", "p5-folders"), ("drive", "p6-drive")):
            top = pop_open(r, geom, btn, name, fails, None if btn == "drive" else 0, rowh)
            if top:
                r.key("Escape")				# the pop-up closes, the dialog stays
                r.still(most=10)
        move(r, 5, 5)
        r.still(most=5)
        back = r.shot("p7-back")
        if not same(shots["0-open"], back):
            fails.append("after the pop-ups the dialog isn't as it was (%s, %s)" % (shots["0-open"], back))
        prev = None
        for name, keys, stays in STEPS:
            for k in keys:
                if k.startswith("type:"):
                    r.type(k[5:])
                else:
                    r.key(k)
            r.still(most=20)
            shots[name] = r.shot(name)
            if prev and same(prev, shots[name]) != stays:
                fails.append("%s: the screen %s" % (name, "changed" if stays else "didn't change"))
            prev = shots[name]
        if not same(shots["4-esc"], shots["7-cleared"]):
            fails.append("Esc and BackSpaces leave different screens (4-esc, 7-cleared)")
        r.key("Return")
        t_end = time.time() + 60
        while time.time() < t_end and "FPT dir" not in r.console():
            time.sleep(1)
        con = r.console()
        took = [l[9:] for l in con.splitlines() if l.startswith("FPT took ")]
        if took != WANT:
            fails.append("took %s, wanted %s" % (took, WANT))
        if "idle called" not in con:
            fails.append("idle() never called")
        if "FPT dir C:\\MUSIC\\; recent C:\\MUSIC\\" not in con:
            fails.append("the folder and the recent ones: %s" %
                         [l for l in con.splitlines() if l.startswith("FPT dir")])
        for d, _, fs in os.walk(r.hd):
            if any(f.upper() == "CRASH.LOG" for f in fs):
                fails.append("CRASH.LOG in %s" % d)
        print("  %s on %s: %s" % (os.path.basename(prg), r.describe(),
                                  "; ".join(l for l in con.splitlines() if " picture " in l)))
    finally:
        r.stop()
    return fails, shots


def closing(work, machine, nvdi, prg, display, how):
    """Esc (twice, once with text) leaves the dialog open; it closes by Cancel (a click) or Undo (key 0x61)
    taking nothing"""
    fifo = os.path.join(os.path.abspath(work), "cmd.fifo")
    scale_for(machine)
    r = speed.Rig(work, machine, nvdi=nvdi, display=display, extra=["--cmd-fifo", fifo])
    fails = []
    try:
        music = os.path.join(r.hd, "MUSIC")
        os.makedirs(music)
        with open(os.path.join(music, "A.MP3"), "wb") as f:
            f.truncate(100)
        shutil.copy(prg, os.path.join(r.hd, "FPTEST.PRG"))
        r.start("C:\\FPTEST.PRG", ready="picture add", boot=900)
        geom = geometry(r.console())
        if "picture add" not in r.console() or len(geom) < 7:
            return ["%s: the dialog never came" % how]
        r.key("Escape")
        r.type("x")
        r.key("Escape")
        r.key("Escape")
        r.still(most=10)
        if "FPT dir" in r.console():
            fails.append("%s: Esc closed the dialog" % how)
        if how == "cancel":
            x, y, w, h = geom["cancel"]
            r.click((x + w // 2) * SX, (y + h // 2) * SY)
        else:
            for _ in range(100):
                if os.path.exists(fifo):
                    break
                time.sleep(0.1)
            with open(fifo, "w") as f:
                f.write("hatari-event keypress 0x61\n")
        t_end = time.time() + 30
        while time.time() < t_end and "FPT dir" not in r.console():
            time.sleep(1)
        con = r.console()
        if "FPT done 0" not in con or "FPT took" in con:
            fails.append("%s: the dialog didn't end with nothing taken: %s" %
                         (how, [l for l in con.splitlines() if l.startswith("FPT")]))
        print("  %s: Esc kept the dialog open, then %s closed it with nothing taken%s" %
              (how, how, "" if not fails else "  <- FAILED"))
    finally:
        r.stop()
    return fails


def double_clicks(work, machine, nvdi, prg, display):
    """a single click on a folder only selects it; a double click on a folder opens it (one more folder picture);
    a double click on a file takes it at once (guidelines 5.8: select on press, the double click acts on its
    release). Rows are found from the list's place on the console"""
    scale_for(machine)
    r = speed.Rig(work, machine, nvdi=nvdi, display=display)
    fails = []
    try:
        music = os.path.join(r.hd, "MUSIC")
        os.makedirs(os.path.join(music, "LIVE"))
        os.makedirs(os.path.join(music, "BONUS"))
        for n in ("LIVE\\X.MP3", "01_NIGHT.MP3"):
            with open(os.path.join(music, *n.split("\\")), "wb") as f:
                f.truncate(100)
        shutil.copy(prg, os.path.join(r.hd, "FPTEST.PRG"))
        r.start("C:\\FPTEST.PRG", ready="picture add", boot=900)
        geom = geometry(r.console())
        if "picture add" not in r.console() or "list" not in geom:
            return ["double clicks: the dialog never came, or no list place on the console"]
        lx, ly, lw, lh = geom["list"]
        rh = geom["type"][3]

        def row(k):		# the middle of row k, on the name
            return (lx + 60) * SX, (ly + 1 + k * rh + rh // 2) * SY

        def folders():
            return r.console().count(" picture folder ")
        move(r, 5, 5)
        r.still(most=5)
        s0 = r.shot("d0-open")
        r.click(*row(1))			# LIVE: one click
        r.still(most=10)
        s1 = r.shot("d1-click")
        if same(s0, s1):
            fails.append("one click on a folder selected nothing")
        if folders():
            fails.append("one click on a folder opened it")
        r.dclick(*row(1))			# LIVE: a double click
        r.still(most=10)
        r.shot("d2-dclick")
        if folders() != 1:
            fails.append("a double click on a folder: %d folder pictures, wanted 1" % folders())
        r.dclick(*row(0))			# X.MP3: the file
        t_end = time.time() + 20
        while time.time() < t_end and "FPT dir" not in r.console():
            time.sleep(1)
        con = r.console()
        took = [l[9:] for l in con.splitlines() if l.startswith("FPT took ")]
        if took != ["C:\\MUSIC\\LIVE\\X.MP3"]:
            fails.append("a double click on a file took %s" % took)
        print("  double clicks on %s: folder opened %d time(s), took %s%s" %
              (r.describe(), folders(), took, "  <- FAILED" if fails else ""))
    finally:
        r.stop()
    return fails


# "Use the system file selector" (Erez, 9 Nov 2026): the Cancel button's place in the AES's own selector, by TOS
# (a screenshot of each; the selector is the AES's, not ours, and has no geometry on the console)
SELECTOR_CANCEL = {"TOS 4.04": (373, 393), "EmuTOS": (325, 400)}


def system_selector(work, machine, nvdi, prg, display):
    """FPTEST.PRG with filepick_system set (C:\\FPSYS.TXT): the AES's selector opens, not our dialog; typing a name and
    Return takes that one file (the folder as the selector gave it, left in dir and first in recent); Cancel takes
    nothing and leaves dir and recent as they were; idle() isn't called meanwhile. Returns (fails, skipped)"""
    scale_for(machine)
    fails = []
    for how in ("take", "cancel"):
        r = speed.Rig(os.path.join(work, how), machine, nvdi=nvdi, display=display)
        try:
            tos = r.describe()
            cancel = next((v for k, v in SELECTOR_CANCEL.items() if k in tos), None)
            if how == "cancel" and not cancel:
                return fails, ["the system selector's Cancel (no place known for %s)" % tos]
            music = os.path.join(r.hd, "MUSIC")
            os.makedirs(music)
            for n in ("NIGHT01.MP3", "RAIN02.WAV", "README.TXT"):
                with open(os.path.join(music, n), "wb") as f:
                    f.truncate(100)
            with open(os.path.join(r.hd, "FPSYS.TXT"), "w") as f:
                f.write("1")
            shutil.copy(prg, os.path.join(r.hd, "FPTEST.PRG"))
            r.start("C:\\FPTEST.PRG", ready="FPT open", boot=900)
            if "FPT open" not in r.console():
                fails.append("system selector: the program never started")
                continue
            r.still(most=10)
            shot = r.shot("sel-open")
            if " picture add " in r.console():
                fails.append("system selector, %s: our dialog opened" % how)
            if how == "take":
                r.type("NIGHT01.MP3")
                r.key("Return")
            else:
                move(r, cancel[0] - 4, cancel[1] - 2)
                move(r, cancel[0], cancel[1])
                r.click(cancel[0] * SX, cancel[1] * SY)
            t_end = time.time() + 30
            while time.time() < t_end and "FPT dir" not in r.console():
                time.sleep(1)
            con = r.console()
            took = [l[9:] for l in con.splitlines() if l.startswith("FPT took ")]
            dirl = [l for l in con.splitlines() if l.startswith("FPT dir")]
            if how == "take":
                want = ["C:\\MUSIC\\NIGHT01.MP3"]
                ok = took == want and "FPT done 1, idle never" in con and \
                    dirl == ["FPT dir C:\\MUSIC\\; recent C:\\MUSIC\\"]
            else:
                ok = not took and "FPT done 0, idle never" in con and \
                    dirl == ["FPT dir C:\\MUSIC\\; recent C:\\MUSIC\\LIVE\\"]
            if not ok:
                fails.append("system selector, %s: took %s, %s, %s" % (how, took, [l for l in con.splitlines()
                                                                                  if l.startswith("FPT done")], dirl))
            print("  system selector, %s on %s: took %s, %s%s" % (how, tos, took, dirl, "" if ok else "  <- FAILED"))
        finally:
            r.stop()
    return fails, []


def diff_box(a, b):
    """the box the two pictures differ in, (w, h, x, y), or None"""
    p = subprocess.run(["convert", a, b, "-compose", "difference", "-composite", "-trim", "info:"],
                       capture_output=True, text=True)
    m = re.search(r" (\d+)x(\d+) \d+x\d+\+(\d+)\+(\d+) ", p.stdout)
    return tuple(int(g) for g in m.groups()) if m else None


def same(a, b):
    p = subprocess.run(["compare", "-metric", "AE", a, b, "null:"], capture_output=True, text=True)
    try:
        return int(float(p.stderr.split()[0])) == 0
    except (ValueError, IndexError):
        return False


def main():
    a = [x for x in sys.argv[1:] if not x.startswith("--")]
    if len(a) < 2:
        sys.exit(__doc__)
    work, machine, nvdi = os.path.abspath(a[0]), a[1], "--nvdi" in sys.argv
    print("the file dialog on %s%s" % (machine, " with NVDI" if nvdi else ""))
    fails, shots = session(os.path.join(work, "new"), machine, nvdi,
                           os.path.join(HERE, "build", "atari", "FPTEST.PRG"), DISPLAY)
    for how in ("cancel", "undo"):
        fails += closing(os.path.join(work, how), machine, nvdi,
                         os.path.join(HERE, "build", "atari", "FPTEST.PRG"), DISPLAY, how)
    fails += double_clicks(os.path.join(work, "dbl"), machine, nvdi,
                           os.path.join(HERE, "build", "atari", "FPTEST.PRG"), DISPLAY)
    f3, skipped = system_selector(os.path.join(work, "sys"), machine, nvdi,
                                  os.path.join(HERE, "build", "atari", "FPTEST.PRG"), DISPLAY)
    fails += f3
    for sk in skipped:
        print("SKIPPED: " + sk)
    if "--old" in sys.argv:
        f2, old = session(os.path.join(work, "old"), machine, nvdi,
                          os.path.join(HERE, "build", "old", "FPOLD.PRG"), DISPLAY)
        fails += ["EPlay's: " + f for f in f2 if not f.startswith("took")]
        small = machine.split(":")[-1] in ("med", "low")
        for name in shots:
            if name not in old:
                continue
            if same(shots[name], old[name]):
                print("ok    %s the same as EPlay's own, pixel for pixel" % name)
                continue
            box = diff_box(shots[name], old[name])
            if small and box and box[0] <= 16:
                print("NOTE  %s: the folders' shapes only (%dx%d at %d,%d), as on rows of 8 they should" %
                      ((name,) + box))
            else:
                fails.append("%s differs from EPlay's own (%s, %s; %s)" % (name, shots[name], old[name], box))
    for f in fails:
        print("FAIL  " + f)
    print(("all passed" if not skipped else "passed, %d skipped" % len(skipped)) if not fails else
          "%d failed" % len(fails))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
