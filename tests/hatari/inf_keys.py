#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""CLAUDEST.INF's window and pane keys (D43, D44; 17 Nov 2026), four
boots on one machine:

  1. SIDEBAR=200, the key before 2.11: read once; the INF is written at
     the start with PANES=200 and without SIDEBAR=.
  1b. HEBFONT=2, a setting gone in 2.11 (17 Nov 2026): read once;
     the INF is written at the start without it.
  2. WINDOW= off the screen: the window opens on the whole desk, the same
     picture as a start with no WINDOW=; a quit without a move writes
     nothing.
  3. WINDOW= on the screen: the window opens there (the desktop shows
     beside it); a quit without a move writes nothing.
  4. no WINDOW=: the window moved by its title bar, then ^Q: WINDOW=
     written, the new place.

    python3 tests/hatari/inf_keys.py WORK [--machine ST:mono] [--nvdi] [--prg PRG]

The gate runs it on Falcon-4:16 under NVDI (the guidelines, 1.2: 60% of
the boots under NVDI).
"""
import argparse
import os
import shutil
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, os.path.join(ROOT, "common", "tests", "speed"))
import speed  # noqa: E402

NVDI = False
BASE = "[OPTIONS]\r\nHOST=127.0.0.1\r\nPORT=2323\r\nNATFEATS=1\r\n"
fails = 0


def check(ok, what, detail=""):
    global fails
    fails += not ok
    print("%s %s%s" % ("ok   " if ok else "FAIL ", what, ("  (" + detail + ")") if detail else ""), flush=True)


def inf_file(hd):
    for f in os.listdir(hd):
        if f.upper() == "CLAUDEST.INF":
            return os.path.join(hd, f)
    return None


def boot(work, name, machine, prg, inf, user):
    """one boot with INF; user(r) does the rest, then ^Q; returns (start
    picture, the INF's text before the quit, after it, its mtime before
    and after)"""
    w = os.path.join(work, name)
    shutil.rmtree(w, ignore_errors=True)
    r = speed.Rig(w, machine, nvdi=NVDI, extra=["--drive-led", "no"])
    p = os.path.join(w + "-p", "CLAUDEST.PRG")
    os.makedirs(os.path.dirname(p), exist_ok=True)
    shutil.copy(prg, p)
    r.put(p)
    r.extra += r.m.network()
    with open(os.path.join(r.hd, "CLAUDEST.INF"), "w", newline="") as f:
        f.write(inf)
    bridge = subprocess.Popen([sys.executable, os.path.join(HERE, "speed_bridge.py"), "--backend", "demo",
                               "--tcp", "127.0.0.1:2323"],
                              stdout=open(w + "-bridge.log", "w"), stderr=subprocess.STDOUT)
    try:
        r.start("C:\\CLAUDEST.PRG", ready="")
        # online: the bridge has answered its first questions (under NVDI
        # the fonts load for a minute behind a still screen)
        t = time.time() + 180
        while time.time() < t and "session usage" not in open(w + "-bridge.log", errors="replace").read():
            time.sleep(2)
        time.sleep(5)
        r.still()
        start = shutil.copy(r.shot("start"), w + "-start.png")
        f = inf_file(r.hd)
        before = (open(f, errors="replace").read(), os.path.getmtime(f))
        user(r)
        r.key("ctrl+q")
        time.sleep(8)
        r.still()
        f = inf_file(r.hd)
        after = (open(f, errors="replace").read(), os.path.getmtime(f))
        named = r.describe()
    finally:
        r.stop()
        bridge.kill()
    return start, before, after, named


def same_pictures(a, b):
    r = subprocess.run(["compare", "-metric", "AE", a, b, "null:"], capture_output=True, text=True)
    try:
        return int(float(r.stderr.split()[0])) == 0
    except (ValueError, IndexError):
        return False


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("work")
    ap.add_argument("--machine", default="ST:mono")
    ap.add_argument("--nvdi", action="store_true", help="under NVDI (a Falcon or the TT)")
    ap.add_argument("--prg", default=os.path.join(ROOT, "binary", "CLAUDEST.PRG"))
    a = ap.parse_args()
    global NVDI
    NVDI = a.nvdi
    work = os.path.abspath(a.work)
    os.makedirs(work, exist_ok=True)
    prg = os.path.abspath(a.prg)

    def nothing(r):
        pass

    # 1. the old key
    _, before, _, named = boot(work, "1-sidebar", a.machine, prg, BASE + "SIDEBAR=200\r\n", nothing)
    print("on", named)
    k = keys_of(before[0])
    check(k.get("PANES") == "200" and "SIDEBAR" not in k, "SIDEBAR=200 read once: written at the start as PANES=200",
          "PANES=%s SIDEBAR=%s" % (k.get("PANES"), k.get("SIDEBAR")))

    # 1b. the Hebrew setting, gone
    _, before, _, _ = boot(work, "1b-hebfont", a.machine, prg, BASE + "HEBFONT=2\r\n", nothing)
    k = keys_of(before[0])
    check("HEBFONT" not in k and k.get("PORT") == "2323", "HEBFONT=2 read once: the INF written at the start without it",
          "HEBFONT=%s" % k.get("HEBFONT"))

    # 2. off the screen, against no WINDOW= at all
    plain, _, _, _ = boot(work, "2a-none", a.machine, prg, BASE, nothing)
    off, before, after, _ = boot(work, "2b-off", a.machine, prg, BASE + "WINDOW=2000,1500,560,300\r\n", nothing)
    check(same_pictures(plain, off), "WINDOW= off the screen: the whole desk, as with no WINDOW=")
    check(before == after, "a quit without a move writes nothing")

    # 3. on the screen
    on, before, after, _ = boot(work, "3-on", a.machine, prg, BASE + "WINDOW=48,60,520,300\r\n", nothing)
    check(not same_pictures(plain, on), "WINDOW= on the screen: the window where it was kept")
    check(before == after, "a quit without a move writes nothing")

    # 4. moved by its title bar
    def move(r):
        g = dict(ln.split("=") for ln in r.xdo("getwindowgeometry", "--shell", r.win()).split())
        x0, y0 = int(g["X"]), int(g["Y"])
        r.xdo("mousemove", str(x0 + 300), str(y0 + 30))
        time.sleep(0.5)
        r.xdo("mousedown", "1")
        time.sleep(0.5)
        for step in range(1, 9):
            r.xdo("mousemove", str(x0 + 300 - step * 4), str(y0 + 30 + step * 5))
            time.sleep(0.2)
        time.sleep(0.5)
        r.xdo("mouseup", "1")
        time.sleep(1)
        r.still()
        shutil.copy(r.shot("moved"), os.path.join(work, "4-moved.png"))
    _, before, after, _ = boot(work, "4-move", a.machine, prg, BASE, move)
    k = keys_of(after[0])
    check("WINDOW" in k and "WINDOW" not in keys_of(before[0]), "moved, then ^Q: WINDOW= written",
          "WINDOW=%s" % k.get("WINDOW"))
    print("inf_keys: %s" % ("all ok" if not fails else "%d failed" % fails))
    return 1 if fails else 0


def keys_of(text):
    out = {}
    for ln in text.splitlines():
        if "=" in ln and not ln.startswith(";"):
            kk, _, v = ln.partition("=")
            out[kk.strip().upper()] = v.strip()
    return out


if __name__ == "__main__":
    sys.exit(main())
