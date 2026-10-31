#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""The crash catcher's check in Hatari (EmuTOS), on a 68000 ST and a
68030 Falcon: each kind of crash in CRASHT.PRG, then a crash in
supervisor mode outside the program, a warm reset and the report of it at
the next start.

    make && python3 run.py etos512us.img [work-dir]
    make && python3 run.py --st tos104us.img --falcon tos404.img [work-dir]

One image for both machines, or one for each (--st, --falcon; either
alone runs that machine only): Erez's TOS 1.04 is an ST's, his TOS 4.04 a
Falcon's, and neither starts on the other. EmuTOS 1.4 or later: 1.3
overwrites TOS's record of the bombs at every boot, so the reset checks
fail there.
"""
import os, re, shutil, subprocess, sys, time

HERE = os.path.dirname(os.path.abspath(__file__))
# TOS's record of the bombs ($380) must live through the warm reset, as on
# a real machine. EmuTOS 1.4 and later keep it on a reset (1.3 overwrote
# $100-$3FF at every boot), but only when the reset is marked as one:
# _warm_magic = 'WARM' at $6FC, which EmuTOS writes while it sizes the
# memory at power-on. Hatari sets the memory up itself, so EmuTOS never
# writes it and takes every reset for a first boot. Write it, then reset.
WARM_MAGIC = "hatari-debug w $6fc $57 $41 $52 $4d"

PRG = os.path.join(HERE, "build", "CRASHT.PRG")
argv, tos_for = sys.argv[1:], {}
while argv and argv[0] in ("--st", "--falcon") and len(argv) > 1:
    tos_for[argv[0][2:]] = os.path.abspath(argv[1])
    argv = argv[2:]
if not tos_for:
    if not argv:
        sys.exit(__doc__)
    tos_for = {"st": os.path.abspath(argv[0]), "falcon": os.path.abspath(argv[0])}
    argv = argv[1:]
work = os.path.abspath(argv[0] if argv else os.path.join(HERE, "build", "work"))
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
    args = ["hatari", "--machine", machine, "--tos", tos_for[machine], "--harddrive", d,
            "--auto", "C:\\CRASHT.PRG", "--fast-boot", "yes", "--natfeats", "yes",
            "--sound", "off", "--confirm-quit", "no", "--statusbar", "no",
            "--cmd-fifo", fifo, "--log-level", "fatal"]
    if machine == "st":
        args += ["--mono"]
    else:
        args += ["--monitor", "vga"]
    env = dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy")
    h = subprocess.Popen(args, env=env, stdout=open(log, "w"), stderr=subprocess.STDOUT)
    want = "CRASHT previous" if reset else "CRASHT repeat done" if kind == "r" else "CRASHT cleanup"
    done = ("CRASHT no crash", "CRASHT protect", "CRASHT line F not ours", "CRASHT emulator")
    t_end = time.time() + (240 if kind == "r" else 60)
    reset_sent = False
    try:
        while time.time() < t_end and h.poll() is None:
            time.sleep(0.5)
            text = open(log, errors="replace").read()
            if want in text or any(x in text for x in done):
                time.sleep(1)
                break
            if reset and not reset_sent and os.path.exists(os.path.join(d, "STAGE1.TXT")):
                time.sleep(3)			# the bombs, then back at the desktop
                with open(fifo, "w") as f:
                    f.write(WARM_MAGIC + "\n")
                time.sleep(0.5)
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
         "i": "illegal instruction, 4 bombs", "z": "division by zero, 5 bombs",
         "k": "bus error, 2 bombs", "f": "line F (an FPU instruction or none), 11 bombs",
         "w": "illegal instruction, 4 bombs"}

for machine in [m for m in ("st", "falcon") if m in tos_for]:
    for kind in "bizakfw":
        if kind == "a" and machine != "st":
            continue			# a 68030 reads odd addresses
        d, con, rep = run(machine, kind)
        tag = "%s %s:" % (machine, NAMES[kind])
        m = re.search(r"CRASHT line F not ours: TOS ([0-9A-F]+), vector 11 ([0-9A-F]+)", con)
        if kind == "f" and m:
            # left to the handler on vector 11: right only for a TOS 1.0x,
            # whose ROM uses line F itself (crash.c hooks it no more there:
            # the guidelines' Speed); a regression anywhere else
            check(int(m.group(1), 16) < 0x200 and not rep,
                  tag + " left to vector 11's handler (TOS %s, %s): only on a TOS 1.0x" % m.groups())
            continue
        check("CRASHT crashing" in con and "CRASHT no crash" not in con, tag + " it crashed")
        check("CrashTest 1.0: " + NAMES[kind] in rep, tag + " CRASH.LOG names it")
        if kind == "w":     # outside the program: no offset, but the caller
            check("callers? +$" in rep, tag + " the caller, as an offset into the program")
        else:
            check("(CrashTest+$" in rep.split("\r\n")[2] if rep.count("\r\n") > 2 else False,
                  tag + " where, as an offset into the program")
        if kind == "k":
            check(", SR 2" in rep, tag + " in supervisor mode")
        check("D0-D7" in rep and "A0-A6" in rep and "USP" in rep, tag + " the registers")
        check("last status: about to crash" in rep, tag + " the last status")
        cl = os.path.join(d, "CLEANUP.TXT")
        check(os.path.exists(cl) and NAMES[kind] in open(cl).read(), tag + " crash_cleanup called")
        rp = os.path.join(d, "REPORT.TXT")
        check(os.path.exists(rp) and open(rp, "rb").read().decode("latin-1") == rep,
              tag + " crash_report got the report")
    # STinG's way into supervisor mode: its own privilege violation,
    # caught by its own handler, must not be taken for a crash
    d, con, rep = run(machine, "p")
    tag = "%s a resident program's privilege violation:" % machine
    check("CRASHT protect ok" in con, tag + " passed on to its handler")
    check(not rep, tag + " no CRASH.LOG")
    # a software FPU emulator on vector 11: the program's line F is its
    d, con, rep = run(machine, "e")
    tag = "%s an FPU emulator's line F:" % machine
    check("CRASHT emulator ok" in con, tag + " run by the emulator")
    check(not rep, tag + " no CRASH.LOG")
    # ten crashes caught in supervisor mode in one boot: TOS's savptr
    # ($4A2) and the supervisor stack hold steady
    d, con, rep = run(machine, "r")
    probes = re.findall(r"CRASHT probe (\d+) savptr ([0-9A-F]+) ssp ([0-9A-F]+)", con)
    tag = "%s ten crashes in one boot:" % machine
    check(len(probes) == 11 and con.count("CRASHT cleanup") == 10, tag + " all caught (%d probes)" % len(probes))
    check(len({p[1] for p in probes}) == 1, tag + " savptr steady (%s)" % " ".join(sorted({p[1] for p in probes})))
    check(len({p[2] for p in probes}) == 1, tag + " the supervisor stack steady")
    d, con, rep = run(machine, "s", reset=True)
    tag = "%s supervisor bus error:" % machine
    check("CRASHT previous" in con, tag + " the program ran again after a reset")
    check("found TOS's record of a crash before this run" in rep and "bus error, 2 bombs at" in rep,
          tag + " CRASH.LOG has TOS's record")
    check("CRASHT previous none" in con, tag + " not counted as the program's (it wasn't in its code)")
    check(re.search(r"hooks now .* VBL AERT \$[0-9A-F]{6};", rep) is not None, tag + " the hooks named (VBL AERT)")
print("%d failed" % failed)
sys.exit(1 if failed else 0)
