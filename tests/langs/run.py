#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""LANGT.PRG (make prg) on one of the family's machines: the Languages dialog (ui/langs.c) against a
fake translategate (fakegate.py) over the network as an app has it (common's atari/net_tos.c on
FAKESTNG and the serial bridge), driven with keys and clicks:

    cancel   the list shown, a tick, Esc: no POST was made (the fake counts)
    over     German ticked (it fits), then Spanish: refused, with the number that doesn't fit; Esc
    keys     German ticked, Hebrew unticked, Return: exactly one POST, "de"; the job followed to its
             end in the status line (the words and the progress bar), the Gateway then has de
    mouse    a press shows the row chosen and ticks nothing; the release ticks; let go elsewhere
             ticks nothing; the pane bar scrolls; OK by a click: one POST with the right codes
    refused  the Gateway refuses the POST (503): an alert, the dialog stays; the next OK is sent
    stop     the list takes long; Esc stops it: no dialog, no alert, result 0
    down     the Gateway isn't there: an alert, the dialog doesn't open, result -1

    run.py WORK MACHINE [--nvdi] [--window] [--flat]
                                       MACHINE as common/tools/machines.py names them (ST:mono,
                                       ST:med, Falcon:16, Falcon:256 ...); --window forces the dialog
                                       into a window of the AES's (as on MiNT and MagiC), --flat the
                                       flat look on a 3D AES

