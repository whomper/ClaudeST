#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""The shared alert() and dialog loop (ui/alert.c, ui/dialog.c) in Hatari:
DLGT.PRG shows alerts and a dialog with a text field, and this presses
the keys and clicks for each (Return, Esc, Undo, typing, a click on a
button where the DLG_TRACE build says it is) and checks the answers.

    make prg && python3 run.py etos512us.img [--modes high,med,low,falcon]
    python3 run.py etos512us.img --xaaes      FreeMiNT 1.17.0 and XaAES (../xaaes: make base)

EmuTOS (512k) is run in ST high, medium and low and in the Falcon's VGA
mode. Hatari runs in a window on an Xvfb display (xdotool moves the
mouse); screenshots go to WORK/MODE/shots, one for each alert and dialog.
"""
import argparse, os, re, shutil, subprocess, sys, time

HERE = os.path.dirname(os.path.abspath(__file__))
PRG = os.path.join(HERE, "build", "atari", "DLGT.PRG")

# name: what to do once it shows, and what must come back (button, the
# field's text); "click X" clicks the exit button whose text is X
CASES = [
    ("a_return", ["key 0x1c"], "1"),
    ("a_esc", ["key 0x01"], "2"),
    ("a_undo", ["key 0x61"], "2"),
    ("a_esc3", ["key 0x01"], "3"),
    ("a_click", ["click Skip"], "2"),
    ("a_ok", ["key 0x01"], "1"),
    ("d_esc", ["key x", "key y", "key 0x01"], "4 helloxy 0"),
    ("d_return", ["key z", "key 0x1c"], "3 helloz 0"),
    ("d_undo", ["key 0x61"], "4 hello 0"),
    ("d_none", ["key q", "key 0x01"], "-1 helloq 0"),
    ("d_click", ["click OK"], "3 hello 0"),
    ("d_close", ["key x", "close"], "4 hellox 0"),
    ("d_band", ["key 0x1c"], "3 hello 0"),
]
# under XaAES the alerts are the AES's own (Erez, 25 Oct 2026): Return
# answers them (its Esc does nothing), and the dialogs are in a window
XA_CASES = [(n, ["key 0x1c"], "1") for n, _, _ in CASES if n.startswith("a_")] + \
    [c for c in CASES if c[0].startswith("d_")]
MODES = {
    "high": ["--machine", "st", "--mono", "--memsize", "4"],
    "med": ["--machine", "st", "--monitor", "rgb", "--tos-res", "med", "--memsize", "4"],
    "low": ["--machine", "st", "--monitor", "rgb", "--tos-res", "low", "--memsize", "4"],
    "falcon": ["--machine", "falcon", "--monitor", "vga", "--dsp", "none", "--memsize", "14"],
}
failed = 0


def check(ok, what):
    global failed
    print(("ok   " if ok else "FAIL ") + what, flush=True)
    failed += not ok
    return ok


class Rig:
    def __init__(self, a, mode, work):
        self.env = dict(os.environ, DISPLAY=a.display, SDL_AUDIODRIVER="dummy")
        self.work = work
        self.shots = os.path.join(work, "shots")
        self.fifo = os.path.join(work, "cmd.fifo")
        self.logp = os.path.join(work, "hatari.log")
        self.mode = mode
        self.win = None
        self.xvfb = None
        if subprocess.run(["xdpyinfo"], env=self.env, capture_output=True).returncode:
            self.xvfb = subprocess.Popen(["Xvfb", a.display, "-screen", "0", "1280x1024x24"],
                                         stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            for _ in range(50):
                time.sleep(0.1)
                if not subprocess.run(["xdpyinfo"], env=self.env, capture_output=True).returncode:
                    break

    def start_emutos(self, a):
        shutil.rmtree(self.work, ignore_errors=True)
        hd = os.path.join(self.work, "hd")
        os.makedirs(hd)
        os.makedirs(self.shots)
        shutil.copy(PRG, os.path.join(hd, "DLGT.PRG"))
        args = ["stdbuf", "-oL", "-eL", "hatari", "-w", "--statusbar", "no", "--borders", "no",
                "--zoom", "1", "--confirm-quit", "no", "--tos", os.path.abspath(a.tos),
                "--harddrive", hd, "--auto", "C:\\DLGT.PRG", "--fast-boot", "yes", "--natfeats", "yes",
                "--sound", "off", "--cmd-fifo", self.fifo, "--screenshot-dir", self.shots,
                "--log-level", "fatal"] + MODES[self.mode]
        self.h = subprocess.Popen(args, env=self.env, stdout=open(self.logp, "w"),
                                  stderr=subprocess.STDOUT, cwd=self.work)

    def start_xaaes(self, a):
        # ../xaaes/run.py makes the disk and starts Hatari; we drive it
        # through its fifo and window, as its own steps do
        shutil.rmtree(self.work, ignore_errors=True)	# no log of a run before
        os.makedirs(os.path.dirname(self.work), exist_ok=True)
        xa = os.path.join(HERE, "..", "xaaes", "run.py")
        self.h = subprocess.Popen([sys.executable, xa, self.work, os.path.abspath(a.tos), PRG,
                                   "--display", a.display], env=self.env,
                                  stdout=open(os.path.join(os.path.dirname(self.work), "xaaes-run.log"), "w"),
                                  stderr=subprocess.STDOUT)
        self.shots = os.path.join(self.work, "shots")

    def log(self):
        try:
            return open(self.logp, errors="replace").read()
        except OSError:
            return ""

    def cmd(self, c):
        for _ in range(100):
            if os.path.exists(self.fifo):
                break
            time.sleep(0.1)
        with open(self.fifo, "w") as f:
            f.write(c + "\n")
        time.sleep(0.25)

    def xdo(self, *args):
        return subprocess.run(["xdotool", *args], env=self.env, capture_output=True, text=True).stdout

    def find_window(self, sw, sh):
        for _ in range(50):
            f = self.xdo("search", "--name", "Hatari").split()
            if f:
                self.win = f[-1]
                break
            time.sleep(0.2)
        geo = dict(l.split("=") for l in self.xdo("getwindowgeometry", "--shell", self.win).split())
        self.wx, self.wy = int(geo["X"]), int(geo["Y"])
        self.sx, self.sy = int(geo["WIDTH"]) / sw, int(geo["HEIGHT"]) / sh

    def click(self, x, y):
        X, Y = int(self.wx + x * self.sx), int(self.wy + y * self.sy)
        self.xdo("mousemove", str(X - 3), str(Y))
        time.sleep(0.2)
        self.xdo("mousemove", str(X), str(Y))
        time.sleep(0.4)
        self.xdo("mousedown", "1")
        time.sleep(0.15)
        self.xdo("mouseup", "1")
        time.sleep(0.5)

    def shot(self, name):
        before = set(os.listdir(self.shots)) if os.path.isdir(self.shots) else set()
        self.cmd("hatari-shortcut screenshot")
        for _ in range(30):
            new = set(os.listdir(self.shots)) - before
            if new:
                time.sleep(0.5)
                os.rename(os.path.join(self.shots, new.pop()), os.path.join(self.shots, name + ".png"))
                return
            time.sleep(0.2)

    def wait(self, text, after=0, timeout=90):
        end = time.time() + timeout
        while time.time() < end and self.h.poll() is None:
            got = self.log()
            i = got.find(text, after)
            if i >= 0:
                return got, i
            time.sleep(0.25)
        return self.log(), -1

    def end(self):
        if self.h.poll() is None:
            self.h.terminate()
            try:
                self.h.wait(20)
            except subprocess.TimeoutExpired:
                self.h.kill()
        if self.xvfb:
            self.xvfb.terminate()


def run_mode(a, mode):
    work = os.path.join(os.path.abspath(a.work), mode)
    r = Rig(a, mode, work)
    try:
        if mode == "xaaes":
            r.start_xaaes(a)
        else:
            r.start_emutos(a)
        got, i = r.wait("DLGT screen", timeout=240 if mode == "xaaes" else 90)
        m = re.search(r"DLGT screen (\d+) (\d+) cell (\d+) (\d+)", got)
        if not check(m is not None, "%s: DLGT.PRG started" % mode):
            print(got[-2000:])
            return
        sw, sh = int(m.group(1)), int(m.group(2))
        print("     %s: screen %dx%d, cell %sx%s" % (mode, sw, sh, m.group(3), m.group(4)), flush=True)
        pos = i
        for name, acts, want in (XA_CASES if mode == "xaaes" else CASES):
            got, i = r.wait("DLGT ask %s\n" % name, pos)
            if not check(i >= 0, "%s %s: shown" % (mode, name)):
                return
            pos = i
            # the DLG lines come once the box is drawn
            if name.startswith("d_") or mode != "xaaes":
                got, j = r.wait("DLG exit", pos)
            else:
                time.sleep(6)		# the AES's alert says nothing when it is up
            time.sleep(1.5)			# past the loop's flush of typed-ahead keys
            got = r.log()
            k = got.find("DLGT got", pos)
            exits = re.findall(r"DLG exit (\d+) (-?\d+) (-?\d+) (\d+) (\d+) ([^\n]*)", got[pos:k if k > 0 else None])
            r.shot("%s-%s" % (mode, name))
            if name.startswith("d_"):
                inwin = "DLG window" in got[pos:k if k > 0 else None]
                check(inwin == (mode == "xaaes"), "%s %s: %s" % (mode, name,
                      "in a window of the AES's" if mode == "xaaes" else "over the screen, no window"))
            for act in acts:
                verb, _, arg = act.partition(" ")
                if verb == "key":
                    r.cmd("hatari-event keypress %s" % arg)
                    time.sleep(0.3)
                elif verb == "close":
                    # the window's closer, at the left of its title bar;
                    # on TOS (no window) Esc does what it does
                    wm = re.findall(r"DLG window (\d+) (\d+) (\d+) (\d+)", got[pos:k if k > 0 else None])
                    if mode != "xaaes":
                        r.cmd("hatari-event keypress 0x01")
                        continue
                    if not check(bool(wm), "%s %s: in a window" % (mode, name)):
                        continue
                    if r.win is None:
                        r.find_window(sw, sh)
                    wx, wy = int(wm[-1][0]), int(wm[-1][1])
                    r.click(wx + 8, wy + 8)
                elif verb == "click":
                    if r.win is None:
                        r.find_window(sw, sh)
                    e = [x for x in exits if x[5].strip() == arg]
                    if not check(bool(e), "%s %s: the button %r on screen" % (mode, name, arg)):
                        continue
                    _, x, y, w, h, _ = e[0]
                    r.click(int(x) + int(w) // 2, int(y) + int(h) // 2)
            got, i = r.wait("DLGT got %s " % name, pos, timeout=20)
            ans = got[i:].split("\n", 1)[0].split(" ", 3)[3] if i >= 0 else "(nothing)"
            if name.startswith("a_"):
                ans = ans.split(" ")[0]
            check(ans == want, "%s %s: %s gave %s (want %s)" % (mode, name, " + ".join(acts), ans, want))
            if i >= 0:
                pos = i
        got, i = r.wait("DLGT done", pos, timeout=20)
        check(i >= 0, "%s: DLGT.PRG ran to its end" % mode)
    finally:
        r.end()


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("tos")
    ap.add_argument("--work", default=os.path.join(HERE, "build", "work"))
    ap.add_argument("--modes", default="high,med,low,falcon")
    ap.add_argument("--xaaes", action="store_true")
    ap.add_argument("--display", default=":96")
    a = ap.parse_args()
    if not os.path.exists(PRG):
        sys.exit("no %s: make prg" % PRG)
    for mode in (["xaaes"] if a.xaaes else a.modes.split(",")):
        run_mode(a, mode)
    print("%d failed; screenshots in %s" % (failed, os.path.abspath(a.work)))
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
