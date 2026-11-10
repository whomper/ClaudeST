#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""run.py WORK EMUTOS.IMG [PRG] [--copy FILE ...] [--display :97] [--wait S]
          [--steps "..."] [--size MB] [--setenv NAME=VALUE ...] [--cnf LINE ...]
          [--ext2 DIR] [--start PATH] [--mem-prot]

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

--setenv    NAME=VALUE: a setenv line in XAAES.CNF, before the run line
            (the AES's environment, which shel_envrn reads)
--cnf       a line of its own in XAAES.CNF, before the run line
            ("xa_bubble = 0", "run c:\\BGEMNG\\BGEMNG.PRG")
--wait S    without --steps: run S seconds (default: until stopped); with
            --steps: S seconds before the first step
--steps     separated by ';', X Y in Atari screen coordinates:
              wait S            seconds
              shot              a screenshot (WORK/shots/grabNNNN.png)
              move X Y          the mouse
              click X Y, rclick X Y, dclick X Y
              drag X1 Y1 X2 Y2  press at X1 Y1, move to X2 Y2, let go
              key 0x1c          a key by its scan code, or a character
              ctrl n            Control with a key, a letter by its scan code (a capital adds Shift)
              until TEXT [S]    wait (120 s) for TEXT in hatari.log, a line
                                the program says through NatFeats; the
                                run fails when it doesn't come
--size MB   the disk (default 32)
--later S   PRG starts S seconds after XaAES has (build/LATER.PRG, make),
            once the desk accessories are in, as when someone starts it
--ext2 DIR  a second disk, a case-sensitive drive as a tester's (the
            guidelines, 6.6): DIR's files, their names exactly as they are,
            on an ext2 partition (ext2img.py) that EmuTOS serves as D:
            through XHDI and FreeMiNT mounts with ext2.xfs (from the
            FreeMiNT archive, copied into the mint folder). Afterwards it is
            read back into WORK/ext2 and listed, names as they are, in
            WORK/ext2.ls (Hatari runs 8 s more at the end, for MiNT to write
            its cache out)
--mem-prot  FreeMiNT's memory protection on (MEM_PROT=YES in mint.ini, Hatari --mmu on), for a program that
            touches its own memory from an interrupt or while another process runs (PRGFLAGS: private by
            default, bits 4-5; 0x10 is global). The default is off, as before
--start P   what XAAES.CNF starts instead of C:\\PRG, e.g. "d:\\eweb.prg"
            for a release folder given as --ext2 (PRG may then be left out;
            given, it is copied to C: all the same)

Prints the screenshots' names at the end; exits 1 when a step failed.
"""
import argparse, os, shutil, signal, subprocess, sys, time

# ST scan codes of the keys a "ctrl X" step names (US layout)
STSCAN = {c: 0x10 + i for i, c in enumerate("qwertyuiop")}
STSCAN.update({c: 0x1e + i for i, c in enumerate("asdfghjkl")})
STSCAN.update({c: 0x2c + i for i, c in enumerate("zxcvbnm")})
STSCAN.update({",": 0x33, ".": 0x34})
STSCAN = {k: "0x%02x" % v for k, v in STSCAN.items()}

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, "..", "..", "tools"))
import ext2img  # noqa: E402
import fatimg  # noqa: E402
import machines  # noqa: E402

BASE = os.path.join(HERE, "build", "base")


def ext2_xfs():
    """FreeMiNT's ext2.xfs (68020 and up), from the archive fetch.py took"""
    out = os.path.join(HERE, "build", "ext2.xfs")
    if not os.path.exists(out):
        import zipfile
        zips = [f for f in os.listdir(os.path.join(HERE, "build")) if f.startswith("freemint-") and f.endswith(".zip")]
        if not zips:
            sys.exit("no FreeMiNT archive in build: make base")
        with zipfile.ZipFile(os.path.join(HERE, "build", zips[0])) as z:
            with open(out, "wb") as f:
                f.write(z.read("driver/xfs/ext2.xfs"))
    return out


