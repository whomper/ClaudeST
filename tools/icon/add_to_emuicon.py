#!/usr/bin/env python3
"""Add the EClaude icon to EmuTOS's desktop icon file.

EmuTOS's desktop loads its icons from EMUICON.RSC in the root of the boot
drive (it ships with EmuTOS). This writes a copy with the black-and-white
EClaude icon added at the end, and prints its icon number, for use in
EMUDESK.INF:

    tools/icon/add_to_emuicon.py path/to/emuicon.rsc EMUICON.RSC

Then put EMUICON.RSC in the root of the boot drive, drag ECLAUDE.PRG to
the desktop, and pick the new icon with Options > Install icon.
"""
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
CLAUDE_RSC = os.path.join(HERE, "..", "..", "icons", "ECLAUDE.RSC")
IB = struct.Struct(">lllhhhhhhhhhhh")        # ICONBLK, 34 bytes


def read_icons(path):
    """-> [(mask, data, text, iconblk fields)] for every ICONBLK in a .RSC"""
    d = open(path, "rb").read()
    h = struct.unpack(">18H", d[:36])
    off, n = h[3], h[13]                     # rsh_iconblk, rsh_nib
    icons = []
    for i in range(n):
        f = list(IB.unpack_from(d, off + i * IB.size))
        pmask, pdata, ptext = f[:3]
        w, hh = f[8], f[9]                   # ib_wicon, ib_hicon
        size = (w + 15) // 16 * 2 * hh
        end = d.index(b"\0", ptext) if ptext else 0
        icons.append((d[pmask:pmask + size], d[pdata:pdata + size],
                      d[ptext:end] if ptext else b"", f[3:]))
    return icons


def write_rsc(path, icons):
    """An old-format resource holding the icons, in one tree of G_ICONs."""
    hdr_size = 36
    strings = b""
    text_offs = []
    for _, _, text, _ in icons:
        text_offs.append(hdr_size + len(strings))
        strings += text + b"\0"
    if len(strings) % 2:
        strings += b"\0"
    images = b""
    img_offs = []
    base = hdr_size + len(strings)
    for mask, data, _, _ in icons:
        img_offs.append((base + len(images), base + len(images) + len(mask)))
        images += mask + data
    off_ib = base + len(images)
    iconblks = b""
    for (mask_off, data_off), t, (_, _, _, f) in zip(img_offs, text_offs, icons):
        iconblks += IB.pack(mask_off, data_off, t, *f)
    off_obj = off_ib + len(iconblks)
    n = len(icons)
    objs = struct.pack(">hhhHHHlHHHH", -1, 1, n, 20, 0, 0, 0x1100, 0, 0, 80, 25)  # G_BOX root
    for i in range(n):
        flags = 0x20 if i == n - 1 else 0                        # LASTOB
        nxt = 0 if i == n - 1 else i + 2
        objs += struct.pack(">hhhHHHlHHHH", nxt, -1, -1, 31, flags, 0,
                            off_ib + i * IB.size, 0, 0, 4, 2)    # G_ICON
    off_tree = off_obj + len(objs)
    total = off_tree + 4
    hdr = struct.pack(">18H", 0, off_obj, off_obj, off_ib, off_obj, off_obj,
                      hdr_size, base, off_obj, off_tree, n + 1, 1, 0, n, 0, 0, 0, total)
    with open(path, "wb") as f:
        f.write(hdr + strings + images + iconblks + objs + struct.pack(">l", off_obj))


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    icons = read_icons(sys.argv[1])
    claude = read_icons(CLAUDE_RSC)[0]          # the black-and-white G_ICON
    claude = (claude[0], claude[1], b"", claude[3])
    write_rsc(sys.argv[2], icons + [claude])
    print("Wrote %s with %d icons; EClaude is icon %d (hex %02X)"
          % (sys.argv[2], len(icons) + 1, len(icons), len(icons)))


if __name__ == "__main__":
    main()
