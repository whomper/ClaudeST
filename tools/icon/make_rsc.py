#!/usr/bin/env python3
"""Write EClaude's icons as files for Atari icon and resource editors:

  icons/ECLAUDE.RSC    GEM resource, one tree: a monochrome G_ICON and a
                      colour G_CICON (16 colours), each with its mask and the
                      label "EClaude". Copy them into DESKICON.RSC /
                      DESKCICN.RSC with a resource editor (Interface, ORCS,
                      RSM...).
  icons/ECLAUDE.ICN    the monochrome image, ICN text format
  icons/ECLAUDMK.ICN  its mask, ICN text format

The colour icon uses the standard VDI colours (white, black, red, green,
yellow, greys), since desktop icons can't bring their own palette.
"""
import os
import struct

import make_icon as mi   # draws the icon (px: mono, col: 16-colour design)

W = H = 32
ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..")
OUT = os.path.join(ROOT, "icons")

# the About box's colours -> standard VDI pens for the desktop
TO_STD = {0: 0, 1: 1, 8: 8, 9: 9, 10: 1, 11: 1, 12: 2, 13: 6, 14: 3, 15: 9}


def mask_of(px):
    """the icon's silhouette: everything not reachable from the border"""
    outside = [[False] * W for _ in range(H)]
    stack = [(x, y) for x in range(W) for y in (0, H - 1)] + \
            [(x, y) for y in range(H) for x in (0, W - 1)]
    while stack:
        x, y = stack.pop()
        if not (0 <= x < W and 0 <= y < H) or outside[y][x] or px[y][x]:
            continue
        outside[y][x] = True
        stack += [(x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)]
    return [[0 if outside[y][x] else 1 for x in range(W)] for y in range(H)]


def plane(bits):
    """32x32 grid of 0/1 -> 128 bytes, rows of two big-endian words"""
    out = bytearray()
    for y in range(H):
        v = 0
        for x in range(W):
            v = (v << 1) | (bits[y][x] & 1)
        out += struct.pack(">I", v)
    return bytes(out)


mono = mi.px
mask = mask_of(mono)
# the colour icon's own mask also covers its coloured (non-zero) pixels
cmask = [[1 if (mask[y][x] or mi.col[y][x]) else 0 for x in range(W)] for y in range(H)]
# Colour icon pixels are hardware colour registers, not VDI pens (pen 1,
# black, is register 15 in 16 colours; pen 2, red, is register 1 ...)
PEN_TO_REG = [0, 15, 1, 2, 4, 6, 3, 5, 7, 8, 9, 10, 12, 14, 11, 13]
colour = [[PEN_TO_REG[TO_STD[mi.col[y][x]]] for x in range(W)] for y in range(H)]
col_planes = b"".join(plane([[(colour[y][x] >> p) & 1 for x in range(W)] for y in range(H)])
                      for p in range(4))
TEXT = b"EClaude\0\0\0\0\0"      # 12 bytes, like every icon text
assert len(TEXT) == 12


def iconblk(pmask, pdata, ptext):
    # ib_char: 0x1000 = black on white, no letter; icon 32x32 at (20,0);
    # text 72x8 below it
    return struct.pack(">lllhhhhhhhhhhh", pmask, pdata, ptext,
                       0x1000, 0, 0, 20, 0, 32, 32, 0, 32, 72, 8)


def obj(nxt, head, tail, typ, flags, state, spec, x, y, w, h):
    return struct.pack(">hhhHHHlHHHH", nxt, head, tail, typ, flags, state, spec, x, y, w, h)


def px_(n):          # a coordinate in pixels (high byte, signed), 0 characters
    assert 0 <= n < 128
    return n << 8


def ch_(n):          # in characters: 8 pixels wide in every resolution
    return n


G_BOX, G_ICON, G_CICON, LASTOB = 20, 31, 33, 0x20

# ---- layout of the file ------------------------------------------------
HDR = 36
off_obj = HDR
nobs = 3
off_ib = off_obj + 24 * nobs
off_imdata = off_ib + 34
off_mono_data = off_imdata
off_mono_mask = off_mono_data + 128
off_strings = off_mono_mask + 128
off_text = off_strings
off_trindex = off_text + 12
rssize = off_trindex + 4          # end of the standard part
off_ext = rssize                  # extension array: size, cicon table, 0
off_ctab = off_ext + 12
off_cicondata = off_ctab + 8      # one table entry + the -1 terminator

objects = (
    obj(-1, 1, 2, G_BOX, 0, 0, 0x00FF1100, 0, 0, ch_(21), px_(56)) +
    obj(2, -1, -1, G_ICON, 0, 0, off_ib, ch_(1), px_(8), ch_(9), px_(40)) +
    obj(0, -1, -1, G_CICON, LASTOB, 0, 0, ch_(11), px_(8), ch_(9), px_(40))
)

ciconblk = (
    iconblk(0, 0, 0) +                     # pointers filled in on load
    struct.pack(">l", 1) +                 # one colour resolution follows
    plane(mono) + plane(mask) + TEXT +
    struct.pack(">hlllll", 4, 1, 1, 0, 0, 0) +   # 4 planes, no "selected" image
    col_planes + plane(cmask)
)

body = (
    objects +
    iconblk(off_mono_mask, off_mono_data, off_text) +
    plane(mono) + plane(mask) +
    TEXT +
    struct.pack(">l", off_obj)             # tree 0 starts at object 0
)
assert HDR + len(body) == rssize
total = rssize + 12 + 8 + len(ciconblk)
ext = struct.pack(">lll", total, off_ctab, 0)
ctab = struct.pack(">ll", 0, -1)

hdr = struct.pack(">18H",
                  0x0004,          # rsh_vrsn: new format (colour icons)
                  off_obj,         # rsh_object
                  rssize,          # rsh_tedinfo (none)
                  off_ib,          # rsh_iconblk
                  rssize,          # rsh_bitblk (none)
                  off_trindex,     # rsh_frstr (none)
                  off_strings,     # rsh_string
                  off_imdata,      # rsh_imdata
                  off_trindex,     # rsh_frimg (none)
                  off_trindex,     # rsh_trindex
                  nobs, 1, 0, 1, 0, 0, 0,
                  rssize)

os.makedirs(OUT, exist_ok=True)
data = hdr + body + ext + ctab + ciconblk
assert len(data) == total
with open(os.path.join(OUT, "ECLAUDE.RSC"), "wb") as f:
    f.write(data)


def write_icn(name, bits):
    words = []
    for y in range(H):
        v = 0
        for x in range(W):
            v = (v << 1) | bits[y][x]
        words += [v >> 16, v & 0xffff]
    with open(os.path.join(OUT, name), "w", newline="\r\n") as f:
        f.write("/* GEM Icon Definition: */\n#define ICON_W 0x0020\n#define ICON_H 0x0020\n")
        f.write("#define DATASIZE 0x0040\nUWORD image[DATASIZE] =\n{ ")
        f.write(",\n  ".join(", ".join("0x%04X" % w for w in words[i:i + 8])
                             for i in range(0, len(words), 8)))
        f.write("\n};\n")


write_icn("ECLAUDE.ICN", mono)
write_icn("ECLAUDMK.ICN", mask)
print("wrote", len(data), "byte ECLAUDE.RSC and two ICN files to", os.path.normpath(OUT))
