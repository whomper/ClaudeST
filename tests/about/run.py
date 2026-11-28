#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""The About box (ui/aboutbox.c) from this tree against git's at REF, in an
emulator: ABOUT.PRG (make prg [REF=...] [ICON=...]: build/new and
build/ref) on each machine, each build's shots of what the box covers, of
the box, and of what it put back. Exit 1 when the two boxes differ by a
pixel, when either put back anything but what was there, or when a box
didn't open or close.

    run.py WORK [MACHINE...] [--nvdi]

The machines, unless given: ST:low ST:med ST:mono TT:ttlow Falcon-4:16 and
Falcon-4:256 with NVDI ("+nvdi" after a machine asks for it; --nvdi for
all). Each build's box's stretches between AES calls (gem.c's AESGAP, 20
ms and more) are printed beside it: the box's longest stretch. The
machine's TOS is the real one where ~/.aera/tos has it, else EmuTOS (each line
says which).
"""
import os
import re
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "speed"))
import speed  # noqa: E402

try:
    from PIL import Image, ImageChops
except ImportError:
    sys.exit("run.py: needs Python's PIL (python3-pil)")

MACHINES = ["ST:low", "ST:med", "ST:mono", "TT:ttlow", "Falcon-4:16", "Falcon-4:256+nvdi"]


def wait_for(r, text, after, most=120):
    end = time.time() + most
    while time.time() < end:
        c = r.console()
        if text in c[after:]:
            return c.index(text, after) + len(text)
        time.sleep(0.5)
    return -1


def one(work, machine, nvdi, build):
    """a build's three shots and its stretches, or (None, reason)"""
    r = speed.Rig(os.path.join(work, "%s-%s%s" % (build, machine.replace(":", "_"), "-nvdi" if nvdi else "")),
                  machine, nvdi=nvdi, display=os.environ.get("XDISPLAY", ":95"))
    try:
        r.put(os.path.join(HERE, "build", build, "ABOUT.PRG"))
        r.start("C:\\ABOUT.PRG", ready="ABOUT ground", boot=900)
        r.xdo("mousemove", "0", "0")             # (the pointer out of the box's way, and still)
        time.sleep(1)
        under = r.shot("under")
        r.key("space")
        at = wait_for(r, "ABOUT open", 0)
        if at < 0:
            return None, "no 'ABOUT open'", r.describe()
        time.sleep(3)
        box = r.shot("box")
        r.key("Escape")
        end = wait_for(r, "ABOUT closed", at, 30)
        if end < 0:
            return None, "the box didn't close on Escape", r.describe()
        time.sleep(1.5)
        back = r.shot("back")
        r.key("space")
        gaps = [int(m) for m in re.findall(r"AESGAP (\d+) ms", r.console()[at:end])]
        return (under, box, back, gaps), None, r.describe()
    finally:
        r.stop()


def differ(a, b):
    """pixels that differ between two shots, and their bounding box"""
    ia, ib = Image.open(a).convert("RGB"), Image.open(b).convert("RGB")
    if ia.size != ib.size:
        return -1, None
    d = ImageChops.difference(ia, ib)
    box = d.getbbox()
    if not box:
        return 0, None
    px = d.crop(box)
    px = px.get_flattened_data() if hasattr(px, "get_flattened_data") else px.getdata()
    n = sum(1 for p in px if p != (0, 0, 0))
    return n, box


def main(argv):
    nvdi_all = "--nvdi" in argv
    a = [x for x in argv if not x.startswith("--")]
    if not a:
        sys.exit(__doc__)
    work = os.path.abspath(a[0])
    for b in ("new", "ref"):
        if not os.path.exists(os.path.join(HERE, "build", b, "ABOUT.PRG")):
            sys.exit("run.py: no build/%s/ABOUT.PRG (make prg)" % b)
    bad = 0
    for m in a[1:] or MACHINES:
        machine, nvdi = m.replace("+nvdi", ""), nvdi_all or m.endswith("+nvdi")
        got = {}
        for b in ("ref", "new"):
            shots, why, where = one(work, machine, nvdi, b)
            if not shots:
                print("%s: %s: %s" % (where, b, why))
                bad = 1
                break
            got[b] = shots
        if len(got) < 2:
            continue
        line = []
        for b in ("ref", "new"):
            under, box, back, gaps = got[b]
            n, _ = differ(under, back)
            if n:
                line.append("%s put back %s pixels wrong" % (b, n if n > 0 else "a screen of another size:"))
                bad = 1
        n, where_box = differ(got["ref"][1], got["new"][1])
        if n:
            bad = 1
            line.append("the boxes differ: %s pixels in %s" % (n, where_box) if n > 0 else "the boxes differ in size")
        else:
            line.append("the same box")
        stretch = ["%s %s" % (b, max(got[b][3]) if got[b][3] else "<20") for b in ("ref", "new")]
        print("%s: %s; its longest stretch (ms): %s  (%s)" % (where, "; ".join(line), ", ".join(stretch),
                                                             os.path.dirname(got["new"][1])))
    return bad


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