EXT2_SETTLE = 8	# s, longer than MiNT's sync interval (5 s)
SHELL = "reverting to internal shell"	# MiNT didn't read MINT.CNF: no XaAES
RETRY = 2


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("work")
    ap.add_argument("tos")
    ap.add_argument("prg", nargs="?", help="the program for C:\\ (may be left out with --start)")
    ap.add_argument("--copy", nargs="*", default=[])
    ap.add_argument("--display", default=":97")
    ap.add_argument("--wait", type=float)
    ap.add_argument("--steps")
    ap.add_argument("--size", type=int, default=32)
    ap.add_argument("--later", type=int)
    ap.add_argument("--setenv", action="append", default=[], metavar="NAME=VALUE")
    ap.add_argument("--cnf", action="append", default=[], metavar="LINE")
    ap.add_argument("--ext2", metavar="DIR")
    ap.add_argument("--start", metavar="PATH")
    ap.add_argument("--machine", default="falcon", choices=["falcon", "tt"],
                    help="falcon: VGA, 14 MB, the IDE disk (default); tt: a TT030 at 32 MHz with 4 MB + 16 MB TT RAM on "
                         "its high resolution monitor (1280x960 mono), the same IDE disk")
    ap.add_argument("--mem-prot", action="store_true",
                    help="MEM_PROT=YES in mint.ini and Hatari's --mmu on: FreeMiNT's memory protection, which a program "
                         "that touches its own memory from an interrupt, or while another process runs, may not survive "
                         "(EPlay halted the Falcon: PRGFLAGS 7 was private memory, 0x17 global). Not the default: the "
                         "MMU makes Hatari slow (the steps' waits may need to be longer)")
    ap.add_argument("--dsp", default="none", choices=["none", "dummy", "emu"],
                    help="Hatari's --dsp (default none; emu for a program that plays through the DSP)")
    a = ap.parse_args()
    if not a.prg and not a.start:
        ap.error("a PRG, or --start")
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
    name = os.path.basename(a.prg).upper() if a.prg else ""
    if a.prg:
        fatimg.name83(name)
        shutil.copy(a.prg, os.path.join(hd, name))
    for f in a.copy:
        dst = os.path.join(hd, os.path.basename(f.rstrip("/")).upper())
        (shutil.copytree if os.path.isdir(f) else shutil.copy)(f, dst)
    mintdir = os.path.join(hd, "mint")
    ver = [d for d in os.listdir(mintdir) if d[0].isdigit()][0]
    if a.mem_prot:
        ini = os.path.join(mintdir, ver, "mint.ini")
        with open(ini) as f:
            t = f.read()
        if "MEM_PROT=NO" not in t:
            sys.exit("run.py: no MEM_PROT=NO in %s" % ini)
        with open(ini, "w") as f:
            f.write(t.replace("MEM_PROT=NO", "MEM_PROT=YES"))
    first = "c:\\" + name
    if a.start:
        first = a.start
    img2 = None
    if a.ext2:
        shutil.copy(ext2_xfs(), os.path.join(mintdir, ver, "ext2.xfs"))
        img2 = os.path.join(work, "ext2.img")
        ext2img.build(os.path.abspath(a.ext2), img2)
    if a.later:
        shutil.copy(os.path.join(HERE, "build", "LATER.PRG"), os.path.join(hd, "LATER.PRG"))
        with open(os.path.join(hd, "LATER.TXT"), "w") as f:
            f.write("%d %s\r\n" % (a.later, first if a.start else "C:\\" + name))
        first = "c:\\LATER.PRG"
    with open(os.path.join(mintdir, ver, "xaaes", "xaaes.cnf"), "a") as f:
        for e in a.setenv:
            k, v = e.split("=", 1)
            f.write("setenv %s %s\n" % (k, v))
        for l in a.cnf:
            f.write(l + "\n")
        f.write("run %s\n" % first)
    img = os.path.join(work, "hd.img")
    fatimg.build(hd, img, a.size)

    env = dict(os.environ, DISPLAY=a.display, SDL_AUDIODRIVER="dummy")
    xvfb = machines.xvfb(a.display)    # works without xdpyinfo too (EUpdate found it)
    fifo = os.path.join(work, "cmd.fifo")
    logp = os.path.join(work, "hatari.log")
    log = open(logp, "w")
    t0 = time.time()
    h = subprocess.Popen(["stdbuf", "-oL", "-eL", "hatari", "-w", "--statusbar", "no", "--borders", "no",
                          "--zoom", "1", "--confirm-quit", "no", "--tos", a.tos,
                          *(["--machine", "falcon", "--monitor", "vga", "--dsp", a.dsp, "--memsize", "14",
                             *(["--mmu", "on"] if a.mem_prot else []),
                             "--ide-master", img, *(["--ide-slave", img2] if img2 else [])] if a.machine == "falcon" else
                            ["--machine", "tt", "--cpuclock", "32", "--fpu", "68882", "--memsize", "4", "--ttram", "16",
                             "--addr24", "off", "--monitor", "mono", *(["--mmu", "on"] if a.mem_prot else []),
                             "--ide-master", img, *(["--ide-slave", img2] if img2 else [])]),
                          "--fast-boot", "no", "--natfeats", "yes", "--conout", "2",
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
                    if len(p[1]) == 1 and p[1].isupper():
                        cmd("hatari-event keydown 42")
                    # a letter goes by its scan code: the character reaches a program
                    # as the plain letter, not as Control+letter (EPlay, 7 Nov 2026)
                    cmd("hatari-event keypress %s" % STSCAN.get(p[1].lower(), p[1]))
                    if len(p[1]) == 1 and p[1].isupper():
                        cmd("hatari-event keyup 42")
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
        if img2 and h.poll() is None and not stop["now"]:
            # MiNT writes its buffer cache out every few seconds: what a
            # program wrote on the ext2 drive reaches the image first
            time.sleep(EXT2_SETTLE)
        if h.poll() is None:
            h.terminate()
            try:
                h.wait(10)
            except subprocess.TimeoutExpired:
                h.kill()
                h.wait()
        if xvfb:
            machines.xvfb_stop(xvfb)
        log.close()
        out = os.path.join(work, "out")
        try:
            fatimg.extract(img, out)
        except Exception as e:  # the disk is for looking at, the run is what counts
            print("could not read the disk back: %s" % e)
        if img2:
            try:
                ext2img.extract(img2, os.path.join(work, "ext2"))
                with open(os.path.join(work, "ext2.ls"), "w") as f:
                    f.write("".join(n + "\n" for n in ext2img.ls(img2)))
            except Exception as e:
                print("could not read the ext2 disk back: %s" % e)
    if stop["shell"]:
        return RETRY
    print(sorted(os.listdir(shots)))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
