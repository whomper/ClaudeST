#!/usr/bin/env python3
"""Convert a statically linked m68k ELF (linked at 0 with --emit-relocs)
into an Atari TOS executable (.PRG / .APP) with a GEMDOS fixup table.

Only absolute 32-bit relocations (R_68K_32) need fixups; PC-relative
ones are position independent already.
"""
import struct
import sys

R_68K_32 = 1


def read_elf(path):
    data = open(path, "rb").read()
    if data[:4] != b"\x7fELF" or data[4] != 1 or data[5] != 2:
        sys.exit("not a 32-bit big-endian ELF: %s" % path)
    (e_shoff,) = struct.unpack_from(">I", data, 0x20)
    e_shentsize, e_shnum, e_shstrndx = struct.unpack_from(">HHH", data, 0x2E)
    secs = []
    for i in range(e_shnum):
        f = struct.unpack_from(">IIIIIIIIII", data, e_shoff + i * e_shentsize)
        secs.append(dict(name=f[0], type=f[1], flags=f[2], addr=f[3], off=f[4],
                         size=f[5], link=f[6], info=f[7], entsize=f[9]))
    strtab = secs[e_shstrndx]
    for s in secs:
        end = data.index(b"\0", strtab["off"] + s["name"])
        s["name"] = data[strtab["off"] + s["name"]:end].decode()
    return data, secs


def main():
    if len(sys.argv) != 3:
        sys.exit("usage: elf2tos.py input.elf output.prg")
    data, secs = read_elf(sys.argv[1])
    by = {s["name"]: s for s in secs}
    text, dat, bss = by[".text"], by.get(".data"), by.get(".bss")
    tsize = text["size"]
    dsize = dat["size"] if dat else 0
    bsize = bss["size"] if bss else 0
    if text["addr"] != 0:
        sys.exit(".text must be linked at 0")
    if dat and dsize and dat["addr"] != tsize:
        sys.exit(".data must follow .text directly (%x != %x)" % (dat["addr"], tsize))

    if bss and bsize and bss["addr"] != tsize + dsize:
        sys.exit(".bss must follow .data directly (%x != %x)" % (bss["addr"], tsize + dsize))

    image = bytearray(data[text["off"]:text["off"] + tsize])
    if dsize:
        image += data[dat["off"]:dat["off"] + dsize]

    fixups = []
    for s in secs:
        if s["type"] != 4:  # SHT_RELA
            continue
        target = secs[s["info"]]
        if target["name"] not in (".text", ".data"):
            continue
        for i in range(s["size"] // 12):
            r_off, r_info, _ = struct.unpack_from(">IIi", data, s["off"] + i * 12)
            if r_info & 0xFF != R_68K_32:
                continue
            # in a linked (ET_EXEC) file r_offset is already the address
            addr = r_off
            if addr & 1:
                sys.exit("odd relocation at %x" % addr)
            fixups.append(addr)
    fixups.sort()

    reloc = bytearray()
    if fixups:
        reloc += struct.pack(">I", fixups[0])
        prev = fixups[0]
        for a in fixups[1:]:
            d = a - prev
            if d == 0:
                continue
            while d > 254:
                reloc.append(1)
                d -= 254
            reloc.append(d)
            prev = a
        reloc.append(0)
    else:
        reloc += b"\0\0\0\0"

    hdr = struct.pack(">HIIIIIIH", 0x601A, tsize, dsize, bsize, 0, 0, 0, 0)
    with open(sys.argv[2], "wb") as f:
        f.write(hdr + image + reloc)
    print("%s: text %d, data %d, bss %d, %d fixups" % (sys.argv[2], tsize, dsize, bsize, len(fixups)))


if __name__ == "__main__":
    main()
