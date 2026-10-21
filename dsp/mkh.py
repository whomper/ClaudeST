#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""a56.out -> a C table of the 512 words the DSP's bootstrap loads, and
for a program with code in external P memory (one block, from P:$7000:
jpeg.a56's) a second, NAME_ext, with its address NAME_EXT_AT and length
NAME_EXT_N, for the program to load with its own command.

    cd dsp && a56 rsa.a56 && python3 mkh.py a56.out rsa_prog.h dsp_rsa_prog
    cd dsp && a56 ec.a56 && python3 mkh.py a56.out ec_prog.h dsp_ec_prog ec.a56
    cd dsp && a56 jpeg.a56 && python3 mkh.py a56.out jpeg_prog.h dsp_jpeg_prog jpeg.a56
"""
import sys
src, dst, name = sys.argv[1:4]
asm = sys.argv[4] if len(sys.argv) > 4 else "rsa.a56"
p = [0] * 512
ext = {}
for line in open(src):
    f = line.split()
    if f and f[0] == "P":
        a = int(f[1], 16)
        if a >= 0x7000:
            ext[a] = int(f[2], 16)
        elif a >= 512:
            sys.exit("program too big for the bootstrap: P:%04X" % a)
        else:
            p[a] = int(f[2], 16)
rows = [", ".join("0x%06lxUL" % x for x in p[i:i + 6]) for i in range(0, 512, 6)]
with open(dst, "w") as f:
    f.write("/* SPDX-License-Identifier: GPL-2.0-or-later\n * Copyright (C) 2026 AERA | Erez Yaary\n"
            " * generated from %s by dsp/mkh.py - do not edit */\n" % src.replace("a56.out", asm))
    f.write("static const unsigned long %s[512] = {\n\t%s\n};\n" % (name, ",\n\t".join(rows)))
    if ext:
        at, n = min(ext), max(ext) - min(ext) + 1
        e = [ext.get(at + i, 0) for i in range(n)]
        rows = [", ".join("0x%06lxUL" % x for x in e[i:i + 6]) for i in range(0, n, 6)]
        f.write("#define %s_EXT_AT 0x%04x\n#define %s_EXT_N %d\n" % (name.upper(), at, name.upper(), n))
        f.write("static const unsigned long %s_ext[%d] = {\n\t%s\n};\n" % (name, n, ",\n\t".join(rows)))
