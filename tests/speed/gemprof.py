# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""Where a GEM action's time goes, by function: an app in Hatari's CPU
profiler, from the key (or click) to its next event call.

An app's own rig (EPDF's tests/hatari/gemprof.py) uses it so:

    import gemprof                      (sys.path: common/tests/speed)
    p = gemprof.Profiler(WORK, "ST:mono", ELF)
    r = p.rig                           # the family's speed.Rig
    ... put the program (gemprof.with_symbols(ELF, PRG, OUT)), its files ...
    r.start("C:\\\\APP.PRG")
    p.begin()                           # once the app is at rest
    print(p.action("a line scrolled", lambda: r.key("Down")))

Each action: a breakpoint on the app's evnt_multi_ (its AES binding's
event call: common/atari/gem.c) set just before the key, so the profile
runs until the app asks for the next event; with after_event (the first
function the app calls when an event comes, EPDF's bg_tick), from there
on, so TOS's share is the stretch's alone; Hatari's
'profile cycles' gives every address's cycles, added up here by the
ELF's functions (shifted to where TOS loaded the text: the break's own
PC is evnt_multi_). TOS's ROM is one line, less the AES's idle 'stop'.
The milliseconds are at the machine's clock (8 MHz on an ST, 16 on a
Falcon or TT's 32), wait states included.

Hatari's command FIFO (--cmd-fifo) passes the debugger commands, and its
debugger reads what to do at the break from its stdin (a pipe here).
Each debugger command from the FIFO starts the profile again, hence the
breakpoint just before the action. GEMPROF_DEBUG adds debugger commands
at each break ("profile callers": who called the busiest functions).
(EPDF, 1 Nov 2026: what found scr_rows' and box_op's per-row overhead.)
"""
import bisect
import collections
import os
import re
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import speed  # noqa: E402

# the debugger reads its commands after a break from Hatari's stdin: the
# Rig starts Hatari without one, so its Popen is given a pipe
_Popen = subprocess.Popen


class _PipedHatari(_Popen):
    def __init__(self, *a, **k):
        if a and a[0] and "hatari" in a[0]:
            k["stdin"] = subprocess.PIPE
        _Popen.__init__(self, *a, **k)


speed.subprocess.Popen = _PipedHatari


def with_symbols(elf, prg, out):
    """the program with its ELF's symbols put in (common/tools' prgsyms.py
    where the app has one, else as it is: the profile is named from the
    ELF either way)"""
    tools = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "tools", "prgsyms.py")
    if os.path.exists(tools):
        subprocess.run([sys.executable, tools, elf, prg, out], check=True, stdout=subprocess.DEVNULL)
    else:
        import shutil
        shutil.copy(prg, out)


def _symbols(elf):
    nm = subprocess.run(["m68k-linux-gnu-nm", "-n", "--defined-only", elf], capture_output=True, text=True).stdout
    syms = [(int(l.split()[0], 16), l.split()[2]) for l in nm.splitlines()
            if len(l.split()) == 3 and l.split()[1] in "tTwW"]
    return syms, [a for a, _ in syms], dict((n, a) for a, n in syms)


class Profiler:
    def __init__(self, work, machine, elf, display=None, break_at="evnt_multi_", after_event=None, top=15):
        self.work = os.path.abspath(work)
        self.fifo = self.work.rstrip("/") + ".fifo"
        if os.path.exists(self.fifo):
            os.remove(self.fifo)
        self.rig = speed.Rig(os.path.join(self.work, machine.replace(":", "_")), machine,
                             display=display or os.environ.get("XDISPLAY", ":93"), extra=["--cmd-fifo", self.fifo])
        self.mhz = 8 if machine.startswith("ST") else 32 if machine.startswith("TT") else 16
        self.syms, self.addrs, self.by_name = _symbols(elf)
        self.break_at = break_at
        self.after_event = after_event      # the first function the app calls after its event call
        self.top = top
        self.base = None

    def debug(self, cmd):
        with open(self.fifo, "w") as f:
            f.write("hatari-debug " + cmd + "\n")

    def begin(self):
        """the program's symbols loaded and the profiler on (once, the app
        at rest)"""
        self.debug("symbols prg")
        time.sleep(1)
        self.debug("profile on")
        time.sleep(1)

    def action(self, what, do, settle=8, turn=1):
        """do() profiled until the app's next event call: a summary. With
        after_event, turn N profiles the Nth turn from the action instead
        of its first (what a timer turn after the key does: EPDF's line
        scrolled was 50 ms, the turn after it 80 on TOS 1.04)"""
        r = self.rig
        mark = len(r.console())
        extra = os.environ.get("GEMPROF_DEBUG", "")
        # with after_event, a first break there (the event's handling begins)
        # starts the profile again: it is then the stretch alone, TOS's share
        # in it too, not the second of idle and interrupts before the key.
        # The break at the event call is set at that first break, so it ends
        # the turn the profile began in (the Nth: after_event's Nth hit)
        if self.after_event:
            r.hatari.stdin.write(("b pc = %s :once\nc\n" % self.break_at +
                                  "profile cycles 100000\n%sc\n" % (extra + "\n" if extra else "")).encode())
        else:
            r.hatari.stdin.write(("profile cycles 100000\n%sc\n" % (extra + "\n" if extra else "")).encode())
        r.hatari.stdin.flush()
        if self.after_event:
            self.debug("b pc = %s %s:once" % (self.after_event, ":%d " % turn if turn > 1 else ""))
        else:
            self.debug("b pc = %s :once" % self.break_at)
        time.sleep(1)
        r.in_act = True
        do()
        time.sleep(settle)
        r.in_act = False
        r.release()
        r.still()
        got = r.console()[mark:]
        if self.base is None:
            # the first break's PC: after_event's when there is one
            m = re.search(r"CPU=\$([0-9a-f]+)", got)
            if m:
                self.base = int(m.group(1), 16) - self.by_name[self.after_event or self.break_at]
        gap = re.findall(r"AESGAP (\d+) ms", got)
        return "%s: the longest stretch %s ms\n%s" % (what, max(map(int, gap)) if gap else "<20", self.summary(got))

    def summary(self, text):
        agg = collections.Counter()
        rom = idle = 0
        base = self.base or 0
        for l in text.splitlines():
            m = re.match(r"0x([0-9a-f]+)\s+([\d.]+)%\s+(\d+)", l)
            if not m:
                continue
            a, c = int(m.group(1), 16), int(m.group(3))
            if a >= 0xE00000:
                if "stop " in l:
                    idle += c
                else:
                    rom += c
                continue
            i = bisect.bisect_right(self.addrs, a - base) - 1
            agg[self.syms[i][1] if i >= 0 else "?"] += c
        mine = sum(agg.values())
        ms = lambda c: c / (self.mhz * 1000.0)
        out = ["  the app %.1f ms, TOS %.1f ms (idle %.1f ms left out)%s" % (ms(mine), ms(rom), ms(idle),
               "" if self.after_event else ": TOS's from before the key on")]
        for k, v in agg.most_common(self.top):
            out.append("    %-30s %7.1f ms" % (k, ms(v)))
        return "\n".join(out)

    def stop(self):
        self.rig.stop()
