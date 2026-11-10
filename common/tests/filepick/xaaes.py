#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""FPTEST.PRG (make) under FreeMiNT with XaAES, a multitasking AES (common/tests/xaaes, make base there
once): double clicks in the file dialog's list.

    xaaes.py WORK [EMUTOS.IMG] [--display :105] [--mem-prot]

One click on a folder only selects it; a double click on a folder opens it; a double click on a file takes it
(guidelines 5.8). Before 9 Nov 2026 a double click was dropped whole there: filepick asks evnt_multi_ for one
click (1, 1, 1) and only 0x101 was turned into the two clicks XaAES wants. The rows are at the places the
dialog says on the console ("FPT geom ... list=x,y,w,h"; a row is a character cell high, 16 pixels); the run
fails when the dialog is somewhere else. --mem-prot runs XaAES's rig with memory protection on (run.py's).
"""
import os
import re
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
XAAES = os.path.join(HERE, "..", "xaaes")
sys.path.insert(0, os.path.join(HERE, "..", "..", "tools"))
import machines  # noqa: E402

# the dialog on the Falcon's VGA screen, as the console says it: the list's well and its first row's middle
LIST_Y = 181
ROW = 16


def slim(work):
    """the run's disks are 200 MB (a copy of the C: drive and its image): gone once the log is read"""
    for n in ("hd", "hd.img", "ext2.img"):
        p = os.path.join(work, n)
        if os.path.isdir(p):
            shutil.rmtree(p, ignore_errors=True)
        elif os.path.exists(p):
            os.remove(p)


def row_y(k):
    return LIST_Y + 1 + k * ROW + ROW // 2


LONG = ["A long file name.mp3", "Mixed Case Song - Artist (2019 remix).MP3",
        "short.wav", "very_long_filename_without_spaces_0123456789_0123456789.wav"]
LONGDIR = "Long Folder Name With Spaces"


MEM_PROT = []		# ["--mem-prot"]: run.py's memory protection


def long_names(work, img, display, prg):
    """the dialog on a folder of an ext2 drive (run.py --ext2, D:) with long names: every music file is listed
    whole (^A takes them all) and a folder of a long name opens and gives its file. Before 9 Nov 2026 the
    list held only the names that fit 8+3, on MiNT with Fsfirst"""
    src = os.path.join(work, "ext2src")
    d = os.path.join(src, "fptlong")
    os.makedirs(os.path.join(d, LONGDIR))
    for n in LONG + ["notes.txt", ".hidden.mp3", LONGDIR + "/inner file in folder.mp3"]:
        with open(os.path.join(d, n), "wb") as f:
            f.truncate(100)
    with open(os.path.join(src, "lfnprobe.txt"), "w") as f:
        f.write("probe\n")
    steps = ["until FPT open 150", "wait 3", "shot", "ctrl a", "wait 2", "shot", "key 0x1c", "until FPT dir 30"]
    cmd = [sys.executable, os.path.join(XAAES, "run.py"), os.path.join(work, "x"), img, prg, "--ext2", src,
           "--display", display, "--steps", ";".join(steps)] + MEM_PROT
    r = subprocess.run(cmd, cwd=XAAES, capture_output=True, text=True)
    print(r.stdout[-300:])
    log = open(os.path.join(work, "x", "hatari.log"), errors="replace").read()
    slim(os.path.join(work, "x"))
    took = [l[9:] for l in log.splitlines() if l.startswith("FPT took ")]
    fails = []
    m = re.match(r"([a-p]):\\fptlong\\", took[0]) if took else None
    drive = m.group(1) if m else "d"
    want = ["%s:\\fptlong\\%s" % (drive, n) for n in LONG]
    if took != want:
        fails.append("^A took %s, wanted %s" % (took, want))
    print("  ext2, long names: took %d of %d files" % (len(took), len(want)))
    return fails


def long_folder(work, img, display, prg):
    """a double click on a folder with a long name opens it; a double click on its file takes it (the full path)"""
    steps = ["until FPT open 150", "wait 3", "dclick 300 %d" % row_y(0), "wait 3", "shot",
             "dclick 300 %d" % row_y(0), "until FPT dir 30"]
    cmd = [sys.executable, os.path.join(XAAES, "run.py"), os.path.join(work, "x2"), img, prg, "--ext2",
           os.path.join(work, "ext2src"), "--display", display, "--steps", ";".join(steps)] + MEM_PROT
    r = subprocess.run(cmd, cwd=XAAES, capture_output=True, text=True)
    print(r.stdout[-300:])
    log = open(os.path.join(work, "x2", "hatari.log"), errors="replace").read()
    slim(os.path.join(work, "x2"))
    took = [l[9:] for l in log.splitlines() if l.startswith("FPT took ")]
    ok = len(took) == 1 and re.fullmatch(r"[a-p]:\\fptlong\\%s\\inner file in folder.mp3" % LONGDIR, took[0])
    print("  ext2, long folder: took %s" % took)
    return [] if ok else ["the long folder's file: took %s" % took]


