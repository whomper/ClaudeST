#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""The family's test machines (guidelines, The machines): one name for
each machine and screen, so that every rig starts the same Hatari.

    machines.py list
    machines.py args NAME[:SCREEN] HD        the Hatari arguments, one a line
    machines.py run NAME[:SCREEN] HD [HATARI ARGS...]

HD is the folder Hatari shows as C:. From a rig:

    import machines
    m = machines.Machine("Falcon:256", hd)
    subprocess.Popen(["hatari"] + m.args() + ["--auto", "C:\\\\APP.PRG"], env=m.env())
    print(m.describe())         # "Falcon:256, TOS 4.04" - put it in the report

The TOS: Erez's own (TOS 1.04 from his ST, TOS 4.04 from his Falcon) when
the folder $AERA_TOS (~/.aera/tos) has them, else EmuTOS 1.4, fetched once
into ~/.cache/aera. AERA_TOS=emutos takes EmuTOS always. The TOS images
are never put in git (Atari's copyright), nor sent anywhere.

Each machine starts with its own settings: its own HOME for Hatari (so no
NVRAM is shared between rigs), the Falcon's screen mode in its NVRAM and,
for a real TOS 4, in C:\\NEWDESK.INF (TOS 4's desktop takes the mode from
there), unless the rig brought its own. Standard Python only.

The network: m.network() puts FAKESTNG.PRG (made with make in
tools/fakesting when it isn't there) in C:\\AUTO, starts serial_bridge.py
on the other end of the serial port and gives Hatari's serial arguments;
the Atari's TCP connections become this computer's. m.stop() ends the
bridge. It works on Erez's TOS too: FAKESTNG makes the cookie jar TOS
1.04 lacks and turns off its XON/XOFF.
"""
import hashlib
import os
import shutil
import struct
import subprocess
import sys
import urllib.request
import zipfile

# name: (what it is, Hatari's arguments, screens (the first is the default), real TOS file)
MACHINES = {
    "ST": ("Erez's 1040 ST: 68000 at 8 MHz, 4 MB",
           ["--machine", "st", "--memsize", "4"], ["mono", "med", "low"], "tos104us.img"),
    "ST-1": ("an ST with 1 MB, the smallest the family runs on (the ST editions)",
             ["--machine", "st", "--memsize", "1"], ["mono", "med", "low"], "tos104us.img"),
    "TT": ("a TT030: 68030 at 32 MHz, 68882, 4 MB ST RAM and 16 MB TT RAM (68030 code in fast RAM)",
           ["--machine", "tt", "--cpuclock", "32", "--fpu", "68882", "--memsize", "4",
            "--ttram", "16", "--addr24", "off"], ["ttmed", "mono", "med"], "tos306.img"),
    "Falcon": ("Erez's Falcon030 with a DFB1: 68030 and 68882 (Hatari's most, 32 MHz), "
               "14 MB ST RAM, 128 MB TT RAM, the DSP, VGA",
               ["--machine", "falcon", "--cpuclock", "32", "--fpu", "68882", "--memsize", "14",
                "--ttram", "128", "--addr24", "off", "--dsp", "emu", "--monitor", "vga"],
               ["16", "256"], "tos404.img"),
}

SCREENS = {
    "mono": ["--monitor", "mono"],                       # SM124, 640x400 (a TT: 1280x960)
    "med": ["--monitor", "rgb", "--tos-res", "med"],     # 640x200, 4 colours
    "low": ["--monitor", "rgb", "--tos-res", "low"],     # 320x200, 16 colours
    "ttmed": ["--monitor", "vga", "--tos-res", "ttmed"],  # TT 640x480, 16 colours
    "16": [],                                           # Falcon 640x480, 16 colours
    "256": [],                                          # Falcon 640x480, 256 colours
}
FALCON_MODE = {"16": 0x001A, "256": 0x001B}    # VGA | 80 columns | 4 or 8 planes

EMUTOS = ("https://downloads.sourceforge.net/project/emutos/emutos/1.4/emutos-512k-1.4.zip",
          "1ef7bd25f61bcfc66d19debc2f0ebb0d5e5ba811ccd7f0fddfcb105bb72d1663", "etos512us.img")

# C:\AUTO\BOOTC.PRG, for Erez's TOS 1.04: Hatari's C: is no hard disk to
# TOS 1.04, which then counts A: as the boot drive (_bootdev) and programs
# that read it look for their files on an empty floppy drive. A hard disk
# driver sets C: on the real ST; this does the same:
#   pea setboot(pc); move.w #38,-(sp); trap #14; addq.l #6,sp   Supexec
#   clr.w -(sp); trap #1                                          Pterm0
#   setboot: move.w #2,$446.w; rts
BOOTC = bytes.fromhex("487A000E" "3F3C0026" "4E4E" "5C8F" "4267" "4E41" "31FC00020446" "4E75")

# TOS 4's desktop file with the screen mode in the #E line's 5th and 6th
# values (Hatari's own TOS 2+ NEWDESK.INF otherwise)
NEWDESK = ("#a000000\r\n#b000000\r\n#c7770007000600070055200505552220770557075055507703111103\r\n"
           "#d                                             \r\n"
           "#K 4F 53 4C 00 46 42 43 57 45 58 00 00 00 00 00 00 00 00 00 00 00 00 00 52 00 00 4D 56 50 00 @\r\n"
           "#E 18 01 00 06 %02X %02X \r\n#Q 41 40 43 40 43 40 \r\n"
           "#W 00 00 02 0B 26 09 00 @\r\n#W 00 00 0A 0F 1A 09 00 @\r\n#W 00 00 0E 01 1A 09 00 @\r\n"
           "#W 00 00 04 07 26 0C 00 @\r\n#W 00 00 0C 0B 26 09 00 @\r\n#W 00 00 08 0F 1A 09 00 @\r\n"
           "#W 00 00 06 01 1A 09 00 @\r\n#N FF 04 000 @ *.*@ @ \r\n#D FF 01 000 @ *.*@ @ \r\n"
           "#G 03 FF 000 *.APP@ @ @ \r\n#G 03 FF 000 *.PRG@ @ @ \r\n#Y 03 FF 000 *.GTP@ @ @ \r\n"
           "#P 03 FF 000 *.TTP@ @ @ \r\n#F 03 04 000 *.TOS@ @ @ \r\n#M 00 01 00 FF C HARD DISK@ @ \r\n"
           "#M 00 00 00 FF A FLOPPY DISK@ @ \r\n#M 01 00 00 FF B FLOPPY DISK@ @ \r\n"
           "#T 00 03 02 FF   TRASH@ @ \r\n")


def emutos():
    """EmuTOS 1.4's 512 KB image (US), fetched once and checked"""
    cache = os.path.join(os.path.expanduser("~"), ".cache", "aera", "emutos-1.4")
    img = os.path.join(cache, EMUTOS[2])
    if not os.path.exists(img):
        os.makedirs(cache, exist_ok=True)
        z = os.path.join(cache, "emutos.zip")
        urllib.request.urlretrieve(EMUTOS[0], z)
        if hashlib.sha256(open(z, "rb").read()).hexdigest() != EMUTOS[1]:
            sys.exit("machines.py: %s is not the expected EmuTOS 1.4" % EMUTOS[0])
        with zipfile.ZipFile(z) as f:
            name = next(n for n in f.namelist() if n.endswith("/" + EMUTOS[2]))
            with f.open(name) as src, open(img, "wb") as dst:
                shutil.copyfileobj(src, dst)
    return img


def tos_version(path):
    v = struct.unpack(">H", open(path, "rb").read(4)[2:4])[0]
    return "%x.%02x" % (v >> 8, v & 0xFF)


class Machine:
    def __init__(self, spec, hd):
        name, _, screen = spec.partition(":")
        if name not in MACHINES:
            sys.exit("machines.py: no machine %r (%s)" % (name, ", ".join(MACHINES)))
        self.name, self.hd = name, os.path.abspath(hd)
        self.what, self.base, screens, real = MACHINES[name]
        self.screen = screen or screens[0]
        if self.screen not in screens:
            sys.exit("machines.py: %s has no screen %r (%s)" % (name, screen, ", ".join(screens)))
        tosdir = os.environ.get("AERA_TOS", os.path.join(os.path.expanduser("~"), ".aera", "tos"))
        path = os.path.join(tosdir, real)
        self.real = tosdir != "emutos" and os.path.exists(path)
        self.tos = path if self.real else emutos()
        self.home = self.hd.rstrip("/") + ".home"

    def describe(self):
        return "%s:%s, %s" % (self.name, self.screen,
                              "TOS " + tos_version(self.tos) if self.real else "EmuTOS 1.4")

    def args(self):
        """Hatari's arguments; also writes the machine's own settings"""
        os.makedirs(self.hd, exist_ok=True)
        cfg = os.path.join(self.home, ".config", "hatari")
        os.makedirs(cfg, exist_ok=True)
        nv = bytearray(50)                  # the TT's and Falcon's NVRAM, past the clock
        if self.name == "Falcon":
            mode = FALCON_MODE[self.screen]
            nv[14:16] = struct.pack(">H", mode)
            inf = os.path.join(self.hd, "NEWDESK.INF")
            if self.real and not os.path.exists(inf):
                with open(inf, "w", newline="") as f:
                    f.write(NEWDESK % (mode >> 8, mode & 0xFF))
        if self.real and self.name.startswith("ST"):
            os.makedirs(os.path.join(self.hd, "AUTO"), exist_ok=True)
            with open(os.path.join(self.hd, "AUTO", "BOOTC.PRG"), "wb") as f:
                f.write(struct.pack(">HIIIIIIH", 0x601A, len(BOOTC), 0, 0, 0, 0, 0, 0) + BOOTC + bytes(4))
        s = sum(nv[:48]) & 0xFF
        nv[48], nv[49] = ~s & 0xFF, s
        with open(os.path.join(cfg, "hatari.nvram"), "wb") as f:
            f.write(nv)
        return (["--tos", self.tos] + self.base + SCREENS[self.screen] +
                ["--harddrive", self.hd, "--gemdos-drive", "C", "--sound", "off",
                 "--confirm-quit", "no"])

    def network(self, rate=None):
        """FAKESTNG and its bridge; Hatari's serial arguments"""
        fs = os.path.join(os.path.dirname(os.path.abspath(__file__)), "fakesting")
        prg = os.path.join(fs, "FAKESTNG.PRG")
        if not os.path.exists(prg):
            subprocess.run(["make", "-s", "-C", fs], check=True)
        os.makedirs(os.path.join(self.hd, "AUTO"), exist_ok=True)
        shutil.copy(prg, os.path.join(self.hd, "AUTO", "FAKESTNG.PRG"))
        out, inp = self.hd.rstrip("/") + ".st_out", self.hd.rstrip("/") + ".st_in"
        for p in (out, inp):
            if os.path.exists(p):
                os.remove(p)
            os.mkfifo(p)
        self.bridge = subprocess.Popen([sys.executable, os.path.join(fs, "serial_bridge.py"), out, inp] +
                                       (["--rate", str(rate)] if rate else []),
                                       stderr=open(self.hd.rstrip("/") + ".bridge.log", "w"))
        return ["--rs232-out", out, "--rs232-in", inp]

    def stop(self):
        if getattr(self, "bridge", None):
            self.bridge.kill()
            self.bridge.wait()
            self.bridge = None

    def env(self, env=None):
        e = dict(os.environ if env is None else env)
        e["HOME"] = self.home
        e.setdefault("SDL_VIDEODRIVER", "dummy")
        e.setdefault("SDL_AUDIODRIVER", "dummy")
        return e


def main(argv):
    if len(argv) < 2 or argv[1] not in ("list", "args", "run"):
        sys.exit(__doc__)
    if argv[1] == "list":
        for name, (what, _, screens, real) in MACHINES.items():
            print("%-7s %s\n        screens: %s; Erez's TOS: %s" % (name, what, ", ".join(screens), real))
        return
    m = Machine(argv[2], argv[3])
    a = m.args()
    print(m.describe(), file=sys.stderr)
    if argv[1] == "args":
        print("\n".join(a))
    else:
        os.execvpe("hatari", ["hatari"] + a + argv[4:], m.env())


if __name__ == "__main__":
    main(sys.argv)
