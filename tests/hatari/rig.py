# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""Drive ClaudeST in the Hatari emulator: an X display (Xvfb), Hatari's
command FIFO for keys and screenshots, xdotool for the mouse. The
machine is one of the family's (common/tools/machines.py: ST, ST-1, TT,
Falcon, each with its screens; the guidelines, The machines), never
Hatari options of a rig's own.
From ECal's tests/hatari/rig.py (whomper/ECal), which was adapted from
ClaudeST's recording rig (tools/reel).
"""
import os
import shutil
import stat
import struct
import subprocess
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "common", "tools"))
import machines  # noqa: E402

# ST scancodes (US layout)
SCAN = {c: i for i, c in enumerate("1234567890-=", 2)}
SCAN.update({c: i for i, c in enumerate("qwertyuiop[]", 16)})
SCAN.update({c: i for i, c in enumerate("asdfghjkl;'`", 30)})
SCAN.update({c: i for i, c in enumerate("zxcvbnm,./", 44)})
SCAN[" "] = 57
SCAN["\\"] = 43
SHIFTED = {"!": "1", "@": "2", "#": "3", "$": "4", "%": "5", "^": "6", "&": "7", "*": "8",
           "(": "9", ")": "0", "_": "-", "+": "=", ":": ";", '"': "'", "<": ",", ">": ".",
           "?": "/", "{": "[", "}": "]", "|": "\\", "~": "`"}

K = dict(RETURN=0x1c, ESC=0x01, TAB=0x0f, BACKSPACE=0x0e, DELETE=0x53, UP=0x48, DOWN=0x50,
         LEFT=0x4b, RIGHT=0x4d, HOME=0x47, HELP=0x62, F10=0x44, CTRL=0x1d, SHIFT=0x2a, ALT=0x38)


def add_machine(ap, default):
    """--machine NAME[:SCREEN] for a rig's argparse, default if left out"""
    ap.add_argument("--machine", default=default,
                    help="one of common/tools/machines.py's: %s, with :SCREEN (default %s)"
                    % (", ".join(machines.MACHINES), default))


