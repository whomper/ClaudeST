#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""A GDOS font chosen in ClaudeST, as Erez uses them under NVDI: the
window should redraw in it with no dialog left, CLAUDEST.INF should keep
it, and after a restart a screenful of text (a Hebrew chat) should draw
whole in it.

    tests/hatari/font_gdos.py WORK [--machine Falcon:16] [--release v1.49]
                              [--boot SECONDS]

With the demo bridge on the serial port, it opens the demo's Hebrew chat,
then Options > Font... (with the mouse: the selector has no key), and
moves down the families from the system font, trying each with Return
until the selector closes: the first fixed-width GDOS font, in the size
it offers first (proportional ones can't be picked, so the dialog stays).
Then:
  1. the window redrew in the new font, and stayed (no dialog left);
  2. CLAUDEST.INF has FONT= with that font's id (not 1, the system font);
  3. after a restart, the same chat draws as it did before the restart.
Screenshots go in WORK/shots. Without GDOS fonts the selector has only the
system font: "no GDOS fonts, skipped", and it passes. For NVDI, run it
with AERA_WITH_NVDI=1 (common/tools/machines.py installs Erez's NVDI on
the Falcons); the boot then takes about 90 s, 120 s on Falcon-4.
--release tests that ClaudeST release (common/tools/getrelease.py) in
place of binary/CLAUDEST.PRG.
"""
import argparse
import os
import shutil
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rig import Rig, K, add_machine  # noqa: E402

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
F2, F10 = 0x3c, 0x44


def changed(a, b, box=None):
    """the share of pixels that differ between two screenshots (in box)"""
    from PIL import Image, ImageChops
    x, y = Image.open(a).convert("RGB"), Image.open(b).convert("RGB")
    if box is None:
        w, h = x.size
        box = (0, h // 20, w, h)	# below the menu bar
    d = ImageChops.difference(x.crop(box), y.crop(box)).convert("L").point(lambda v: 255 if v else 0)
    return d.histogram()[255] / float((box[2] - box[0]) * (box[3] - box[1]))


def inf_font(path):
    """FONT= from CLAUDEST.INF, as (id, size), or None"""
    try:
        with open(path, errors="replace") as f:
            for line in f:
                if line.upper().startswith("FONT="):
                    i, _, s = line.strip()[5:].partition(",")
                    return int(i), int(s or 0)
    except (OSError, ValueError):
        pass
    return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("work")
    add_machine(ap, "Falcon:16")
    ap.add_argument("--release", help="a ClaudeST release to test (e.g. v1.49), fetched from GitHub")
    ap.add_argument("--boot", type=int, help="seconds for the machine to boot and ClaudeST to come "
                    "online (default 90 under NVDI, 120 on a Falcon-4 under NVDI, else 45)")
    a = ap.parse_args()
    work = os.path.abspath(a.work)
    shutil.rmtree(work, ignore_errors=True)
    hd = os.path.join(work, "hd")
    os.makedirs(hd)
    nvdi = bool(os.environ.get("AERA_WITH_NVDI"))
    boot = a.boot or ((120 if a.machine.startswith("Falcon-4") else 90) if nvdi else 45)

    prg = os.path.join(ROOT, "binary", "CLAUDEST.PRG")
    if a.release:
        rel = os.path.join(work, "release")
        subprocess.run([sys.executable, os.path.join(ROOT, "common", "tools", "getrelease.py"),
                        "ClaudeST@" + a.release, rel, "CLAUDEST.PRG"], check=True)
        prg = os.path.join(rel, "CLAUDEST.PRG")
    print("ClaudeST from", a.release or prg)
    shutil.copy(prg, os.path.join(hd, "CLAUDEST.PRG"))
    inf = os.path.join(hd, "CLAUDEST.INF")
    with open(inf, "w", newline="") as f:
        # Hatari wires only the MFP port to the bridge: on a Falcon, port 6
        f.write("[OPTIONS]\r\nLINK=serial\r\n" +
                ("SERIALPORT=6\r\n" if a.machine.startswith("Falcon") else ""))

    out, inp = os.path.join(work, "st_out"), os.path.join(work, "st_in")
    bridge = subprocess.Popen([sys.executable, os.path.join(ROOT, "bridge", "claude_bridge.py"),
                               "--backend", "demo", "--pipe", out, inp],
                              stdout=open(os.path.join(work, "bridge.log"), "w"), stderr=subprocess.STDOUT)
    serial = ["--rs232-out", out, "--rs232-in", inp]

    fails = []

    def check(what, ok, detail=""):
        print("  %s %s%s" % ("ok  " if ok else "FAIL", what, ("  (" + detail + ")") if detail else ""),
              flush=True)
        if not ok:
            fails.append(what)

    def hebrew_chat(r, name):
        """the demo's Hebrew chat: Chats (F2), the fourth in the list"""
        r.key(F2)
        r.wait(4)
        for _ in range(4):
            r.key(K["TAB"])
            time.sleep(0.3)
        r.key(K["RETURN"])
        r.wait(5)
        r.move(*rest)
        return r.shot(name)

    r = Rig(work, a.machine, extra=serial)
    skipped = False
    try:
        r.wait(boot, limit=60)
        w, h = r.size()
        ch = 16 if h >= 400 else 8		# the system font's height
        bar = ch + 3
        rest = (w - 8, h * 3 // 4)		# the mouse, out of the way
        before = hebrew_chat(r, "1-chat-system-font")

        # Options > Font...: Options is the fourth title (" Desk " starts
        # two cells in), Font... its twelfth item
        r.move(16 + 8 * (6 + 6 + 6) + 8 * 4, bar // 2)
        time.sleep(1)
        r.click(16 + 8 * (6 + 6 + 6) + 8 * 4, bar + 11 * ch + ch // 2)
        r.wait(3, limit=300)			# the first time, every GDOS font is measured
        dlg = r.shot("2-font-dialog")
        check("the font selector opened", changed(before, dlg) > 0.05,
              "%.0f%% of the screen" % (100 * changed(before, dlg)))

        picked = None
        for n in range(1, 60):
            r.key(K["DOWN"])
            r.wait(1, limit=30)		# a vector font's sample takes a while under NVDI
            moved = r.shot("_moved")
            if changed(dlg, moved) < 0.0005:	# the list didn't move: the last family
                break
            dlg = moved
            r.key(K["RETURN"])		# OK, if this one can be picked
            r.wait(3, limit=60)
            now = r.shot("_now")
            if changed(dlg, now) > 0.05:	# the selector closed (the sample alone is less)
                picked = n
                break
        if picked is None:
            print("  no fixed-width GDOS font in the selector (no GDOS, or none listed), skipped")
            skipped = True
            r.key(K["ESC"])
            r.wait(2)
        else:
            r.move(*rest)
            after = r.shot("3-chosen")
            print("  picked the font %d down from the system font" % picked)
            check("the window redrew in the new font", changed(before, after) > 0.01,
                  "%.1f%% changed from the system font" % (100 * changed(before, after)))
            check("the selector is gone", changed(dlg, after) > 0.05,
                  "%.1f%% changed from the selector" % (100 * changed(dlg, after)))
            time.sleep(4)
            later = r.shot("4-later")
            check("the screen stayed as drawn (no dialog left)", changed(after, later) < 0.02,
                  "%.1f%% changed since" % (100 * changed(after, later)))
            font = inf_font(inf)
            check("CLAUDEST.INF keeps the font", bool(font) and font[0] != 1, "FONT=%s" % (font,))
    finally:
        r.stop()

    if not skipped and not fails:
        r = Rig(work, a.machine, extra=serial)
        try:
            r.wait(boot, limit=60)
            again = hebrew_chat(r, "5-restarted")
            check("after a restart, the chat draws as before (in the GDOS font)",
                  changed(after, again) < 0.03,
                  "%.1f%% differs from before the restart" % (100 * changed(after, again)))
            check("... and not in the system font", changed(before, again) > 0.01,
                  "%.1f%% differs from the system font" % (100 * changed(before, again)))
        finally:
            r.stop()
    bridge.kill()
    for f in os.listdir(r.shots):
        if f.startswith("_"):
            os.remove(os.path.join(r.shots, f))
    print("screenshots in", r.shots)
    if skipped and not fails:
        print("font_gdos: no GDOS fonts, skipped")
        return 0
    print("font_gdos:", "FAIL: " + "; ".join(fails) if fails else "ok")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
