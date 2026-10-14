#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Erez Yaary
"""a56.out -> a C table of the 512 words the DSP's bootstrap loads.

    cd dsp && a56 rsa.a56 && python3 mkh.py a56.out rsa_prog.h dsp_rsa_prog
"""
import sys
src, dst, name = sys.argv[1:4]
p = [0] * 512
for line in open(src):
    f = line.split()
    if f and f[0] == "P":
        a = int(f[1], 16)
        if a >= 512:
            sys.exit("program too big for the bootstrap: P:%04X" % a)
        p[a] = int(f[2], 16)
rows = [", ".join("0x%06lxUL" % x for x in p[i:i + 6]) for i in range(0, 512, 6)]
with open(dst, "w") as f:
    f.write("/* generated from %s by dsp/mkh.py - do not edit */\n" % src.replace("a56.out", "rsa.a56"))
    f.write("static const unsigned long %s[512] = {\n\t%s\n};\n" % (name, ",\n\t".join(rows)))
