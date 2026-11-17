#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""The FireBee stand-in (machines.py's "FireBee": a Falcon with a 68060 on
EmuTOS, C:\\AUTO\\FBSTAND.PRG setting FireTOS's cookies) seen from inside,
by FBCHECK.PRG (Hatari's --auto):

  - the cookies: _CF_ there, _CPU 60, _SND without the DSP (bit 3);
  - ovl_flush() (src/ovl.c) takes its _CF_ path: Hatari's debugger counts
    the instructions run between FBCHECK's lines before and after it,
    2048 cpushl %bc,(%a0) (0xF4E8) and no movec cacr (0x4E7A 0002, which
    a ColdFire lacks) nor cpusha (0xF4F8), and nothing faults;
  - TLS.OVL's loader (atari/tls_ovl.c) asks for tlsfb.ovl, and PIC.OVL's
    (atari/pic_ovl.c) for nothing.

Three boots: FireBee as it starts (FBSTAND adds to the jar in place),
FireBee with C:\\AUTO\\AAFILL.PRG (FILLJAR.PRG, run before FBSTAND: the
AUTO folder goes in name order) leaving the jar no free slot, so that
FBSTAND makes a jar of 128 and stays resident; and a Falcon, the same
checks the other way round (no _CF_: TLS.OVL, pic.ovl, no cpushl). A
screenshot of the FireBee's desktop goes to WORK/firebee/shots.

    make && python3 run.py WORK
"""
import os, re, shutil, subprocess, sys, time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "tools"))
import machines  # noqa: E402

B = os.path.join(HERE, "build")
work = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else os.path.join(B, "work"))
failed = 0
OPS = {"cpushl": "$f4e8", "cpusha": "$f4f8", "movec": "$4e7a0002"}


def check(ok, what):
    global failed
    print("%s %s" % ("ok  " if ok else "FAIL", what), flush=True)
    failed += not ok


def boot(name, spec, fill=False, shot=False):
    w = os.path.join(work, name)
    shutil.rmtree(w, ignore_errors=True)
    shutil.rmtree(w + ".home", ignore_errors=True)
    hd = os.path.join(w, "hd")
    os.makedirs(os.path.join(hd, "AUTO"))
    shutil.copy(os.path.join(B, "FBCHECK.PRG"), hd)
    if fill:
        shutil.copy(os.path.join(B, "FILLJAR.PRG"), os.path.join(hd, "AUTO", "AAFILL.PRG"))
    m = machines.Machine(spec, hd)
    # each instruction counted by the debugger, a line a time it runs
    bp = os.path.join(w, "count.ini")
    with open(bp, "w") as f:
        for op in ("cpushl", "cpusha", "movec"):
            f.write("b (pc).%s = %s :trace\n" % ("l" if op == "movec" else "w", OPS[op]))
    fifo, shots = os.path.join(w, "cmd.fifo"), os.path.join(w, "shots")
    os.makedirs(shots)
    args = (["stdbuf", "-oL", "-eL", "hatari", "--fast-boot", "yes", "--natfeats", "yes", "--log-level", "warn",
             "--statusbar", "no", "--parse", bp, "--cmd-fifo", fifo, "--screenshot-dir", shots] + m.args() +
            ["--auto", "C:\\FBCHECK.PRG"])
    log = os.path.join(w, "hatari.log")
    p = subprocess.Popen(args, env=m.env(), stdout=open(log, "w"), stderr=subprocess.STDOUT)
    lines = []
    end = time.time() + 180
    while time.time() < end:
        time.sleep(0.5)
        lines = open(log, errors="replace").read().splitlines()
        if any(l.startswith("FBCHECK done") for l in lines) or p.poll() is not None:
            break
    if shot and p.poll() is None:
        time.sleep(2)                       # the desktop drawn again after FBCHECK
        with open(fifo, "w") as f:
            f.write("hatari-shortcut screenshot\n")
        time.sleep(2)
    p.kill()
    p.wait()
    what = m.describe()
    print("%s: %s" % (name, what), flush=True)
    return what, lines, sorted(os.listdir(shots))


def parse(lines):
    """the cookies, FBCHECK's lines and each instruction's count between
    'FBCHECK jar' and 'FBCHECK flushed'"""
    cookies, said, count, inside = {}, [], dict.fromkeys(OPS, 0), False
    for l in lines:
        m = re.match(r"FBCHECK cookie (.{4}) ([0-9a-f]{8})$", l)
        if m:
            cookies[m.group(1)] = int(m.group(2), 16)
        if l.startswith("FBCHECK jar"):
            inside = True
        if l.startswith("FBCHECK flushed"):
            inside = False
        if l.startswith("FBCHECK ") or l.startswith("FILLJAR "):
            said.append(l)
        elif inside:
            for op, code in OPS.items():
                if re.search(r"\(\s*pc\s*\)\s*\.\s*[wl]\s*=\s*\%s\b" % code, l):
                    count[op] += 1
    return cookies, said, count


def one(said, start):
    return next((l for l in said if l.startswith(start)), "")


for name, spec, fill in (("firebee", "FireBee", False), ("firebee-full", "FireBee", True),
                         ("falcon", "Falcon", False)):
    what, lines, shots = boot(name, spec, fill, shot=name == "firebee")
    cookies, said, count = parse(lines)
    for l in said:
        if not l.startswith("FBCHECK cookie"):
            print("    " + l)
    print("    cookies: " + " ".join("%s=%x" % kv for kv in sorted(cookies.items())))
    print("    between 'jar' and 'flushed': " + ", ".join("%s %d" % kv for kv in count.items()), flush=True)
    jar = re.match(r"FBCHECK jar at ([0-9a-f]+): (\d+) cookies, (\d+) of FILLJAR's, (\d+) slots", one(said, "FBCHECK jar"))
    check(one(said, "FBCHECK done") != "", "%s: FBCHECK ran to its end" % name)
    if spec == "FireBee":
        check("stand-in" in what and "EmuTOS" in what, "%s: describe() says EmuTOS and a stand-in" % name)
        check(cookies.get("_CF_", 0) != 0, "%s: _CF_ there" % name)
        check(cookies.get("_CPU") == 60, "%s: _CPU 60" % name)
        check("_SND" in cookies and not cookies["_SND"] & 8, "%s: _SND without the DSP (bit 3)" % name)
        check(count["cpushl"] == 2048 and count["cpusha"] == 0 and count["movec"] == 0 and
              one(said, "FBCHECK flushed") != "",
              "%s: ovl_flush() pushed each cache line (2048 cpushl, no cpusha, no movec cacr)" % name)
        check("FBCHECK open C:\\FBT\\tlsfb.ovl" in said and "FBCHECK open C:\\FBT\\TLS.OVL" not in said,
              "%s: tls_ovl_load() asks for tlsfb.ovl" % name)
        check(not any("pic.ovl" in l for l in said), "%s: pic_ovl_step() asks for no pic.ovl" % name)
        if fill:
            f = re.match(r"FILLJAR jar at ([0-9a-f]+): (\d+) added, (\d+) slots", one(said, "FILLJAR"))
            check(f is not None and int(f.group(2)) > 0 and jar is not None and jar.group(1) != f.group(1) and
                  jar.group(4) == "128" and jar.group(3) == f.group(2),
                  "%s: a full jar: FBSTAND made a new one of 128 with FILLJAR's %s cookies, and kept it" %
                  (name, f.group(2) if f else "?"))
        else:
            check(jar is not None and jar.group(4) != "128" and jar.group(3) == "0",
                  "%s: FBSTAND added to TOS's own jar" % name)
            check(len(shots) == 1, "%s: a screenshot of the desktop, %s" %
                  (name, os.path.join(work, name, "shots", shots[0]) if shots else "none"))
    else:
        check("_CF_" not in cookies and cookies.get("_CPU") == 30, "%s: no _CF_, _CPU 30" % name)
        check(count["cpushl"] == 0 and one(said, "FBCHECK flushed") != "",
              "%s: ovl_flush() without cpushl" % name)
        check("FBCHECK open C:\\FBT\\TLS.OVL" in said and "FBCHECK open C:\\FBT\\pic.ovl" in said,
              "%s: TLS.OVL and pic.ovl asked for" % name)
print("all passed" if not failed else "%d failed" % failed)
sys.exit(1 if failed else 0)
