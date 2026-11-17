#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""The regression gate's rigs (the guidelines 1.2; AERA-Atari's
standards/TEST-MATRIX.md, the gate table; Erez, 17 Nov 2026): ClaudeST on
every configuration it runs on, on Erez's TOS images and NVDI, one boot a
row, each named as machines.py's describe() names it.

    python3 tests/hatari/gate.py WORK [--prg TIMED.PRG] [--rows 1,2,8 ...]

PRG is the timed build (make prof: binary/prof/CLAUDEST.PRG, built when
not given). The rows:

  1  Falcon-4:256 + NVDI (Erez's configuration)   speed.py, every check
  2  Falcon-4:16, TOS's VDI                       speed.py
  3  Falcon:65536 + NVDI                          speed.py
  4  TT:ttmed + NVDI, TT:ttmed TOS's VDI;         speed.py
     TT:ttlow                                     refused (320 wide)
  5  MiNT + XaAES: tt, ct60, --ext2               start, Settings, quit
  6  68060: FireBee's 68060 under plain TOS is    (row 7), and ct60 above
  7  FireBee:16, FireBee:256 (EmuTOS: a stand-in) speed.py
  8  ST:mono, ST:med, ST-1:mono; ST:low           speed.py; refused
  9  STE:mono                                     speed.py
 10  accessories                                  none: ClaudeST is a program only
 11  the network: STinG through the Gateway       every speed.py row (FAKESTNG)
 12  NatFeats off: no NATFEATS= in the INF        start, quit, no CRASH.LOG

Under MiNT ClaudeST has no STinG (the rig's FAKESTNG is a TOS program):
it starts offline, as on a MiNT machine without STinG. Each MiNT row
writes LOG=1, and CLAUDEST.LOG's last line says the quit got to the end.
"""
import argparse
import glob
import os
import shutil
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, os.path.join(ROOT, "common", "tests", "speed"))
sys.path.insert(0, os.path.join(ROOT, "common", "tools"))
import speed  # noqa: E402
import machines  # noqa: E402

TOS = os.path.expanduser(os.environ.get("AERA_TOS", "~/.aera/tos"))
LAST_STEP = "out of the AES"

SPEED_ROWS = [
    ("1", "Falcon-4:256", True), ("2", "Falcon-4:16", False), ("3", "Falcon:65536", True),
    ("4", "TT:ttmed", True), ("4", "TT:ttmed", False),
    ("7", "FireBee:16", False), ("7", "FireBee:256", False),
    ("8", "ST:mono", False), ("8", "ST:med", False), ("8", "ST-1:mono", False),
    ("9", "STE:mono", False),
]
REFUSED_ROWS = [("4", "TT:ttlow"), ("8", "ST:low")]
MINT_ROWS = [("5", "tt", []), ("5", "ct60", []), ("5", "falcon", ["--ext2"])]

results = []


def note(row, what, ok, detail=""):
    results.append((row, what, ok, detail))
    print("%s  row %-2s %s%s" % ("ok  " if ok else "FAIL", row, what, ("  " + detail) if detail else ""), flush=True)


def crash_logs(folder):
    return [p for p in glob.glob(os.path.join(folder, "**", "*"), recursive=True)
            if os.path.basename(p).upper() == "CRASH.LOG"]


def find(folder, name):
    for p in glob.glob(os.path.join(folder, "**", "*"), recursive=True):
        if os.path.basename(p).upper() == name:
            return p
    return None


def speed_row(work, row, machine, nvdi, prg):
    w = os.path.join(work, "r%s-%s%s" % (row, machine.replace(":", "_"), "-nvdi" if nvdi else ""))
    shutil.rmtree(w, ignore_errors=True)
    cmd = [sys.executable, os.path.join(HERE, "speed.py"), w, "--machine", machine, "--prg", prg]
    if nvdi:
        cmd.append("--nvdi")
    with open(w + ".log", "w") as f:
        code = subprocess.run(cmd, stdout=f, stderr=subprocess.STDOUT).returncode
    out = open(w + ".log", errors="replace").read()
    named = next((ln[len("speed on "):] for ln in out.splitlines() if ln.startswith("speed on ")), machine)
    fails = [ln.strip() for ln in out.splitlines() if ln.startswith("FAIL") or ln.rstrip().endswith("OVER")]
    note(row, named, code == 0, "; ".join(fails[:4]) or "speed rig: every check and time")


def refused_row(work, row, machine, prg):
    w = os.path.join(work, "r%s-%s" % (row, machine.replace(":", "_")))
    shutil.rmtree(w, ignore_errors=True)
    r = speed.Rig(w, machine, extra=["--drive-led", "no"])
    named_prg = os.path.join(w + "-p", "CLAUDEST.PRG")
    os.makedirs(os.path.dirname(named_prg), exist_ok=True)
    shutil.copy(prg, named_prg)
    r.put(named_prg)
    r.write("claudest.inf", "[OPTIONS]\nHOST=127.0.0.1\nPORT=2323\nNATFEATS=1\nLOG=1\n")
    try:
        r.start("C:\\CLAUDEST.PRG", ready="")
        time.sleep(15)
        r.still()
        alert = shutil.copy(r.shot("alert"), w + "-alert.png")
        r.key("Return")
        time.sleep(5)
        r.still()
        gone = shutil.copy(r.shot("after"), w + "-after.png")
        named = r.describe()
    finally:
        r.stop()
    differs = subprocess.run(["compare", "-metric", "AE", alert, gone, "null:"], capture_output=True, text=True)
    try:
        changed = int(float(differs.stderr.split()[0])) > 0
    except (ValueError, IndexError):
        changed = False
    crashes = crash_logs(r.hd)
    ok = changed and not crashes
    note(row, named, ok, "the screen-width alert, OK, back to the desktop%s" %
         ("" if not crashes else "; CRASH.LOG"))


def natfeats_off_row(work, prg):
    """row 12: Erez's configuration with no NATFEATS= (as a user's INF):
    starts, opens Settings, quits by ^Q, no CRASH.LOG, nothing on the
    emulator's console from ClaudeST"""
    w = os.path.join(work, "r12-natfeats-off")
    shutil.rmtree(w, ignore_errors=True)
    r = speed.Rig(w, "Falcon-4:256", nvdi=True, extra=["--drive-led", "no"])
    named_prg = os.path.join(w + "-p", "CLAUDEST.PRG")
    os.makedirs(os.path.dirname(named_prg), exist_ok=True)
    shutil.copy(prg, named_prg)
    r.put(named_prg)
    r.extra += r.m.network()
    r.write("claudest.inf", "[OPTIONS]\nHOST=127.0.0.1\nPORT=2323\nLOG=1\n")
    bridge = subprocess.Popen([sys.executable, os.path.join(HERE, "speed_bridge.py"), "--backend", "demo",
                               "--tcp", "127.0.0.1:2323"],
                              stdout=open(w + "-bridge.log", "w"), stderr=subprocess.STDOUT)
    try:
        r.start("C:\\CLAUDEST.PRG", ready="")
        time.sleep(30)
        r.still()
        shutil.copy(r.shot("started"), w + "-started.png")
        r.key("ctrl+comma")
        r.still()
        shutil.copy(r.shot("settings"), w + "-settings.png")
        r.key("Undo")
        r.still()
        r.key("ctrl+q")
        time.sleep(8)
        r.still()
        shutil.copy(r.shot("quit"), w + "-quit.png")
        con = r.console()
        named = r.describe()
    finally:
        r.stop()
        bridge.kill()
    log = find(r.hd, "CLAUDEST.LOG")
    last = open(log, errors="replace").read().strip().splitlines()[-1] if log else ""
    online = "session usage" in open(w + "-bridge.log", errors="replace").read()
    crashes = crash_logs(r.hd)
    ok = not crashes and "AESSTART" not in con and "AESGAP" not in con and last == LAST_STEP and online
    note("12", named + "; NatFeats off", ok,
         "online %s, quit's last step %r, console %s, CRASH.LOG %s" %
         ("yes" if online else "no", last, "silent" if "AES" not in con else "has lines",
          "none" if not crashes else "yes"))


def mint_row(work, row, machine, extra, prg):
    w = os.path.join(work, "r%s-mint-%s%s" % (row, machine, "-ext2" if extra else ""))
    shutil.rmtree(w, ignore_errors=True)
    files = w + "-files"
    shutil.rmtree(files, ignore_errors=True)
    os.makedirs(files)
    with open(os.path.join(files, "CLAUDEST.INF"), "w", newline="") as f:
        f.write("[OPTIONS]\r\nHOST=127.0.0.1\r\nPORT=2323\r\nNATFEATS=1\r\nLOG=1\r\n")
    shutil.copy(prg, os.path.join(files, "CLAUDEST.PRG"))
    tos = os.path.join(TOS, "tos306.img" if machine == "tt" else "tos404.img")
    cmd = [sys.executable, os.path.join(ROOT, "common", "tests", "xaaes", "run.py"), w, tos,
           os.path.join(files, "CLAUDEST.PRG"), "--copy", os.path.join(files, "CLAUDEST.INF"),
           "--machine", machine, "--nvdi", "--display", ":96",
           "--steps", "until AESSTART 300; wait 20; shot; ctrl 0x33; wait 8; shot; key 0x61; wait 6; "
                      "shot; ctrl 0x10; wait 15; shot"]
    if extra:
        ext = w + "-ext2src"
        shutil.rmtree(ext, ignore_errors=True)
        os.makedirs(ext)
        open(os.path.join(ext, "Notes.txt"), "w").write("a case-sensitive drive\n")
        cmd += ["--ext2", ext]
    with open(w + ".log", "w") as f:
        code = subprocess.run(cmd, stdout=f, stderr=subprocess.STDOUT, timeout=1500).returncode
    out = os.path.join(w, "out")
    log = find(out, "CLAUDEST.LOG")
    last = open(log, errors="replace").read().strip().splitlines()[-1] if log else ""
    crashes = crash_logs(out)
    hlog = open(os.path.join(w, "hatari.log"), errors="replace").read() if os.path.exists(
        os.path.join(w, "hatari.log")) else ""
    named = "MiNT 1.17 + XaAES, %s, %s, NVDI%s" % (
        {"tt": "TT030", "ct60": "CT60 stand-in (68040)", "falcon": "Falcon"}[machine],
        "TOS 3.06" if machine == "tt" else "TOS 4.04", ", an ext2 drive" if extra else "")
    ok = code == 0 and not crashes and last == LAST_STEP and "AESSTART" in hlog
    note(row, named, ok, "started, Settings opened and cancelled, ^Q: quit's last step %r%s" %
         (last, "" if not crashes else "; CRASH.LOG"))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("work")
    ap.add_argument("--prg", help="the timed build (default: make prof)")
    ap.add_argument("--rows", help="only these rows, e.g. 1,2,8")
    a = ap.parse_args()
    work = os.path.abspath(a.work)
    os.makedirs(work, exist_ok=True)
    prg = a.prg
    if not prg:
        subprocess.run(["make", "-s", "-C", ROOT, "prof"], check=True)
        prg = os.path.join(ROOT, "binary", "prof", "CLAUDEST.PRG")
    prg = os.path.abspath(prg)
    want = set(a.rows.split(",")) if a.rows else None

    def on(row):
        return want is None or row in want

    for row, m, nvdi in SPEED_ROWS:
        if on(row):
            speed_row(work, row, m, nvdi, prg)
    for row, m in REFUSED_ROWS:
        if on(row):
            refused_row(work, row, m, prg)
    for row, m, extra in MINT_ROWS:
        if on(row):
            mint_row(work, row, m, extra, prg)
    if on("12"):
        natfeats_off_row(work, prg)
    for row, why in (("6", "a 68060: the FireBee stand-in's 68060 under TOS (row 7) and the CT60 under MiNT (row 5); "
                           "TOS 4.04 itself runs no 68060"),
                     ("10", "ClaudeST has no accessory edition"),
                     ("11", "STinG through the Gateway is in every speed.py row (FAKESTNG and the bridge); "
                            "ClaudeST has no Direct connection")):
        if on(row):
            print("note  row %-2s %s" % (row, why))
    bad = [r for r in results if not r[2]]
    print("gate: %d rows run, %d failed" % (len(results), len(bad)))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
