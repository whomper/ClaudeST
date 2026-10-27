#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""ClaudeST's speed rig (the guidelines' Speed: the stock machines set it),
on the family's common/tests/speed: ClaudeST built with
-DAES_PROFILE=20 into WORK-build, the bridge speed_bridge.py's (a long
conversation, a long streamed reply, at 19200 baud's pace), and each
action's longest stretch between event calls against its time.

    python3 tests/hatari/speed.py WORK [--machine ST:mono] [--nvdi]

The machines are ST:mono, ST:med and Falcon-4:16 (and Falcon-4:16 with
--nvdi). Exits 1 when an action is over its time.
"""
import argparse
import os
import shutil
import subprocess
import sys
import time

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
sys.path.insert(0, os.path.join(ROOT, "common", "tests", "speed"))
import speed  # noqa: E402

STREAM = 50     # a piece of a reply drawn, as a key's answer
SLICE = 100     # a long job's slice: a key answered within 100 ms


def build(work):
    """ClaudeST with -DAES_PROFILE=20, built apart from binary/"""
    b = os.path.abspath(work)
    shutil.rmtree(b, ignore_errors=True)
    shutil.copytree(os.path.join(ROOT, "st"), os.path.join(b, "st"),
                    ignore=shutil.ignore_patterns("*.o", "*.elf", "build.h"))
    os.symlink(os.path.join(ROOT, "common"), os.path.join(b, "common"))
    subprocess.run(["make", "-s", "-C", os.path.join(b, "st"), "-j8", "EXTRA_CFLAGS=-DAES_PROFILE=20"],
                   check=True)
    return os.path.join(b, "binary", "CLAUDEST.PRG")


def shot(r, n):
    """the picture r.act took after its Nth action"""
    return os.path.join(r.shots, next(f for f in sorted(os.listdir(r.shots)) if f.startswith("%02d-" % n)))


def same(a, b, what):
    """pictures a and b alike to the pixel (ImageMagick's compare)"""
    diff = subprocess.run(["compare", "-metric", "AE", a, b, "null:"], capture_output=True, text=True).stderr.split()
    ok = bool(diff) and diff[0] == "0"
    print(("ok    " if ok else "FAIL  ") + "%s (%s pixels differ)" % (what, diff[0] if diff else "?"), flush=True)
    return ok


def same_area(a, b, box, what):
    """the part x, y, w, h of pictures a and b alike to the pixel"""
    x, y, w, h = box
    cut = []
    for p in (a, b):
        c = os.path.join(os.path.dirname(os.path.dirname(p)), os.path.basename(p)[:-4] + "-cut.png")
        subprocess.run(["convert", p, "-crop", "%dx%d+%d+%d" % (w, h, x, y), "+repage", c], check=True)
        cut.append(c)
    return same(cut[0], cut[1], what)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("work")
    ap.add_argument("--machine", default="ST:mono")
    ap.add_argument("--nvdi", action="store_true")
    ap.add_argument("--prg", help="a profiling build to use instead of building one")
    a = ap.parse_args()
    work = os.path.abspath(a.work)
    prg = a.prg or build(work + "-build")
    out, inp = os.path.join(work, "st_out"), os.path.join(work, "st_in")
    r = speed.Rig(work, a.machine, nvdi=a.nvdi, extra=["--rs232-out", out, "--rs232-in", inp, "--drive-led", "no"])
    lock = "/tmp/.X%s-lock" % r.display.lstrip(":")
    for _ in range(100):        # the last run's X server, still going
        if not os.path.exists(lock):
            break
        time.sleep(0.2)
    named = os.path.join(work + "-build", "CLAUDEST.PRG")    # C:\\CLAUDEST.PRG, whatever the build's name
    if os.path.abspath(prg) != named:
        os.makedirs(os.path.dirname(named), exist_ok=True)
        shutil.copy(prg, named)
    r.put(named)
    falcon = a.machine.startswith("Falcon")
    # Hatari wires only the MFP port to the bridge: on a Falcon, port 6
    r.write("CLAUDEST.INF", "[OPTIONS]\nLINK=serial\n" + ("SERIALPORT=6\n" if falcon else ""))
    for p in (out, inp):        # before Hatari, which won't start without them
        os.mkfifo(p)
    bridge = subprocess.Popen([sys.executable, os.path.join(ROOT, "tests", "hatari", "speed_bridge.py"),
                               "--backend", "demo", "--pipe", out, inp],
                              stdout=open(os.path.join(work, "bridge.log"), "w"), stderr=subprocess.STDOUT)
    try:
        r.start("C:\\CLAUDEST.PRG")
        r.act("Recents shown (F2)", speed.WINDOW, lambda: r.key("F2"))
        r.act("list cursor moved (Tab)", speed.KEY, lambda: r.key("Tab"))
        # 40 KB at 1920 bytes a second: still() would take a pause for the end
        r.act("a long chat arriving (each slice)", SLICE, lambda: (r.key("Return"), time.sleep(40)))
        r.act("a line up (5 times)", speed.LINE, lambda: r.key(*["Up"] * 5))
        r.act("a line down (5 times)", speed.LINE, lambda: r.key(*["Down"] * 5))
        r.act("a page up (Shift+Up)", speed.PAGE, lambda: r.key("shift+Up"))
        r.act("a page down (Shift+Down)", speed.PAGE, lambda: r.key("shift+Down"))
        r.act("the top (Clr/Home)", speed.WINDOW, lambda: r.key("Home"))
        r.act("the end (Shift+Clr/Home)", speed.WINDOW, lambda: r.key("shift+Home"))
        r.act("letters typed", speed.KEY, lambda: r.type("abcdefgh"))
        r.act("Settings opened (^,)", speed.DIALOG, lambda: r.key("ctrl+comma"))
        # Undo: Esc empties the field with the cursor (the guidelines, 3.5)
        r.act("Settings closed (Undo), uncovered", speed.WINDOW, lambda: r.key("Undo"))
        r.act("About opened", speed.DIALOG, lambda: r.menu(40, 27))
        r.act("About closed, uncovered", speed.WINDOW, lambda: r.key("Return"))
        r.act("a reply streamed (each piece)", STREAM, lambda: (r.key("BackSpace", "Return"), time.sleep(12)))
        # the reply as it was drawn, piece by piece, is the reply drawn whole
        # (to the top and back: the pane drawn again)
        drawn = shutil.copy(r.shot("streamed"), os.path.join(work, "streamed.png"))
        r.key("Home")
        r.still()
        r.xdo("keydown", "shift")      # held until the pane is drawn (r.key lets go outside r.act)
        r.key("Home")
        r.still()
        r.xdo("keyup", "shift")
        fails = not same(drawn, r.shot("streamed-whole"), "the reply as it streamed is the reply drawn whole")
        fails += not same(shot(r, 3), shot(r, 5), "five lines up and down again: the pane as it arrived")
        # the list scrolled a row at a time: the rows that stay moved on the
        # screen, those that come in drawn (Erez's film, 26 Oct 2026: each
        # arrow blanked the list and its bar, then drew it all again)
        bx, by, bw, bh = [int(v) for v in
                          [l for l in r.console().splitlines() if l.startswith("LISTBAR ")][-1].split()[1:]]
        r.act("the list a row down (Tab, 15 times)", speed.LINE, lambda: r.key(*["Tab"] * 15))
        r.act("the list a row up (Shift+Tab, 15 times)", speed.LINE, lambda: r.key(*["shift+Tab"] * 15))
        r.act("the list a row down (its bar's arrow, 5 clicks)", speed.LINE,
              lambda: [r.click(bx + bw // 2, by + bh - 6) for _ in range(5)])
        r.act("the list a row up (its bar's arrow, 5 clicks)", speed.LINE,
              lambda: [r.click(bx + bw // 2, by + 6) for _ in range(5)])
        fails += not same_area(shot(r, 17), shot(r, 19), (0, by, bx, bh),
                               "five rows down and up again by the bar: the list as it was")
        lost = "the serial ring was full" in r.console()
        print(("FAIL  " if lost else "ok    ") + "no byte from the bridge lost (the ring never full)", flush=True)
        fails += lost
        return r.report() or (1 if fails else 0)
    finally:
        r.stop()
        bridge.kill()


if __name__ == "__main__":
    sys.exit(main())
