#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Erez Yaary
"""run.py WORK EMUTOS.IMG PRG [--copy FILE ...] [--display :97] [--wait S]
          [--steps "..."] [--size MB]

An Atari GEM program in Hatari under FreeMiNT 1.17.0 with XaAES, an AES
that iconifies windows, multitasks and does drag and drop, on a Falcon
(68030, VGA, 14 MB) with EmuTOS (512k, 1.4). make base first: fetch.py
makes build/base, the C: drive (MINT030.PRG and OPNWK.PRG in AUTO, then
XaAES).

WORK is made afresh: WORK/hd is a copy of build/base with PRG and the
--copy files (folders too) in its root, and a "run C:\\PRG" line at the
end of XAAES.CNF, so the program starts by itself (a desk accessory in
the root is loaded too). FreeMiNT can't see Hatari's GEMDOS-emulated
drive (its FAT driver reads sectors through the BIOS), so the folder
becomes WORK/hd.img (fatimg.py), an IDE disk that EmuTOS boots from and
FreeMiNT mounts as C:. Afterwards the disk is read back into WORK/out,
with anything the program wrote. --fast-boot is off: with it EmuTOS
doesn't boot from the IDE disk.

Hatari runs in a window on an Xvfb display (started when none answers on
--display), with --cmd-fifo WORK/cmd.fifo, --screenshot-dir WORK/shots
and its output (NatFeats, MiNT's boot messages) in WORK/hatari.log.

--wait S    without --steps: run S seconds (default: until stopped); with
            --steps: S seconds before the first step
--steps     separated by ';', X Y in Atari screen coordinates:
              wait S            seconds
              shot              a screenshot (WORK/shots/grabNNNN.png)
              move X Y          the mouse
              click X Y, rclick X Y, dclick X Y
              drag X1 Y1 X2 Y2  press at X1 Y1, move to X2 Y2, let go
              key 0x1c          a key by its scan code, or a character
              ctrl n            Control with a key (a capital adds Shift)
              until TEXT [S]    wait (120 s) for TEXT in hatari.log, a line
                                the program says through NatFeats; the
                                run fails when it doesn't come
--size MB   the disk (default 32)

Prints the screenshots' names at the end; exits 1 when a step failed.
"""
import argparse, os, shutil, signal, subprocess, sys, time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import fatimg  # noqa: E402

BASE = os.path.join(HERE, "build", "base")


SHELL = "reverting to internal shell"	# MiNT didn't read MINT.CNF: no XaAES
RETRY = 2


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("work")
    ap.add_argument("tos")
    ap.add_argument("prg")
    ap.add_argument("--copy", nargs="*", default=[])
    ap.add_argument("--display", default=":97")
    ap.add_argument("--wait", type=float)
    ap.add_argument("--steps")
    ap.add_argument("--size", type=int, default=32)
    a = ap.parse_args()
    if not os.path.isdir(BASE):
        sys.exit("no %s: make base" % BASE)
    a.tos = os.path.abspath(a.tos)
    # Now and then, mostly with several Hatari at once, FreeMiNT can't open
    # MINT.CNF on the IDE disk and starts its own shell: then once more
    for n in range(3):
        r = run(a)
        if r != RETRY:
            return r
        print("FreeMiNT came up in its shell, not XaAES: starting again", flush=True)
    return 1


