#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""An app's crash catcher in Hatari (EmuTOS), with a build made with
-DCRASH_TEST (crash_test() in its keyboard handler):

  1. F1, a bus error in the program: the app's alert, CRASH.LOG, and
     back to the desktop with the emulator still running;
  2. F2, a bus error in supervisor mode: the bombs; after a warm reset
     the app starts again, says it crashed last time and, with Return
     (Show), shows the report.

    app_crash.py WORK EMUTOS.IMG APP.PRG NAME [--machine falcon|st] [--memsize MB]
                 [--wait SECONDS] [--log DIR] [--copy FILE ...]

NAME is the program's name in the report ("ECal"); --log is the folder
of CRASH.LOG on drive C:, relative to it (default: the root); --copy
puts more files (an INF) on drive C:. Screenshots go to WORK/*/shots.
"""
import argparse, os, shutil, subprocess, sys, time

ap = argparse.ArgumentParser()
ap.add_argument("work")
ap.add_argument("tos")
ap.add_argument("prg")
ap.add_argument("name")
ap.add_argument("--machine", default="falcon")
ap.add_argument("--memsize", help="MB (default: 14 on a Falcon, Hatari's own on an ST)")
ap.add_argument("--wait", type=float, default=15)
ap.add_argument("--log", default="")
ap.add_argument("--copy", nargs="*", default=[])
a = ap.parse_args()
failed = 0


def check(ok, what):
    global failed
    print("%s %s: %s" % ("ok  " if ok else "FAIL", a.name, what))
    if not ok:
        failed += 1


class Run:
    def __init__(self, tag):
        self.d = os.path.join(os.path.abspath(a.work), tag)
        shutil.rmtree(self.d, ignore_errors=True)
        hd = os.path.join(self.d, "hd")
        self.shots = os.path.join(self.d, "shots")
        os.makedirs(hd)
        os.makedirs(self.shots)
        prg = os.path.basename(a.prg).upper()
        shutil.copy(a.prg, os.path.join(hd, prg))
        for f in a.copy:
            if os.path.isdir(f):
                shutil.copytree(f, os.path.join(hd, os.path.basename(f)))
            else:
                shutil.copy(f, hd)
        self.logfile = os.path.join(hd, a.log, "CRASH.LOG")
        self.fifo = os.path.join(self.d, "cmd.fifo")
        args = ["hatari", "--machine", a.machine, "--tos", os.path.abspath(a.tos), "--harddrive", hd,
                "--auto", "C:\\" + prg, "--fast-boot", "yes", "--natfeats", "yes", "--sound", "off",
                "--confirm-quit", "no", "--statusbar", "no", "--cmd-fifo", self.fifo,
                "--screenshot-dir", self.shots, "--log-level", "fatal"]
        args += ["--mono"] if a.machine == "st" else ["--monitor", "vga", "--dsp", "none"]
        mem = a.memsize or ("14" if a.machine == "falcon" else None)
        if mem:
            args += ["--memsize", mem]
        env = dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy")
        self.h = subprocess.Popen(args, env=env, stdout=open(os.path.join(self.d, "hatari.log"), "w"),
                                  stderr=subprocess.STDOUT)
        time.sleep(3)				# a screenshot asked for too early can crash Hatari

    def cmd(self, c):
        with open(self.fifo, "w") as f:
            f.write(c + "\n")
        time.sleep(0.1)

    def key(self, scan):
        self.cmd("hatari-event keypress 0x%02x" % scan)

    def shot(self):
        self.cmd("hatari-shortcut screenshot")
        time.sleep(1)

    def alive(self):
        return self.h.poll() is None

    def log(self):
        return open(self.logfile, "rb").read().decode("latin-1") if os.path.exists(self.logfile) else ""

    def end(self):
        if self.alive():
            self.h.terminate()
            self.h.wait()


# 1: a crash in the program
r = Run("f1")
try:
    time.sleep(a.wait)
    r.shot()				# the app running
    r.key(0x3b)				# F1
    time.sleep(4)
    r.shot()				# its alert
    rep = r.log()
    check("%s " % a.name in rep and ": bus error, 2 bombs" in rep, "F1: CRASH.LOG names the crash")
    check("(%s+$" % a.name in rep, "F1: where, as an offset into the program")
    r.key(0x1c)				# Return: Quit
    time.sleep(4)
    r.shot()				# the desktop
    check(r.alive(), "F1: the emulator still runs")
finally:
    r.end()

# 2: a crash in supervisor mode, then the next start
r = Run("f2")
try:
    time.sleep(a.wait)
    r.key(0x3c)				# F2: the bombs
    time.sleep(5)
    r.shot()
    r.cmd("hatari-shortcut warmreset")
    time.sleep(a.wait + 3)
    r.shot()				# "crashed last time", offering to show it
    r.key(0x1c)				# Return: Show, the default
    time.sleep(3)
    r.shot()				# the report, in a box (crash_offer)
    r.key(0x1c)				# OK
    time.sleep(2)
    rep = r.log()
    check("found TOS's record of a crash before this run" in rep and "bus error, 2 bombs at" in rep,
          "F2: CRASH.LOG has TOS's record after the reset")
    check(r.alive(), "F2: the emulator still runs")
finally:
    r.end()
print("%d failed; screenshots in %s" % (failed, os.path.abspath(a.work)))
sys.exit(1 if failed else 0)
