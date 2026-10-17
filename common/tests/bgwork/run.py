#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""ui/bgwork.c in Hatari (EmuTOS), on a 68000 ST and a 68030 Falcon:
BGT.PRG does 300 ms of work in one go, which the AES_PROFILE logger must
see, then 3 s of work through bgwork, whose longest stretch between event
calls must stay within one slice (80 ms on the ST, 40 on the Falcon) and
a step of the clock.

    make prg && python3 run.py etos512us.img [work-dir]
"""
import os, re, shutil, subprocess, sys, time

HERE = os.path.dirname(os.path.abspath(__file__))
tos = os.path.abspath(sys.argv[1])
work = os.path.abspath(sys.argv[2] if len(sys.argv) > 2 else os.path.join(HERE, "build", "work"))
prg = os.path.join(HERE, "build", "atari", "BGT.PRG")
failed = 0


def run(machine):
    d = os.path.join(work, machine)
    shutil.rmtree(d, ignore_errors=True)
    os.makedirs(d)
    shutil.copy(prg, os.path.join(d, "BGT.PRG"))
    args = ["hatari", "--machine", machine, "--tos", tos, "--harddrive", d,
            "--auto", "C:\\BGT.PRG", "--fast-boot", "yes", "--natfeats", "yes",
            "--fast-forward", "no", "--sound", "off", "--confirm-quit", "no",
            "--statusbar", "no", "--log-level", "fatal"]
    args += ["--memsize", "4", "--mono"] if machine == "st" else \
            ["--memsize", "14", "--monitor", "vga", "--cpuclock", "32"]
    env = dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy")
    h = subprocess.Popen(args, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                         text=True, errors="replace")
    lines = []
    t_end = time.time() + 120
    try:
        for l in h.stdout:
            lines.append(l.strip())
            if l.startswith("BGT done") or time.time() > t_end:
                break
    finally:
        h.terminate()
        h.wait()
    return lines


def check(ok, what):
    global failed
    print(("ok   " if ok else "FAIL ") + what)
    failed += not ok


for machine, limit in (("st", 80), ("falcon", 40)):
    lines = run(machine)
    gaps = [(int(m.group(1)), m.group(2)) for m in (re.match(r"AESGAP (\d+) ms (\S+)", l) for l in lines) if m]
    one = max([g for g, t in gaps if t == "in-one-go"] or [0])
    bg = max([g for g, t in gaps if t == "bgwork"] or [0])
    done = next((l for l in lines if l.startswith("BGT done")), None)
    check(one >= 280, "%s: the logger saw the 300 ms in one go (%d ms)" % (machine, one))
    check(done is not None, "%s: BGT finished (%s)" % (machine, done))
    check(bg <= limit + 15, "%s: through bgwork the longest stretch was %d ms (slice %d ms)" % (machine, bg, limit))
print("%d failed" % failed)
sys.exit(1 if failed else 0)
