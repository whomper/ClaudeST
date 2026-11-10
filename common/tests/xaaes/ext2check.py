#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""ext2check.py WORK EMUTOS.IMG [--display :88]: the rig's own check of its
ext2 drive (run.py --ext2; make ext2check). EXT2TEST.PRG, under FreeMiNT and
XaAES, finds probe.txt that the rig put on the ext2 drive in lowercase, fails
to open PROBE.TXT (the drive keeps case apart, as a tester's: the guidelines,
6.6) and creates made.txt, which must come back off the image under that name.
Twice: from C:, and from the ext2 drive itself as ext2test.prg (run.py
--start d:\\ext2test.prg), as an app's release folder is run there.
Prints ok/FAIL lines; exits 1 when one failed."""
import argparse, os, shutil, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("work")
    ap.add_argument("tos")
    ap.add_argument("--display", default=":88")
    ap.add_argument("--mem-prot", action="store_true", help="run.py's --mem-prot")
    a = ap.parse_args()
    bad = 0
    prg = os.path.join(HERE, "build", "EXT2TEST.PRG")
    for where in ("c", "d"):
        work = os.path.abspath(a.work) + "-" + where
        src = work + "-src"
        shutil.rmtree(src, ignore_errors=True)
        os.makedirs(src)
        with open(os.path.join(src, "probe.txt"), "w", newline="") as f:
            f.write("lowercase probe\r\n")
        with open(os.path.join(src, "UPPER.TXT"), "w", newline="") as f:
            f.write("a name in capitals stays so\r\n")
        args = [sys.executable, os.path.join(HERE, "run.py"), work, a.tos]
        if where == "c":
            print("EXT2TEST.PRG on C:, the ext2 drive D: beside it", flush=True)
            args.append(prg)
        else:
            print("ext2test.prg on the ext2 drive, started from there", flush=True)
            shutil.copy(prg, os.path.join(src, "ext2test.prg"))
            args += ["--start", "d:\\ext2test.prg"]
        r = subprocess.run(args + ["--ext2", src, "--display", a.display, "--steps", "until EXT2 done 150"] +
                           (["--mem-prot"] if a.mem_prot else []),
                           capture_output=True, text=True)
        print(r.stdout, end="")
        bad += checks(work, r.returncode)
    print("all passed" if not bad else "%d failed" % bad)
    return 1 if bad else 0


def checks(work, rc):
    log = open(os.path.join(work, "hatari.log"), errors="replace").read() if os.path.exists(
        os.path.join(work, "hatari.log")) else ""
    names = open(os.path.join(work, "ext2.ls")).read().split() if os.path.exists(
        os.path.join(work, "ext2.ls")) else []
    bad = 0

    def check(cond, what):
        nonlocal bad
        print(("ok    " if cond else "FAIL  ") + what, flush=True)
        bad += not cond
    check("Ext2 filesystem driver" in log and "ext2 (main.c): loaded and ready" in log,
          "FreeMiNT loaded ext2.xfs")
    check("EXT2 probe.txt opened on " in log, "probe.txt, put there in lowercase, opens on the ext2 drive")
    check("EXT2 PROBE.TXT not found" in log, "PROBE.TXT doesn't: the drive keeps case apart")
    check("EXT2 DP_CASE 0" in log, "Dpathconf says the drive is case-sensitive (DP_CASE 0)")
    check("EXT2 made.txt created" in log, "EXT2TEST.PRG created made.txt")
    check("made.txt" in names and "MADE.TXT" not in names, "made.txt is on the image in lowercase")
    check("UPPER.TXT" in names and "probe.txt" in names, "the rig's names kept their case")
    check(rc == 0, "run.py ended well")
    return bad


if __name__ == "__main__":
    sys.exit(main())
