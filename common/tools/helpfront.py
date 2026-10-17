#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""The AERA front page of an app's help file (guidelines 4.3).

    helpfront.py page docs/front.json IN.STG OUT.STG
        OUT.STG is IN.STG with its "Main" node made the front page; the
        pictures it shows are copied next to OUT.STG, where hcp finds them.
        Standard Python only, so the release build needs nothing new.

    helpfront.py thumb SCREEN.PNG OUT.IMG CROP [BADGE ...]
        A screen capture (Hatari, in the standard 16 colours) as the
        half-size picture beside the text: CROP is x,y,w,h of the capture,
        each BADGE x,y (in the capture) a numbered teal disc, 1, 2, 3...
        Needs Pillow; make it once and commit it (docs/help/).

The page, from top to bottom:
  - the AERA banner (common/help/NAME.IMG, from AERA-Atari's brand/);
  - "welcome": a paragraph saying what the program is;
  - "window": the program's window, the picture on the right with its
    numbered parts explained on the left, and where to read more;
  - "diagram": how the program reaches its servers: up to three boxes
    with arrows between them, and a way round underneath (Falcon mode);
  - "contents": the guide's pages in groups, two side by side;
  - a footer: name, version, AERA | Erez Yaary and the project's address.

In the texts **bold** is bold, [[Page]] a link to that page and
[[text|Page]] a link with other words.
"""
import json
import os
import re
import shutil
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gemimg  # noqa: E402

COMMON = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
WIDTH = 75          # the page, in characters
CELL = 16           # pixels a line of text takes in the 8 x 16 system font
CW = 8


# ---------------------------------------------------------------- text

def markup(s):
    """**bold** and [[links]] to ST-Guide's commands"""
    s = s.replace("@", "@@")
    s = re.sub(r"\*\*([^*]+)\*\*", r"@{B}\1@{b}", s)
    return re.sub(r"\[\[([^]|]+)(?:\|([^]]+))?\]\]",
                  lambda m: '@{"%s" LINK "%s"}' % (m.group(1), m.group(2) or m.group(1)), s)


def vis(s):
    """the width of ST-Guide text on the page"""
    s = re.sub(r'@\{"([^"]*)" LINK "[^"]*"\}', r"\1", s)
    return len(re.sub(r"@\{[^}\"]*\}", "", s).replace("@@", "@"))


def wrap(text, width, indent=""):
    """markup'd text in lines of at most width; a link stays whole"""
    words = re.sub(r'@\{"[^"]*" LINK "[^"]*"\}', lambda m: m.group(0).replace(" ", "\0"),
                   text).split()
    out, line = [], ""
    for w in words:
        w = w.replace("\0", " ")
        t = (line + " " + w) if line else w
        if line and vis(t) > width:
            out.append(line)
            line = indent + w
        else:
            line = t
    if line:
        out.append(line)
    return out


def at(cols):
    """a line with text at character positions: [(index, text), ...]"""
    out = ""
    for i, t in cols:
        out += " " * max(0, i - vis(out)) + t
    return out.rstrip()


def heading(title):
    return ["@{B}%s@{b}" % markup(title), "@line 1 %d 0" % WIDTH, ""]


# ---------------------------------------------------------------- page

def window(w, d, pics):
    """the picture on the right, what it shows on the left"""
    img = os.path.join(d, w["picture"])
    pw, ph = gemimg.img_size(img)
    name = "WINDOW.IMG"
    pics.append((img, name))
    col = WIDTH - (pw + CW - 1) // CW + 1          # its first column (from 1)
    left = col - 3                                  # the text's width beside it
    o = heading(w.get("title", "The main window"))
    o.append("@image %s %d %s" % (name, col, gemimg.mask(gemimg.MONO_SCREEN)))
    text = []
    if w.get("intro"):
        text += wrap(markup(w["intro"]), left) + [""]
    for n, item in enumerate(w.get("items", []), 1):
        lines = wrap(markup(item), left - 3)
        text.append("@{B}%d@{b}  %s" % (n, lines[0]))
        text += ["   " + l for l in lines[1:]]
    if w.get("more"):
        text += [""] + wrap(markup(w["more"]), left)
    need = (ph + CELL - 1) // CELL + 1              # lines the picture covers, and a gap
    text += [""] * max(1, need - len(text))
    return o + text


def diagram(g):
    """boxes 16 wide in columns 2, 30 and 58, four lines tall, arrows
    across their middle; "round" a way from the first to the last
    underneath, its label above the line"""
    boxes, arrows = g["boxes"], g.get("arrows", [])
    xs = [2, 30, 58][:len(boxes)]
    o = heading(g["title"])
    o += ["@box %d 16 4" % x for x in xs] + [""]
    row1, row2 = [], []
    for b in boxes:
        assert all(vis(markup(t)) <= 13 for t in b), "box text over 13 characters: %s" % b
    for i, (x, b) in enumerate(zip(xs, boxes)):
        row1.append((x + 1, markup(b[0])))
        if len(b) > 1:
            row2.append((x + 1, markup(b[1])))
        if i < len(arrows):
            a = markup(arrows[i])
            row1.append((x + 15 + (12 - vis(a)) // 2, a))   # between the boxes
    o.append(at(row1))
    o += ["@line %d 12 0 2" % (x + 16) for x in xs[:-1]]
    o += [at(row2), ""]
    if g.get("round"):
        a, b = xs[0] + 8, xs[-1] + 8                # the boxes' middles
        label = markup(g["round"])
        o += ["@line %d 0 2" % a, "@line %d 0 2 1" % b, "",
              at([((a + b) // 2 - vis(label) // 2 - 1, label)]),
              "@line %d %d 0" % (a, b - a), ""]
    for p in g.get("after", []):
        o += ["  " + l for l in wrap(markup(p), WIDTH - 4)] + [""]
    return o


def contents(groups):
    """the guide's pages in groups, side by side in pairs: the first
    group left, the second right, the third left under the first..."""
    def block(g):
        title, pages = g
        return ["@{B}%s@{b}" % markup(title)] + [markup(p if "[[" in p else "[[%s]]" % p) for p in pages]

    o = heading("Contents")
    for i in range(0, len(groups), 2):
        a = block(groups[i])
        b = block(groups[i + 1]) if i + 1 < len(groups) else []
        for j in range(max(len(a), len(b))):
            o.append(at([(2, a[j] if j < len(a) else ""), (38, b[j] if j < len(b) else "")]))
        o.append("")
    return o


def front(spec, d, version, pics):
    banner = os.path.join(COMMON, "help", spec["banner"] + ".IMG")
    pics.append((banner, "BANNER.IMG"))
    o = ['@node "Main" "%s %s"' % (spec["name"], version),
         "@limage BANNER.IMG 0 " + gemimg.mask(gemimg.MONO_BANNER), ""]
    for p in spec["welcome"]:
        o += wrap(markup(p), WIDTH) + [""]
    if spec.get("window"):
        o += window(spec["window"], d, pics)
    if spec.get("diagram"):
        o += diagram(spec["diagram"])
    o += contents(spec["contents"])
    o += ["@line 1 %d 0" % WIDTH,
          "%s %s  \xfa  AERA | Erez Yaary  \xfa  %s" % (spec["name"], version, spec["address"]),
          "@endnode", ""]
    for line in o:
        if not line.startswith("@") or line.startswith("@{"):
            assert vis(line) <= WIDTH, "too wide: " + line
    return "\n".join(o)


def page(spec_path, src, out):
    spec = json.load(open(spec_path, encoding="utf-8"))
    d = os.path.dirname(os.path.dirname(os.path.abspath(spec_path)))    # the repository
    raw = open(src, "rb").read()
    # the Atari character set (most guides), or UTF-8 that hcp converts
    # (ECal's): keep what the source is in
    utf8 = not re.search(rb"@inputenc\s+\"?atarist", raw)
    try:
        stg = raw.decode("utf-8") if utf8 else None
    except UnicodeDecodeError:
        utf8 = False
    if not utf8:
        stg = raw.decode("latin-1")
    m = re.search(r'@node\s+"?Main\b"?[^\n]*\n.*?@endnode[^\n]*\n', stg, re.S)
    if not m:
        sys.exit("%s: no Main node" % src)
    v = re.search(r"@\$VER:\s*\S+\s+([0-9.]+)", stg)
    w = re.search(r"^@width\s+(\d+)", stg, re.M)
    global WIDTH
    WIDTH = min(75, int(w.group(1))) if w else 75
    pics = []
    page_ = front(spec, d, v.group(1) if v else "", pics)
    if not re.search(r"^@autorefoff", stg[:m.start()], re.M):
        # the guide links words to pages by itself: not on the front page,
        # whose links are all written out
        page_ = page_.replace("\n", "\n@autorefoff\n", 1).replace("@endnode", "@autorefon\n@endnode")
    if utf8:
        page_ = page_.replace("\xfa", "\u00b7")     # the middle dot
    stg = stg[:m.start()] + page_ + stg[m.end():]
    with open(out, "w", encoding="utf-8" if utf8 else "latin-1") as f:
        f.write(stg)
    for img, name in pics:
        shutil.copyfile(img, os.path.join(os.path.dirname(os.path.abspath(out)), name))


# ---------------------------------------------------------------- picture

DIGITS = {                 # 5 x 7, for the badges
    "1": ["00100", "01100", "00100", "00100", "00100", "00100", "01110"],
    "2": ["01110", "10001", "00001", "00110", "01000", "10000", "11111"],
    "3": ["11110", "00001", "00001", "01110", "00001", "00001", "11110"],
    "4": ["00010", "00110", "01010", "10010", "11111", "00010", "00010"],
    "5": ["11111", "10000", "11110", "00001", "00001", "10001", "01110"],
    "6": ["00110", "01000", "10000", "11110", "10001", "10001", "01110"],
    "7": ["11111", "00001", "00010", "00100", "01000", "01000", "01000"],
    "8": ["01110", "10001", "10001", "01110", "10001", "10001", "01110"],
    "9": ["01110", "10001", "10001", "01111", "00001", "00010", "01100"],
}


def badge(pens, w, cx, cy, digit, r=7):
    """a numbered teal disc with a black ring"""
    h = len(pens) // w
    for y in range(cy - r - 1, cy + r + 2):
        for x in range(cx - r - 1, cx + r + 2):
            if 0 <= x < w and 0 <= y < h:
                d2 = (x - cx) ** 2 + (y - cy) ** 2
                if d2 <= r * r:
                    pens[y * w + x] = gemimg.TEAL
                elif d2 <= (r + 1.2) ** 2:
                    pens[y * w + x] = gemimg.BLACK
    for j, row in enumerate(DIGITS[digit]):
        for i, b in enumerate(row):
            if b == "1":
                pens[(cy - 3 + j) * w + cx - 2 + i] = gemimg.WHITE


def thumb(src, out, crop, badges, width=288):
    from PIL import Image
    x, y, cw, ch = crop
    im = Image.open(src).convert("RGB").crop((x, y, x + cw, y + ch))
    w = width
    h = round(ch * w / cw)
    im = im.resize((w, h), Image.BOX)
    pens = [gemimg.nearest(c) for c in im.get_flattened_data()]
    for n, (bx, by) in enumerate(badges, 1):
        badge(pens, w, round((bx - x) * w / cw), round((by - y) * h / ch), str(n))
    # a frame, so it reads as a picture and not as a window of its own
    for i in range(w):
        pens[i] = pens[(h - 1) * w + i] = gemimg.BLACK
    for j in range(h):
        pens[j * w] = pens[j * w + w - 1] = gemimg.BLACK
    gemimg.write_img(out, pens, w, h)
    gemimg.preview(pens, w, h, os.path.splitext(out)[0] + ".png")


def main():
    a = sys.argv[1:]
    if len(a) == 4 and a[0] == "page":
        page(a[1], a[2], a[3])
    elif len(a) >= 4 and a[0] == "thumb":
        nums = lambda s: tuple(int(v) for v in s.split(","))   # noqa: E731
        thumb(a[1], a[2], nums(a[3]), [nums(b) for b in a[4:]])
    else:
        sys.exit(__doc__)


if __name__ == "__main__":
    main()
