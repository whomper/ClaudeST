#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Erez Yaary
"""gadget.py SHOT: where XaAES's iconify gadgets are in a screenshot (a red
square with a small green one in its corner; the closer has no green), as
"X Y" lines, top to bottom. An iconified window's title bar has one too:
it brings the window back."""
import sys
from PIL import Image


def gadgets(path):
    im = Image.open(path).convert("RGB")
    w, h = im.size
    px = im.load()

    def red(p):
        return p[0] > 150 and p[1] < 60 and p[2] < 60

    def green(p):
        return p[1] > 150 and p[0] < 100 and p[2] < 100

    found = []
    for y in range(h):
        for x in range(w):
            if not green(px[x, y]):
                continue
            if not any(red(px[x + d, y + e]) for d in range(-10, 1) for e in range(-10, 1)
                       if 0 <= x + d < w and 0 <= y + e < h):
                continue
            if not any(abs(fx - x) < 16 and abs(fy - y) < 16 for fx, fy in found):
                found.append((x, y))
    return found


if __name__ == "__main__":
    for x, y in gadgets(sys.argv[1]):
        print(x, y)
