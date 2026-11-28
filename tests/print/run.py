#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""PRT.PRG (make prg) on one of the family's machines with the NVDI copy and WDIALOG: common/ui/print.c's
whole path (WDIALOG's settings, v_opnprn, the pages drawn, v_updwk/v_clrwk, v_clswk), by default three
pages to NVDI's picture driver (91), each checked for its size and ink. Hatari's printer port is captured
to WORK-printer.out. NVDI's printer drivers (PCL, ESC/P2...) don't open in Hatari: WDIALOG finds the
emulated port unusable and puts its metafile driver (31) in their place; they are tried on the real
printer.

    run.py WORK MACHINE [DRIVER [PAGES [DEVICE]]] [--no-wdialog]

--no-wdialog boots without WDIALOG.PRG: print_setup must give 2 (the default printer) and TOS's AES no
"Illegal AES function call" alert (EMail, 21 Nov 2026); the run must reach its end. With
PRT_ASSIGN21=META.SYS (device 21 pointed at the metafile driver in this run's ASSIGN.SYS only, CRLF and
the rest of the file kept) the default printer must open and print too (EPDF, 21 Nov 2026)
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "speed"))
import speed  # noqa: E402

a = [x for x in sys.argv[1:] if not x.startswith("--")]
if len(a) < 2:
    sys.exit(__doc__)
out = os.path.join(os.path.abspath(a[0]) + "-printer.out")
r = speed.Rig(a[0], a[1], nvdi=True, display=os.environ.get("PRT_DISPLAY", ":93"), extra=["--printer", out] + os.environ.get("PRT_EXTRA", "").split())
try:
    r.put(os.path.join(HERE, "build", "atari", "PRT.PRG"))
    with open(os.path.join(r.hd, "PRT.CFG"), "w") as f:
        f.write(" ".join((a[2:] + ["91", "3", "C:\\PAGE.IMG"][len(a) - 2:])[:3]) + "\n")
    wd = os.path.join(os.environ.get("AERA_NVDI", os.path.expanduser("~/.aera/nvdi/x")), "auto", "wdialog.prg")
    a21 = os.environ.get("PRT_ASSIGN21")       # device 21 pointed elsewhere in this run's ASSIGN.SYS only
    if a21:
        asg = os.path.join(r.hd, "ASSIGN.SYS")
        with open(asg, "rb") as f:
            t = f.read()
        import re
        t = re.sub(rb"(?m)^21 +\S+", b"21  " + a21.encode(), t)
        with open(asg, "wb") as f:
            f.write(t)
    nowd = "--no-wdialog" in sys.argv          # TOS's AES alone: print_setup must not ask it for the dialog
    if os.path.exists(wd) and not nowd:
        r.put(wd, "AUTO")                       # WDIALOG after NVDI: the print dialog
        import atexit                           # the NVDI copy: never left in a kept hd
        atexit.register(lambda: os.path.exists(os.path.join(r.hd, "AUTO", "WDIALOG.PRG")) and
                        os.remove(os.path.join(r.hd, "AUTO", "WDIALOG.PRG")))
    r.start("C:\\PRT.PRG", ready="PRT done", boot=600)
    print("print on", r.describe())
    for l in r.console().splitlines():
        if l.startswith("PRT "):
            print("  " + l[4:])
finally:
    r.stop()


def img_ink(path):
    """a GEM IMG file's width, height and how many of its bytes have ink (the picture driver's pages)"""
    d = open(path, "rb").read()
    hl, pat, w, h = (int.from_bytes(d[o:o + 2], "big") for o in (2, 6, 12, 14))
    bpl, i, rows, ink = (w + 7) // 8, hl * 2, 0, 0
    while rows < h and i < len(d):
        line, rep = bytearray(), 1
        while len(line) < bpl and i < len(d):
            c = d[i]
            if c == 0 and d[i + 1] == 0:
                rep, i = d[i + 3], i + 4
            elif c == 0:
                line += d[i + 2:i + 2 + pat] * d[i + 1]
                i += 2 + pat
            elif c == 0x80:
                line += d[i + 2:i + 2 + d[i + 1]]
                i += 2 + d[i + 1]
            else:
                line += (b"\xff" if c & 0x80 else b"\x00") * (c & 0x7f)
                i += 1
        ink += rep * sum(1 for b in line[:bpl] if b)
        rows += rep
    return w, h, ink


bad = 0
log = r.console()
if nowd:                                        # the default printer, no dialog and no AES alert
    # the open itself only where 21 is a driver Hatari opens (PRT_ASSIGN21=META.SYS: the metafile);
    # NVDI's printer drivers (PCL.SYS, device 21) don't open here
    ok = "PRT setup 2" in log and "PRT done" in log and (not a21 or "PRT open 1" in log)
    print("ok: no WDIALOG, the default printer (setup 2)" if ok else "FAIL: no WDIALOG: %s" %
          [l for l in log.splitlines() if l.startswith("PRT setup")])
    sys.exit(0 if ok else 1)
if (a[2:3] or ["91"])[0] == "91":                  # the picture driver: one IMG a page, each with the page on it
    want = int((a[3:4] or ["3"])[0])
    names = sorted(n for n in os.listdir(r.hd) if n.upper().startswith("PAGE") and n.upper().endswith(".IMG"))
    for n in names:
        w, h, ink = img_ink(os.path.join(r.hd, n))
        print("  %s %dx%d, %d bytes of ink" % (n, w, h, ink))
        bad += ink == 0
    if len(names) != want or bad:
        print("FAIL: %d pages printed, %d asked for, %d empty" % (len(names), want, bad))
        sys.exit(1)
    print("ok: %d pages" % want)
print("printer port:", os.path.getsize(out) if os.path.exists(out) else "nothing", "bytes")
