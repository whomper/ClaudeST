#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Erez Yaary
"""An app's Help key (common/ui/help.c) in Hatari (EmuTOS), with the
stand-in viewer build/FAKEVIEW.ACC (make) installed under viewers' names:

  1. EHypView and ST-Guide both running, NAME.HYP next to the app:
     EHypView gets "C:\\NAME.HYP" (and ST-Guide nothing);
  2. only ST-Guide: it gets "C:\\NAME.HYP";
  3. no NAME.HYP next to the app: "*:\\NAME.HYP";
  4. no viewer: whatever the app does then (a screenshot).

    app_help.py WORK EMUTOS.IMG APP.PRG NAME.HYP [--machine falcon|st] [--memsize MB]
                [--wait SECONDS] [--page PAGE] [--copy FILE ...]

--page: the page the app asks for (" PAGE" after the file name).
Screenshots go to WORK/*/shots.
"""
import argparse, os, shutil, subprocess, sys, time

HERE = os.path.dirname(os.path.abspath(__file__))
ap = argparse.ArgumentParser()
ap.add_argument("work")
ap.add_argument("tos")
ap.add_argument("prg")
ap.add_argument("hyp")
ap.add_argument("--machine", default="falcon")
ap.add_argument("--memsize", help="MB (default: 14 on a Falcon, Hatari's own on an ST)")
ap.add_argument("--wait", type=float, default=15)
ap.add_argument("--page", default="")
ap.add_argument("--copy", nargs="*", default=[])
a = ap.parse_args()
hyp = os.path.basename(a.hyp).upper()
failed = 0


def check(ok, what):
    global failed
    print("%s %s" % ("ok  " if ok else "FAIL", what))
    if not ok:
        failed += 1


def run(tag, viewers, with_hyp):
    d = os.path.join(os.path.abspath(a.work), tag)
    shutil.rmtree(d, ignore_errors=True)
    hd = os.path.join(d, "hd")
    shots = os.path.join(d, "shots")
    os.makedirs(hd)
    os.makedirs(shots)
    prg = os.path.basename(a.prg).upper()
    shutil.copy(a.prg, os.path.join(hd, prg))
    if with_hyp:
        shutil.copy(a.hyp, os.path.join(hd, hyp))
    for v in viewers:
        shutil.copy(os.path.join(HERE, "build", "FAKEVIEW.ACC"), os.path.join(hd, v + ".ACC"))
    for f in a.copy:
        (shutil.copytree if os.path.isdir(f) else shutil.copy)(f, os.path.join(hd, os.path.basename(f)))
    fifo = os.path.join(d, "cmd.fifo")
    args = ["hatari", "--machine", a.machine, "--tos", os.path.abspath(a.tos), "--harddrive", hd,
            "--auto", "C:\\" + prg, "--fast-boot", "yes", "--natfeats", "yes", "--sound", "off",
            "--confirm-quit", "no", "--statusbar", "no", "--cmd-fifo", fifo,
            "--screenshot-dir", shots, "--log-level", "fatal"]
    args += ["--mono"] if a.machine == "st" else ["--monitor", "vga", "--dsp", "none"]
    mem = a.memsize or ("14" if a.machine == "falcon" else None)
    if mem:
        args += ["--memsize", mem]
    env = dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy")
    h = subprocess.Popen(args, env=env, stdout=open(os.path.join(d, "hatari.log"), "w"),
                         stderr=subprocess.STDOUT)

    def cmd(c):
        with open(fifo, "w") as f:
            f.write(c + "\n")
        time.sleep(0.1)
    try:
        time.sleep(3 + a.wait)
        cmd("hatari-event keypress 0x62")	# Help
        time.sleep(3)
        cmd("hatari-shortcut screenshot")
        time.sleep(1)
    finally:
        h.terminate()
        h.wait()
    got = {}
    for v in ("EHYPVI", "ST-GUI", "HYPVIE"):
        p = os.path.join(hd, "V_%s.TXT" % v)
        if os.path.exists(p):
            got[v] = open(p, "rb").read().decode("latin-1")
    return got


page = " " + a.page if a.page else ""
got = run("ehypview", ["EHYPVIEW", "ST-GUIDE"], True)
check(got == {"EHYPVI": "C:\\" + hyp + page}, "EHypView first, with the file next to the app: %r" % got)
got = run("st-guide", ["ST-GUIDE"], True)
check(got == {"ST-GUI": "C:\\" + hyp + page}, "ST-Guide when EHypView isn't there: %r" % got)
got = run("no-hyp", ["EHYPVIEW"], False)
check(got == {"EHYPVI": "*:\\" + hyp + page}, "no %s next to the app: the viewer's own paths: %r" % (hyp, got))
got = run("no-viewer", [], True)
check(got == {}, "no viewer: nothing sent (the app's own fallback is in no-viewer/shots)")
print("%d failed" % failed)
sys.exit(1 if failed else 0)