def system_selector(work, img, display, prg):
    """"Use the system file selector" (filepick_system, C:\\FPSYS.TXT for FPTEST.PRG): XaAES's own selector opens, not
    our dialog; a double click on a file takes it once (full path, the folder first in recent); Cancel takes nothing and
    leaves dir and recent"""
    fails = []
    src = os.path.join(work, "sys")
    music = os.path.join(src, "MUSIC")
    os.makedirs(music)
    for n in ("01_NIGHT.MP3", "LIVE.M3U", "README.TXT"):
        with open(os.path.join(music, n), "wb") as f:
            f.truncate(100)
    with open(os.path.join(src, "FPSYS.TXT"), "w") as f:
        f.write("1")
    # XaAES's selector lists . and .. and the folders first: the rows of the screen at 640 x 480
    for how, step in (("take", "dclick 300 216"), ("cancel", "move 450 418; click 458 422")):
        w = os.path.join(work, how)
        steps = ["until FPT open 150", "wait 4", step, "until FPT dir 30"]
        cmd = [sys.executable, os.path.join(XAAES, "run.py"), w, img, prg, "--copy", music,
               os.path.join(src, "FPSYS.TXT"), "--display", display, "--steps", ";".join(steps)] + MEM_PROT
        r = subprocess.run(cmd, cwd=XAAES, capture_output=True, text=True)
        print(r.stdout[-200:])
        log = open(os.path.join(w, "hatari.log"), errors="replace").read()
        slim(w)
        took = [l[9:] for l in log.splitlines() if l.startswith("FPT took ")]
        dirl = [l for l in log.splitlines() if l.startswith("FPT dir")]
        if how == "take":
            ok = took == ["C:\\MUSIC\\LIVE.M3U"] and "FPT done 1, idle never" in log and \
                dirl == ["FPT dir C:\\MUSIC\\; recent C:\\MUSIC\\"]
        else:
            ok = not took and "FPT done 0, idle never" in log and \
                dirl == ["FPT dir C:\\MUSIC\\; recent C:\\MUSIC\\LIVE\\"]
        if " picture add " in log:
            ok = False
        print("  XaAES system selector, %s: took %s, %s" % (how, took, dirl))
        if not ok:
            fails.append("the system selector, %s: took %s, %s" % (how, took, dirl))
    return fails


def main():
    a = [x for x in sys.argv[1:] if not x.startswith("--")]
    opt = {}
    args = sys.argv[1:]
    for i, x in enumerate(args):
        if x == "--display":
            opt["display"] = args[i + 1]
            a.remove(args[i + 1])
    if not a:
        sys.exit(__doc__)
    if "--mem-prot" in args:
        MEM_PROT.append("--mem-prot")
    work = os.path.abspath(a[0])
    img = a[1] if len(a) > 1 else machines.emutos()
    display = opt.get("display", ":105")
    prg = os.path.join(HERE, "build", "atari", "FPTEST.PRG")
    if not os.path.exists(prg):
        sys.exit("no FPTEST.PRG: make")
    if not os.path.isdir(os.path.join(XAAES, "build", "base")):
        sys.exit("no XaAES base: make base in common/tests/xaaes")
    shutil.rmtree(work, ignore_errors=True)
    os.makedirs(work)
    src = tempfile.mkdtemp(prefix="fpx", dir=work)
    music = os.path.join(src, "MUSIC")
    os.makedirs(os.path.join(music, "LIVE"))
    os.makedirs(os.path.join(music, "BONUS"))
    for n in ("LIVE/X.MP3", "01_NIGHT.MP3"):
        with open(os.path.join(music, n), "wb") as f:
            f.truncate(100)
    # rows: BONUS, LIVE, 01_NIGHT.MP3; then LIVE's: X.MP3
    steps = ["until FPT open 150", "wait 3",
             "click 300 %d" % row_y(1), "wait 3", "shot",
             "dclick 300 %d" % row_y(1), "wait 3", "shot",
             "dclick 300 %d" % row_y(0), "until FPT dir 30"]
    cmd = [sys.executable, os.path.join(XAAES, "run.py"), os.path.join(work, "x"), img, prg, "--copy", music,
           "--display", display, "--steps", ";".join(steps)]
    cmd += MEM_PROT
    r = subprocess.run(cmd, cwd=XAAES, capture_output=True, text=True)
    print(r.stdout[-600:])
    log = open(os.path.join(work, "x", "hatari.log"), errors="replace").read()
    slim(os.path.join(work, "x"))
    fails = []
    g = re.findall(r"FPT geom (.*)", log)
    lst = re.search(r"list=(\d+),(\d+),(\d+),(\d+)", g[-1]) if g else None
    if not lst:
        fails.append("the dialog never came (no geometry on the console)")
    elif int(lst.group(2)) != LIST_Y:
        fails.append("the list is at y=%s, the steps click for %d" % (lst.group(2), LIST_Y))
    folders = log.count(" picture folder ")
    took = [l[9:] for l in log.splitlines() if l.startswith("FPT took ")]
    shots = sorted(os.listdir(os.path.join(work, "x", "shots")))
    if len(shots) >= 2:
        p = subprocess.run(["compare", "-metric", "AE", os.path.join(work, "x", "shots", shots[0]),
                            os.path.join(work, "x", "shots", shots[1]), "null:"], capture_output=True, text=True)
        try:
            if int(float(p.stderr.split()[0])) == 0:
                fails.append("one click on a folder selected nothing")
        except (ValueError, IndexError):
            pass
    if folders != 1:
        fails.append("%d folder pictures (one click must not open a folder, one double click must)" % folders)
    if took != ["C:\\MUSIC\\LIVE\\X.MP3"]:
        fails.append("a double click on a file took %s" % took)
    print("  XaAES: folders opened %d, took %s" % (folders, took))
    fails += long_names(os.path.join(work, "long"), img, display, prg)
    fails += long_folder(os.path.join(work, "long"), img, display, prg)
    fails += system_selector(os.path.join(work, "selector"), img, display, prg)
    for f in fails:
        print("FAIL  " + f)
    print("all passed" if not fails else "%d failed" % len(fails))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
