#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""appicon.py - an AERA app's icon, from its drawing in common/icons/NAME.txt
(the guidelines, 5.6; Erez, 22 Oct 2026):

    python3 common/tools/appicon.py NAME [--rsc icons/NAME.RSC] [--rscnv icons/NAMENV.RSC] [--icn icons/NAME.ICN]
            [--mask icons/NAMEMK.ICN] [--headers DIR] [--docs DIR] [--label TEXT]
    python3 common/tools/appicon.py --sheet FILE.png      (all of them, for review)

The drawing is 32 x 32 pixels in three depths, as letters, each drawn by
hand: [256] the master, in TOS 4.04's 256 colours (brand/tools/falcon256.txt);
[16] TOS's 16 system colours; [2] black (X) and white (o). The selected
images are made from them: in colour every shade goes a step lighter on its
own ramp, the outline stays, and a yellow glow goes round the outside (not
into holes); in monochrome the AES inverts the icon, so the [2] drawing must
read both ways.

It writes, each when asked:
  --rsc      a GEM resource for the desktop: one tree with a G_ICON (the
             monochrome image and mask, for TOS 1 and 2) and a G_CICON with
             it and the 16- and 256-colour images, each with its selected
             one, both labelled --label (NAME). Desktop
             icons bring no palette, so a pixel is a register of the
             screen's standard palette. Copy it into DESKCICN.RSC with a
             resource editor (Interface, ORCS, RSM...), or use it as it is
             in Thing or Teradesk.
  --rscnv    the same for NVDI's own 256 colours (Erez, 23 Oct 2026): the
             desktop draws a pixel as a hardware register, and NVDI's 256
             colours share only 4 registers with TOS's, so --rsc's
             256-colour image comes out in other colours there. This one
             has each colour on NVDI's nearest register
             (brand/tools/nvdi256.txt). --rsc is for TOS, EmuTOS, and NVDI
             with TOS's palette loaded (COLORVDI.CPX); --rscnv for NVDI
             with its own.
  --icn      the monochrome image, ICN text (resource editors read it);
  --mask     its mask, ICN text;
  --headers  icon.h (icon_bits, one plane), icon16.h (icon16_px in VDI
             pens, 0 not drawn; icon16_rgb, pens 8-15 as TOS has them) and
             icon256.h (icon256_px, a byte a pixel in VDI pens 16-255;
             icon256_rgb, those pens' colours; both 0 when NO_ICON256 is
             defined, for an ST edition) for the About box and the
             iconified window (common/ui);
  --docs     icon.png (256 colours), icon16.png and icon2.png, each 8 times
             bigger, clear round it, for the README;
  --png      the 256-colour icon alone, 8 times bigger, clear round it
             (AERA-Atari's brand/icons, for the profile and the previews).
"""
import argparse
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ICONS = os.path.join(HERE, "..", "icons")
W = H = 32


def hx(h):
    return tuple(int(h[i:i + 2], 16) for i in (0, 2, 4))


# [256]: the letters and their colours, each one of TOS 4.04's 256
C256 = dict(K="222222", W="ffffff", w="eeeeee", l="cccccc", g="aaaaaa", m="888888", d="555555",
            R="ff2200", r="bb1100", q="771100", T="ff3300", O="ff6600", o="bb4400", p="772200",
            Y="ffcc00", A="ffaa00", B="bb7700", b="774400", U="0077ff", u="0055bb", n="003377", N="002244",
            S="00bbff", s="0088bb", E="00ff44", e="00bb33", f="007722")
LIT256 = dict(w="W", l="w", g="l", m="g", d="m", r="R", q="r", o="O", p="o", A="Y", B="A", b="B",
              U="S", u="U", n="u", N="n", s="S", e="E", f="e")
# [16]: TOS's 16 colours: letter -> (register, VDI pen, colour)
C16 = dict(W=(0, 0, "ffffff"), R=(1, 2, "ff0000"), E=(2, 3, "00ff00"), Y=(3, 6, "ffff00"), U=(4, 4, "0000ff"),
           M=(5, 7, "ff00ff"), S=(6, 5, "00ffff"), l=(7, 8, "bbbbbb"), m=(8, 9, "888888"), r=(9, 10, "aa0000"),
           e=(10, 11, "00aa00"), A=(11, 14, "aaaa00"), u=(12, 12, "0000aa"), P=(13, 15, "aa00aa"),
           s=(14, 13, "00aaaa"), K=(15, 1, "000000"))
LIT16 = dict(r="R", e="E", A="Y", u="U", s="S", m="l", l="W", P="M")
GLOW256, GLOW16 = "Y", "Y"
PAL256 = [hx(h) for h in open(os.path.join(HERE, "..", "..", "brand", "tools", "falcon256.txt")).read().split()] \
    if os.path.exists(os.path.join(HERE, "..", "..", "brand", "tools", "falcon256.txt")) else None
# registers of the [256] letters, past the 16 system colours (falcon256.txt;
# written here so that the apps' copies of common/ need no brand/)
REG256 = dict(K=29, W=16, w=17, l=19, g=21, m=23, d=26, R=120, r=187, q=229, T=119, O=116, o=184, p=228,
              Y=110, A=112, B=181, b=226, U=69, u=149, n=205, N=240, S=73, s=152, E=88, e=163, f=214)
# the same letters on NVDI 5.03's own 256 colours, each its nearest register
# (brand/tools/nvdi256.txt, read from the hardware under NVDI, 23 Oct 2026)
REGNV = dict(K=15, W=0, w=250, l=187, g=253, m=8, d=254, R=201, r=232, q=9, T=201, O=207, o=165, p=93,
             Y=219, A=213, B=171, b=93, U=32, u=31, n=23, N=22, S=44, s=37, E=46, e=40, f=28)
# more steps of the grey and red ramps, for a [256] drawing shaded as a
# 256-colour picture is (EPDF's red book; Erez, 30 Oct 2026): each a TOS
# 4.04 register, its lit step, its nearest on NVDI's palette
C256.update(h="dddddd", i="bbbbbb", j="999999", k="777777", v="666666", x="444444", y="333333",
            F="ff0000", V="ff1100", J="ff4400", L="ff5500", z="bb3300", Q="bb2200", G="bb0000",
            H="770000", I="440000", Z="441100")
LIT256.update(h="w", i="l", j="g", k="m", v="k", x="d", y="x", F="T", V="T", J="O", L="O", z="J",
              Q="V", G="Q", H="G", I="H", Z="I")
REG256.update(h=18, i=20, j=22, k=24, v=25, x=27, y=28, F=32, V=121, J=118, L=117, z=185, Q=186,
              G=122, H=188, I=230, Z=253)
REGNV.update(h=7, i=252, j=144, k=8, v=101, x=254, y=58, F=195, V=195, J=201, L=207, z=165, Q=165,
             G=232, H=9, I=235, Z=235)
# and of the blue and green ramps, for EPlay's loudspeaker (Erez, 31 Oct
# 2026: "much more refined in terms of shading"); digits and signs, the
# letters being used up
C256.update({"1": "001177", "2": "002277", "3": "0044bb", "4": "0066bb", "6": "0066ff", "7": "0088ff",
             "8": "0099ff", "9": "00aaff", "0": "00ccff", '"': "004400", "!": "004411", "%": "007711",
             "=": "00bb11", "*": "00bb22", "+": "00ff22"})
LIT256.update({"1": "n", "2": "3", "3": "4", "4": "U", "6": "7", "7": "9", "8": "S", "9": "0", "0": "0",
               '"': "%", "!": "f", "%": "=", "=": "e", "*": "+", "+": "E"})
REG256.update({"1": 203, "2": 204, "3": 148, "4": 150, "6": 68, "7": 70, "8": 71, "9": 72, "0": 74,
               '"': 246, "!": 245, "%": 215, "=": 165, "*": 164, "+": 90})
REGNV.update({"1": 12, "2": 23, "3": 25, "4": 31, "6": 32, "7": 38, "8": 38, "9": 38, "0": 44,
              '"': 242, "!": 242, "%": 10, "=": 239, "*": 40, "+": 46})


def load(name):
    """-> {256: rows, 16: rows, 2: rows}"""
    out, cur = {}, None
    for line in open(os.path.join(ICONS, name + ".txt")):
        line = line.rstrip("\n")
        if line.startswith("#") or not line:
            continue
        if line.startswith("["):
            cur = int(line[1:-1])
            out[cur] = []
            continue
        out[cur].append(line)
    for d, rows in out.items():
        assert len(rows) == H and all(len(r) == W for r in rows), (name, d)
        legal = set(C256) if d == 256 else set(C16) if d == 16 else set("Xo")
        bad = {c for r in rows for c in r} - legal - {"."}
        assert not bad, (name, d, bad)
    return out


def outside(rows):
    """the clear pixels the edge reaches"""
    out = set()
    st = [(x, y) for x in range(W) for y in (0, H - 1)] + [(x, y) for y in range(H) for x in (0, W - 1)]
    while st:
        x, y = st.pop()
        if 0 <= x < W and 0 <= y < H and (x, y) not in out and rows[y][x] == ".":
            out.add((x, y))
            st += [(x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)]
    return out


def selected(rows, lit, glow):
    """the selected image: each letter lit, a glow round the outside"""
    out = [[lit.get(c, c) for c in r] for r in rows]
    for x, y in outside(rows):
        if any(0 <= x + dx < W and 0 <= y + dy < H and rows[y + dy][x + dx] != "."
               for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1))):
            out[y][x] = glow
    return ["".join(r) for r in out]


def mask(rows):
    return [[0 if c == "." else 1 for c in r] for r in rows]


def plane(bits):
    out = bytearray()
    for row in bits:
        v = 0
        for b in row:
            v = (v << 1) | (b & 1)
        out += struct.pack(">I", v)
    return bytes(out)


def planes(rows, reg, n):
    pix = [[reg[c] if c != "." else 0 for c in r] for r in rows]
    return b"".join(plane([[(p >> k) & 1 for p in r] for r in pix]) for k in range(n))


def write_rsc(path, label, d, reg256=None):
    """one tree: a box with the monochrome G_ICON (TOS 1 and 2 know no
    other) and the G_CICON side by side"""
    mono = [[1 if c == "X" else 0 for c in r] for r in d[2]]
    mmask = mask(d[2])
    s16, s256 = selected(d[16], LIT16, GLOW16), selected(d[256], LIT256, GLOW256)
    reg16 = {k: v[0] for k, v in C16.items()}
    text = label.encode("ascii")[:11]
    text += b"\0" * (12 - len(text))

    def iconblk(pmask, pdata, ptext):
        return struct.pack(">lllhhhhhhhhhhh", pmask, pdata, ptext, 0x1000, 0, 0, 20, 0, 32, 32, 0, 32, 72, 8)

    def obj(nxt, head, tail, typ, flags, state, spec, x, y, w, h):
        return struct.pack(">hhhHHHlHHHH", nxt, head, tail, typ, flags, state, spec, x, y, w, h)

    G_BOX, G_ICON, G_CICON, LASTOB = 20, 31, 33, 0x20
    HDR = 36
    off_obj = HDR
    nobs = 3
    off_ib = off_obj + 24 * nobs
    off_imdata = off_ib + 34
    off_text = off_imdata + 256
    off_trindex = off_text + 12
    rssize = off_trindex + 4
    off_ctab = rssize + 12
    objects = (obj(-1, 1, 2, G_BOX, 0, 0, 0x00FF1100, 0, 0, 21, 56 << 8) +
               obj(2, -1, -1, G_ICON, 0, 0, off_ib, 1, 8 << 8, 9, 40 << 8) +
               obj(0, -1, -1, G_CICON, LASTOB, 0, 0, 11, 8 << 8, 9, 40 << 8))
    body = objects + iconblk(off_imdata + 128, off_imdata, off_text) + plane(mono) + plane(mmask) + text + \
        struct.pack(">l", off_obj)
    assert HDR + len(body) == rssize
    ciconblk = (iconblk(0, 0, 0) + struct.pack(">l", 2) + plane(mono) + plane(mmask) + text +
                struct.pack(">hlllll", 4, 1, 1, 1, 1, 1) + planes(d[16], reg16, 4) + plane(mask(d[16])) +
                planes(s16, reg16, 4) + plane(mask(s16)) +
                struct.pack(">hlllll", 8, 1, 1, 1, 1, 0) + planes(d[256], reg256 or REG256, 8) + plane(mask(d[256])) +
                planes(s256, reg256 or REG256, 8) + plane(mask(s256)))
    total = rssize + 12 + 8 + len(ciconblk)
    hdr = struct.pack(">18H", 0x0004, off_obj, rssize, off_ib, rssize, off_trindex, off_text, off_imdata,
                      off_trindex, off_trindex, nobs, 1, 0, 1, 0, 0, 0, rssize)
    data = hdr + body + struct.pack(">lll", total, off_ctab, 0) + struct.pack(">ll", 0, -1) + ciconblk
    assert len(data) == total
    open(path, "wb").write(data)


def write_icn(path, bits):
    words = []
    for row in bits:
        v = 0
        for b in row:
            v = (v << 1) | b
        words += [v >> 16, v & 0xffff]
    with open(path, "w", newline="\r\n") as f:
        f.write("/* GEM Icon Definition: */\n#define ICON_W 0x0020\n#define ICON_H 0x0020\n")
        f.write("#define DATASIZE 0x0040\nUWORD image[DATASIZE] =\n{ ")
        f.write(",\n  ".join(", ".join("0x%04X" % w for w in words[i:i + 8]) for i in range(0, len(words), 8)))
        f.write("\n};\n")


HEAD = "/* SPDX-License-Identifier: GPL-2.0-or-later\n * Copyright (C) 2026 AERA | Erez Yaary */\n"


def write_headers(dirname, name, d):
    mono = [[1 if c == "X" else 0 for c in r] for r in d[2]]
    with open(os.path.join(dirname, "icon.h"), "w") as f:
        f.write(HEAD + "/* %s's icon, 32x32 one plane, from common/icons/%s.txt by\n"
                " * common/tools/appicon.py: don't edit */\n" % (name, name))
        f.write("#define ICON_W 32\n#define ICON_H 32\nstatic const unsigned short icon_bits[ICON_H * 2] = {\n")
        for row in mono:
            v = 0
            for b in row:
                v = (v << 1) | b
            f.write("\t0x%04x, 0x%04x,\n" % (v >> 16, v & 0xffff))
        f.write("};\n")
    pen = {k: v[1] for k, v in C16.items()}
    with open(os.path.join(dirname, "icon16.h"), "w") as f:
        f.write(HEAD + "/* %s's icon, 32x32 in TOS's 16 colours (VDI pens, 2 a byte; 0, white,\n"
                " * isn't drawn), from common/icons/%s.txt by common/tools/appicon.py:\n"
                " * don't edit */\n" % (name, name))
        f.write("static const unsigned char icon16_px[32 * 16] = {\n")
        for r in d[16]:
            p = [pen.get(c, 0) if c != "." else 0 for c in r]
            f.write("\t" + ", ".join("0x%x%x" % (p[i], p[i + 1]) for i in range(0, W, 2)) + ",\n")
        f.write("};\n")
        f.write("/* pens 8-15 as TOS has them (NVDI's greys differ), lent while the\n"
                " * About box is open */\nstatic const short icon16_rgb[8][3] = {\n")
        for p in range(8, 16):
            c = hx(next(v[2] for v in C16.values() if v[1] == p))
            f.write("\t{ %d, %d, %d },\n" % tuple(round(x * 1000 / 255) for x in c))
        f.write("};\n")
    with open(os.path.join(dirname, "icon256.h"), "w") as f:
        f.write(HEAD + "/* %s's icon, 32x32 in 256 colours (VDI pens, a byte a pixel; 0 isn't\n"
                " * drawn), from common/icons/%s.txt by common/tools/appicon.py:\n"
                " * don't edit. Used with 256 colours or more (the About box) */\n" % (name, name))
        f.write("#ifdef NO_ICON256\t/* an ST edition: never 256 colours, 2 KB kept */\n"
                "#define icon256_px ((const unsigned char *)0)\n"
                "#define icon256_rgb ((const short (*)[4])0)\n#else\n")
        f.write("static const unsigned char icon256_px[32 * 32] = {\n")
        for r in d[256]:
            f.write("\t" + ", ".join("%d" % (REG256[c] if c != "." else 0) for c in r) + ",\n")
        f.write("};\n")
        used = sorted({c for r in d[256] for c in r} - {"."}, key=lambda c: REG256[c])
        f.write("/* its pens and their colours in TOS's 256: the About box draws with\n"
                " * the pen that has the colour now (an app's theme may have taken some) */\n"
                "static const short icon256_rgb[%d][4] = {\n" % (len(used) + 1))
        for c in used:
            f.write("\t{ %d, %d, %d, %d },\n" % ((REG256[c],) + tuple(round(x * 1000 / 255) for x in hx(C256[c]))))
        f.write("\t{ 0, 0, 0, 0 }\n};\n#endif\n")


# ---- pictures ---------------------------------------------------------------
def image(rows, depth, lit=False):
    from PIL import Image
    im = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    if lit and depth != 2:
        rows = selected(rows, LIT256 if depth == 256 else LIT16, GLOW256)
    for y, r in enumerate(rows):
        for x, c in enumerate(r):
            if c == ".":
                continue
            if depth == 256:
                col = hx(C256[c])
            elif depth == 16:
                col = hx(C16[c][2])
            else:
                col = (0, 0, 0) if (c == "X") != lit else (255, 255, 255)     # selected: inverted
            im.putpixel((x, y), col + (255,))
    return im


def write_docs(dirname, d, z=8):
    from PIL import Image
    for depth, fn in ((256, "icon.png"), (16, "icon16.png"), (2, "icon2.png")):
        image(d[depth], depth).resize((W * z, H * z), Image.NEAREST).save(os.path.join(dirname, fn))


NAMES = ["ECAL", "ECONTACT", "EMAIL", "EFTP", "EHYPVIEW", "CLAUDEST", "EWEB", "EUPDATE", "EDOS", "EFTPXL", "GATEWAY", "EPLAY"]


def write_sheet(path, z=4):
    """every app, the three depths, idle and selected, and at actual size"""
    from PIL import Image, ImageDraw
    names = [n for n in NAMES if os.path.exists(os.path.join(ICONS, n + ".txt"))]
    cell = W * z + 6
    desk = {256: (0, 170, 0), 16: (0, 170, 0), 2: (255, 255, 255)}
    im = Image.new("RGB", (110 + 6 * cell + 20 + 6 * 44, 20 + len(names) * (cell + 8)), (244, 242, 236))
    dr = ImageDraw.Draw(im)
    for i, n in enumerate(names):
        d = load(n)
        y = 10 + i * (cell + 8)
        dr.text((10, y + cell // 2 - 6), n, (20, 20, 20))
        for k, depth in enumerate((256, 16, 2)):
            for j, lit in enumerate((False, True)):
                x = 110 + (2 * k + j) * cell
                dr.rectangle([x, y, x + cell - 3, y + cell - 3], fill=desk[depth])
                ic = image(d[depth], depth, lit).resize((W * z, H * z), Image.NEAREST)
                im.paste(ic, (x + 1, y + 1), ic)
                x = 110 + 6 * cell + 20 + (2 * k + j) * 44
                dr.rectangle([x, y + 30, x + 40, y + 70], fill=desk[depth])
                ic = image(d[depth], depth, lit)
                im.paste(ic, (x + 4, y + 34), ic)
    im.save(path)


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("name", nargs="?")
    ap.add_argument("--rsc")
    ap.add_argument("--rscnv")
    ap.add_argument("--icn")
    ap.add_argument("--mask")
    ap.add_argument("--headers")
    ap.add_argument("--docs")
    ap.add_argument("--label")
    ap.add_argument("--png")
    ap.add_argument("--sheet")
    a = ap.parse_args(argv[1:])
    if PAL256:                          # the letters' registers are the palette's colours
        for k, v in C256.items():
            assert PAL256[REG256[k]] == hx(v), k
    if a.sheet:
        write_sheet(a.sheet)
    if not a.name:
        return
    d = load(a.name)
    if a.rsc:
        write_rsc(a.rsc, a.label or a.name, d)
    if a.rscnv:
        write_rsc(a.rscnv, a.label or a.name, d, REGNV)
    if a.icn:
        write_icn(a.icn, [[1 if c == "X" else 0 for c in r] for r in d[2]])
    if a.mask:
        write_icn(a.mask, mask(d[2]))
    if a.headers:
        write_headers(a.headers, a.name, d)
    if a.docs:
        write_docs(a.docs, d)
    if a.png:
        from PIL import Image
        image(d[256], 256).resize((W * 8, H * 8), Image.NEAREST).save(a.png)


if __name__ == "__main__":
    main(sys.argv)
