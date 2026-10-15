#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""app_icon.py WORK EMUTOS.IMG APP.PRG [--copy FILE ...] [--wait S]
              [--pre "steps"] [--gadget X Y] [--key 0x1c] [--display :96]

An app's iconify (the AERA guidelines, 5.1) under XaAES, with run.py.
The app's window on top (after --pre, run.py's steps: "ctrl n" for
EMail's editor) is iconified with its gadget, a key is pressed on the
icon, and the icon's own gadget brings the window back. Two runs: the
first finds the gadget (gadget.py; the first from the top, unless
--gadget says where), the second does it, with a screenshot after each
step (WORK/b/shots) and the four side by side in WORK/sheet.png. The
checks:
  - the window has the iconify gadget (icon_kind());
  - the window becomes an icon at the bottom left, its gadget gone;
  - the key changes nothing on the screen (an icon takes no keys);
  - the window comes back, the screen as it was before.
"""
import argparse
import os
import subprocess
import sys

from PIL import Image, ImageChops

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from gadget import gadgets  # noqa: E402

ap = argparse.ArgumentParser()
ap.add_argument("work")
ap.add_argument("tos")
ap.add_argument("prg")
ap.add_argument("--copy", nargs="*", default=[])
ap.add_argument("--wait", default="50", help="seconds to the app's window (default 50)")
ap.add_argument("--pre", default="")
ap.add_argument("--gadget", nargs=2, type=int)
ap.add_argument("--key", default="0x1c")
ap.add_argument("--display", default=":96")
a = ap.parse_args()
failed = 0


def check(ok, what):
    global failed
    print("%s %s" % ("ok  " if ok else "FAIL", what), flush=True)
    if not ok:
        failed += 1
    return ok


def run(work, steps):
    cmd = [sys.executable, os.path.join(HERE, "run.py"), work, a.tos, a.prg, "--display", a.display,
           "--wait", a.wait, "--steps", steps]
    if a.copy:
        cmd += ["--copy"] + a.copy
    subprocess.run(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=900)
    d = os.path.join(work, "shots")
    return [os.path.join(d, f) for f in sorted(os.listdir(d)) if f.endswith(".png")]


def changed(p, q):
    """the share of the screen that differs"""
    x, y = Image.open(p).convert("RGB"), Image.open(q).convert("RGB")
    d = ImageChops.difference(x, y).convert("L").point(lambda v: 255 if v > 16 else 0)
    return sum(d.histogram()[255:]) / float(x.size[0] * x.size[1])


def near(g, x, y):
    return any(abs(gx - x) < 8 and abs(gy - y) < 8 for gx, gy in g)


work = os.path.abspath(a.work)
pre = (a.pre + "; wait 3; ") if a.pre else ""
if a.gadget:
    gx, gy = a.gadget
else:
    shots = run(os.path.join(work, "a"), pre + "shot")
    g = gadgets(shots[-1]) if shots else []
    if not check(bool(g), "the window has the iconify gadget"):
        sys.exit(1)
    gx, gy = g[0]
shots = run(os.path.join(work, "b"), pre + "shot; click %d %d; wait 3; shot; key %s; wait 2; shot; "
            "click 60 421; wait 3; shot" % (gx, gy, a.key))
if not check(len(shots) == 4, "four screenshots"):
    sys.exit(1)
first, icon, key, back = shots
g1, g2, g4 = gadgets(first), gadgets(icon), gadgets(back)
check(near(g1, gx, gy), "the iconify gadget at %d,%d" % (gx, gy))
check(not near(g2, gx, gy) and any(y >= 380 and x < 80 for x, y in g2),
      "iconified: an icon at the bottom left, the window gone")
c = changed(icon, key)
check(c < 0.002, "the key on the icon changes nothing (%.2f%% of the screen)" % (100 * c))
c = changed(first, back)
check(near(g4, gx, gy) and c < 0.002, "back as it was (%.2f%% of the screen differs)" % (100 * c))
ims = [Image.open(s).convert("RGB") for s in shots]
w, h = ims[0].size
sheet = Image.new("RGB", (w * 2, h * 2), "white")
for k, im in enumerate(ims):
    sheet.paste(im, ((k % 2) * w, (k // 2) * h))
sheet.save(os.path.join(work, "sheet.png"))
print("%d failed" % failed)
sys.exit(1 if failed else 0)
