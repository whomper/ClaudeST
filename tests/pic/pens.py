#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""aerapens (tools/pens), the Pi gateway's pens, against the Atari's own
code: each picture fetch.sh gets and run.py makes, to the pens of a
monochrome ST, ST medium, ST low and a Falcon's 256 colours, W wide and at
a third of its size, by aerapens built for this machine and for a Pi
(arm64 and armhf, under QEMU), and by the same sources built as 68030 code
(its EWEB.PRG build, libjpeg-turbo) and 68000 code (its EWEBST.PRG build,
TJpgDec) under qemu-m68k: every output must be the 68k one to the byte (or every
build must refuse it alike). make pens; run.py first (it makes build/gen).

    pens.py [-q] [FILE...]"""
import glob
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
TOOL = os.path.normpath(os.path.join(HERE, "..", "..", "tools", "pens"))
B = os.path.join(HERE, "build", "pens")

ST16 = [(255, 255, 255), (255, 0, 0), (0, 255, 0), (255, 255, 0), (0, 0, 255), (255, 0, 255), (0, 255, 255),
        (170, 170, 170), (85, 85, 85), (170, 0, 0), (0, 170, 0), (170, 170, 0), (0, 0, 170), (170, 0, 170),
        (0, 170, 170), (0, 0, 0)]
PALS = {
    "mono": [(255, 255, 255), (0, 0, 0)],
    "med": [(255, 255, 255), (255, 0, 0), (0, 255, 0), (0, 0, 0)],
    "low": ST16,
    "256": ST16 + [(k // 36 * 51, k // 6 % 6 * 51, k % 6 * 51) for k in range(216)] +
           [((k - 232) * 11,) * 3 for k in range(232, 256)],
}


def build():
    def make(*args):
        subprocess.run(["make", "-s", "-C", TOOL] + list(args), check=True)
    make()
    make("pi")
    # the reference: EWEB.PRG's build as 68030 code, EWEBST.PRG's as 68000
    for ed, arch in (("lj", "-m68030"), ("tj", "-m68000")):
        make("CC=m68k-linux-gnu-gcc", "NM=m68k-linux-gnu-nm", "ARCH=" + arch, "B=" + os.path.join(B, "m68k-" + ed),
             "LDFLAGS=-static")


def runners(ed):
    """the reference (the Atari's code) first"""
    return [("68k", ["qemu-m68k", "-cpu", "m68030", os.path.join(B, "m68k-" + ed, "aerapens")]),
            ("here", [os.path.join(TOOL, "build", "aerapens")]),
            ("arm64", ["qemu-aarch64", os.path.join(TOOL, "build", "arm64", "aerapens")]),
            ("armhf", ["qemu-arm", os.path.join(TOOL, "build", "armhf", "aerapens")])]


def request(ed, w, h, pal, data):
    """aerapens' request (aerapens.c)"""
    return (b"AEPQ\1" + (b"l" if ed == "lj" else b"t") + bytes([len(pal) - 1, 0]) + w.to_bytes(2, "big") +
            h.to_bytes(2, "big") + len(data).to_bytes(4, "big") + bytes(v for c in pal for v in c) + data)


def size_of(path):
    try:
        from PIL import Image
        with Image.open(path) as im:
            return im.size
    except Exception:
        return None


def main():
    quiet = "-q" in sys.argv
    files = [a for a in sys.argv[1:] if a != "-q"]
    build()
    if not files:
        files = sorted(glob.glob(os.path.join(HERE, "build", "images", "*")) +
                       glob.glob(os.path.join(HERE, "build", "gen", "*")))
    runs = same = refused = 0
    bad = []
    for path in files:
        data = open(path, "rb").read()
        own = size_of(path)
        sizes = [(160, 0)]
        if own:
            sizes.append((max(1, own[0] // 3), max(1, own[1] // 3)))
        for ed in ("lj", "tj"):
            for pname, pal in PALS.items():
                for w, h in sizes:
                    ref = None
                    for name, cmd in runners(ed):
                        p = subprocess.run(cmd, input=request(ed, w, h, pal, data), capture_output=True, timeout=600)
                        got = (p.returncode, p.stdout)
                        runs += 1
                        if ref is None:
                            ref = got
                            refused += p.returncode != 0
                        elif got != ref:
                            bad.append("%s %s %s %sx%s: %s differs from the 68k (exit %d against %d, %d bytes "
                                       "against %d)" % (os.path.basename(path), ed, pname, w, h, name, got[0],
                                                        ref[0], len(got[1]), len(ref[1])))
                        else:
                            same += 1
        if not quiet:
            print(os.path.basename(path), "done", flush=True)
    for line in bad:
        print("FAIL", line)
    print("pens: %d runs, %d the same as the Atari's code, %d refused alike by all, %d differ" %
          (runs, same, refused, len(bad)))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
