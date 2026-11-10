#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""The family's Settings dialog (ui/settings.c) in Hatari, against the real programs': SETDEMO.PRG builds EMail 2.14's
six Settings pages and EHypView 1.39's two with the shared dialog, with their real words; this opens each in the demo
and in the real program's own release (fetched with getrelease.py: what Erez runs), takes a screenshot of every page
the same way, and compares the dialog's pixels.

    make prof                                  (here: SETDEMO.PRG with the AES's stretches on the console)
    python3 run.py WORK MACHINE [--shots DIR]  MACHINE as common/tools/machines.py: ST:mono (TOS 1.04, flat),
                                               Falcon:16 (TOS 4.04, 3D), Falcon-4:16 ... (Erez's TOS from ~/.aera/tos
                                               where it is there, else EmuTOS; the report names it)
    python3 run.py WORK MACHINE --speed        the demo's opening times only (the speed rig's AESGAP)
    python3 run.py WORK MACHINE --speed --prg cpu   the same with the programs' own flags (make cpu: DLG_CPU, DLG_CLICK)
    python3 run.py WORK MACHINE --keys         the radio buttons and the keyboard's reach (SETDEMO's third dialog), screenshots keys-*.png
    python3 run.py WORK MACHINE --again        compare the screenshots an earlier run left in WORK again
    python3 run.py WORK --xaaes TOS_IMAGE      the demo in a window under FreeMiNT and XaAES (../xaaes: make base)

The comparison is the box and its 3-pixel outline, the pointer taken away; a difference is listed with
its pixel count and where it is. Screenshots, the real ones next to ours, and a diff for each page that differs go to
SHOTS (default WORK/shots).
"""
import argparse
import os
import re
import shutil
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "speed"))
sys.path.insert(0, os.path.join(HERE, "..", "..", "tools"))
import machines  # noqa: E402
import speed  # noqa: E402
from PIL import Image, ImageChops  # noqa: E402

PRG = os.path.join(HERE, "build", "prof", "SETDEMO.PRG")
GETREL = os.path.join(HERE, "..", "..", "tools", "getrelease.py")
failed = 0


def check(ok, what):
    global failed
    print(("ok   " if ok else "FAIL ") + what, flush=True)
    failed += not ok
    return ok


def release(app, tag, files, rel):
    d = os.path.join(rel, app + "-" + tag)
    if not all(os.path.exists(os.path.join(d, f)) for f in files):
        os.makedirs(d, exist_ok=True)
        subprocess.run([sys.executable, GETREL, "%s@%s" % (app, tag), d] + files, check=True)
    return d


class Session:
    """one Hatari: a program started, keys and clicks, screenshots"""

    def __init__(self, work, machine, display, nvdi=False):
        self.fifo = os.path.join(work, "cmd.fifo")
        self.r = speed.Rig(work, machine, nvdi=nvdi, display=display, extra=["--cmd-fifo", self.fifo])
        self.st = machine.startswith("ST")

    def cmd(self, c):
        with open(self.fifo, "w") as f:
            f.write(c + "\n")

    def undo(self):
        self.cmd("hatari-event keypress 0x61")

    def at(self, text, after=0, timeout=60):
        """position in the console after `text` appears past `after`, or -1"""
        end = time.time() + timeout
        while time.time() < end:
            c = self.r.console()
            i = c.find(text, after)
            if i >= 0:
                return i + len(text)
            time.sleep(0.3)
        return -1

    def away(self):
        """the pointer out of the way, in the screen's bottom right corner"""
        g = dict(l.split("=") for l in self.r.xdo("getwindowgeometry", "--shell", self.r.win()).split())
        self.r.xdo("mousemove", str(int(g["X"]) + int(g["WIDTH"]) - 3), str(int(g["Y"]) + int(g["HEIGHT"]) - 3))
        time.sleep(0.6)

    def shot(self, name):
        self.away()
        self.r.still(most=10)
        return Image.open(self.r.shot(name)).convert("RGB")


def find_box(img, w_px, h_px):
    """top left of a dialog box w_px by h_px on the screenshot: the first row (below the menu bar) with a
    black run of exactly w_px that begins on a multiple of 16 (the dialog's text on a byte: align16)"""
    px = img.load()
    W, H = img.size
    for y in range(20, H - h_px):
        x = 0
        while x < W - w_px + 1:
            if px[x, y] == (0, 0, 0):
                e = x
                while e < W and px[e, y] == (0, 0, 0):
                    e += 1
                if e - x == w_px and x % 16 == 0:
                    # and its bottom edge the same
                    if all(px[x + i, y + h_px - 1] == (0, 0, 0) for i in range(0, w_px, 7)):
                        return x, y
                x = e
            else:
                x += 1
    return None


def crop(img, box, w_px, h_px, m=3):
    x, y = box
    return img.crop((x - m, y - m, x + w_px + m, y + h_px + m))


