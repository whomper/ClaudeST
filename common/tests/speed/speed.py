#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""The family's speed rig (the guidelines' Speed: the stock machines set it;
Erez, 26 Oct 2026). An app's own speed rig (tests/hatari/speed.py) imports
this, starts its program (built with -DAES_PROFILE=20) on one of the
family's machines and does the user's actions one at a time. For each
action it reads the longest stretch between event calls, from gem.c's
AESGAP lines, and checks it against the guidelines' time:

    sys.path.insert(0, "common/tests/speed"); import speed
    r = speed.Rig(work, "Falcon-4:16", nvdi=False, display=":91")
    r.put("binary/EMAIL.PRG", "EMAIL")          # files into C:\\EMAIL
    r.start("C:\\\\EMAIL\\\\EMAIL.PRG")           # AESSTART: the 2 s start
    r.act("a line scrolled", speed.LINE, lambda: r.key("Down"))
    r.act("Settings", speed.DIALOG, lambda: r.key("ctrl+comma"))
    r.act("Settings closed", speed.WINDOW, lambda: r.key("Escape"))
    sys.exit(r.report())

r.click(x, y) clicks in the Atari's screen; r.menu(title_x, row_y) picks
a menu entry. r.report() prints a table (action, longest stretch, time,
over or not) and returns 1 if any is over. Stretches under 20 ms aren't
reported by the build: they show as "<20".

Run as a script for its own check, with SPT.PRG (make prg):

    speed.py --selftest WORK [MACHINE]

Hatari's CPU is cycle-exact (its default); the Falcon's sharing of ST RAM
with the screen is approximated, so Erez's own Falcon with the DFB1 off
is the referee.

