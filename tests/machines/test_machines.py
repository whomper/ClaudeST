#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""common/tools/machines.py without Hatari: the arguments, the machine's
own NVRAM and the Falcon's NEWDESK.INF. Stand-in TOS files, so nothing is
fetched.

    python3 common/tests/machines/test_machines.py
"""
import os
import struct
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "tools"))
import machines  # noqa: E402

failed = 0


def check(what, ok):
    global failed
    print("%s %s" % ("ok  " if ok else "FAIL", what))
    failed += not ok


with tempfile.TemporaryDirectory() as d:
    tos = os.path.join(d, "tos")
    os.makedirs(tos)
    for name, ver in (("tos104us.img", 0x104), ("tos404.img", 0x404), ("tos306.img", 0x306)):
        with open(os.path.join(tos, name), "wb") as f:
            f.write(struct.pack(">HH", 0x602E, ver) + bytes(60))
    os.environ["AERA_TOS"] = tos

    m = machines.Machine("ST-1", os.path.join(d, "st1"))
    a = m.args()
    check("ST-1: 1 MB, mono by default, Erez's TOS 1.04",
          a[a.index("--memsize") + 1] == "1" and "mono" in a and m.describe() == "ST-1:mono, TOS 1.04")
    check("ST-1: its own HOME", m.env()["HOME"] == os.path.join(d, "st1.home"))

    check("ST-1 with Erez's TOS 1.04: C:\\AUTO\\BOOTC.PRG makes C: the boot drive",
          os.path.exists(os.path.join(d, "st1", "AUTO", "BOOTC.PRG")))

    prg = open(os.path.join(d, "st1", "AUTO", "CLOCK.PRG"), "rb").read()
    t = time.localtime()
    check("ST-1: C:\\AUTO\\CLOCK.PRG sets today's date (Tsetdate)",
          prg[:2] == b"\x60\x1a" and struct.unpack(">HHHH", prg[28:36]) ==
          (0x3F3C, (t.tm_year - 1980) << 9 | t.tm_mon << 5 | t.tm_mday, 0x3F3C, 0x2B))

    m = machines.Machine("TT", os.path.join(d, "tt"))
    a = m.args()
    check("TT: 68030 code in 16 MB of TT RAM, TT medium", a[a.index("--ttram") + 1] == "16"
          and a[a.index("--tos-res") + 1] == "ttmed" and "--addr24" in a)

    for screen, mode in (("16", 0x1A), ("256", 0x1B)):
        hd = os.path.join(d, "f" + screen)
        m = machines.Machine("Falcon:" + screen, hd)
        a = m.args()
        nv = open(os.path.join(hd + ".home", ".config", "hatari", "hatari.nvram"), "rb").read()
        s = sum(nv[:48]) & 0xFF
        check("Falcon:%s: NVRAM mode %04x, checksum" % (screen, mode),
              struct.unpack(">H", nv[14:16])[0] == mode and nv[48] == (~s & 0xFF) and nv[49] == s)
        inf = open(os.path.join(hd, "NEWDESK.INF"), newline="").read()
        check("Falcon:%s: NEWDESK.INF's #E carries the mode" % screen, "#E 18 01 00 06 00 %02X \r\n" % mode in inf)
        check("Falcon:%s: 14 MB, 128 MB TT RAM, DSP" % screen, a[a.index("--memsize") + 1] == "14"
              and a[a.index("--ttram") + 1] == "128" and a[a.index("--dsp") + 1] == "emu")

    check("no BOOTC.PRG or CLOCK.PRG on the Falcon", not os.path.exists(os.path.join(d, "f16", "AUTO")))

    hd = os.path.join(d, "own")
    os.makedirs(hd)
    open(os.path.join(hd, "NEWDESK.INF"), "w").write("mine")
    machines.Machine("Falcon", hd).args()
    check("a rig's own NEWDESK.INF stays", open(os.path.join(hd, "NEWDESK.INF")).read() == "mine")

print("%d failed" % failed)
sys.exit(1 if failed else 0)
