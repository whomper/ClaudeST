#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""profpad.py --ship SHIP.ELF --out OUT.ELF --cpu=CPU --dir DIR
   [--exclude OBJ ...] -- LINK...

A timed build (common's gem.c and the app's main with AES_PROFILE, which
the speed rig runs) linked at the shipped build's places modulo 256, as
the 68030's caches see them, so the rig times the layout that ships (the
hub, 17 Nov 2026, from EPlay 1.45; first EWeb's tools/profpad.py).

Three things move it:
- the timed objects are bigger;
- the one block the linker merges every string into (.str1.1, at the
  start of the read-only data) grows by their strings;
- it keeps functions only the timed code calls. Put those last in their
  source file, where leaving them out moves nothing in the shipped build.

The tool links LINK (the link without its -o) with a map. It takes the
first symbol both builds have that is not at its shipped place modulo
256, looking at unique names only, and skipping the --exclude objects'
own symbols and the strings' .L labels, whose order inside the merged
block is the linker's. It puts a pad (common/atari/codepad.S: code,
read-only data, data and BSS, 1 to 256 bytes each) after the object
before that symbol's object in the link, and grows the pad until the
symbol is in place; then the next, until every one is.
It fails the link, so make fails, when a symbol moved inside an object
whose first symbols are in place, or when any symbol is still out of
place; the ELF is then left as OUT.ELF.bad, to look at. The pads are
kept in DIR/pads.txt, so the next link starts from them.

What an app's Makefile needs:
- the shipped link writes its map beside the ELF, -Wl,-Map,$@.map
  (SHIP.ELF.map: where the read-only data starts, which nm calls code);
- the timed link runs through it, $@ given as --out and no -o of its own:
    build/prof/app.elf: build/atari/app.elf build/prof/gem.o build/prof/main.o \\
                        common/tools/profpad.py common/atari/codepad.S
    	python3 common/tools/profpad.py --ship build/atari/app.elf --out $@ \\
    	    --cpu=-m68030 --dir build/prof/pads \\
    	    --exclude build/atari/main.o --exclude build/prof/main.o \\
    	    --exclude build/atari/gem.o --exclude build/prof/gem.o \\
    	    -- $(CC) $(LDFLAGS) $(TIMED_OBJS) -lgcc
  (--cpu=-m68000 for an ST build; each --exclude an object that differs
  between the two builds, both its builds);
- make test depends on the timed program, so the check runs with it.
- the link script puts .rodata in .text, as common/atari/link.ld does:
  the read-only data's start is read from the map's
  "*(.rodata .rodata.*)" line.
"""
import argparse
import os
import re
import subprocess
import sys

KINDS = (("text", "t", ""), ("ro", "r", "_ro"), ("data", "d", "_data"), ("bss", "b", "_bss"))
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def nm(path):
    """name -> (address, kind), and the names defined twice"""
    out = subprocess.run(["m68k-linux-gnu-nm", "-n", "--defined-only", path], capture_output=True, text=True,
                         check=True).stdout
    syms, twice = {}, set()
    for ln in out.splitlines():
        p = ln.split()
        if len(p) != 3 or p[1].lower() not in "trdb":
            continue
        if p[2] in syms:
            twice.add(p[2])
        syms[p[2]] = (int(p[0], 16), p[1].lower())
    return syms, twice


def defined(obj):
    out = subprocess.run(["m68k-linux-gnu-nm", "--defined-only", obj], capture_output=True, text=True).stdout
    return {ln.split()[-1] for ln in out.splitlines() if ln.split()}


def pieces(mapfile):
    """the map's input sections: (start, size, object), kept ones only"""
    out = []
    text = open(mapfile, errors="replace").read()
    text = text[text.find("Linker script and memory map"):]
    for m in re.finditer(r"^ \S*\s+0x([0-9a-f]+)\s+0x([0-9a-f]+) (\S+\.o)\s*$", text, re.M):
        start, size = int(m.group(1), 16), int(m.group(2), 16)
        if size:
            out.append((start, size, m.group(3)))
    return sorted(out)


def ro_start(mapfile):
    """where the read-only data starts (it shares .text with the code, so
    nm calls it code)"""
    text = open(mapfile, errors="replace").read()
    m = re.search(r"\*\(\.rodata \.rodata\.\*\)\s*\n\s*\S*\s+0x([0-9a-f]+)", text)
    return int(m.group(1), 16) if m else None


