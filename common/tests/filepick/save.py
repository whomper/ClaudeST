#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""FPTEST.PRG (make) in save mode, filepick_save (C:\\FPSAVE.TXT there): the dialog opened on C:\\MUSIC\\ with
"PLAYLIST.M3U" proposed, the types Playlists and All files. "LIVE" typed over it with the keys' place on a
folder and Return asks to replace LIVE.M3U (it saves, never opens the folder), Return again is Cancel. Down three times reaches LIVE.M3U (the folders first),
whose name goes to the field; Return asks "LIVE.M3U is there already. Replace it?", Return again is Cancel and
the dialog stays; the field emptied with Esc, "PARTY" typed, Return saves as C:\\MUSIC\\PARTY.M3U (the
extension added). Checks: each step changed the screen, the path saved, idle() called, no CRASH.LOG. A
screenshot each step in WORK/shots.

    save.py WORK MACHINE [--nvdi]
"""
import os
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import run  # noqa: E402  (its files, dates and the rig)


def session(work, machine, nvdi, prg, display):
    run.scale_for(machine)
    r = run.speed.Rig(work, machine, nvdi=nvdi, display=display)
    fails, shots = [], {}
    try:
        music = os.path.join(r.hd, "MUSIC")
        for name, size in run.FILES:
            p = os.path.join(music, *name.rstrip("\\").split("\\"))
            if name.endswith("\\"):
                os.makedirs(p, exist_ok=True)
            else:
                os.makedirs(os.path.dirname(p), exist_ok=True)
                with open(p, "wb") as f:
                    f.truncate(size)
        for d, ds, fs in os.walk(music):
            for n in ds + fs:
                os.utime(os.path.join(d, n), (run.WHEN, run.WHEN))
        open(os.path.join(r.hd, "FPSAVE.TXT"), "w").write("save\n")
        run.shutil.copy(prg, os.path.join(r.hd, "FPTEST.PRG"))
        r.start("C:\\FPTEST.PRG", ready="picture add", boot=900)
        if "picture add" not in r.console():
            fails.append("the dialog never came")
            return fails, shots
        run.move(r, 5, 5)
        r.still(most=5)
        prev = shots["1-save-open"] = r.shot("1-save-open")
        # the name typed with the keys' place on a folder, nothing chosen: Return saves (the replace question
        # here), never opens the folder; Return again is Cancel
        steps = [("1b-typed", ["Escape", "type:LIVE"]), ("1c-replace", ["Return"]), ("1d-kept", ["Return"]),
                 ("2-chosen", ["Down", "Down", "Down"]), ("3-replace", ["Return"]), ("4-kept", ["Return"]),
                 ("5-emptied", ["Escape"]), ("6-typed", ["type:PARTY"])]
        for name, keys in steps:
            for k in keys:
                if k.startswith("type:"):
                    r.type(k[5:])
                else:
                    r.key(k)
            r.still(most=20)
            shots[name] = r.shot(name)
            if run.same(prev, shots[name]):
                fails.append("%s: the screen didn't change" % name)
            prev = shots[name]
        r.key("Return")
        t_end = time.time() + 60
        while time.time() < t_end and "FPT saved" not in r.console():
            time.sleep(1)
        con = r.console()
        if "FPT saved 1 C:\\MUSIC\\PARTY.M3U" not in con:
            fails.append("saved: %s" % [l for l in con.splitlines() if l.startswith("FPT saved")])
        if "idle called" not in con and "FPT done" in con:
            fails.append("idle() never called")
        for d, _, fs in os.walk(r.hd):
            if any(f.upper() == "CRASH.LOG" for f in fs):
                fails.append("CRASH.LOG in %s" % d)
        print("  save mode on %s" % r.describe())
    finally:
        r.stop()
    return fails, shots


def main():
    a = [x for x in sys.argv[1:] if not x.startswith("--")]
    if len(a) < 2:
        print(__doc__)
        return 2
    work, machine = a[0], a[1]
    prg = os.path.join(HERE, "build", "atari", "FPTEST.PRG")
    fails, _ = session(work, machine, "--nvdi" in sys.argv, prg, run.DISPLAY)
    for f in fails:
        print("FAIL  " + f)
    print("all passed" if not fails else "%d failed" % len(fails))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
