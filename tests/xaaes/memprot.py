#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""memprot.py WORK [EMUTOS.IMG] [--display :105]: FreeMiNT's memory protection (run.py --mem-prot) and the program flags.

INTTEST.PRG (PRGFLAGS 7: memory protection bits 4-5 = 0, private) and INTTESTG.PRG (0x17: global) put a routine on
the vertical blank queue whose code and counter are in the program's own memory. Without protection both count.
With it the interrupt comes while another process runs, whose MMU context doesn't map a private program: MiNT
dies ("Operating system killed", as EPlay's did on Hatari's Falcon with music playing); a global program counts.
This is the check that a program needing Global (an interrupt handler or callback of its own) has the flag.
Prints ok/FAIL lines; exits 1 when one failed. make base in this folder once first."""
import os
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "tools"))
import machines  # noqa: E402

# (program, memory protection, the count must come)
CASES = [("INTTEST", False, True), ("INTTESTG", False, True), ("INTTEST", True, False), ("INTTESTG", True, True)]


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
    bad = 0
    for prg, prot, counts in CASES:
        w = os.path.join(work, "%s-%s" % (prg, "prot" if prot else "noprot"))
        subprocess.run([sys.executable, os.path.join(HERE, "run.py"), w, img, os.path.join(HERE, "build", prg + ".PRG"),
                        "--display", display, "--steps", "until INT start 150; wait 12"] + (["--mem-prot"] if prot else []),
                       capture_output=True, text=True)
        log = open(os.path.join(w, "hatari.log"), errors="replace").read()
        for n in ("hd", "hd.img"):
            p = os.path.join(w, n)
            shutil.rmtree(p, ignore_errors=True) if os.path.isdir(p) else os.path.exists(p) and os.remove(p)
        counted = "INT count " in log and "INT count 0" not in log
        dead = "Operating system killed" in log
        ok = counted == counts and (dead == (not counts))
        bad += not ok
        print("%s  %s, memory protection %s: %s" % ("ok  " if ok else "FAIL", prg, "on" if prot else "off",
              "counted" if counted else "MiNT killed (Operating system killed)" if dead else "nothing"))
    print("all passed" if not bad else "%d failed" % bad)
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
