#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""termcheck.py WORK [EMUTOS.IMG] [--display :105] [--mem-prot]: TERMTEST.PRG (make) under FreeMiNT with XaAES: which ways of
ending a program run its terminate handler (etv_term), and whether the crash catcher's chain (atari/crash.c:
crash_install puts crash_term on the vector with Setexc(0x102) under MiNT) runs with the program's own on top of it.

The vector is the process's own under MiNT: a handler written to $408 (what crash.c did) is never called, even at a
Pterm; one put by Setexc(0x102) is, at a Pterm and when a signal ends the process (SIGTERM, SIGBUS), but not for
SIGKILL. FreeMiNT 1.17.0 measured it on 9 Nov 2026. make base in this folder once first."""
import os
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "tools"))
import machines  # noqa: E402

# mode: (what it does, the handlers' lines that must come, in this order)
MODES = {
    "d": ("a handler written to $408, then Pterm", []),
    "s": ("Setexc(0x102), then Pterm", ["term 1 ran"]),
    "t": ("Setexc(0x102), then SIGTERM", ["term 1 ran"]),
    "k": ("Setexc(0x102), then SIGKILL", []),
    "b": ("Setexc(0x102), then a bus error (SIGBUS)", ["term 1 ran"]),
    "S": ("crash_install, the app's handler over it, then Pterm", ["term 1 ran", "term 2 ran"]),
    "T": ("the same, SIGTERM", ["term 1 ran", "term 2 ran"]),
    "K": ("the same, SIGKILL", []),
    "B": ("the same, a bus error", ["term 1 ran", "term 2 ran"]),
}


def main():
    a = [x for x in sys.argv[1:] if not x.startswith("--")]
    display = ":105"
    if "--display" in sys.argv:
        display = sys.argv[sys.argv.index("--display") + 1]
        a.remove(display)
    if not a:
        sys.exit(__doc__)
    work = os.path.abspath(a[0])
    img = a[1] if len(a) > 1 else machines.emutos()
    shutil.rmtree(work, ignore_errors=True)
    os.makedirs(work)
    prg = os.path.join(HERE, "build", "TERMTEST.PRG")
    fails = 0
    for mode, (what, want) in MODES.items():
        inf = os.path.join(work, "inf_" + mode)
        os.makedirs(inf)
        with open(os.path.join(inf, "termt.inf"), "w") as f:
            f.write(mode)
        w = os.path.join(work, "run_" + mode)
        subprocess.run([sys.executable, os.path.join(HERE, "run.py"), w, img, prg, "--copy",
                        os.path.join(inf, "termt.inf"), "--display", display, "--steps",
                        "until TRM start 150; until TRM before end 30; wait 8"] + (["--mem-prot"] if "--mem-prot" in sys.argv else []), capture_output=True, text=True)
        log = open(os.path.join(w, "hatari.log"), errors="replace").read()
        for n in ("hd", "hd.img"):
            p = os.path.join(w, n)
            shutil.rmtree(p, ignore_errors=True) if os.path.isdir(p) else os.path.exists(p) and os.remove(p)
        lines = [l[4:] for l in log.splitlines() if l.startswith("TRM ")]
        ran = [l for l in lines if l.endswith(" ran")]
        ok = ran == want and "before end" in lines
        if mode in "STKB" and not any(l.startswith("chain: the catcher is under") for l in lines):
            ok = False
        fails += not ok
        print("%s  %s: %s" % ("ok  " if ok else "FAIL", what, ran or "no handler ran"))
    print("all passed" if not fails else "%d failed" % fails)
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