def str_block(mapfile):
    """the block the linker merged every string into: (start, end), or None"""
    text = open(mapfile, errors="replace").read()
    i = text.find("*(.rodata .rodata.*)")
    if i < 0:
        return None
    m = re.search(r"^ \S*\.str1\.1\s+0x([0-9a-f]+)\s+0x([0-9a-f]+) ", text[i:], re.M)
    if not m:
        return None
    start = int(m.group(1), 16)
    return start, start + int(m.group(2), 16)


def kinds(syms, ro):
    """each symbol's kind, read-only data apart from code"""
    return {n: (a, "r" if k == "t" and ro is not None and a >= ro else k) for n, (a, k) in syms.items()}


def object_at(parts, addr):
    """the object of the innermost piece holding addr (the merged strings'
    piece spans others)"""
    found = None
    for start, size, obj in parts:
        if start > addr:
            break
        if addr < start + size:
            found = obj
    return found


def link(a, link_cmd, pads, mapfile):
    cmd, us, after = [], [], {}
    for i, (obj, sizes) in enumerate(pads):
        name = "profpad_%d" % i
        o = os.path.join(a.dir, name + ".o")
        subprocess.run(["m68k-linux-gnu-gcc", a.cpu, "-DPAD_NAME=" + name] +
                       ["-DPAD_%s=%d" % (k.upper(), sizes[k]) for k, _, _ in KINDS] +
                       ["-DPAD_STR=%d" % sizes.get("str", 0)] +
                       ["-c", os.path.join(ROOT, "atari", "codepad.S"), "-o", o], check=True)
        after[obj] = o
        us += ["-Wl,-u," + name + s for _, _, s in KINDS]
        if sizes.get("str", 0):
            us.append("-Wl,-u," + name + "_str")
    for w in link_cmd:
        cmd.append(w)
        if w in after:
            cmd.append(after[w])
    subprocess.run(cmd[:1] + us + cmd[1:] + ["-o", a.out, "-Wl,-Map," + mapfile], check=True)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--ship", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--cpu", required=True)
    ap.add_argument("--dir", required=True)
    ap.add_argument("--exclude", action="append", default=[])
    ap.add_argument("link", nargs=argparse.REMAINDER)
    a = ap.parse_args()
    link_cmd = a.link[1:] if a.link[:1] == ["--"] else a.link
    objs = [w for w in link_cmd if w.endswith(".o")]
    os.makedirs(a.dir, exist_ok=True)
    kept = os.path.join(a.dir, "pads.txt")
    pads = []			# [(object it follows, {kind: size})], in link order
    try:
        for ln in open(kept):
            p = ln.split()
            if len(p) in (1 + len(KINDS), 2 + len(KINDS)) and p[0] in objs:
                sizes = dict(zip([k for k, _, _ in KINDS], map(int, p[1:1 + len(KINDS)])))
                sizes["str"] = int(p[1 + len(KINDS)]) if len(p) > 1 + len(KINDS) else 0
                pads.append((p[0], sizes))
    except (OSError, ValueError):
        pads = []
    ship, ship2 = nm(a.ship)
    ship = kinds(ship, ro_start(a.ship + ".map"))	# (its link's map, beside it)
    skip = set().union(*(defined(o) for o in a.exclude)) if a.exclude else set()
    ship_blk = str_block(a.ship + ".map")
    mapfile = a.out + ".map"
    why = None
    for it in range(60):
        link(a, link_cmd, pads, mapfile)
        prof, prof2 = nm(a.out)
        prof = kinds(prof, ro_start(mapfile))
        # (not the strings' labels, .LC: the merged block's order is the
        # linker's, with the timed build's strings in it; only where it
        # ends can be put back)
        blk = str_block(mapfile)

        def in_blk(addr, b):
            return b is not None and b[0] <= addr < b[1]
        common = sorted((prof[n][0], n) for n in prof
                        if n in ship and n not in skip and n not in ship2 and n not in prof2
                        and not n.startswith(("profpad_", ".L")) and ship[n][1] == prof[n][1]
                        and not in_blk(prof[n][0], blk) and not in_blk(ship[n][0], ship_blk))
        bad = [(addr, n) for addr, n in common if (ship[n][0] - addr) % 256]
        if not bad:
            break
        addr, n = bad[0]
        kind = prof[n][1]
        d = (ship[n][0] - addr) % 256
        k = next(k for k, letter, _ in KINDS if letter == kind)
        if kind == "r" and blk and addr >= blk[1] and \
           all((ship[x[1]][0] - x[0]) % 256 == 0 for x in common if prof[x[1]][1] == "r" and x[0] < blk[0]) and \
           not any(prof[x[1]][1] == "r" and blk[1] <= x[0] < addr for x in common):
            # (read-only data after the merged strings, all before it in
            # place: the timed build's strings made the block longer; as
            # many again of its own, mod 256, put the rest back: EConvert,
            # 18 Nov 2026, the timed gem.c's 55 bytes). Only the first
            # symbol after the block: a later one out of place moved with an
            # object before it, its own pad's work (EHypView, 19 Nov 2026:
            # none.6 then libwin's what.3, after the timed ehypview.o's
            # longer read-only data, the two pads undoing each other for
            # 60 tries)
            for o, sizes in pads:
                if o == objs[-1]:
                    sizes["str"] = (sizes.get("str", 0) + d) % 256
                    break
            else:
                sizes = {kk: 256 for kk, _, _ in KINDS}
                sizes["str"] = d
                pads.append((objs[-1], sizes))
            continue
        # the first symbol of its kind out of place: the section itself
        # starts elsewhere, because the one before it ends elsewhere (the
        # BSS after a timed object's longer data). Only the section before
        # it, made longer at its very end, puts it back: a pad of that kind
        # after the last object (EPDF, 18 Nov 2026: crt0's _BasePage, the
        # first BSS, with no object before it to pad after)
        if all(prof[x[1]][1] != kind for x in common if x[0] < addr) and k != KINDS[0][0]:
            prev = KINDS[[kk for kk, _, _ in KINDS].index(k) - 1][0]
            for o, sizes in pads:
                if o == objs[-1]:
                    sizes[prev] = (sizes[prev] + d - 1) % 256 + 1
                    break
            else:
                sizes = {kk: 256 for kk, _, _ in KINDS}
                sizes[prev] = d
                pads.append((objs[-1], sizes))
            continue
        parts = pieces(mapfile)
        obj = object_at(parts, addr)
        if obj not in objs:
            # an archive's member (libgcc.a's __divdi3, after the last
            # object listed: EConvert, 18 Nov 2026): the pad goes after the
            # last listed object before it
            listed = [o for st, _, o in parts if st < addr and o in objs]
            if listed:
                before = listed[-1]
                for o, sizes in pads:
                    if o == before:
                        sizes[k] = (sizes[k] + d - 1) % 256 + 1
                        break
                else:
                    sizes = {kk: 256 for kk, _, _ in KINDS}
                    sizes[k] = d
                    pads.append((before, sizes))
                    pads.sort(key=lambda p: objs.index(p[0]))
                continue
        # its object's first symbol of that kind: out of place too, or a pad
        # before the object can't help
        first = next((x for x in common if prof[x[1]][1] == kind and object_at(parts, x[0]) == obj), None)
        if obj not in objs or first is None or (ship[first[1]][0] - first[0]) % 256 == 0 \
           or objs.index(obj) == 0:
            why = "%s (%s) moved inside %s: a pad can't put it back" % (n, kind, obj)
            break
        before = objs[objs.index(obj) - 1]
        for o, sizes in pads:
            if o == before:
                sizes[k] = (sizes[k] + d - 1) % 256 + 1
                break
        else:
            sizes = {kk: 256 for kk, _, _ in KINDS}
            sizes[k] = d
            pads.append((before, sizes))
            pads.sort(key=lambda p: objs.index(p[0]))
    with open(kept, "w") as f:
        for o, sizes in pads:
            f.write(o + " " + " ".join(str(sizes[k]) for k, _, _ in KINDS) + " %d\n" % sizes.get("str", 0))
    print("profpad: %s: %d symbols at the shipped places modulo 256, %d not; pads after %s"
          % (a.out, len(common) - len(bad), len(bad),
             ", ".join("%s %s" % (os.path.basename(o), "/".join(str(s[k]) for k, _, _ in KINDS))
                       for o, s in pads) or "none"))
    if bad:
        if why:
            print("profpad:   " + why)
        for addr, n in bad[:8]:
            print("profpad:   %s shipped at %#x, timed at %#x" % (n, ship[n][0], addr))
        os.replace(a.out, a.out + ".bad")	# (make sees no ELF; kept to look at)
        sys.exit(1)


if __name__ == "__main__":
    main()