Where a stretch's time goes (EFTP's recipe): stop Hatari's profiler at the
stretch's end with a breakpoint on nf_debug that runs a file holding
"profile save" (b pc = nf_debug :once :file x.dbg), then hatari_profile -p
with the ELF's symbols (nm, made relative to TEXT).
"""
import os
import re
import shutil
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
TOOLS = os.path.join(HERE, "..", "..", "tools")
sys.path.insert(0, TOOLS)
import machines  # noqa: E402

# the guidelines' times, in ms
KEY = 50        # a key, click, tick or row
LINE = 50       # a line scrolled
PAGE = 150      # a page scrolled
DIALOG = 150    # a dialog, pop-up or alert opened
WINDOW = 300    # the main window drawn whole; what a move or resize uncovers
ACC = 500       # the accessory opened
START = 2000    # the program started


class Rig:
    def __init__(self, work, machine, nvdi=False, display=":91", extra=()):
        self.work = os.path.abspath(work)
        shutil.rmtree(self.work, ignore_errors=True)
        self.hd = os.path.join(self.work, "hd")
        self.shots = os.path.join(self.work, "shots")
        os.makedirs(self.hd)
        os.makedirs(self.shots)
        st = machine.startswith("ST")
        self.m = machines.Machine(machine + ("" if ":" in machine or not st else ":mono"), self.hd)
        self.nvdi = nvdi
        if nvdi and not self.m.nvdi():
            sys.exit("speed: no NVDI here (machines.py, AERA_NVDI)")
        self.st = st
        self.display = display
        self.results = []
        self.hatari = self.x = None
        self.log = os.path.join(self.work, "hatari.log")
        self.n = 0
        self.extra = list(extra)        # more of Hatari's arguments (ClaudeST's --rs232-in/out)
        self.held = []                  # modifiers held down until an action settles
        self.in_act = False
        self.start_end = 0

    def describe(self):
        return self.m.describe() + (" with NVDI" if self.nvdi else "")

    def put(self, src, folder=""):
        """a file into C:\\FOLDER"""
        d = os.path.join(self.hd, folder.upper())
        os.makedirs(d, exist_ok=True)
        shutil.copy(src, os.path.join(d, os.path.basename(src).upper()))

    def write(self, name, text):
        """a text file on C: (an INF, say), with CR/LF"""
        p = os.path.join(self.hd, name)
        os.makedirs(os.path.dirname(p), exist_ok=True)
        with open(p, "w", newline="") as f:
            f.write(text.replace("\n", "\r\n"))

    # the emulator
    def start(self, auto=None, ready=None, boot=None):
        """boots the machine, with AUTO starting the program; waits until the
        screen is still (and READY is in the console, if given). The start's
        stretch (AESSTART) is the first result, when the program has one"""
        self.x = machines.xvfb(self.display)
        self.env = self.m.env(dict(os.environ, DISPLAY=self.display, SDL_VIDEODRIVER="x11"))
        # line-buffered: Hatari's console is a file here, and a line held
        # in its buffer would count for the next action
        self.hatari = subprocess.Popen(
            ["stdbuf", "-oL", "-eL", "hatari", "--statusbar", "no", "--borders", "no", "--zoom", "1", "--natfeats", "yes",
             "--fast-boot", "yes", "--log-level", "warn"] + self.m.args() + self.extra +
            (["--auto", auto] if auto else []),
            env=self.env, stdout=open(self.log, "w"), stderr=subprocess.STDOUT)
        # a program's own start is its first event call (AESSTART, from a
        # -DAES_PROFILE build): a still screen alone isn't, NVDI's outline
        # fonts load for a minute behind one
        if auto and ready is None:
            ready = "AESSTART"
        t_end = time.time() + (boot or (120 if self.nvdi else 45) * (2 if self.st else 1))
        while time.time() < t_end:
            time.sleep(2)
            if ready is None or ready in self.console():
                if self.still(most=10):
                    break
        if auto:
            # the start is AESSTART and every stretch until the screen came to
            # rest: a program that opens its window after its first event
            # call draws it in the next one (ClaudeST: 280 + 1515 ms)
            self.start_end = os.path.getsize(self.log)
            self.results.append(("the program started, window drawn", "AESSTART", START))

    def console(self):
        with open(self.log, errors="replace") as f:
            return f.read()

    def xdo(self, *a):
        return subprocess.run(["xdotool", *a], env=self.env, capture_output=True, text=True).stdout

    def win(self):
        for _ in range(100):
            w = (self.xdo("search", "--name", "Hatari") or self.xdo("search", "--class", "hatari")).split()
            if w:
                return w[0]
            time.sleep(0.2)
        sys.exit("speed: no Hatari window")

    def shot(self, name):
        path = os.path.join(self.shots, name + ".png")
        subprocess.run(["import", "-display", self.display, "-window", self.win(), path], env=self.env)
        return path

    def still(self, most=30):
        """waits until neither the screen nor the console changes for a
        second (most seconds at most); True if it came to rest"""
        prev, size = self.shot("still"), os.path.getsize(self.log)
        for _ in range(most):
            time.sleep(1)
            cur, now = self.shot("still-next"), os.path.getsize(self.log)
            r = subprocess.run(["compare", "-metric", "AE", prev, cur, "null:"], capture_output=True, text=True)
            try:
                same = int(float(r.stderr.split()[0])) == 0
            except (ValueError, IndexError):
                same = False
            if same and now == size:
                return True
            prev, size = shutil.copy(cur, prev), now
        return False

    # the user
    def key(self, *keys):
        """keys as xdotool names them (Down, shift+Down, ctrl+comma, Escape).
        A modifier is held down until the action settles: the AES gives the
        modifiers as they are when the program reads the key, after a long
        draw on an ST, so a Shift let go at once made Shift+Down a Down
        (ClaudeST, 26 Oct 2026)"""
        for k in keys:
            parts = k.split("+")
            for m in parts[:-1]:
                self.xdo("keydown", m)
                self.held.append(m)
            self.xdo("key", "--delay", "120", parts[-1])
            if not self.in_act:
                self.release()

    def release(self):
        while self.held:
            self.xdo("keyup", self.held.pop())

    def type(self, text):
        self.xdo("type", "--delay", "150", text)

    def click(self, x, y, button="1"):
        g = dict(l.split("=") for l in self.xdo("getwindowgeometry", "--shell", self.win()).split())
        self.xdo("mousemove", str(int(g["X"]) + x), str(int(g["Y"]) + y))
        time.sleep(0.4)
        self.xdo("click", button)

    def menu(self, title_x, row_y):
        self.click(title_x, 8)
        time.sleep(1)
        self.click(title_x + 20, row_y)

    def act(self, what, budget, do):
        """does it, waits until the Atari is still, and keeps the longest
        stretch that came meanwhile"""
        start = os.path.getsize(self.log)
        self.in_act = True
        try:
            do()
            time.sleep(1)
            self.still()
        finally:
            self.in_act = False
            self.release()
        with open(self.log, errors="replace") as f:
            f.seek(start)
            new = f.read()
        gaps = [int(g) for g in re.findall(r"AESGAP (\d+) ms", new)]
        self.n += 1
        self.shot("%02d-%s" % (self.n, re.sub(r"[^a-z0-9]+", "-", what.lower()).strip("-")))
        self.results.append((what, max(gaps) if gaps else 0, budget))
        return max(gaps) if gaps else 0

    def report(self):
        """the table; 1 if any action is over its time"""
        over = 0
        con = self.console()
        m = re.search(r"AESSTART (\d+) ms", con)
        drawn = sum(int(g) for g in re.findall(r"AESGAP (\d+) ms", con[:self.start_end]))
        for i, (what, ms, budget) in enumerate(self.results):
            if ms == "AESSTART":
                self.results[i] = (what, int(m.group(1)) + drawn if m else None, budget)
                if not m:
                    print("NOTE  no AESSTART: is the program built with -DAES_PROFILE?", flush=True)
        print("speed on %s" % self.describe())
        print("  %-40s %8s %8s" % ("action", "longest", "time"))
        for what, ms, budget in self.results:
            if ms is None:
                shown, bad = "?", True
            else:
                shown, bad = ("<20" if ms == 0 else "%d" % ms), ms > budget
            over += bad
            print("  %-40s %8s %8d  %s" % (what, shown, budget, "OVER" if bad else "ok"), flush=True)
        crashes = [os.path.join(d, f) for d, _, fs in os.walk(self.hd) for f in fs if f.upper() == "CRASH.LOG"]
        if crashes:
            print("FAIL  CRASH.LOG: " + ", ".join(crashes))
        print("all within their times" if not over and not crashes else "%d over their times" % over)
        return 1 if over or crashes else 0

    def stop(self):
        if self.hatari:
            self.hatari.kill()
        self.m.stop()
        if self.x:
            self.x.kill()


def selftest(work, machine):
    """SPT.PRG's known stretches read back"""
    prg = os.path.join(HERE, "build", "atari", "SPT.PRG")
    if not os.path.exists(prg):
        sys.exit("speed: make prg first")
    r = Rig(work, machine)
    fails = 0
    try:
        r.put(prg)
        r.start("C:\\SPT.PRG", ready="SPT ready")
        r.act("down (30 ms)", LINE, lambda: r.key("Down"))
        r.act("shift+down (200 ms)", PAGE, lambda: r.key("shift+Down"))
        r.act("return (80 ms)", DIALOG, lambda: r.key("Return"))
        r.report()
        want = [300, 30, 200, 80]
        for (what, ms, _), w in zip(r.results, want):
            ok = ms is not None and abs(ms - w) <= max(10, w // 10)
            print(("ok    " if ok else "FAIL  ") + "%s: %s ms read back (%d ms of work)" % (what, ms, w))
            fails += not ok
        over = [what for what, ms, b in r.results if ms is not None and ms > b]
        ok = over == ["shift+down (200 ms)"]
        print(("ok    " if ok else "FAIL  ") + "only the 200 ms is over its time (%s)" % ", ".join(over))
        fails += not ok
        r.key("q")
        time.sleep(2)
        ok = "SPT done" in r.console()
        print(("ok    " if ok else "FAIL  ") + "SPT.PRG quit")
        fails += not ok
    finally:
        r.stop()
    print("all passed" if not fails else "%d failed" % fails)
    return 1 if fails else 0


if __name__ == "__main__":
    a = sys.argv[1:]
    if len(a) >= 2 and a[0] == "--selftest":
        sys.exit(selftest(a[1], a[2] if len(a) > 2 else "ST"))
    sys.exit(__doc__)
