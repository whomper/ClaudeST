#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""The heap on an Atari: HEAPT.PRG in Hatari (EmuTOS), on a 68000 ST,
where a word or long at an odd address bombs, and on a 68030 Falcon.

    make prg && python3 run.py etos512us.img [work-dir] [PRG]

It prints how long the calendar-shaped part took (200 Hz ticks, in the
emulated machine's time). PRG names another build to time, for comparing.
"""
import os, shutil, subprocess, sys, time

HERE = os.path.dirname(os.path.abspath(__file__))
tos = os.path.abspath(sys.argv[1])
work = os.path.abspath(sys.argv[2] if len(sys.argv) > 2 else os.path.join(HERE, "build", "work"))
prg = os.path.abspath(sys.argv[3] if len(sys.argv) > 3 else os.path.join(HERE, "build", "atari", "HEAPT.PRG"))
failed = 0


def run(machine, mem):
    d = os.path.join(work, "%s-%dmb" % (machine, mem))
    shutil.rmtree(d, ignore_errors=True)
    os.makedirs(d)
    shutil.copy(prg, os.path.join(d, "HEAPT.PRG"))
    log = os.path.join(d, "hatari.log")
    args = ["hatari", "--machine", machine, "--tos", tos, "--harddrive", d,
            "--auto", "C:\\HEAPT.PRG", "--fast-boot", "yes", "--natfeats", "yes",
            "--fast-forward", "yes", "--sound", "off", "--confirm-quit", "no",
            "--statusbar", "no", "--log-level", "fatal"]
    args += ["--memsize", str(mem)]
    args += ["--mono"] if machine == "st" else ["--monitor", "vga"]
    env = dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy")
    h = subprocess.Popen(args, env=env, stdout=open(log, "w"), stderr=subprocess.STDOUT)
    t_end = time.time() + 300
    try:
        while time.time() < t_end and h.poll() is None:
            time.sleep(0.5)
            text = open(log, errors="replace").read()
            if "HEAPT done" in text or "FAIL" in text:
                break
    finally:
        if h.poll() is None:
            h.terminate()
            h.wait()
    return open(log, errors="replace").read()


for machine, mem in (("st", 4), ("falcon", 14)):
    text = run(machine, mem)
    ok = "HEAPT mix ok" in text and "HEAPT done" in text
    ticks = [l for l in text.splitlines() if l.startswith("HEAPT events")]
    print("%s %s: mixed calls, bytes checked%s" % ("ok  " if ok else "FAIL", machine,
          "; " + ticks[0][6:] if ticks else ""))
    if not ok:
        failed += 1
print("%d failed" % failed)
sys.exit(1 if failed else 0)
