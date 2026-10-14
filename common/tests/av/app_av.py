#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Erez Yaary
"""An app and the AV protocol (common/ui/av.c) in Hatari (EmuTOS), with
the stand-in AV server build/THING.ACC (make) installed: the app must
tell it it takes VA_START (AV_PROTOKOLL, with its name), answer each
VA_START with AV_STARTED, and say AV_EXIT when it quits.

    app_av.py WORK EMUTOS.IMG APP.PRG NAME SCRIPT [--machine falcon|st]
              [--memsize MB] [--copy FILE ...]

SCRIPT is THING.ACC's C:\\AVSEND.TXT (see fakeserver.c), lines separated
by ';' or a file. "say shot NAME" takes a screenshot (WORK/shots/NAME.png),
"say key 0x1c" presses a key ("say ctrl 0x2f": with Control), "icon NAME" and "uniconify NAME" send the
app's window WM_ICONIFY and WM_UNICONIFY. The checks:
  - AV_PROTOKOLL from NAME, saying VA_START, AV_STARTED and quoting;
  - as many AV_STARTED as VA_START and VA_DRAGACCWIND sent;
  - AV_EXIT after AP_TERM, when the script sends one ("term NAME").
"""
import argparse, os, re, shutil, subprocess, sys, time

HERE = os.path.dirname(os.path.abspath(__file__))
ap = argparse.ArgumentParser()
ap.add_argument("work")
ap.add_argument("tos")
ap.add_argument("prg")
ap.add_argument("name")
ap.add_argument("script")
ap.add_argument("--machine", default="falcon")
ap.add_argument("--memsize", help="MB (default: 14 on a Falcon, Hatari's own on an ST)")
ap.add_argument("--copy", nargs="*", default=[])
ap.add_argument("--timeout", type=float, default=240)
a = ap.parse_args()
failed = 0


def check(ok, what):
    global failed
    print("%s %s" % ("ok  " if ok else "FAIL", what))
    if not ok:
        failed += 1


script = open(a.script).read() if os.path.exists(a.script) else a.script.replace(";", "\n")
script = "\r\n".join(l.strip() for l in script.splitlines() if l.strip()) + "\r\n"
d = os.path.abspath(a.work)
shutil.rmtree(d, ignore_errors=True)
hd, shots = os.path.join(d, "hd"), os.path.join(d, "shots")
os.makedirs(hd)
os.makedirs(shots)
prg = os.path.basename(a.prg).upper()
shutil.copy(a.prg, os.path.join(hd, prg))
shutil.copy(os.path.join(HERE, "build", "THING.ACC"), os.path.join(hd, "THING.ACC"))
open(os.path.join(hd, "AVSEND.TXT"), "w", newline="").write(script)
for f in a.copy:
    (shutil.copytree if os.path.isdir(f) else shutil.copy)(f, os.path.join(hd, os.path.basename(f)))
fifo = os.path.join(d, "cmd.fifo")
# line by line: the console lines must reach the log as they are said
args = ["stdbuf", "-oL", "-eL", "hatari", "--machine", a.machine, "--tos", os.path.abspath(a.tos), "--harddrive", hd,
        "--auto", "C:\\" + prg, "--fast-boot", "yes", "--natfeats", "yes", "--sound", "off",
        "--confirm-quit", "no", "--statusbar", "no", "--cmd-fifo", fifo,
        "--screenshot-dir", shots, "--log-level", "fatal"]
args += ["--mono"] if a.machine == "st" else ["--monitor", "vga", "--dsp", "none"]
mem = a.memsize or ("14" if a.machine == "falcon" else None)
if mem:
    args += ["--memsize", mem]
env = dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy")
log = os.path.join(d, "hatari.log")
h = subprocess.Popen(args, env=env, stdout=open(log, "w"), stderr=subprocess.STDOUT)


def cmd(c):
    with open(fifo, "w") as f:
        f.write(c + "\n")
    time.sleep(0.1)


done = False
try:
    seen, t0 = 0, time.time()
    while time.time() - t0 < a.timeout and not done:
        time.sleep(0.3)
        said = re.findall(r"AVTEST (shot|key|ctrl|done)[ \t]*([^\s]*?)(?=AVTEST|\s|$)",
                          open(log, errors="replace").read())
        for m in said[seen:]:
            what, arg = m
            if what == "shot":
                before = set(os.listdir(shots))
                cmd("hatari-shortcut screenshot")
                for _ in range(30):
                    new = set(os.listdir(shots)) - before
                    if new:
                        time.sleep(0.5)
                        os.rename(os.path.join(shots, new.pop()), os.path.join(shots, arg + ".png"))
                        break
                    time.sleep(0.2)
            elif what == "key":
                cmd("hatari-event keypress %s" % arg)
            elif what == "ctrl":
                cmd("hatari-event keydown 29")
                cmd("hatari-event keypress %s" % arg)
                cmd("hatari-event keyup 29")
            elif what == "done":
                done = True
        seen = len(said)
finally:
    h.terminate()
    h.wait()

check(done, "the script ran to its end")
p = os.path.join(hd, "AVLOG.TXT")
got = open(p, "rb").read().decode("latin-1").splitlines() if os.path.exists(p) else []
print("".join("     | %s\n" % l for l in got), end="")
name = (a.name + "        ")[:8]
check("AV_PROTOKOLL bits 0016 name " + name in got, "AV_PROTOKOLL from %r: VA_START, AV_STARTED, quoting" % name)
sent = sum(1 for l in got if l.startswith("VA_START sent") or l.startswith("VA_DRAGACCWIND sent"))
check(sent == sum(1 for l in got if l == "AV_STARTED"),
      "an AV_STARTED for each of the %d VA_START and VA_DRAGACCWIND" % sent)
if "AP_TERM sent" in got:
    i = got.index("AP_TERM sent")
    check("AV_EXIT" in got[i:], "AV_EXIT when it quit")
print("%d failed" % failed)
sys.exit(1 if failed else 0)