Checks the console's "LANGT ..." lines and the fake's log, and takes a screenshot at each step
(WORK/shots); the pictures are for looking at. Exits 1 if anything failed.
"""
import os
import re
import shutil
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "speed"))
sys.path.insert(0, HERE)
import speed  # noqa: E402
import fakegate  # noqa: E402

fails = []


def check(ok, what):
    print("%s %s" % ("ok   " if ok else "FAIL ", what), flush=True)
    if not ok:
        fails.append(what)
    return ok


def order():
    """the codes in the order the dialog lists them: English, then by name (case-insensitive, then code)"""
    rows = sorted(fakegate.ROWS, key=lambda r: (r[0] != "en", r[1].lower(), r[0]))
    return [r[0] for r in rows], {r[0]: r[1] for r in rows}


class Run:
    def __init__(self, work, machine, nvdi):
        self.gate = fakegate.Gate(port=0, cap_mb=600, installed=("he",))
        self.gate.start()
        self.r = speed.Rig(work, machine, nvdi=nvdi, display=":%d" % (100 + os.getpid() % 100))     # (one of 100: runs can go side by side)
        self.r.extra = self.r.m.network()
        self.mark = 0
        self.geom = None
        self.codes, self.names = order()
        self.cursor = 0
        # Hatari shows the ST's 200-line screens doubled: the window is twice as high as the screen
        self.yscale = 2 if machine.startswith("ST") and machine.split(":")[-1] in ("med", "low") else 1

    def start(self):
        r = self.r
        r.put(os.path.join(HERE, "build", "atari", "LANGT.PRG"))
        r.write("LANGT.CFG", "127.0.0.1 %d\n" % self.gate.port)
        if "--window" in sys.argv:          # the dialog in a window of the AES's, forced on TOS (dlg_window_on)
            r.write("WINDOW", "")
        if "--flat" in sys.argv:            # the flat look on an AES that has 3D (dlg_3d_on 0)
            r.write("FLAT", "")
        r.start("C:\\LANGT.PRG", ready="LANGT ask cancel", boot=240 if not r.nvdi else 400)

    def console(self):
        return self.r.console()[self.mark:]

    def wait(self, text, secs=60):
        """text appears in the console after the mark; the mark moves past it"""
        end = time.time() + secs
        while time.time() < end:
            c = self.r.console()
            i = c.find(text, self.mark)
            if i >= 0:
                self.mark = i + len(text)
                return True
            time.sleep(0.4)
        return False

    def shot(self, name):
        time.sleep(1.0)
        return self.r.shot(name)

    def row_xy(self, k):
        """screen point in row k of the list shown (0 the first shown)"""
        x, y, w, h = self.geom["rows"]
        return x + w // 2, y + k * self.geom["row"] + self.geom["row"] // 2

    def key(self, *k):
        for one in k:
            self.r.key(one)
            time.sleep(0.9)

    def goto(self, code):
        """the letter keys that take the cursor to language `code` from where it is (the list keeps its own
        cursor: row 0 when the dialog opens); the letter is pressed as often as names before it start so"""
        i = self.codes.index(code)
        letter = self.names[code][0].lower()
        keys, at = [], self.cursor
        while at != i:
            j = next(k for k in range(1, len(self.codes) + 1)
                     if self.names[self.codes[(at + k) % len(self.codes)]][0].lower() == letter)
            at = (at + j) % len(self.codes)
            keys.append(letter)
        self.cursor = i
        return keys

    def geometry(self):
        m = re.findall(r"LANGS geom rows (-?\d+) (-?\d+) (-?\d+) (-?\d+) bar (-?\d+) (-?\d+) (-?\d+) (-?\d+) "
                       r"ok (-?\d+) (-?\d+) (-?\d+) (-?\d+) cancel (-?\d+) (-?\d+) (-?\d+) (-?\d+) row (\d+)",
                       self.r.console())
        if not m:
            return None
        v = [int(x) for x in m[-1]]
        self.geom = {"rows": v[0:4], "bar": v[4:8], "ok": v[8:12], "cancel": v[12:16], "row": v[16]}
        return self.geom

    def at(self, x, y):
        """the pointer at the Atari screen's x, y (the window's place added)"""
        g = dict(l.split("=") for l in self.r.xdo("getwindowgeometry", "--shell", self.r.win()).split())
        self.r.xdo("mousemove", str(int(g["X"]) + x), str(int(g["Y"]) + y * self.yscale))

    def press(self, x, y):
        self.at(x, y)
        time.sleep(0.5)
        self.r.xdo("mousedown", "1")

    def release(self):
        self.r.xdo("mouseup", "1")
        time.sleep(1.2)

    def click(self, x, y):
        self.at(x, y)
        time.sleep(0.5)
        self.r.xdo("click", "1")
        time.sleep(1.2)

    def stop(self):
        self.r.stop()
        try:
            self.gate.stop()
        except Exception:
            pass


def same(a, b):
    p = subprocess.run(["compare", "-metric", "AE", a, b, "null:"], capture_output=True, text=True)
    try:
        return int(float(p.stderr.split()[0])) == 0
    except (ValueError, IndexError):
        return False


def session(work, machine, nvdi):
    t = Run(work, machine, nvdi)
    g, r = t.gate, t.r
    shots = {}
    try:
        t.start()
        check("LANGT net" in r.console(), "the network is up (%s)" % ([l for l in r.console().splitlines() if "LANGT net" in l] or ["?"])[0])

        # ---- cancel ----
        if not check(t.wait("LANGS open", 90), "cancel: the dialog opened"):
            return t, shots
        g1 = t.geometry()
        check(g1 is not None, "the dialog says where its list, bar and buttons are: %s" % g1)
        shots["1-open"] = t.shot("1-open")
        check([m for m, _, _ in g.log] == ["GET"] and g.log[0][1] == "/languages?all=1", "cancel: one GET /languages?all=1 so far")
        t.key("Down", "space")
        shots["2-ticked"] = t.shot("2-ticked")
        check("LANGS tick %s 1" % t.codes[1] in t.console(), "cancel: the second row (%s) ticked" % t.names[t.codes[1]])
        check(not same(shots["1-open"], shots["2-ticked"]), "cancel: the screen changed with the tick")
        check(len(g.posts()) == 0, "cancel: nothing sent while ticking")
        t.key("Escape")
        check(t.wait("LANGT got cancel 0 job 0", 20), "cancel: Esc: result 0, no job")
        check(len(g.posts()) == 0 and all(m == "GET" and p == "/languages?all=1" for m, p, _ in g.log),
              "cancel: zero POSTs, nothing asked but the list")

        # ---- over ----
        t.cursor = 0
        check(t.wait("LANGT ask over", 20) and t.wait("LANGS open", 60), "over: the dialog opened again")
        t.key(*t.goto("de"))
        t.key("space")
        check("LANGS tick de 1" in t.console(), "over: German ticked (230 + 287 of 600 fits)")
        shots["3-german"] = t.shot("3-german")
        t.key(*t.goto("es"))
        shots["4-spanish-cursor"] = t.shot("4-spanish-cursor")
        t.key("space")
        check("LANGS tick es 3" in t.console(), "over: Spanish refused (LT_REFUSED)")
        check(t.gate.cap_mb == 600, "over: (the Gateway's room is 600 MB: 230 + 287 + 355 doesn't fit)")
        shots["5-refused"] = t.shot("5-refused")
        check(not same(shots["4-spanish-cursor"], shots["5-refused"]), "over: the message shows")
        t.key("space")
        t.key("Down")
        shots["6-after"] = t.shot("6-after")
        check(len(g.posts()) == 0, "over: nothing sent")
        t.key("Escape")
        check(t.wait("LANGT got over 0 job 0", 20), "over: Esc: result 0")
        check(len(g.posts()) == 0, "over: zero POSTs")

        # ---- keys ----
        t.cursor = 0
        check(t.wait("LANGT ask keys", 20) and t.wait("LANGS open", 60), "keys: the dialog opened")
        t.key(*t.goto("de"))
        t.key("space")
        t.key(*t.goto("he"))
        t.key("space")
        shots["7-keys"] = t.shot("7-keys")
        check("LANGS tick de 1" in t.console() and "LANGS tick he 2" in t.console(), "keys: German ticked, Hebrew unticked")
        t.key("Return")
        check(t.wait("LANGT got keys 1 job 1", 40), "keys: OK: result 1, job 1")
        check(g.posts() == [b"de"], "keys: exactly one POST, 'de' (%r)" % g.posts())
        shots["8-job-start"] = t.shot("8-job-start")
        m0 = t.mark
        check(t.wait("LANGT job ended", 100), "keys: the job followed to its end")
        c = r.console()[m0:t.mark]
        runs = re.findall(r"LANGT job (Installing languages: \d+ of 287 MB) / (\d+)", c)
        check(len(runs) >= 2, "keys: the status line went through 'Installing languages: N of 287 MB' (%d steps)" % len(runs))
        check("LANGT job Languages installed / 1024" in c, "keys: ... and 'Languages installed' with the bar full")
        check(g.installed == {"en", "de"}, "keys: the Gateway has en and de now")
        shots["9-job-done"] = t.shot("9-job-done")

        # ---- mouse ----
        check(t.wait("LANGT ask mouse", 30) and t.wait("LANGS open", 60), "mouse: the dialog opened")
        t.geometry()
        n0 = len(g.posts())
        x, y = t.row_xy(2)                        # the third row (Arabic)
        t.press(x, y)
        shots["10-pressed"] = t.shot("10-pressed")
        check("LANGS tick" not in t.console().split("LANGT ask mouse")[-1], "mouse: a press ticks nothing")
        t.release()
        shots["11-released"] = t.shot("11-released")
        check("LANGS tick %s 1" % t.codes[2] in t.console(), "mouse: the release ticks (%s)" % t.names[t.codes[2]])
        check(not same(shots["10-pressed"], shots["11-released"]), "mouse: the tick shows on the release")
        # pressed on one row, let go on another: nothing
        ticks = t.console().count("LANGS tick")
        x, y = t.row_xy(4)
        t.press(x, y)
        t.at(x, y + 6 * t.geom["row"])
        time.sleep(0.5)
        t.release()
        check(t.console().count("LANGS tick") == ticks, "mouse: let go on another row ticks nothing")
        # the pane bar: a click below the slider scrolls a page
        bx, by, bw, bh = t.geom["bar"]
        before = t.shot("12-before-scroll")
        t.click(bx + bw // 2, by + bh - bw - 6)
        shots["13-scrolled"] = t.shot("13-scrolled")
        check(not same(before, shots["13-scrolled"]), "mouse: a click in the bar's track scrolled the list")
        # a row near the top of the scrolled list: ticked
        ticks = t.console().count("LANGS tick")
        t.click(*t.row_xy(1))
        check(t.console().count("LANGS tick") == ticks + 1, "mouse: a click on a row in the scrolled list ticks it")
        shots["14-two-ticks"] = t.shot("14-two-ticks")
        # OK by a click
        ox, oy, ow, oh = t.geom["ok"]
        t.click(ox + ow // 2, oy + oh // 2)
        check(t.wait("LANGT got mouse 1 job", 40), "mouse: the OK button: result 1 with a job")
        posts = g.posts()
        check(len(posts) == n0 + 1, "mouse: exactly one POST more (%r)" % posts[n0:])
        got = set(posts[-1].decode().split(","))
        check(t.codes[2] in got and "de" in got and len(got) == 3, "mouse: the POST holds de and the two ticked rows (%s)" % posts[-1].decode())
        check(t.wait("LANGT job ended", 100), "mouse: the job followed to its end")
        check(g.installed == {"en"} | got, "mouse: the Gateway has what was ticked")

        # ---- refused ----
        check(t.wait("LANGT ask refused", 30) and t.wait("LANGS open", 60), "refused: the dialog opened")
        n0 = len(g.posts())
        g.post_status = 503
        t.key("Down", "Down", "space")            # Arabic, which the Gateway has now: unticked
        shots["14b-refused-ticks"] = t.shot("14b-refused-ticks")
        check("LANGS tick ar 2" in t.console(), "refused: Arabic unticked")
        t.key("Return")
        time.sleep(4)
        shots["15-refused-alert"] = t.shot("15-refused-alert")
        check(len(g.posts()) == n0 + 1, "refused: the POST was made once")
        check("LANGT got refused" not in t.console(), "refused: the dialog is still open behind the alert")
        t.key("Return")                           # the alert's OK
        time.sleep(1)
        shots["16-refused-after"] = t.shot("16-refused-after")
        check("LANGT got refused" not in t.console(), "refused: ... and after the alert")
        g.post_status = 0
        t.key("Return")
        m0 = t.mark
        check(t.wait("LANGT got refused 1 job", 40), "refused: tried again: accepted, result 1")
        check(len(g.posts()) == n0 + 2 and g.posts()[-1] == b"de,zt", "refused: two POSTs in all (one refused, one sent: de,zt) %r" % g.posts()[n0:])
        check(t.wait("LANGT job ended", 40), "refused: the removal followed to its end")
        c = r.console()[m0:t.mark]
        check("LANGT job The Gateway is removing languages... / 0" in c or "LANGT job Languages removed / 1024" in c,
              "refused: a removal says 'removing' / 'Languages removed', not 'installing'")
        check("Installing" not in c, "refused: ... and nothing about installing")
        g.delay_list = 40                         # the next list takes long: a stop is tried against it

        # ---- stop ----
        check(t.wait("LANGT ask stop", 30), "stop: asked")
        time.sleep(6)
        shots["17-stop-waiting"] = t.shot("17-stop-waiting")
        nopen = r.console().count("LANGS open")
        t.key("Escape")
        check(t.wait("LANGT got stop 0 job 0", 20), "stop: Esc: result 0")
        g.delay_list = 0
        g.stop()                                  # nobody listens from now on: the next dialog can't get its list
        check(r.console().count("LANGS open") == nopen, "stop: no dialog opened")
        shots["18-stopped"] = t.shot("18-stopped")

        # ---- down ----
        check(t.wait("LANGT ask down", 30), "down: asked")
        time.sleep(8)
        shots["19-down-alert"] = t.shot("19-down-alert")
        nopen = r.console().count("LANGS open")
        t.key("Return")
        check(t.wait("LANGT got down -1", 30), "down: the alert, then result -1")
        check(r.console().count("LANGS open") == nopen, "down: the dialog did not open")
        check(t.wait("LANGT done", 20), "LANGT finished")

        for d, _, fs in os.walk(r.hd):
            if any(f.upper() == "CRASH.LOG" for f in fs):
                check(False, "CRASH.LOG in %s" % d)
        print("  %s on %s; shots in %s" % ("LANGT.PRG", r.describe(), r.shots))
    finally:
        t.stop()
    return t, shots


def main():
    a = [x for x in sys.argv[1:] if not x.startswith("--")]
    if len(a) < 2:
        sys.exit(__doc__)
    work, machine, nvdi = os.path.abspath(a[0]), a[1], "--nvdi" in sys.argv
    print("the Languages dialog on %s%s" % (machine, " with NVDI" if nvdi else ""), flush=True)
    session(work, machine, nvdi)
    print("all passed" if not fails else "%d failed" % len(fails))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
