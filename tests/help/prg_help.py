#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""The Help key under a multitasking AES (FreeMiNT 1.17 and XaAES,
../xaaes): with no viewer running, help.c starts EHYPVIEW.PRG beside the
app, found by FOLDER= in the boot drive's EHYPVIEW.INF, with the request
on its command line (Erez, 25 Oct 2026).

  1. EHYPVIEW.INF says FOLDER=C:\\EHV, and C:\\EHV\\EHYPVIEW.PRG is the
     stand-in FAKEPRG.PRG: it is started and gets "C:\\TEST.HYP Keys"
     (through cmd_line(): XaAES passes it the ARGV way);
  2. no EHYPVIEW.INF: nothing is started, help_show() gives 0.

    make prg && python3 prg_help.py WORK EMUTOS.IMG [--display :97]
"""
import argparse, os, shutil, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
B = os.path.join(HERE, "build")
ap = argparse.ArgumentParser()
ap.add_argument("work")
ap.add_argument("tos")
ap.add_argument("--display", default=":97")
a = ap.parse_args()
failed = 0


def check(ok, what):
    global failed
    print("%s %s" % ("ok  " if ok else "FAIL", what), flush=True)
    failed += not ok


def run(name, inf):
    files = os.path.join(a.work, name + "-files")
    shutil.rmtree(files, ignore_errors=True)
    os.makedirs(os.path.join(files, "EHV"))
    shutil.copy(os.path.join(B, "FAKEPRG.PRG"), os.path.join(files, "EHV", "EHYPVIEW.PRG"))
    with open(os.path.join(files, "TEST.HYP"), "wb") as f:
        f.write(b"HDOC")
    copy = [os.path.join(files, "EHV"), os.path.join(files, "TEST.HYP")]
    if inf:
        with open(os.path.join(files, "EHYPVIEW.INF"), "wb") as f:
            f.write(b"; EHypView settings\r\nSTART=opened\r\n\r\nFOLDER=C:\\EHV\r\n\r\n")
        copy.append(os.path.join(files, "EHYPVIEW.INF"))
    w = os.path.join(a.work, name)
    subprocess.run([sys.executable, os.path.join(HERE, "..", "xaaes", "run.py"), w, a.tos,
                    os.path.join(B, "HELPT.PRG"), "--copy", *copy, "--display", a.display,
                    "--steps", "until HELPT 90;wait 8"], stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT)
    return open(os.path.join(w, "hatari.log"), errors="replace").read()


log = run("found", True)
check("HELPT shown 1" in log, "EHYPVIEW.INF names the folder: help_show() gives 1")
check("FAKEPRG args C:\\TEST.HYP Keys" in log, "EHYPVIEW.PRG started with \"C:\\TEST.HYP Keys\"")
log = run("none", False)
check("HELPT shown 0" in log, "no EHYPVIEW.INF: help_show() gives 0")
check("FAKEPRG" not in log, "... and nothing is started")
print("%d failed" % failed)
sys.exit(1 if failed else 0)
