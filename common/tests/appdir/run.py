#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""app_dir() (atari/gem.c) in a program another one started, as EMail
starts EWeb (EMail's finding, 28 Oct 2026): C:\\LAUNCH.PRG makes A:\\APP
current and starts A:\\APP\\PROBE.PRG with shel_write. TOS 1.04 and 4.04
start it in the desktop's folder whatever the caller set; app_dir()
must still say A:\\APP\\. A: is a real FAT floppy (mtools), since
Hatari's emulated C: keeps one current folder for all programs and hid
it. Then the probe started from the desktop as Hatari's --auto does
(C:\\APP\\PROBE.PRG): app_dir() says C:\\APP\\. The probe also says the
screen it got. Use a machine without TT RAM for a program flagged to load
there (PRGFLAGS 2): the floppy's DMA can't reach TT RAM, and on Falcon
(128 MB) such a program never starts from A: (no _FRB here). Falcon-4 is
the Falcon to use.

    make && python3 run.py WORK [MACHINE...]     (ST and Falcon-4 by default)
"""
import os, shutil, subprocess, sys, time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "tools"))
import machines  # noqa: E402

B = os.path.join(HERE, "build")
work = os.path.abspath(sys.argv[1])
specs = sys.argv[2:] or ["ST", "Falcon-4"]
failed = 0


def check(ok, what):
    global failed
    print("%s %s" % ("ok  " if ok else "FAIL", what), flush=True)
    failed += not ok


def run(name, spec, auto, floppy):
    w = os.path.join(work, name)
    shutil.rmtree(w, ignore_errors=True)
    hd = os.path.join(w, "hd")
    os.makedirs(os.path.join(hd, "APP"))
    shutil.copy(os.path.join(B, "LAUNCH.PRG"), hd)
    shutil.copy(os.path.join(B, "PROBE.PRG"), os.path.join(hd, "APP"))
    m = machines.Machine(spec, hd)
    args = ["hatari", "--fast-boot", "yes", "--natfeats", "yes", "--log-level", "warn"] + m.args() + ["--auto", auto]
    if floppy:
        img = os.path.join(w, "a.st")
        subprocess.run(["mformat", "-i", img, "-f", "720", "-C", "::"], check=True, capture_output=True)
        subprocess.run(["mmd", "-i", img, "::/APP"], check=True, capture_output=True)
        subprocess.run(["mcopy", "-i", img, os.path.join(B, "PROBE.PRG"), "::/APP/PROBE.PRG"], check=True,
                       capture_output=True)
        # with a floppy in A:, Hatari's start-up desktop file has no screen
        # mode, and a Falcon comes up in ST low (320 wide: the family's
        # programs then only say they need 640). A mono monitor has one
        # mode, 640x400
        if "--monitor" in args:
            args[args.index("--monitor") + 1] = "mono"
        args += ["--disk-a", img]
    log = os.path.join(w, "hatari.log")
    p = subprocess.Popen(args, env=m.env(), stdout=open(log, "w"), stderr=subprocess.STDOUT)
    line = ""
    for _ in range(240):
        time.sleep(0.5)
        line = next((l for l in open(log, errors="replace") if l.startswith("PROBE ")), "")
        if line:
            break
    p.kill()
    p.wait()
    return m.describe(), line.strip()


for spec in specs:
    what, line = run("launched-" + spec.replace(":", "-"), spec, "C:\\LAUNCH.PRG", True)
    print("on %s: %s" % (what, line or "(no PROBE line)"), flush=True)
    check(" app A:\\APP\\ " in line, "started by another program from A:\\APP: app_dir() says A:\\APP\\")
    what, line = run("desktop-" + spec.replace(":", "-"), spec, "C:\\APP\\PROBE.PRG", False)
    print("on %s: %s" % (what, line or "(no PROBE line)"), flush=True)
    check(" app C:\\APP\\ " in line, "started as from the desktop: app_dir() says C:\\APP\\")
print("all passed" if not failed else "%d failed" % failed)
sys.exit(1 if failed else 0)
