#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""16-colour GEM IMG pictures for the help files (helpfront.py, and
AERA-Atari's brand/tools/mkhelpbanner.py).

A picture is a list of pens, row by row, in the standard 16 colours as
TOS and EmuTOS set them on a Falcon or TT, in hardware pen order: the
order EHypView and ST-Guide copy them to the screen in. mask() is hcp's
dither mask for a monochrome screen: which pens draw black there.
"""
import struct

# measured on a Falcon in Hatari (EmuTOS), pens 0-15
PAL = [(255, 255, 255), (255, 0, 0), (0, 255, 0), (255, 255, 0), (0, 0, 255), (255, 0, 255),
       (0, 255, 255), (186, 186, 186), (138, 138, 138), (170, 0, 0), (0, 170, 0), (170, 170, 0),
       (0, 0, 170), (170, 0, 170), (0, 170, 170), (0, 0, 0)]
(WHITE, RED, GREEN, YELLOW, BLUE, MAGENTA, CYAN, LGREY, GREY, DRED, DGREEN, OLIVE, DBLUE, PURPLE,
 TEAL, BLACK) = range(16)
VDI2PEN = [0, 15, 1, 2, 4, 6, 3, 5, 7, 8, 9, 10, 12, 14, 11, 13]

# on a mono screen: a GEM screen capture as the program draws itself
# there (dark text and bars black, light bars white), and a banner's
# dark ground black with its lettering white
MONO_SCREEN = {BLACK, BLUE, DBLUE, DRED, RED, DGREEN, GREEN, PURPLE, OLIVE, GREY, TEAL, MAGENTA}
MONO_BANNER = {BLACK, GREY, DBLUE, BLUE, DRED, RED, PURPLE, DGREEN}


def mask(black_pens):
    """hcp's dither mask: a digit per VDI colour, 1 for black on a mono screen"""
    return "%" + "".join("1" if VDI2PEN[v] in black_pens else "0" for v in range(16))


def nearest(rgb, pens=range(16)):
    return min(pens, key=lambda p: sum((a - b) ** 2 for a, b in zip(rgb, PAL[p])))


def dither(img, pens):
    """a Pillow image to the given pens, Floyd-Steinberg; a list of pens"""
    w, h = img.size
    px = [list(map(float, c)) for c in img.convert("RGB").get_flattened_data()]
    out = [0] * (w * h)
    for y in range(h):
        for x in range(w):
            i = y * w + x
            old = px[i]
            p = nearest(old, pens)
            out[i] = p
            err = [o - n for o, n in zip(old, PAL[p])]
            for dx, dy, f in ((1, 0, 7 / 16), (-1, 1, 3 / 16), (0, 1, 5 / 16), (1, 1, 1 / 16)):
                xx, yy = x + dx, y + dy
                if 0 <= xx < w and yy < h:
                    q = px[yy * w + xx]
                    for k in range(3):
                        q[k] += err[k] * f
    return out


def write_img(path, pens, w, h, planes=4):
    """a GEM IMG: plane by plane for each row, as bit strings"""
    out = bytearray(struct.pack(">8H", 1, 8, planes, 2, 372, 372, w, h))
    nb = (w + 7) // 8
    for y in range(h):
        row = pens[y * w:(y + 1) * w]
        for p in range(planes):
            bits = bytearray(nb)
            for x, v in enumerate(row):
                if v >> p & 1:
                    bits[x >> 3] |= 0x80 >> (x & 7)
            i = 0
            while i < nb:
                n = min(255, nb - i)
                out += bytes([0x80, n]) + bits[i:i + n]
                i += n
    with open(path, "wb") as f:
        f.write(out)


def img_size(path):
    """(width, height) of a GEM IMG"""
    with open(path, "rb") as f:
        h = struct.unpack(">8H", f.read(16))
    return h[6], h[7]


def preview(pens, w, h, path):
    """the picture as a PNG, to look at"""
    from PIL import Image
    im = Image.new("RGB", (w, h))
    im.putdata([PAL[p] for p in pens])
    im.save(path)
