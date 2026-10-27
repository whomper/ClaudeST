#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""Word and long accesses at an odd offset from the stack or frame pointer
in a 68000 build's functions: an address error on a 68000 when they run.

    common/tools/oddaccess.py FILE.elf...

GCC fills an initialised char array on the stack with long moves and
clears its tail with clr.w, at the array's own offset, odd or not
(EMail 1.65's candidate, 27 Oct 2026: accmem's path at sp+39, and
dlg_settings' "CONTACTS.TXT" tail at fp-203). Only the code of the
functions (readelf's FUNC symbols) is looked at, not strings and tables
the disassembler would read as code. Exits 1 when it finds any.
"""
import re
import subprocess
import sys

ODD = re.compile(r'\t(move|movea|clr|add|adda|sub|suba|cmp|cmpa|tst|and|or|eor|neg|not)[lw]\b.*%(sp|fp|a6|a7)@\(-?\d*[13579]\)')


def funcs(elf):
    out = subprocess.run(["m68k-linux-gnu-readelf", "-sW", elf], capture_output=True, text=True).stdout
    for line in out.splitlines():
        f = line.split()
        if len(f) >= 8 and f[3] == "FUNC" and int(f[2]) > 0:
            yield int(f[1], 16), int(f[2]), f[7]


def scan(elf):
    found = 0
    dis = subprocess.run(["m68k-linux-gnu-objdump", "-d", "-m", "m68k:68000", elf],
                         capture_output=True, text=True).stdout
    lines = {}
    for line in dis.splitlines():
        m = re.match(r'\s*([0-9a-f]+):\t', line)
        if m:
            lines[int(m.group(1), 16)] = line
    for start, size, name in funcs(elf):
        for a in range(start, start + size, 2):
            line = lines.get(a)
            if line and ODD.search(line):
                print("%s: %s+0x%x: %s" % (elf, name, a - start, line.split("\t", 2)[-1].strip()))
                found += 1
    return found


if __name__ == "__main__":
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    n = sum(scan(e) for e in sys.argv[1:])
    print("%d odd stack accesses" % n)
    sys.exit(1 if n else 0)
