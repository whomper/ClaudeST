#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""A program's FireBee edition (ColdFire, -mcpu=5475), made from the very rules its 68000/68030 build is
made with (Erez, 22 Nov 2026: "Make the other apps native on the FireBee too"; EUpdate's and EWeb's
tools/mkfirebee.py before it, EUPDFB.PRG and EWEBFB.PRG). `make -n -B ELF MAKEARGS` gives each command,
and each cross-compiler command is run again for the ColdFire:

- -m68000, -m68020, -m68020-60, -m68030, -m68060, -mtune=..., -msoft-float and -m68881 become
  -mcpu=5475 (the MCF5474 has an FPU; libgcc, all 68020 code, is left out);
- build/'s objects and ELF become OUTDIR's (default build/cf/); generated sources stay where they are;
- common/atari/cpucheck.c becomes cpucheck_cf.S, the same bytes for the 68000 and the ColdFire: on a
  machine without the _CF_ cookie the program's cpu_alert (under __mcoldfire__) names the other file;
- -lgcc becomes common's atari/ll64.c (64-bit multiply and divide in C);
- it fails where the code has an EMAC instruction: on a ColdFire 0xa000 (a 68000's Line-A) is the EMAC's
  macw (common 13a4971: no Line-A under __mcoldfire__); an app's own file that means its EMAC (EPlay's
  MP3 decoder) is named with --emac-ok FILE.o, and its instructions are allowed there only;
- no pads: the 68030's caches aren't the ColdFire's.

TLS on the FireBee is TLSFB.OVL (common/tlsovl, copied as it is). An accessory is made the same way from
its own ELF; it is installed in place of the 68000 one, never beside it (a FireBee loads every .ACC in the
root: the guidelines, 7.4).

    common/tools/mkfirebee.py --elf build/NAME.elf [--make "PADS=no"] [--outdir build/cf/]
                              [--prgflags 7] [--emac-ok build/x.o ...] OUT.PRG
"""
import argparse
import os
import re
import shlex
import subprocess
import sys

CC = "m68k-linux-gnu-gcc"
MCPU = "-mcpu=5475"
CPU_FLAGS = ("-m68000", "-m68020", "-m68020-60", "-m68030", "-m68040", "-m68060", "-msoft-float", "-m68881")


def run(cmd):
    p = subprocess.run(cmd, shell=True, capture_output=True, text=True)
    if p.returncode != 0:
        sys.stderr.write(cmd + "\n" + p.stdout + p.stderr)
        sys.exit("mkfirebee: a command failed")
    return p.stdout


def cpu(words, outdir):
    out = []
    for w in words:
        if w in CPU_FLAGS or w.startswith("-mtune="):
            if MCPU not in out:
                out.append(MCPU)
            continue
        out.append(w.replace("build/", outdir, 1) if w.startswith("build/") and w.endswith((".o", ".elf")) else w)
    return out


def emac_lines(elf, stop, start=0):
    return [l for l in run("m68k-linux-gnu-objdump -d -m m68k:cfv4e --start-address=0x%x --stop-address=0x%x %s"
                           % (start, stop, elf)).splitlines()
            if re.search(r"\s(mac|msac|movclr)[.a-z]*\s|%acc", l)]


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("out")
    ap.add_argument("--elf", required=True, help="the 68000/68030 build's ELF target, e.g. build/email.elf")
    ap.add_argument("--make", default="", help="extra make arguments, e.g. PADS=no")
    ap.add_argument("--outdir", default="build/cf/")
    ap.add_argument("--prgflags", default="7")
    ap.add_argument("--emac-ok", action="append", default=[], metavar="FILE.o")
    a = ap.parse_args()
    outdir = a.outdir.rstrip("/") + "/"
    lines = run("make -n -B %s %s" % (a.elf, a.make)).replace("\\\n", " ").splitlines()
    compiles, link = [], None
    for line in lines:
        if not line.startswith(CC + " "):
            continue
        words = shlex.split(line)
        if "-c" in words:
            compiles.append(words)
        elif "-o" in words and words[words.index("-o") + 1] == a.elf:
            link = words
    if link is None:
        sys.exit("mkfirebee: make gave no link of " + a.elf)
    os.makedirs(outdir + "rt", exist_ok=True)
    n = 0
    for words in compiles:
        obj = words[words.index("-o") + 1]
        if "common/atari/cpucheck.c" in words:
            words = [CC, MCPU, "-c", "common/atari/cpucheck_cf.S", "-o", obj]
        words = cpu(words, outdir)
        os.makedirs(os.path.dirname(words[words.index("-o") + 1]) or ".", exist_ok=True)
        run(" ".join(shlex.quote(w) for w in words))
        n += 1
    run(" ".join([CC, MCPU, "-O2", "-ffreestanding", "-fno-builtin", "-fno-pic", "-fno-pie",
                  "-fno-asynchronous-unwind-tables", "-c", "common/atari/ll64.c", "-o", outdir + "rt/ll64.o"]))
    words = cpu([w for w in link if w != "-lgcc" and not w.startswith("-Wl,-Map")], outdir)
    elf = words[words.index("-o") + 1]
    words += [outdir + "rt/ll64.o", "-Wl,-Map," + elf + ".map"]
    run(" ".join(shlex.quote(w) for w in words))
    # no EMAC instruction in the code but where --emac-ok allows it (the read-only data follows the code
    # in .text: the check stops there)
    stop, ok = None, []
    allowed = set(f.replace("build/", outdir, 1) for f in a.emac_ok)
    cur = None
    for line in open(elf + ".map"):
        m = re.match(r"\s*\.text\S*\s+0x([0-9a-f]+)\s+0x([0-9a-f]+)\s+(\S+\.o)\s*$", line)
        if m and m.group(3) in allowed:
            ok.append((int(m.group(1), 16), int(m.group(1), 16) + int(m.group(2), 16)))
        if stop == "next":
            m = re.search(r"0x([0-9a-f]+)", line)
            if m:
                stop = int(m.group(1), 16)
                break
        elif "*(.rodata .rodata.*)" in line:
            stop = "next"
    if not isinstance(stop, int):
        sys.exit("mkfirebee: no read-only data line in " + elf + ".map")
    bad = []
    for l in emac_lines(elf, stop):
        m = re.match(r"\s*([0-9a-f]+):", l)
        at = int(m.group(1), 16) if m else -1
        if not any(lo <= at < hi for lo, hi in ok):
            bad.append(l)
    if bad:
        sys.exit("mkfirebee: %d EMAC instructions in %s (Line-A run on a ColdFire), as\n%s"
                 % (len(bad), elf, "\n".join(bad[:5])))
    env = dict(os.environ, PRGFLAGS=a.prgflags)
    subprocess.run(["python3", "common/tools/elf2tos.py", elf, a.out], env=env, check=True)
    print("mkfirebee: %s, %d objects, %d bytes" % (a.out, n, os.path.getsize(a.out)))


if __name__ == "__main__":
    main()
