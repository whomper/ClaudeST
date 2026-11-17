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
the disassembler would read as code; after a switch table the code is
read again from the table's end. Exits 1 when it finds any.
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


LINE = re.compile(r'\s*([0-9a-f]+):\t')
SWITCH = re.compile(r'\tjmp %pc@\((2|[0-9a-f]+ <[^>]*>),%d\d:w\)')


def disasm(elf, start=None, stop=None):
    cmd = ["m68k-linux-gnu-objdump", "-d", "-m", "m68k:68000"]
    if start is not None:
        cmd += ["--start-address=%d" % start, "--stop-address=%d" % stop]
    lines = {}
    for line in subprocess.run(cmd + [elf], capture_output=True, text=True).stdout.splitlines():
        m = LINE.match(line)
        if m:
            lines[int(m.group(1), 16)] = line
    return lines


def text_words(elf):
    """the .text section's bytes and its address, to read switch tables"""
    out = subprocess.run(["m68k-linux-gnu-readelf", "-SW", elf], capture_output=True, text=True).stdout
    for line in out.splitlines():
        f = line.replace("[ ", "[").split()
        if len(f) > 5 and f[1] == ".text":
            addr, off, size = int(f[3], 16), int(f[4], 16), int(f[5], 16)
            with open(elf, "rb") as fh:
                fh.seek(off)
                return addr, fh.read(size)
    return 0, b""


def resync(elf, lines, start, size, base, text):
    """GCC's switch tables (jmp %pc@(2,%dN:w), its target the table, then
    16-bit offsets) are data in the code: objdump reads them as
    instructions and may run on out of step into the code after them (a
    table ending in 0x0072 took the jsr after it as an ori's operand, and
    read the moveq after that as a move at an odd offset: EContact, 13 Nov
    2026; the same can hide a real odd access). Each table is skipped to
    its end (its nearest target) and the rest of the function read again
    from there."""
    end = start + size
    a = start
    while a < end:
        line = lines.get(a)
        if line and SWITCH.search(line):
            tbl = a + 4
            stop = end
            k = tbl
            while k < stop and k + 2 <= base + len(text):
                o = k - base
                e = int.from_bytes(text[o:o + 2], "big", signed=True)
                if e > 0:
                    stop = min(stop, tbl + e)
                k += 2
            for b in range(tbl, end, 2):
                lines.pop(b, None)
            lines.update(disasm(elf, stop, end))
            a = stop
            continue
        a += 2


def scan(elf):
    found = 0
    lines = disasm(elf)
    base, text = text_words(elf)
    for start, size, name in funcs(elf):
        resync(elf, lines, start, size, base, text)
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
