#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Erez Yaary
"""The crash catcher's check in Hatari (EmuTOS), on a 68000 ST and a
68030 Falcon: each kind of crash in CRASHT.PRG, then a crash in
supervisor mode, a warm reset and the report of it at the next start.

    make && python3 run.py etos512us.img [work-dir]
"""
import os, shutil, subprocess, sys, time

HERE = os.path.dirname(os.path.abspath(__file__))
PRG = os.path.join(HERE, "build", "CRASHT.PRG")
tos = os.path.abspath(sys.argv[1])
work = os.path.abspath(sys.argv[2] if len(sys.argv) > 2 else os.path.join(HERE, "build", "work"))
failed = 0


def check(ok, what):
    global failed
    print("%s %s" % ("ok  " if ok else "FAIL", what))
    if not ok:
        failed += 1


def run(machine, kind, reset=False):
    d = os.path.join(work, "%s-%s" % (machine, kind))
    shutil.rmtree(d, ignore_errors=True)
    os.makedirs(d)
    shutil.copy(PRG, os.path.join(d, "CRASHT.PRG"))
    with open(os.path.join(d, "CRASHT.INF"), "w") as f:
        f.write(kind)
    fifo = os.path.join(d, "cmd.fifo")
    log = os.path.join(d, "hatari.log")
    args = ["hatari", "--machine", machine, "--tos", tos, "--harddrive", d,
            "--auto", "C:\\CRASHT.PRG", "--fast-boot", "yes", "--natfeats", "yes",
            "--sound", "off", "--confirm-quit", "no", "--statusbar", "no",
            "--cmd-fifo", fifo, "--log-level", "fatal"]
    if machine == "st":
        args += ["--mono"]
    else:
        args += ["--monitor", "vga"]
    env = dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy")
    h = subprocess.Popen(args, env=env, stdout=open(log, "w"), stderr=subprocess.STDOUT)
    want = "CRASHT previous" if reset else "CRASHT cleanup"
    t_end = time.time() + 60
    reset_sent = False
    try:
        while time.time() < t_end and h.poll() is None:
            time.sleep(0.5)
            text = open(log, errors="replace").read()
            if want in text or "CRASHT no crash" in text:
                time.sleep(1)
                break
            if reset and not reset_sent and os.path.exists(os.path.join(d, "STAGE1.TXT")):
                time.sleep(3)			# the bombs, then back at the desktop
                with open(fifo, "w") as f:
                    f.write("hatari-shortcut warmreset\n")
                reset_sent = True
    finally:
        if h.poll() is None:
            h.terminate()
            h.wait()
    text = open(log, errors="replace").read()
    rep = os.path.join(d, "CRASH.LOG")
    return d, text, open(rep, "rb").read().decode("latin-1") if os.path.exists(rep) else ""


NAMES = {"b": "bus error, 2 bombs", "a": "address error, 3 bombs",
         "i": "illegal instruction, 4 bombs", "z": "division by zero, 5 bombs"}

for machine in ("st", "falcon"):
    for kind in "biza":
        if kind == "a" and machine != "st":
            continue			# a 68030 reads odd addresses
        d, con, rep = run(machine, kind)
        tag = "%s %s:" % (machine, NAMES[kind])
        check("CRASHT crashing" in con and "CRASHT no crash" not in con, tag + " it crashed")
        check("CrashTest 1.0: " + NAMES[kind] in rep, tag + " CRASH.LOG names it")
        check("(CrashTest+$" in rep.split("\r\n")[2] if rep.count("\r\n") > 2 else False,
              tag + " where, as an offset into the program")
        check("D0-D7" in rep and "A0-A6" in rep and "USP" in rep, tag + " the registers")
        check("last status: about to crash" in rep, tag + " the last status")
        cl = os.path.join(d, "CLEANUP.TXT")
        check(os.path.exists(cl) and NAMES[kind] in open(cl).read(), tag + " crash_cleanup called")
        rp = os.path.join(d, "REPORT.TXT")
        check(os.path.exists(rp) and open(rp, "rb").read().decode("latin-1") == rep,
              tag + " crash_report got the report")
    d, con, rep = run(machine, "s", reset=True)
    tag = "%s supervisor bus error:" % machine
    check("CRASHT previous" in con, tag + " the program ran again after a reset")
    check("found TOS's record of a crash before this run" in rep and "bus error, 2 bombs at" in rep,
          tag + " CRASH.LOG has TOS's record")
    check("CRASHT previous bus error, 2 bombs" in con, tag + " counted as the program's (it was in its code)")
print("%d failed" % failed)
sys.exit(1 if failed else 0)