def diff_report(a, b):
    """(count, [(x0, y0, x1, y1, n)]) the differing pixels and their regions: rows of differences grouped"""
    d = ImageChops.difference(a, b).convert("L").point(lambda v: 255 if v else 0)
    bbox = d.getbbox()
    if not bbox:
        return 0, [], d
    px = d.load()
    W, H = d.size
    seen = set()
    regions = []
    for y in range(H):
        for x in range(W):
            if px[x, y] and (x, y) not in seen:
                stack = [(x, y)]
                seen.add((x, y))
                x0 = x1 = x
                y0 = y1 = y
                n = 0
                while stack:
                    cx, cy = stack.pop()
                    n += 1
                    x0, x1, y0, y1 = min(x0, cx), max(x1, cx), min(y0, cy), max(y1, cy)
                    for dx in range(-4, 5):
                        for dy in range(-4, 5):
                            nx, ny = cx + dx, cy + dy
                            if 0 <= nx < W and 0 <= ny < H and px[nx, ny] and (nx, ny) not in seen:
                                seen.add((nx, ny))
                                stack.append((nx, ny))
                regions.append((x0, y0, x1, y1, n))
    count = sum(r[4] for r in regions)
    return count, regions, d


def tab_xy(box, k, ch, sw=13):
    """the middle of page k's (0-based) name in the list"""
    x0, y0 = box
    return x0 + 40, y0 + (2 + 2 * k) * ch + k * (ch // 2) + ch // 2


def compare(name, demo, real, box_d, box_r, wpx, hpx, out, tag, known=None):
    """the dialog's pixels, ours and the real program's: its box and the 3-pixel outline round it (OUTLINED), which is
    the dialog's own; a ring of background beyond that is counted apart (the desktop in the demo, the program's window
    in the real one: they can't be the same)"""
    a, b = crop(demo, box_d, wpx, hpx, 3), crop(real, box_r, wpx, hpx, 3)
    am, bm = crop(demo, box_d, wpx, hpx, 6), crop(real, box_r, wpx, hpx, 6)
    a.save(os.path.join(out, "%s-%s-ours.png" % (tag, name)))
    b.save(os.path.join(out, "%s-%s-real.png" % (tag, name)))
    n, regions, d = diff_report(a, b)
    nm, _, _ = diff_report(am, bm)
    if n:
        d.save(os.path.join(out, "%s-%s-diff.png" % (tag, name)))
    desc = "; ".join("%dx%d at (%d,%d) %d px" % (r[2] - r[0] + 1, r[3] - r[1] + 1, r[0], r[1], r[4]) for r in regions[:6])
    outline = "; the background beyond the outline differs in %d px (a desktop, a window)" % (nm - n) if nm - n else ""
    if known and n:
        print("NOTE %s %s: %d px differ (%s): %s%s" % (tag, name, n, desc, known, outline), flush=True)
        return n, regions
    check(n == 0, "%s %s: the box and its outline: pixels %s%s%s" % (tag, name, "identical to the real program's" if not n else "differ: %d px" % n,
                                                      (" (" + desc + ")") if n else "", outline))
    return n, regions


def demo_run(a, machine, out, rel, tag):
    """the demo: every page of both dialogs; its screenshots by name"""
    shots, boxes = {}, {}
    s = Session(os.path.join(a.work, "demo-" + tag), machine, a.display)
    try:
        s.r.put(PRG, "")
        s.r.start("C:\\SETDEMO.PRG", ready="SETDEMO ready")
        con = s.r.console()
        m = re.search(r"SETDEMO screen\s+(\d+) (\d+) (\d+) (\d+)", con)
        sw_, sh_, cwid, chei = [int(v) for v in m.groups()] if m else (640, 400, 8, 16)
        print("     demo: screen %dx%d, cell %dx%d, %s" % (sw_, sh_, cwid, chei, s.r.describe()), flush=True)
        # EMail's
        pos = len(con)
        s.r.act("EMail's Settings opened (key 1)", speed.DIALOG, lambda: s.r.key("1"))
        i = s.at("SETDEMO box email", pos)
        m = re.search(r"SETDEMO box email (\d+) (\d+) (\d+) (\d+)", s.r.console()[pos:])
        if not check(m is not None, "%s demo: EMail's dialog opened" % tag):
            return shots, boxes, None
        bx, by, bw, bh = [int(v) for v in m.groups()]
        boxes["email"] = ((bx, by), (bw, bh))
        names = ["mail", "writing", "signature", "look", "contacts", "connection"]
        for k, n in enumerate(names):
            if k:
                x, y = tab_xy((bx, by), k, chei)
                s.r.act("EMail's Settings: page %s" % n, speed.KEY, lambda: s.r.click(x, y))
            shots["email-%d-%s" % (k + 1, n)] = s.shot("demo-email-%d-%s" % (k + 1, n))
        # the pop-up on the Connection page: its button, 14 wide at SX + 26
        px_, py_ = bx + (15 + 26) * cwid + 20, by + (9 * chei + 2 + 5 * (chei * 3 // 8)) + chei // 2
        s.r.act("EMail's Translate pop-up opened", speed.DIALOG, lambda: s.r.click(px_, py_))
        shots["email-6-connection-popup"] = s.shot("demo-email-6-popup")
        s.r.key("Escape")
        time.sleep(1.5)
        s.r.still(most=10)
        # a number typed and Return: OK. The first field holds '0'; the cursor is in it
        pos2 = len(s.r.console())
        # (Connection is the page now: a click on Mail first)
        s.r.click(*tab_xy((bx, by), 0, chei))
        time.sleep(1)
        s.r.key("7")
        time.sleep(0.5)
        s.r.key("Return")
        i = s.at("SETDEMO got email", pos2)
        got = s.r.console()[pos2:].split("SETDEMO got email", 1)[-1].split("\n", 1)[0] if i > 0 else ""
        check(i > 0 and got.strip().startswith("1:") and "check '07'" in got, "%s demo: typed 7, Return: OK, the value kept (%s)" % (tag, got.strip()[:50]))
        # Undo: Cancel
        s.r.key("1")
        s.at("SETDEMO box email", pos2 + 10)
        time.sleep(1.5)
        s.r.key("9")
        time.sleep(0.5)
        p3 = len(s.r.console())
        s.undo()
        i = s.at("SETDEMO got email", p3)
        got = s.r.console()[p3:].split("SETDEMO got email", 1)[-1].split("\n", 1)[0] if i > 0 else ""
        check(i > 0 and got.strip().startswith("0:") and "check '0'" in got, "%s demo: typed 9, Undo: Cancel, the field is back (%s)" % (tag, got.strip()[:50]))
        # Esc empties a field; then OK
        s.r.key("1")
        s.at("SETDEMO box email", p3)
        time.sleep(1.5)
        s.r.key("Escape")
        time.sleep(0.5)
        s.r.key("4")
        time.sleep(0.5)
        p4 = len(s.r.console())
        s.r.key("Return")
        i = s.at("SETDEMO got email", p4)
        got = s.r.console()[p4:].split("SETDEMO got email", 1)[-1].split("\n", 1)[0] if i > 0 else ""
        check(i > 0 and got.strip().startswith("1:") and "check '4'" in got, "%s demo: Esc emptied the field, '4' typed, Return (%s)" % (tag, got.strip()[:50]))
        # EHypView's
        pos = len(s.r.console())
        s.r.act("EHypView's Settings opened (key 2)", speed.DIALOG, lambda: s.r.key("2"))
        s.at("SETDEMO box ehypview", pos)
        m = re.search(r"SETDEMO box ehypview (\d+) (\d+) (\d+) (\d+)", s.r.console()[pos:])
        if not check(m is not None, "%s demo: EHypView's dialog opened" % tag):
            return shots, boxes, None
        hx, hy, hw, hh = [int(v) for v in m.groups()]
        boxes["ehypview"] = ((hx, hy), (hw, hh))
        shots["ehypview-1-general"] = s.shot("demo-ehv-1-general")
        # its pop-up: the button at SX + 2, row 5 + half a character
        ppx, ppy = hx + (15 + 2) * cwid + 40, hy + 5 * chei + chei // 2 + chei // 2
        s.r.act("EHypView's pop-up opened", speed.DIALOG, lambda: s.r.click(ppx, ppy))
        shots["ehypview-1-general-popup"] = s.shot("demo-ehv-1-popup")
        s.r.key("Escape")
        time.sleep(1.5)
        x, y = tab_xy((hx, hy), 1, chei)
        s.r.act("EHypView's Settings: page Library", speed.KEY, lambda: s.r.click(x, y))
        shots["ehypview-2-library"] = s.shot("demo-ehv-2-library")
        ppx, ppy = hx + (15 + 9) * cwid + 40, hy + 4 * chei + chei // 2
        s.r.click(ppx, ppy)
        time.sleep(1.5)
        shots["ehypview-2-library-popup"] = s.shot("demo-ehv-2-popup")
        s.r.key("Escape")
        time.sleep(1)
        s.undo()
        s.at("SETDEMO got ehypview", pos)
        time.sleep(1)
        s.r.key("q")
        s.at("SETDEMO done", pos, 20)
        speed_rows = list(s.r.results)
        con = s.r.console()
        gaps = {}
        s.r.report()
        return shots, boxes, speed_rows
    finally:
        s.r.stop()


def email_real(a, machine, out, rel, tag):
    d = release("EMail", "v2.14", ["EMAIL.PRG", "TLS.OVL", "CACERT.PEM", "ROOTS.DAT"], rel)
    s = Session(os.path.join(a.work, "email-" + tag), machine, a.display)
    shots = {}
    try:
        for f in ("EMAIL.PRG", "TLS.OVL", "CACERT.PEM", "ROOTS.DAT"):
            s.r.put(os.path.join(d, f), "EMAIL")
        s.r.write("EMAIL\\email.inf".replace("\\", os.sep),
                  "[options]\ntz=180\nstartoffline=1\nsignature=Dana\\nFalcon owner\n[account]\nname=Dana\nfullname=Dana Falcon\n"
                  "email=dana@test.local\nin=imap\nhost=127.0.0.1\nport=10143\nuser=dana\npass=secret\n"
                  "smtphost=127.0.0.1\nsmtpport=10025\n")
        s.r.start("C:\\EMAIL\\EMAIL.PRG", ready="")
        time.sleep(8)
        s.r.still(most=20)
        img = s.shot("start")
        chei = 16 if img.size[1] >= 400 else 8
        print("     EMail %s: %s" % (open(os.path.join(d, "EMAIL.VERSION")).read().strip(), s.r.describe()), flush=True)
        s.r.key("ctrl+comma")
        time.sleep(3)
        s.r.still(most=20)
        img = s.shot("settings")
        box = find_box(img, 57 * 8, 17 * chei)
        if not check(box is not None, "%s EMail: Settings found on the screen" % tag):
            return shots, None
        names = ["mail", "writing", "signature", "look", "contacts", "connection"]
        for k, n in enumerate(names):
            if k:
                s.r.click(*tab_xy(box, k, chei))
                time.sleep(1.5)
            shots["email-%d-%s" % (k + 1, n)] = s.shot("email-%d-%s" % (k + 1, n))
        px_, py_ = box[0] + (15 + 26) * 8 + 20, box[1] + (9 * chei + 2 + 5 * (chei * 3 // 8)) + chei // 2
        s.r.click(px_, py_)
        time.sleep(2.5)
        shots["email-6-connection-popup"] = s.shot("email-6-popup")
        s.r.key("Escape")
        return shots, box
    finally:
        s.r.stop()


def ehv_real(a, machine, out, rel, tag):
    d = release("EHypView", "v1.39", ["EHYPVIEW.PRG"], rel)
    s = Session(os.path.join(a.work, "ehv-" + tag), machine, a.display)
    shots = {}
    try:
        s.r.put(os.path.join(d, "EHYPVIEW.PRG"), "EHYPVIEW")
        s.r.start("C:\\EHYPVIEW\\EHYPVIEW.PRG", ready="")
        time.sleep(8)
        s.r.still(most=20)
        img = s.shot("start")
        chei = 16 if img.size[1] >= 400 else 8
        print("     EHypView %s: %s" % (open(os.path.join(d, "EHYPVIEW.VERSION")).read().strip(), s.r.describe()), flush=True)
        s.r.key("ctrl+comma")
        time.sleep(3)
        s.r.still(most=20)
        img = s.shot("settings")
        box = find_box(img, 57 * 8, 18 * chei)
        if not check(box is not None, "%s EHypView: Settings found on the screen" % tag):
            return shots, None
        shots["ehypview-1-general"] = img
        px_, py_ = box[0] + (15 + 2) * 8 + 40, box[1] + 5 * chei + chei // 2 + chei // 2
        s.r.click(px_, py_)
        time.sleep(2.5)
        shots["ehypview-1-general-popup"] = s.shot("ehv-1-popup")
        s.r.key("Escape")
        time.sleep(1.5)
        s.r.click(*tab_xy(box, 1, chei))
        time.sleep(1.5)
        shots["ehypview-2-library"] = s.shot("ehv-2-library")
        s.r.click(box[0] + (15 + 9) * 8 + 40, box[1] + 4 * chei + chei // 2)
        time.sleep(2.5)
        shots["ehypview-2-library-popup"] = s.shot("ehv-2-popup")
        s.r.key("Escape")
        return shots, box
    finally:
        s.r.stop()


def xaaes(a):
    """SETDEMO.PRG in a window under FreeMiNT and XaAES (../xaaes): the dialog opened, a page chosen, the window moved
    by its title bar (what it covered and uncovers is drawn again), a pop-up, Undo and Esc closing it; screenshots to SHOTS"""
    xa = os.path.join(HERE, "..", "xaaes", "run.py")
    tos = a.tos or machines.emutos()
    out = os.path.abspath(a.shots)
    os.makedirs(out, exist_ok=True)

    def go(work, steps):
        r = subprocess.run([sys.executable, xa, work, tos, PRG, "--display", a.display, "--steps", steps],
                           capture_output=True, text=True)
        return r.returncode, r.stdout + r.stderr

    w1 = os.path.join(a.work, "xaaes1")
    rc, txt = go(w1, "until SETDEMO ready 300; key 1; until SETDEMO box email 90; wait 3; shot; key 0x61; "
                     "until SETDEMO got email 60; key q; until SETDEMO done 60")
    log = open(os.path.join(w1, "hatari.log"), errors="replace").read()
    m = re.search(r"SETDEMO box email (\d+) (\d+) (\d+) (\d+)", log)
    if not check(rc == 0 and m is not None, "xaaes: SETDEMO.PRG started, the dialog opened in a window and closed by Undo (rc %d)" % rc):
        print(txt[-1500:])
        return
    wx, wy, ww, wh = [int(v) for v in m.groups()]
    print("     xaaes: the dialog's work area at %d,%d, %dx%d" % (wx, wy, ww, wh), flush=True)
    ch = 16
    # in the window the band is left out and the rest is a row higher
    tab5 = (wx + 40, wy + (2 + 2 * 5) * ch + 5 * (ch // 2) + ch // 2 - ch)
    dx, dy = 70, 50
    title = (wx + ww // 2, wy - 10)
    pop = (wx + (15 + 26) * 8 + 20 + dx, wy + (9 * ch + 2 + 5 * (ch * 3 // 8)) + ch // 2 - ch + dy)
    w2 = os.path.join(a.work, "xaaes2")
    steps = ("until SETDEMO ready 300; key 1; until SETDEMO box email 90; wait 3; shot; "
             "click %d %d; wait 3; shot; drag %d %d %d %d; wait 4; shot; click %d %d; wait 3; shot; key 0x01; wait 2; shot; "
             "key 0x61; until SETDEMO got email 60; key 2; until SETDEMO box ehypview 90; wait 3; shot; key 0x01; "
             "until SETDEMO got ehypview 60; wait 2; shot; key q; until SETDEMO done 60"
             % (tab5[0], tab5[1], title[0], title[1], title[0] + dx, title[1] + dy, pop[0], pop[1]))
    rc, txt = go(w2, steps)
    log = open(os.path.join(w2, "hatari.log"), errors="replace").read()
    check(rc == 0, "xaaes: every step done (rc %d)" % rc)
    check("SETDEMO got email 0" in log, "xaaes: Undo closed EMail's dialog as Cancel (the window closed)")
    check("SETDEMO got ehypview 0" in log, "xaaes: Esc closed EHypView's dialog as Cancel")
    boxes = re.findall(r"SETDEMO box (\w+) (\d+) (\d+)", log)
    shots = sorted(os.listdir(os.path.join(w2, "shots")))
    for i, f in enumerate(shots):
        shutil.copy(os.path.join(w2, "shots", f), os.path.join(out, "xaaes-%02d.png" % (i + 1)))
    print("     xaaes: %d screenshots in %s: open, Connection page, window moved, pop-up, pop-up closed, EHypView opened, closed"
          % (len(shots), out), flush=True)
    # the radio buttons and the keyboard's reach in a window (the ring is drawn through dlg_more over the AES's own redraws):
    # Tab to the group, Right Right (DSP), Tab to the tick, Space, Tab Tab Tab Tab to the list, Down (page Plain), Up, Return
    w3 = os.path.join(a.work, "xaaes3")
    steps = ("until SETDEMO ready 300; key 3; until SETDEMO box radios 90; wait 3; shot; key 0x0f; wait 2; shot; "
             "key 0x4d; wait 2; key 0x4d; wait 2; shot; key 0x0f; wait 2; key 0x39; wait 2; shot; "
             "key 0x0f; wait 2; key 0x0f; wait 2; key 0x0f; wait 2; key 0x0f; wait 2; key 0x50; wait 3; shot; key 0x48; wait 3; shot; "
             "key 0x1c; until SETDEMO radios out 60; key q; until SETDEMO done 60")
    rc, txt = go(w3, steps)
    log = open(os.path.join(w3, "hatari.log"), errors="replace").read()
    check(rc == 0, "xaaes: the keys' steps done (rc %d)" % rc)
    check("SETDEMO radios out 2 nodsp 1 rate 1 log 0 tick2 0 acts 0" in log and "SETDEMO radios" in log,
          "xaaes: in a window Tab, Space, Right, Right and Return did what they do on TOS (DSP chosen, 'no DSP' ticked, OK)")
    shots = sorted(os.listdir(os.path.join(w3, "shots")))
    for i, f in enumerate(shots):
        shutil.copy(os.path.join(w3, "shots", f), os.path.join(out, "xaaes-keys-%02d.png" % (i + 1)))
    print("     xaaes: %d screenshots of the keys in %s" % (len(shots), out), flush=True)
    # the Font dialog in a window: the window's own title replaces the black band (D22), Cancel left of OK (C1), both 10 wide
    w5 = os.path.join(a.work, "xaaes5")
    rc, txt = go(w5, "until SETDEMO ready 300; key 4; wait 6; shot; key 0x61; until SETDEMO got font 60; key q; until SETDEMO done 60")
    log = open(os.path.join(w5, "hatari.log"), errors="replace").read()
    check(rc == 0 and "SETDEMO got font 0" in log, "xaaes: the Font dialog opened in a window and closed by Undo (rc %d)" % rc)
    for i, f in enumerate(sorted(os.listdir(os.path.join(w5, "shots")))):
        shutil.copy(os.path.join(w5, "shots", f), os.path.join(out, "xaaes-font-%02d.png" % (i + 1)))
    # MD17: a menu title chosen while a dialog window is open (the bar is the demo's, built where the AES has windows): dlg_evnt
    # drops the choice and sets the title normal; before, it was kept and run when the dialog closed ("SETDEMO menu T I")
    w4 = os.path.join(a.work, "xaaes4")
    steps = ("until SETDEMO ready 300; wait 2; key 1; until SETDEMO box email 90; wait 3; move 88 8; wait 2; shot; "
             "drag 88 8 92 30; wait 3; shot; key 0x61; until SETDEMO got email 60; wait 3; shot; key q; until SETDEMO done 60")
    rc, txt = go(w4, steps)
    log = open(os.path.join(w4, "hatari.log"), errors="replace").read()
    check(rc == 0 and "SETDEMO got email 0" in log, "xaaes: the dialog window open, a menu item chosen, the dialog closed by Undo (rc %d)" % rc)
    check("SETDEMO menu" not in log, "xaaes: the menu choice made while the dialog window was open was dropped, not run when it closed (MD17)")
    shots = sorted(os.listdir(os.path.join(w4, "shots")))
    for i, f in enumerate(shots):
        shutil.copy(os.path.join(w4, "shots", f), os.path.join(out, "xaaes-menu-%02d.png" % (i + 1)))
    if len(shots) >= 2:
        # the title shown normal again: the bar's pixels as before the menu was opened (the shot before the dropdown is the second)
        a1 = Image.open(os.path.join(w4, "shots", shots[1])).convert("RGB").crop((0, 0, 200, 16))
        a2 = Image.open(os.path.join(w4, "shots", shots[2])).convert("RGB").crop((0, 0, 200, 16))
        check(ImageChops.difference(a1, a2).getbbox() is None, "xaaes: the menu title is shown normal again with the dialog still open (menu_tnormal)")


def keys_run(a, machine):
    """the radio buttons and the keyboard's reach (D24, D25) in SETDEMO's third dialog: Tab through the controls, Space,
    the arrows, the list's Up and Down, Shift+Tab, Return; a screenshot after each step (SHOTS/keys-*.png); the AESGAP of
    every key against the click rule; and a control's ring gone when the focus has moved on is the pixels of the dialog
    as it opened: the region of the control it was round is compared with the first screenshot, pixel for pixel"""
    tag = machine.replace(":", "_")
    out = os.path.abspath(a.shots)
    os.makedirs(out, exist_ok=True)
    s = Session(os.path.join(a.work, "keys-" + tag), machine, a.display)
    try:
        s.r.put(PRG, "")
        s.r.start("C:\\SETDEMO.PRG", ready="SETDEMO ready")
        m = re.search(r"SETDEMO screen\s+(\d+) (\d+) (\d+) (\d+)", s.r.console())
        chei = int(m.group(4)) if m else 16
        pos = len(s.r.console())
        s.r.act("Radios Settings opened (key 3)", speed.DIALOG, lambda: s.r.key("3"))
        s.at("SETDEMO box radios", pos)
        m = re.search(r"SETDEMO box radios (\d+) (\d+) (\d+) (\d+)", s.r.console()[pos:])
        if not check(m is not None, "%s keys: the dialog opened" % tag):
            return
        bx, by, bw, bh = [int(v) for v in m.groups()]
        gap = chei * 3 // 8
        row = lambda n: by + (4 + n) * chei + 2 + n * gap          # set_y(s, n)
        shots = []

        def step(name, key, budget=speed.KEY):
            s.r.act(name, budget, lambda: s.r.key(key))
            time.sleep(0.8)
            img = s.shot("keys-%02d" % (len(shots) + 1))
            img.save(os.path.join(out, "keys-%s-%02d.png" % (tag, len(shots) + 1)))
            shots.append(img)
            return img

        first = s.shot("keys-00")
        first.save(os.path.join(out, "keys-%s-00.png" % tag))

        def box(img, x0, y0, x1, y1):
            return img.crop((x0, y0, x1, y1)).convert("RGB")

        def same_as_opened(img, x0, y0, x1, y1, what):
            d = ImageChops.difference(box(img, x0, y0, x1, y1), box(first, x0, y0, x1, y1)).getbbox()
            check(d is None, "%s keys: %s" % (tag, what))

        sx = bx + 15 * 8
        radio_y, pop_y, btn_y = row(2), row(5), row(7)
        # the dialog opens with the cursor in the first field; Tab goes to the group of radio buttons: a ring round Auto
        img = step("Tab: to the radio group", "Tab")
        d = ImageChops.difference(box(img, sx - 3, radio_y - 3, sx + 85, radio_y + chei + 3),
                                  box(first, sx - 3, radio_y - 3, sx + 85, radio_y + chei + 3)).getbbox()
        check(d is not None, "%s keys: Tab: a ring appeared round the chosen radio button" % tag)
        step("Right: FDI", "Right")
        step("Right: DSP", "Right")
        step("Tab: to the 'no DSP' tick", "Tab")
        img = step("Space: the tick on, DSP greyed", "space")
        step("Tab: to the 'Write a log' tick", "Tab")
        step("Space: the log on", "space")
        step("Tab: to the pop-up button", "Tab")
        pos2 = len(s.r.console())
        s.r.act("Space: the pop-up opened", speed.DIALOG, lambda: s.r.key("space"))
        time.sleep(1.5)
        img = s.shot("keys-pop")
        img.save(os.path.join(out, "keys-%s-pop.png" % tag))
        s.r.key("Escape")
        time.sleep(1.5)
        img = step("Tab: to the Languages button", "Tab")
        same_as_opened(img, sx + 14 * 8 - 3, pop_y - 3, sx + 28 * 8 + 3, pop_y + chei + 3,
                       "the ring left the pop-up button clean: its pixels are the opened dialog's")
        img = step("Tab: to the page list", "Tab")
        same_as_opened(img, sx - 3, btn_y - 3, sx + 14 * 8 + 3, btn_y + chei + 3,
                       "the ring left the Languages button clean")
        step("Down: page Plain", "Down", speed.KEY)
        step("Up: page Output", "Up", speed.KEY)
        img = step("Shift+Tab: back to the button", "shift+Tab")
        same_as_opened(img, bx + 2, by + 2 * chei - 3, bx + 13 * 8 + 8, by + 6 * chei,
                       "the ring left the list clean (its line too): the names are the opened dialog's")
        pos3 = len(s.r.console())
        s.r.key("Return")
        i = s.at("SETDEMO radios out", pos3)
        got = s.r.console()[pos3:].split("SETDEMO radios", 1)[-1].split("\n", 1)[0] if i > 0 else ""
        check(i > 0 and " out 2 nodsp 1 rate 1 log 1 " in got and "acts 0" in got,
              "%s keys: Return is OK; Right, Right chose DSP, Space ticked 'no DSP' and the log (%s)" % (tag, got.strip()[:70]))
        time.sleep(1)
        s.r.key("q")
        s.at("SETDEMO done", pos3, 20)
        s.r.report()
    finally:
        s.r.stop()


def font_run(a, machine):
    """the shared Font dialog (ui/fontsel.c, SETDEMO's key 4): its black title band, Cancel left of OK, both 10 wide; a click on
    the left button is Cancel (nothing applied), on the right one OK (applied); a screenshot to SHOTS (font-MACHINE-open.png)"""
    tag = machine.replace(":", "_")
    out = os.path.abspath(a.shots)
    os.makedirs(out, exist_ok=True)
    s = Session(os.path.join(a.work, "font-" + tag), machine, a.display)
    try:
        s.r.put(PRG, "")
        s.r.start("C:\\SETDEMO.PRG", ready="SETDEMO ready")
        m = re.search(r"SETDEMO screen\s+(\d+) (\d+) (\d+) (\d+)", s.r.console())
        chei = int(m.group(4)) if m else 16
        w_px, h_px = 48 * 8, 17 * chei + 14
        pos = len(s.r.console())
        s.r.act("Font dialog opened (key 4)", speed.DIALOG, lambda: s.r.key("4"))
        time.sleep(4)
        img = s.shot("font-open")
        img.save(os.path.join(out, "font-%s-open.png" % tag))
        box = find_box(img, w_px, h_px)
        if not check(box is not None, "%s font: the dialog (48 x 17 cells) found on the screen" % tag):
            return
        bx, by = box
        px = img.load()
        # the title band: row 0 black from the left edge to the right (inside the 2-pixel frame), its text white
        band_row = by + chei // 2
        dark = sum(1 for x in range(bx + 4, bx + w_px - 4) if sum(px[x, band_row][:3]) < 150)
        check(dark > (w_px - 8) * 0.7, "%s font: a black title band across the first row (%d of %d pixels dark)" % (tag, dark, w_px - 8))
        by_ = by + 15 * chei + 12 + chei // 2
        left = (bx + (24 + 5) * 8, by_)
        right = (bx + (36 + 5) * 8, by_)
        # a 10-wide button is 80 pixels: its frame at the two ends, none in the gap between them
        row = [px[x, by_] for x in range(bx + 24 * 8 - 3, bx + 46 * 8 + 3)]
        s.r.click(*left)
        i = s.at("SETDEMO got font", pos)
        got = s.r.console()[pos:].split("SETDEMO got font", 1)[-1].split("\n", 1)[0] if i > 0 else ""
        check(i > 0 and got.strip().startswith("0") and "applied" not in s.r.console()[pos:], "%s font: the left button is Cancel: result 0, nothing applied (%s)" % (tag, got.strip()[:12]))
        pos = len(s.r.console())
        s.r.key("4")
        time.sleep(4)
        s.r.click(*right)
        i = s.at("SETDEMO got font", pos)
        got = s.r.console()[pos:].split("SETDEMO got font", 1)[-1].split("\n", 1)[0] if i > 0 else ""
        check(i > 0 and got.strip().startswith("1") and "SETDEMO font applied" in s.r.console()[pos:],
              "%s font: the right button is OK: result 1, applied (%s)" % (tag, got.strip()[:12]))
        s.r.key("q")
        s.at("SETDEMO done", pos, 20)
    finally:
        s.r.stop()


def load_shots(a, tag):
    """the screenshots an earlier run left in WORK (--again): to compare them again"""
    def img(*p):
        return Image.open(os.path.join(a.work, *p)).convert("RGB")
    names = ["mail", "writing", "signature", "look", "contacts", "connection"]
    shots, es, hs, boxes = {}, {}, {}, {}
    log = open(os.path.join(a.work, "demo-" + tag, "hatari.log"), errors="replace").read()
    for key in ("email", "ehypview"):
        m = re.search(r"SETDEMO box %s (\d+) (\d+) (\d+) (\d+)" % key, log)
        x, y, w, h = [int(v) for v in m.groups()]
        boxes[key] = ((x, y), (w, h))
    for k, n in enumerate(names):
        shots["email-%d-%s" % (k + 1, n)] = img("demo-" + tag, "shots", "demo-email-%d-%s.png" % (k + 1, n))
        es["email-%d-%s" % (k + 1, n)] = img("email-" + tag, "shots", "email-%d-%s.png" % (k + 1, n))
    shots["email-6-connection-popup"] = img("demo-" + tag, "shots", "demo-email-6-popup.png")
    es["email-6-connection-popup"] = img("email-" + tag, "shots", "email-6-popup.png")
    for name, d, r in (("ehypview-1-general", "demo-ehv-1-general", "settings"), ("ehypview-1-general-popup", "demo-ehv-1-popup", "ehv-1-popup"),
                       ("ehypview-2-library", "demo-ehv-2-library", "ehv-2-library"), ("ehypview-2-library-popup", "demo-ehv-2-popup", "ehv-2-popup")):
        shots[name] = img("demo-" + tag, "shots", d + ".png")
        hs[name] = img("ehv-" + tag, "shots", r + ".png")
    chei = 16 if es["email-1-mail"].size[1] >= 400 else 8
    ebox = find_box(img("email-" + tag, "shots", "settings.png"), 57 * 8, 17 * chei)
    hbox = find_box(img("ehv-" + tag, "shots", "settings.png"), 57 * 8, 18 * chei)
    return shots, boxes, es, ebox, hs, hbox


def run_machine(a, machine):
    tag = machine.replace(":", "_")
    out = os.path.abspath(a.shots)
    os.makedirs(out, exist_ok=True)
    rel = os.path.abspath(a.rel)
    if a.again:
        shots, boxes, es, ebox, hs, hbox = load_shots(a, tag)
        rows = None
    else:
        shots, boxes, rows = demo_run(a, machine, out, rel, tag)
        if a.speed:
            return
        es, ebox = email_real(a, machine, out, rel, tag)
        hs, hbox = ehv_real(a, machine, out, rel, tag)
    for name, dshot in shots.items():
        real, box_r, key = (es, ebox, "email") if name.startswith("email") else (hs, hbox, "ehypview")
        if name not in real or box_r is None:
            check(False, "%s %s: no screenshot of the real program" % (tag, name))
            continue
        (bx, by), (bw, bh) = boxes[key]
        known = ("EMail 2.14 opens its pop-up below the button, the guidelines' 5.7 (and EHypView, and this) over it"
                 if name == "email-6-connection-popup" else
                 "the releases pad their pop-up's words to 4 cells in; popup_m puts them 2 in (D15, 9 Nov 2026)"
                 if name.endswith("-popup") else None)
        compare(name, dshot, real[name], (bx, by), box_r, bw, bh, out, tag, known)
    return rows


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("work")
    ap.add_argument("machine", nargs="?", default="Falcon:16")
    ap.add_argument("--shots")
    ap.add_argument("--rel", default=os.path.join(HERE, "build", "rel"))
    ap.add_argument("--display", default=":99")
    ap.add_argument("--speed", action="store_true")
    ap.add_argument("--prg", metavar="DIR", help="build/DIR's SETDEMO.PRG instead of build/prof's (make cpu: the programs' own flags, -DDLG_CPU -DDLG_CLICK)")
    ap.add_argument("--keys", action="store_true", help="the radio buttons and the keyboard's reach in SETDEMO's third dialog (screenshots keys-*.png)")
    ap.add_argument("--font", action="store_true", help="the Font dialog (SETDEMO's key 4): its band and its Cancel and OK")
    ap.add_argument("--again", action="store_true", help="compare the screenshots an earlier run left in WORK again (no emulator)")
    ap.add_argument("--xaaes", nargs="?", const="", metavar="TOS_IMAGE", help="the demo under FreeMiNT and XaAES (EmuTOS 1.4 where no image is given)")
    a = ap.parse_args()
    global PRG
    if a.prg:
        PRG = os.path.join(HERE, "build", a.prg, "SETDEMO.PRG")
    if not os.path.exists(PRG) and not a.again:
        sys.exit("no %s: make prof" % PRG)
    a.work = os.path.abspath(a.work)
    a.shots = a.shots or os.path.join(a.work, "shots")
    a.tos = a.xaaes or None
    if a.keys:
        keys_run(a, a.machine)
    elif a.font:
        font_run(a, a.machine)
    elif a.xaaes is not None:
        xaaes(a)
    else:
        run_machine(a, a.machine)
    print("%d failed" % failed)
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
