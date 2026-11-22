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
- it fails where a source puts an EMAC instruction or a Line-A word into the code: on a ColdFire 0xa000 (a
  68000's Line-A) is the EMAC's macw (common 13a4971: no Line-A under __mcoldfire__). It reads each file's
  own assembly (gcc -S, or a .S file preprocessed), not a disassembly, which a switch's jump table throws
  out of step (ECal, 22 Nov 2026); an app's own file that means its EMAC (EPlay's MP3 decoder) is named
  with --emac-ok FILE.o (the 68000 build's object path);
- no pads: the 68030's caches aren't the ColdFire's.

TLS on the FireBee is common/tlsovl's tlsfb.ovl, shipped in the FireBee zip as tls.ovl (Erez, 22 Nov
2026: every file of the FireBee package under its standard name; a __mcoldfire__ build's tls_ovl.c loads
tls.ovl as named). An accessory is made the same way from its own ELF; it is installed in place of the
68000 one, never beside it (a FireBee loads every .ACC in the root: the guidelines, 7.4). OUT is the
build's own name (build/cf/ or binary/cf/); the release's FireBee zip carries it under the standard one.

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


EMAC_RE = re.compile(r"^\s*(mac|msac|macl|msacl|movclr|maaac|masac|msaac|mssac)(\.[wl])?\s|%acc|%macsr|%mask\b", re.I)
# a Line-A word written by hand (0xa000-0xafff: on a ColdFire the EMAC's): hex, or as GCC or gas would read it
LINEA_RE = re.compile(r"\.(short|word|dc\.w|2byte)\s+(.*)", re.I)


def linea_values(args):
    for v in re.split(r"\s*,\s*", args.strip()):
        v = v.strip().split()[0] if v.strip() else ""
        try:
            n = int(v, 0) if not v.startswith("$") else int(v[1:], 16)
        except ValueError:
            continue
        if 0xA000 <= (n & 0xFFFF) <= 0xAFFF and -32768 <= n <= 65535:
            yield n


def emac_in_source(words):
    """the EMAC instructions and Line-A words a compile puts in its object, from the compiler's own assembly
    (-S) or a .S file's preprocessed text: no disassembly, so a switch's jump table can't pass for code"""
    src = [w for w in words if w.endswith((".c", ".S", ".s")) and not w.startswith("-")]
    if not src:
        return []
    obj = words[words.index("-o") + 1]
    w = [x for x in words if x != "-c"]
    w[w.index("-o") + 1] = obj + (".s" if src[-1].endswith(".c") else ".i")
    w.insert(1, "-S" if src[-1].endswith(".c") else "-E")
    run(" ".join(shlex.quote(x) for x in w))
    hits, in_code = [], True
    for n, line in enumerate(open(w[w.index("-o") + 1], errors="replace"), 1):
        code = line.split("|")[0].split("/*")[0]
        d = code.strip()
        # only what goes into code: .text, a .section .text.* or one flagged "x" (data tables don't run)
        if re.match(r"\.(text)\b", d):
            in_code = True
        elif re.match(r"\.(data|bss|rodata)\b", d):
            in_code = False
        elif d.startswith(".section"):
            m = re.match(r'\.section\s+([^,\s]+)\s*(?:,\s*"([^"]*)")?', d)
            in_code = bool(m) and (m.group(1).startswith(".text") or "x" in (m.group(2) or ""))
        if not in_code:
            continue
        if EMAC_RE.search(code):
            hits.append("%s:%d: %s" % (src[-1], n, line.strip()))
        m = LINEA_RE.search(code)
        if m and any(True for _ in linea_values(m.group(2))):
            hits.append("%s:%d: %s" % (src[-1], n, line.strip()))
    return hits


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
    n, bad = 0, []
    allowed = set(f.replace("build/", outdir, 1) for f in a.emac_ok)
    for words in compiles:
        obj = words[words.index("-o") + 1]
        if "common/atari/cpucheck.c" in words:
            words = [CC, MCPU, "-c", "common/atari/cpucheck_cf.S", "-o", obj]
        words = cpu(words, outdir)
        os.makedirs(os.path.dirname(words[words.index("-o") + 1]) or ".", exist_ok=True)
        run(" ".join(shlex.quote(w) for w in words))
        if words[words.index("-o") + 1] not in allowed:
            bad += emac_in_source(words)
        n += 1
    run(" ".join([CC, MCPU, "-O2", "-ffreestanding", "-fno-builtin", "-fno-pic", "-fno-pie",
                  "-fno-asynchronous-unwind-tables", "-c", "common/atari/ll64.c", "-o", outdir + "rt/ll64.o"]))
    words = cpu([w for w in link if w != "-lgcc" and not w.startswith("-Wl,-Map")], outdir)
    elf = words[words.index("-o") + 1]
    words += [outdir + "rt/ll64.o", "-Wl,-Map," + elf + ".map"]
    run(" ".join(shlex.quote(w) for w in words))
    # no EMAC instruction and no Line-A word but in a file --emac-ok names (on a ColdFire 0xa000 is the
    # EMAC's macw: EWEBFB.PRG killed at its first scroll on a tester's FireBee)
    if bad:
        sys.exit("mkfirebee: %d EMAC instructions or Line-A words (a ColdFire runs them as the EMAC's):\n%s"
                 % (len(bad), "\n".join(bad[:8])))
    env = dict(os.environ, PRGFLAGS=a.prgflags)
    subprocess.run(["python3", "common/tools/elf2tos.py", elf, a.out], env=env, check=True)
    print("mkfirebee: %s, %d objects, %d bytes" % (a.out, n, os.path.getsize(a.out)))


if __name__ == "__main__":
    main()
