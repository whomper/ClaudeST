#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""ui/notify.c under FreeMiNT 1.17 and XaAES (../xaaes), against BUBBLE.PRG,
a stand-in for BGEM.NG that does what bgemng.hyp's API page says
(BGEM.NG 1.02 itself stops with a bus error in this FreeMiNT and XaAES):
NOTET.PRG sends a notification with two buttons, as an app does for new
mail.

  1. the stand-in is asked at start (notify_ask: BGEMNG_INQUIRE) and answers;
  2. the notification goes once the answer came: BUBBLEGEM_SHOW, our id,
     0x10 (a notification), the text read from NOTET's global memory by
     another program, header and buttons as BGEM.NG wants them;
  3. its ACK, then the click on the first button: notify_clicked(42, 1);
  4. without a BUBBLE: notify_send gives 0, nothing else happens.

    make prg && python3 notify.py WORK EMUTOS.IMG [--display :97]
"""
import argparse, os, subprocess, sys

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


def run(name, bubble):
    w = os.path.join(a.work, name)
    extra = ["--copy", os.path.join(B, "BUBBLE.PRG"), "--cnf", "run c:\\BUBBLE.PRG"] if bubble else []
    subprocess.run([sys.executable, os.path.join(HERE, "..", "xaaes", "run.py"), w, a.tos,
                    os.path.join(B, "NOTET.PRG"), "--display", a.display, *extra,
                    "--steps", "until NOTET 120;until NOTET done 60"],
                   stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT)
    return open(os.path.join(w, "hatari.log"), errors="replace").read()


log = run("bubble", True)
check("BUBBLE asked by" in log, "BUBBLE asked first (BGEMNG_INQUIRE)")
check("NOTET features f" in log, "... its answer came to NOTET")
check("NOTET sent 1" in log, "notify_send gives 1")
want = 'BUBBLE show 42 10 [ Title="New mail" From="NOTET   " ][0][3 new messages|from the test][Open|Later]'
check(want in log, "the notification: id 42, 0x10, the text as BGEM.NG wants it, read by another program")
check(log.find("BUBBLE asked") < log.find("BUBBLE show"), "... sent after the answer")
check("NOTET ack" in log, "its ACK came back")
check("NOTET clicked 42 1" in log, "the first button's click: notify_clicked(42, 1)")
log = run("none", False)
check("NOTET sent 0" in log, "no BUBBLE: notify_send gives 0")
check("NOTET features" not in log and "NOTET ack" not in log, "... and nothing comes back")
print("%d failed" % failed)
sys.exit(1 if failed else 0)