def run(a):
    work = os.path.abspath(a.work)
    shutil.rmtree(work, ignore_errors=True)
    hd, shots = os.path.join(work, "hd"), os.path.join(work, "shots")
    shutil.copytree(BASE, hd)
    os.makedirs(shots)
    name = os.path.basename(a.prg).upper()
    fatimg.name83(name)
    shutil.copy(a.prg, os.path.join(hd, name))
    for f in a.copy:
        dst = os.path.join(hd, os.path.basename(f.rstrip("/")).upper())
        (shutil.copytree if os.path.isdir(f) else shutil.copy)(f, dst)
    mintdir = os.path.join(hd, "mint")
    ver = [d for d in os.listdir(mintdir) if d[0].isdigit()][0]
    with open(os.path.join(mintdir, ver, "xaaes", "xaaes.cnf"), "a") as f:
        f.write("run c:\\%s\n" % name)
    img = os.path.join(work, "hd.img")
    fatimg.build(hd, img, a.size)

    env = dict(os.environ, DISPLAY=a.display, SDL_AUDIODRIVER="dummy")
    xvfb = None
    if subprocess.run(["xdpyinfo"], env=env, capture_output=True).returncode:
        xvfb = subprocess.Popen(["Xvfb", a.display, "-screen", "0", "1024x768x24"],
                                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        for _ in range(50):
            time.sleep(0.1)
            if not subprocess.run(["xdpyinfo"], env=env, capture_output=True).returncode:
                break
    fifo = os.path.join(work, "cmd.fifo")
    logp = os.path.join(work, "hatari.log")
    log = open(logp, "w")
    t0 = time.time()
    h = subprocess.Popen(["stdbuf", "-oL", "-eL", "hatari", "-w", "--statusbar", "no", "--borders", "no",
                          "--zoom", "1", "--confirm-quit", "no", "--tos", a.tos,
                          "--machine", "falcon", "--monitor", "vga", "--dsp", "none", "--memsize", "14",
                          "--ide-master", img, "--fast-boot", "no", "--natfeats", "yes", "--conout", "2",
                          "--sound", "off", "--cmd-fifo", fifo, "--screenshot-dir", shots],
                         env=env, stdout=log, stderr=subprocess.STDOUT, cwd=work)
    stop = {"now": False, "shell": False}

    def on_signal(signum, frame):
        stop["now"] = True
    signal.signal(signal.SIGTERM, on_signal)
    signal.signal(signal.SIGINT, on_signal)

    def cmd(c):
        for _ in range(100):
            if os.path.exists(fifo):
                break
            time.sleep(0.1)
        with open(fifo, "w") as f:
            f.write(c + "\n")
        time.sleep(0.15)

    def xdo(*args):
        return subprocess.run(["xdotool", *args], env=env, capture_output=True, text=True).stdout

    def in_shell():
        with open(logp, errors="replace") as f:
            return SHELL in f.read()

    def sleep(s):
        end = time.time() + s
        while time.time() < end and not stop["now"] and h.poll() is None and not stop["shell"]:
            time.sleep(min(0.2, max(0, end - time.time())))
            if time.time() - t0 < 60 and in_shell():
                stop["shell"] = True

    win = None
    wx = wy = 0
    ok = True
    try:
        if a.steps is None:
            sleep(a.wait if a.wait is not None else 10 ** 9)
        else:
            if a.wait:
                sleep(a.wait)
            for st in a.steps.split(";"):
                p = st.split()
                if not stop["shell"] and time.time() - t0 < 60 and in_shell():
                    stop["shell"] = True
                if stop["shell"]:
                    break
                if not p or stop["now"] or h.poll() is not None:
                    continue
                print("%6.1fs %s" % (time.time() - t0, " ".join(p)), flush=True)
                if p[0] in ("move", "click", "rclick", "dclick", "drag") and win is None:
                    for _ in range(50):
                        f = (xdo("search", "--pid", str(h.pid)) or xdo("search", "--name", "Hatari")).split()
                        if f:
                            win = f[-1]
                            break
                        time.sleep(0.2)
                    geo = dict(l.split("=") for l in xdo("getwindowgeometry", "--shell", win).split())
                    wx, wy = int(geo["X"]), int(geo["Y"])
                if p[0] == "wait":
                    sleep(float(p[1]))
                elif p[0] == "shot":
                    cmd("hatari-shortcut screenshot")
                    time.sleep(1)
                elif p[0] in ("move", "click", "rclick", "dclick"):
                    x, y = int(p[1]), int(p[2])
                    xdo("mousemove", str(wx + x - 3), str(wy + y))
                    time.sleep(0.15)
                    xdo("mousemove", str(wx + x), str(wy + y))
                    time.sleep(0.3)
                    if p[0] != "move":
                        b = "3" if p[0] == "rclick" else "1"
                        for _ in range(2 if p[0] == "dclick" else 1):
                            xdo("mousedown", b)
                            time.sleep(0.12 if p[0] != "dclick" else 0.06)
                            xdo("mouseup", b)
                            time.sleep(0.3 if p[0] != "dclick" else 0.06)
                        time.sleep(0.3)
                elif p[0] == "drag":
                    x1, y1, x2, y2 = (int(v) for v in p[1:5])
                    xdo("mousemove", str(wx + x1), str(wy + y1))
                    time.sleep(0.3)
                    xdo("mousedown", "1")
                    time.sleep(0.4)
                    for k in range(1, 11):	# in steps, as a hand would
                        xdo("mousemove", str(wx + x1 + (x2 - x1) * k // 10), str(wy + y1 + (y2 - y1) * k // 10))
                        time.sleep(0.08)
                    time.sleep(0.4)
                    xdo("mouseup", "1")
                    time.sleep(0.5)
                elif p[0] == "key":
                    cmd("hatari-event keypress %s" % p[1])
                elif p[0] == "ctrl":
                    cmd("hatari-event keydown 29")
                    cmd("hatari-event keypress %s" % p[1])
                    cmd("hatari-event keyup 29")
                elif p[0] == "until":
                    timeout = 120.0
                    text = " ".join(p[1:])
                    if len(p) > 2 and p[-1].replace(".", "").isdigit():
                        timeout, text = float(p[-1]), " ".join(p[1:-1])
                    end = time.time() + timeout
                    while not stop["now"] and h.poll() is None:
                        with open(logp, errors="replace") as f:
                            got = f.read()
                        if text in got:
                            break
                        if SHELL in got:
                            stop["shell"] = True
                            break
                        if time.time() > end:
                            print("until: '%s' did not come in %g s" % (text, timeout), flush=True)
                            ok = False
                            break
                        time.sleep(0.25)
                    if not ok or stop["shell"]:
                        break
                    print("%6.1fs   got '%s'" % (time.time() - t0, text), flush=True)
                else:
                    print("unknown step: %s" % st, flush=True)
                    ok = False
                    break
    finally:
        if h.poll() is None:
            h.terminate()
            try:
                h.wait(10)
            except subprocess.TimeoutExpired:
                h.kill()
                h.wait()
        if xvfb:
            xvfb.terminate()
        log.close()
        out = os.path.join(work, "out")
        try:
            fatimg.extract(img, out)
        except Exception as e:  # the disk is for looking at, the run is what counts
            print("could not read the disk back: %s" % e)
    if stop["shell"]:
        return RETRY
    print(sorted(os.listdir(shots)))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
