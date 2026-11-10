#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""KEYT.PRG (make) in Hatari: a key typed while a program is busy, as the AES gives it (gem.h key_ctrl,
key_shift; evnt_multi_'s kstate; Erez's task #139, 9 Nov 2026).

    run.py WORK [MACHINE] [--mode MODE] [--busy SECONDS]

MACHINE: ST:mono (TOS 1.04) by default; Falcon:16 (TOS 4.04). On EmuTOS 1.4 (AERA_TOS=emutos) only 12 of the 23
keys typed while busy arrive at all, with or without this (its own key queue); those that do have the right kstate,
so the count check fails there.
MODE: init (default: key_state_init() at the start), lazy (evnt_multi_'s first call starts it, after the busy
time), bios (init, and the BIOS buffer's entries are listed).
The same keys are typed twice: while the program is busy (the AES reads them afterwards, the keys long let go)
and while it waits for them. Checked for every key: Shift and Ctrl in kstate as typed (busy, init: from the
shift state the BIOS keeps with each key; idle: as the AES gives it), and key_shift() never says Shift for a
key typed without it. Where the state was lost (lazy, busy) key_shift() must still tell what the code tells:
Shift+letter, the cursor keys, Home, Insert and F1 to F10.

Measured on TOS 1.04 before evnt_multi_ kept it (all kstate 0 while busy): Shift+letter 0x1E41 (told by the
upper case); Shift+Up 0x4838, Down 0x5032, Left 0x4B34, Right 0x4D36, Home 0x4737, Insert 0x5230, F1 0x5400
(told); Ctrl+Shift+N and Ctrl+N both 0x310E, Ctrl+Left and Ctrl+Shift+Left both 0x7300, Delete, Tab, Return
and Backspace with Shift the same as without (not told).
"""
import os
import re
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "speed"))
import speed  # noqa: E402

# (xdotool key, ctrl, shift, the code's own telling of Shift: shift is in the code)
KEYS = [("a", 0, 0), ("shift+a", 0, 1), ("shift+n", 0, 1), ("ctrl+n", 1, 0), ("ctrl+shift+n", 1, 1),
        ("Up", 0, 0), ("shift+Up", 0, 1), ("shift+Down", 0, 1), ("shift+Left", 0, 1), ("shift+Right", 0, 1),
        ("Home", 0, 0), ("shift+Home", 0, 1), ("Insert", 0, 0), ("shift+Insert", 0, 1), ("ctrl+Left", 1, 0),
        ("ctrl+shift+Left", 1, 1), ("Delete", 0, 0), ("shift+Delete", 0, 1), ("shift+Tab", 0, 1),
        ("shift+Return", 0, 1), ("shift+F1", 0, 1), ("F1", 0, 0), ("shift+BackSpace", 0, 1)]
# the keys whose code says Shift (what key_shift can tell without kstate)
CODE = {"shift+a", "shift+n", "shift+Up", "shift+Down", "shift+Left", "shift+Right", "shift+Home", "shift+Insert",
        "shift+F1"}
fails = 0


def check(ok, what):
    global fails
    print("  %s %s" % ("ok  " if ok else "FAIL", what))
    fails += not ok


def opt(name, default):
    return sys.argv[sys.argv.index(name) + 1] if name in sys.argv else default


def lines(con):
    out = []
    for l in con.splitlines():
        m = re.match(r"KEYT kstate (\w+) key (\w+) ctrl (\d) shift (\d)", l)
        if m:
            out.append((int(m.group(1), 16), int(m.group(2), 16), int(m.group(3)), int(m.group(4))))
    return out


def main():
    a = [x for x in sys.argv[1:] if not x.startswith("--") and x not in ("init", "lazy", "bios")
         and not x.isdigit()]
    work = os.path.abspath(a[0])
    machine = a[1] if len(a) > 1 else "ST:mono"
    mode, busy = opt("--mode", "init"), int(opt("--busy", "25"))
    r = speed.Rig(work, machine, display=opt("--display", ":167"))
    try:
        r.put(os.path.join(HERE, "build", "KEYT.PRG"))
        r.write("BUSY.TXT", str(busy))
        r.write("MODE.TXT", mode)
        r.start("C:\\KEYT.PRG", ready="KEYT ready", boot=300)
        print("%s, mode %s, busy %d s" % (r.describe(), mode, busy))
        time.sleep(1)
        for k, _, _ in KEYS:
            r.key(k)
            time.sleep(0.3)
        time.sleep(busy + 3)
        for k, _, _ in KEYS:
            r.key(k)
            time.sleep(0.8)
        time.sleep(3)
        con = r.console()
        print("\n".join(l for l in con.splitlines() if l.startswith("KEYT") and not l.startswith("KEYT kstate")))
        got = lines(con)
        r.key("q")
    finally:
        r.stop()
    n = len(KEYS)
    check(len(got) >= 2 * n, "%d keys read, %d typed twice" % (len(got), n))
    if len(got) < 2 * n:
        sys.exit(1)
    for phase, name, rows in (("busy", "while busy", got[:n]), ("idle", "while waiting", got[n:2 * n])):
        for (k, ctrl, shift), (ks, code, kc, kshift) in zip(KEYS, rows):
            exact = phase == "idle" or mode != "lazy"
            if exact:
                check(bool(ks & 3) == bool(shift) and bool(ks & 4) == bool(ctrl),
                      "%s, %s: kstate %x (Shift %d, Ctrl %d wanted), key %04x" % (name, k, ks, shift, ctrl, code))
            elif k in CODE:
                check(bool(kshift), "%s, %s: key_shift tells it from the code %04x (kstate %x)" % (name, k, code, ks))
            check(not (kshift and not shift), "%s, %s: key_shift false for no Shift (%04x)" % (name, k, code))
            check(bool(kc) == bool(ctrl) or not exact, "%s, %s: key_ctrl %d" % (name, k, kc))
    print("all passed" if not fails else "%d failed" % fails)
    sys.exit(1 if fails else 0)


if __name__ == "__main__":
    main()
