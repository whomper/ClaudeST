#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""tos_ms() (atari/tos.c) in Hatari (EmuTOS), on a 68000 ST and a Falcon:
once with the 200 Hz clock at 312 Hz (a file C:\\FASTCLK makes CLOCKT.PRG
set MFP timer C so, as on Erez's Falcon), once at 200 Hz. The 8 seconds
CLOCKT.PRG counts by tos_ms() are timed with this computer's clock;
Hatari runs in real time here (no fast-forward).

    make prg && python3 run.py etos512us.img [work-dir]
"""
import os, re, shutil, subprocess, sys, time

HERE = os.path.dirname(os.path.abspath(__file__))
tos = os.path.abspath(sys.argv[1])
work = os.path.abspath(sys.argv[2] if len(sys.argv) > 2 else os.path.join(HERE, "build", "work"))
prg = os.path.join(HERE, "build", "CLOCKT.PRG")
failed = 0


def run(machine, fast):
    d = os.path.join(work, "%s-%s" % (machine, "fast" if fast else "true"))
    shutil.rmtree(d, ignore_errors=True)
    os.makedirs(d)
    shutil.copy(prg, os.path.join(d, "CLOCKT.PRG"))
    if fast:
        open(os.path.join(d, "FASTCLK"), "w").close()
    args = ["hatari", "--machine", machine, "--tos", tos, "--harddrive", d,
            "--auto", "C:\\CLOCKT.PRG", "--fast-boot", "yes", "--natfeats", "yes",
            "--fast-forward", "no", "--sound", "off", "--confirm-quit", "no",
            "--statusbar", "no", "--log-level", "fatal"]
    args += ["--memsize", "4", "--mono"] if machine == "st" else \
            ["--memsize", "14", "--monitor", "vga", "--cpuclock", "32", "--fpu", "68882"]
    env = dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy")
    h = subprocess.Popen(args, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                         text=True, errors="replace")
    lines, when = [], {}
    t_end = time.time() + 120
    try:
        for l in h.stdout:
            l = l.strip()
            lines.append(l)
            m = re.match(r"CLOCKT (start|end|done)", l)
            if m:
                when[m.group(1)] = time.time()
            if "done" in when or time.time() > t_end:
                break
    finally:
        h.terminate()
        h.wait()
    note = next((l[14:] for l in lines if l.startswith("CLOCKT clock: ")), None)
    return note, (when["end"] - when["start"]) if "end" in when and "start" in when else None


def check(ok, what):
    global failed
    print(("ok   " if ok else "FAIL ") + what)
    failed += not ok


for machine in ("st", "falcon"):
    for fast in (True, False):
        tag = "%s at %s" % (machine, "312 Hz" if fast else "200 Hz")
        note, real = run(machine, fast)
        if fast:
            m = re.search(r"runs at (\d+)\.\d Hz: times corrected", note or "")
            check(m is not None and 300 <= int(m.group(1)) <= 325, "%s: %s" % (tag, note))
        else:
            check(note == "true", "%s: %s" % (tag, note))
        check(real is not None and abs(real - 8) <= 0.6,
              "%s: 8 s by tos_ms() took %s" % (tag, "%.2f s" % real if real else "no time"))
print("%d failed" % failed)
sys.exit(1 if failed else 0)