class Rig:
    def __init__(self, work, machine="Falcon", display=":99", network=False, extra=(),
                 auto="C:\\CLAUDEST.PRG"):
        """machine: NAME[:SCREEN] from machines.py, drive C: is work/hd;
        network: FAKESTNG in C:\\AUTO and its bridge (machines.py's
        network()), the Atari's TCP connections becoming this computer's"""
        self.work, self.display = work, display
        self.machine = machines.Machine(machine, os.path.join(work, "hd"))
        print("machine:", self.machine.describe(), flush=True)
        self.env = self.machine.env(dict(os.environ, DISPLAY=display, SDL_VIDEODRIVER="x11",
                                         SDL_AUDIODRIVER="dummy"))
        self.fifo = os.path.join(work, "cmd.fifo")
        self.shots = os.path.join(work, "shots")
        os.makedirs(self.shots, exist_ok=True)
        self.win = None
        self.xvfb = machines.xvfb(display, "1280x1024x24")  # once it answers
        args = (["hatari", "-w", "--statusbar", "no", "--borders", "no", "--zoom", "1"] + self.machine.args() +
                ["--fast-boot", "yes", "--natfeats", "yes", "--cmd-fifo", self.fifo,
                 "--screenshot-dir", self.shots, "--log-level", "warn"])
        if auto:
            args += ["--auto", auto]
        if network:
            args += self.machine.network()
        args += list(extra)
        self.log = open(os.path.join(work, "hatari.log"), "w")
        self.proc = subprocess.Popen(args, env=self.env, stdout=self.log, stderr=subprocess.STDOUT)
        self._size = None

    def size(self):
        """the screen's width and height in pixels (640x480 on the Falcon,
        640x400 mono, 640x200 med...), from a screenshot"""
        if not self._size:
            path = self.shot("_size", wait=0.1)
            with open(path, "rb") as f:
                self._size = struct.unpack(">II", f.read(24)[16:24])
            os.remove(path)
        return self._size

    def cmd(self, *args):
        for _ in range(200):
            if os.path.exists(self.fifo) and stat.S_ISFIFO(os.stat(self.fifo).st_mode):
                break
            time.sleep(0.1)
        else:
            raise RuntimeError("Hatari's command FIFO never appeared")
        with open(self.fifo, "w") as f:
            f.write(" ".join(args) + "\n")
        time.sleep(0.05)

    def key(self, scancode, shift=False, ctrl=False, alt=False):
        for on, k in ((ctrl, K["CTRL"]), (shift, K["SHIFT"]), (alt, K["ALT"])):
            if on:
                self.cmd("hatari-event", "keydown", str(k))
        self.cmd("hatari-event", "keypress", "0x%02x" % scancode)
        for on, k in ((alt, K["ALT"]), (shift, K["SHIFT"]), (ctrl, K["CTRL"])):
            if on:
                self.cmd("hatari-event", "keyup", str(k))
        time.sleep(0.08)

    def ctrl(self, letter):
        self.key(SCAN[letter], ctrl=True)

    def type(self, text, pace=0.08):
        for ch in text:
            if ch in SHIFTED:
                self.key(SCAN[SHIFTED[ch]], shift=True)
            elif ch.isupper():
                self.key(SCAN[ch.lower()], shift=True)
            elif ch == "\n":
                self.key(K["RETURN"])
            else:
                self.key(SCAN[ch])
            time.sleep(pace)

    def shot(self, name, wait=0.6):
        """Save a screenshot as shots/NAME.png and return its path."""
        before = set(os.listdir(self.shots))
        self.cmd("hatari-shortcut", "screenshot")
        for _ in range(100):
            new = [f for f in set(os.listdir(self.shots)) - before if f.startswith("grab")]
            if new:
                time.sleep(wait)
                dst = os.path.join(self.shots, name + ".png")
                shutil.move(os.path.join(self.shots, new[0]), dst)
                return dst
            time.sleep(0.1)
        raise RuntimeError("no screenshot")

    def wait(self, secs, limit=30):
        """Sleep, then go on sleeping until the screen stops changing: an
        8 MHz ST with TOS's own VDI can take seconds to repaint a window."""
        time.sleep(secs)
        last, end = None, time.time() + limit
        while time.time() < end:
            path = self.shot("_settle", wait=0.1)
            with open(path, "rb") as f:
                now = f.read()
            os.remove(path)
            if now == last:
                return
            last = now
            time.sleep(0.5)

    # ---- mouse ----
    def xdo(self, *args):
        return subprocess.run(["xdotool", *args], env=self.env, capture_output=True, text=True).stdout

    def window(self):
        for _ in range(100):
            if self.win:
                break
            found = (self.xdo("search", "--name", "Hatari") or self.xdo("search", "--class", "hatari")).split()
            if found:
                self.win = found[0]
            else:
                time.sleep(0.2)
        if not self.win:
            raise RuntimeError("no Hatari window")
        geo = dict(l.split("=") for l in self.xdo("getwindowgeometry", "--shell", self.win).split())
        return int(geo["X"]), int(geo["Y"])

    def move(self, x, y, secs=0.4):
        wx, wy = self.window()
        loc = dict(l.split("=") for l in self.xdo("getmouselocation", "--shell").split())
        x0, y0, x1, y1 = int(loc["X"]), int(loc["Y"]), wx + x, wy + y
        steps = max(8, int(secs / 0.02))
        for i in range(1, steps + 1):
            t = i / steps
            self.xdo("mousemove", str(int(x0 + (x1 - x0) * t)), str(int(y0 + (y1 - y0) * t)))
            time.sleep(secs / steps)
        time.sleep(0.15)

    def click(self, x=None, y=None, double=False):
        if x is not None:
            self.move(x, y)
        self.xdo("click", "--repeat", "2" if double else "1", "--delay", "120", "1")
        time.sleep(0.3)

    def drag(self, x1, y1, x2, y2, secs=0.6):
        """press at (x1, y1), move to (x2, y2), release"""
        self.move(x1, y1)
        self.xdo("mousedown", "1")
        time.sleep(0.2)
        self.move(x2, y2, secs)
        time.sleep(0.2)
        self.xdo("mouseup", "1")
        time.sleep(0.4)

    def rclick(self, x=None, y=None):
        if x is not None:
            self.move(x, y)
        self.xdo("click", "3")
        time.sleep(0.5)

    def stop(self):
        self.proc.terminate()
        try:
            self.proc.wait(5)
        except Exception:
            self.proc.kill()
        self.machine.stop()
        if self.xvfb:
            self.xvfb.terminate()
