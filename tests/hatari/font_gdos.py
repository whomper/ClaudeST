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
then Options > Font... (with the mouse: there is no key), the family's
Font dialog (common/ui/fontsel.c): Family, Style and Size lists.
  1. The dialog opens on the system font (the click that opened it chose
     nothing), the pointer resting on a family chooses nothing (ClaudeST
     1.50's own dialog did under TOS 4.04), and under the dialog and
     after Cancel the status line is drawn as before (1.49 drew it in a
     Symbol face once NVDI's fonts were loaded).
Then it opens the dialog again, which lists the system font and the
fixed-width GDOS fonts only, by family; moves one Down to the first
family, in the style and size offered first, and presses Return:
  2. the window redrew in the new font, and stayed (no dialog left), and
     asked the bridge for nothing (a late click once opened Projects);
  3. CLAUDEST.INF has FONT= with that font's id (not 1, the system font);
  4. after a restart, the same chat draws as it did before the restart.
Screenshots go in WORK/shots. Without GDOS fonts the selector has only the
system font and no CLAUDEST.FNT is written: "no GDOS fonts, skipped",
and it passes after the checks in 1. For NVDI, run it
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


def selected_row(shot, x, y, w, ch):
    """the row of the selector's family list drawn selected (inverted:
    mostly dark), 0 the top one shown; None if none is"""
    from PIL import Image
    im = Image.open(shot).convert("L")
    px = im.load()
    for k in range(8):
        dark = sum(1 for yy in range(y + k * ch + 2, y + (k + 1) * ch - 2, 2)
                   for xx in range(x + 2, x + w, 2) if px[xx, yy] < 128)
        if dark > 0.5 * ((ch - 4) // 2) * ((w - 2) // 2):
            return k
    return None


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
    for p in (out, inp):        # before Hatari, which won't start without them
        if not os.path.exists(p):
            os.mkfifo(p)
    bridge = subprocess.Popen([sys.executable, os.path.join(ROOT, "bridge", "claude_bridge.py"),
                               "--backend", "demo", "-v", "--pipe", out, inp],
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

        # the status box's first line ("Online: demo"), right of the ring:
        # the same text in the same font, whatever happened in between
        status = (5 * 8, h - 2 * (ch + 6), 23 * 8, h - (ch + 6))

        # the family's Font dialog (common/ui/fontsel.c): 48 cells of the
        # system font by 17, in the middle of the desktop, under the menu
        # bar (form_center)
        dw, dh = 48 * 8, 17 * ch
        dx, dy = (w - dw) // 2, bar + (h - bar - dh) // 2
        dialog = (dx, dy, dx + dw, dy + dh)
        # its Family list: two cells in and two rows down (and 3 pixels),
        # 26 cells wide, the scroll bar in its last two
        list_x, list_y, list_w = dx + 2 * 8, dy + 2 * ch + 3, 24 * 8
        fam_rows = (list_x, list_y, list_x + list_w, list_y + 4 * ch)	# its first four rows

        def font_dialog(name):
            """Options > Font...: Options is the fifth title (the first,
            " ClaudeST " since 1.68, starts two cells in; Chat before
            Options since 1.67), Font... its twelfth item. The first
            time, every GDOS font is measured before the selector shows:
            minutes at 16 MHz with Erez's 137 families"""
            r.move(16 + 8 * (10 + 6 + 6 + 6) + 8 * 4, bar // 2)
            time.sleep(1)
            r.click(16 + 8 * (10 + 6 + 6 + 6) + 8 * 4, bar + 11 * ch + ch // 2)
            end = time.time() + 900
            while time.time() < end:
                time.sleep(3)
                if changed(before, r.shot("_wait"), dialog) > 0.08:
                    break
            r.wait(2, limit=120)
            return r.shot(name)

        def step(key, limit):
            """one key, then wait until the selector shows it: under NVDI
            each move draws a Bitstream sample, and a key sent before
            ClaudeST is back in its loop is lost. False if nothing moved"""
            for _ in range(2):		# once more if the key went astray
                a = r.shot("_k0")
                r.key(key)
                end = time.time() + limit
                while time.time() < end:
                    time.sleep(0.3)
                    if changed(a, r.shot("_k1"), dialog) > 0.0005:
                        r.wait(0.3, limit=60)	# the sample finished
                        return True
            return False

        dlg = font_dialog("2-font-dialog")
        check("the font selector opened", changed(before, dlg) > 0.05,
              "%.0f%% of the screen" % (100 * changed(before, dlg)))
        row = selected_row(dlg, list_x, list_y, list_w, ch)
        check("the opening click chose no font: the system font, the top row, is selected",
              row == 0, "selected row: %s" % row)
        # (the part the selector leaves showing: on a VGA screen it is tall
        # enough to cover the rest)
        shown = status if dy + dh <= status[1] else (status[0], status[1], min(status[2], dx - 6), status[3])	# clear of its 3-pixel outline
        check("the status line, under the selector, is still in the window's font",
              changed(before, dlg, shown) < 0.02,
              "%.1f%% of it differs" % (100 * changed(before, dlg, shown)))
        # the pointer resting on a row picks nothing: on the Family list's
        # third row (the system font is the first). ClaudeST 1.50's own
        # dialog chose the row under it again and again under AES 4
        r.move(list_x + 40, list_y + 2 * ch + ch // 2)
        time.sleep(3)
        resting = r.shot("2a-pointer-resting")
        # (the pointer itself, in the shot, is a few per cent of it; a row
        # chosen moves the highlight, half of these rows or more)
        check("the pointer resting on a row picks nothing", changed(dlg, resting, fam_rows) < 0.2,
              "%.1f%% of the list's first rows changed" % (100 * changed(dlg, resting, fam_rows)))
        r.key(K["ESC"])			# Cancel
        r.wait(3)
        r.move(*rest)
        cancelled = r.shot("2b-cancelled")
        check("after Cancel the window is as before", changed(before, cancelled) < 0.02,
              "%.1f%% differs" % (100 * changed(before, cancelled)))
        check("... its status line too", changed(before, cancelled, status) < 0.02,
              "%.1f%% of it differs" % (100 * changed(before, cancelled, status)))

        # CLAUDEST.FNT has every font measured, "1 Name..." for one the
        # selector lists; it lists those by family, after the system font:
        # the first is one Down from it
        picked = None
        try:
            with open(os.path.join(hd, "CLAUDEST.FNT"), errors="replace") as f:
                fams = [l.rstrip("\r\n") for l in f if l.startswith("1 ")]
            picked = 1 if fams else None
        except OSError:
            pass
        if picked is None:
            print("  no fixed-width GDOS font (no CLAUDEST.FNT, or no \"1 \" line in it), skipped")
            skipped = True
        else:
            print("  %d fixed-width fonts in CLAUDEST.FNT; the first: %s"
                  % (len(fams), fams[0][2:].split("\t")[0]))
            dlg = font_dialog("2c-font-dialog-again")
            ups = 0
            while ups < len(fams) + 5 and step(K["UP"], 3):	# to the system font,
                ups += 1					# wherever the click left it
            downs = 0
            while downs < picked and step(K["DOWN"], 10):
                downs += 1
            on = r.shot("2d-on-the-font")
            print("  %d Up to the system font, %d Down to the font" % (ups, downs))
            row = selected_row(dlg, list_x, list_y, list_w, ch)
            check("... nor the second time", row == 0, "selected row: %s" % row)
            check("the selector moved to the font", downs == picked, "%d of %d Downs" % (downs, picked))
            blog = os.path.join(work, "bridge.log")
            sent_before = os.path.getsize(blog)
            r.key(K["RETURN"])			# OK
            r.wait(3, limit=120)
            r.move(*rest)
            after = r.shot("3-chosen")
            check("the selector closed on OK", changed(on, after) > 0.05,
                  "%.1f%% changed from the selector" % (100 * changed(on, after)))
            check("the window redrew in the new font", changed(before, after) > 0.01,
                  "%.1f%% changed from the system font" % (100 * changed(before, after)))
            time.sleep(4)
            later = r.shot("4-later")
            # nothing reaches the window as the selector closes: the click
            # that opened it, held by the AES, once landed on the sidebar's
            # Projects row as laid out in the new font (ba6fe50b, Falcon-4)
            with open(blog, errors="replace") as f:
                f.seek(sent_before)
                asked = [l.strip() for l in f if "<- ['LIST'" in l or "<- ['OPEN'" in l]
            check("the window asked the bridge for nothing after OK", not asked, "; ".join(asked))
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
