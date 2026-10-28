#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""Retake the README's and the guide's screenshots in Hatari, from
binary/CLAUDEST.PRG (make first), and the help front page's picture.

    tools/docshots.py [WORK] [--only NAME,NAME]

Five runs, one machine at a time (common/tools/machines.py, your own
TOS images when ~/.aera/tos has them). The app is shown in 256 colours
(Erez, 28 Oct 2026); only the pictures there to show another mode are
taken in it:
  Falcon:256, the demo        tooltip, context-menu, move-to-project, model-menu,
  backend                     code, account, hebrew
  Falcon:256, the showcase    falcon-colour (the README's), about-colour
  account (tools/reel)
  ST:mono, the demo backend   st-high-mono and about (black and white)
  ST:med, the demo backend    st-medium-colour (a project's chat)
  Falcon:16, the showcase     docs/help/WINDOW.IMG (common/tools/helpfront.py
                              thumb), which ST-Guide shows in 16 colours
Each picture goes to docs/NAME.png, one bit deep where it is black and
white. Look at them before committing: a slow machine can leave one
half drawn.
"""
import argparse
import os
import shutil
import subprocess
import sys
import time

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
sys.path.insert(0, os.path.join(ROOT, "tests", "hatari"))
from rig import Rig, K  # noqa: E402

F2, F3, F4, F6, F8, F9 = 0x3c, 0x3d, 0x3e, 0x40, 0x42, 0x43
INSERT = 0x52
REST = (600, 330)           # where the pointer waits, out of the way
# the help front page's picture: the capture's crop and its numbered
# badges (the sidebar, the conversation, the reply line, the status area)
WINDOW_CROP = "0,18,640,462"
WINDOW_BADGES = ("147,116", "516,245", "602,458", "104,447")


def boot(work, machine, showcase):
    shutil.rmtree(work, ignore_errors=True)
    hd = os.path.join(work, "hd")
    os.makedirs(hd)
    shutil.copy(os.path.join(ROOT, "binary", "CLAUDEST.PRG"), hd)
    with open(os.path.join(hd, "CLAUDEST.INF"), "w", newline="") as f:
        # Hatari wires only the MFP port to the bridge: on a Falcon, port 6
        f.write("[OPTIONS]\r\nLINK=serial\r\n" + ("SERIALPORT=6\r\n" if machine.startswith("Falcon") else ""))
    out, inp = os.path.join(work, "st_out"), os.path.join(work, "st_in")
    for p in (out, inp):        # before Hatari, which won't start without them
        os.mkfifo(p)
    bridge = ([os.path.join(ROOT, "tools", "reel", "reel_bridge.py")] if showcase else
              [os.path.join(ROOT, "bridge", "claude_bridge.py"), "--backend", "demo"])
    b = subprocess.Popen([sys.executable] + bridge + ["--pipe", out, inp],
                         stdout=open(os.path.join(work, "bridge.log"), "w"), stderr=subprocess.STDOUT)
    r = Rig(work, machine, extra=["--rs232-out", out, "--rs232-in", inp, "--drive-led", "no"])
    r.bridge_proc = b
    r.wait(45)
    r.move(*REST)
    return r


def stop(r):
    r.stop()
    r.bridge_proc.kill()


def keep(r, name, colours):
    """the screen as docs/NAME.png, in 2, 4, 16 or 256 colours"""
    from PIL import Image
    path = r.shot(name)
    im = Image.open(path)
    im = (im.convert("1", dither=Image.NONE) if colours == 2 else
          im.convert("P", palette=Image.ADAPTIVE, colors=colours, dither=Image.NONE))
    dst = os.path.join(ROOT, "docs", name + ".png")
    im.save(dst, optimize=True)
    print("  docs/%s.png" % name, flush=True)
    return path


def open_nth(r, area, n):
    """area's list (an F key), the Nth item picked with Tab and opened"""
    r.key(area)
    r.wait(4)
    for _ in range(n):
        r.key(K["TAB"])
        time.sleep(0.3)
    r.key(K["RETURN"])
    r.wait(5)


def about(r, name, colours):
    r.move(40, 9)              # Desk: the menu drops as the pointer reaches it
    time.sleep(0.8)
    r.click(45, 27)            # About ClaudeST..., straight down (not across File)
    r.wait(3)
    r.move(*REST)
    keep(r, name, colours)
    r.key(K["RETURN"])
    r.wait(2)


def features(work, want):
    r = boot(work, "Falcon:256", False)
    try:
        open_nth(r, F2, 1)                      # Atari Falcon DSP tricks
        if want("tooltip"):
            r.key(F2)
            r.wait(4)
            open_nth(r, F2, 4)                  # the Hebrew chat
            r.move(90, 341)                     # a name cut off: its tooltip
            r.wait(2)
            keep(r, "tooltip", 256)
            r.move(*REST)
            open_nth(r, F2, 1)
        if want("context-menu"):
            r.rclick(90, 297)                   # Fix my GFA BASIC loop
            r.wait(1)
            keep(r, "context-menu", 256)
            r.key(K["ESC"])
            r.wait(1)
        if want("move-to-project"):
            r.rclick(90, 297)
            r.wait(1)
            for _ in range(4):                  # Open, Pin, Rename, Move to project
                r.key(K["DOWN"])
                time.sleep(0.3)
            r.key(K["RETURN"])
            r.wait(3)
            keep(r, "move-to-project", 256)
            r.key(K["ESC"])
            r.wait(1)
        r.move(*REST)
        if want("model-menu"):
            r.key(F9)
            r.wait(1)
            keep(r, "model-menu", 256)
            r.key(K["ESC"])
            r.wait(1)
        if want("account"):
            r.key(F8)
            r.wait(3)
            keep(r, "account", 256)
            r.key(K["ESC"])
            r.wait(2)
        if want("code"):
            open_nth(r, F6, 1)
            r.move(*REST)
            keep(r, "code", 256)
        if want("hebrew"):
            open_nth(r, F2, 4)
            r.move(*REST)
            keep(r, "hebrew", 256)
    finally:
        stop(r)


def st_mono(work, want):
    r = boot(work, "ST:mono", False)
    try:
        open_nth(r, F2, 1)                      # Atari Falcon DSP tricks
        if want("st-high-mono"):                # BUILD_STORY's
            r.move(*REST)
            keep(r, "st-high-mono", 2)
        if want("about"):
            about(r, "about", 2)
    finally:
        stop(r)


def st_med(work, want):
    r = boot(work, "ST:med", False)
    try:
        open_nth(r, F3, 1)                      # the first project
        for _ in range(2):                      # < All projects, then its chat
            r.key(K["TAB"])
            time.sleep(0.3)
        r.key(K["RETURN"])
        r.wait(5)
        r.move(*REST)
        keep(r, "st-medium-colour", 4)
    finally:
        stop(r)


def falcon(work, want):
    r = boot(work, "Falcon:256", True)
    try:
        open_nth(r, F2, 1)                      # 68000: fastest way to clear the screen
        r.move(*REST)
        if want("falcon-colour"):
            keep(r, "falcon-colour", 256)
        if want("about-colour"):
            about(r, "about-colour", 256)
    finally:
        stop(r)


def window(work, want):
    """the help front page's picture, from the same window in 16 colours"""
    r = boot(work, "Falcon:16", True)
    try:
        open_nth(r, F2, 1)
        r.move(*REST)
        subprocess.run([sys.executable, os.path.join(ROOT, "common", "tools", "helpfront.py"), "thumb",
                        r.shot("window"), os.path.join(ROOT, "docs", "help", "WINDOW.IMG"), WINDOW_CROP] +
                       list(WINDOW_BADGES), check=True)
        print("  docs/help/WINDOW.IMG", flush=True)
    finally:
        stop(r)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("work", nargs="?", default=os.path.join(ROOT, "out", "docshots"))
    ap.add_argument("--only", help="NAME,NAME: only these pictures (and the runs they need)")
    a = ap.parse_args()
    only = set(a.only.split(",")) if a.only else None

    def want(name):
        return only is None or name in only
    runs = [(features, ("tooltip", "context-menu", "move-to-project", "model-menu", "account", "code", "hebrew")),
            (falcon, ("falcon-colour", "about-colour")),
            (st_mono, ("st-high-mono", "about")),
            (st_med, ("st-medium-colour",)),
            (window, ("WINDOW",))]
    for run, names in runs:
        if any(want(n) for n in names):
            print(run.__name__, flush=True)
            run(os.path.join(a.work, run.__name__), want)


if __name__ == "__main__":
    main()
