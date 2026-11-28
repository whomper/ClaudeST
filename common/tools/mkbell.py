#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""mkbell.py OUT.c: the family's bell (ui/bell.c) for DMA sound (STE, TT,
Falcon) as a C array, made here from a bell's partials, so nothing recorded
goes into an app. An app's Makefile makes it at build time.

A soft bell (decided 27 Nov 2026: soothing, not harsh): E5 (659 Hz) with
its hum an octave below, a quiet fifth and octave above, and two faint
upper partials. Each partial dies away on its own, the upper ones first,
as a struck bell does; a slight detuned copy of the prime gives the
slow beat of a real bell. It starts with an 8 ms rise (no click), ends
with a 300 ms fade to silence, and peaks at a quarter of full scale,
-12 dBFS (the family's rule: the level is the sample's, the user's volume
is never touched).
A small room's reverb (decided 27 Nov 2026: "a bit of reverb, so that
they have ambience"): four comb filters and two all-pass filters, a
Schroeder reverb, mixed in at about a third. 8-bit signed mono at
12517 Hz (the rate the STE, TT and Falcon share), 1.7 s; an even number
of bytes, as DMA needs."""
import math
import sys

RATE = 12517
SECS = 1.7
WET = 0.32
F0 = 659.26
# (ratio, amplitude, decay time constant in seconds)
PARTIALS = [(0.5, 0.20, 1.10), (1.0, 1.00, 0.90), (1.5, 0.10, 0.45), (2.0, 0.30, 0.55),
            (3.0, 0.08, 0.30), (4.16, 0.04, 0.18)]
BEAT = (1.0 + 1.6 / F0, 0.15, 0.90)		# the prime again, 1.6 Hz higher
PEAK = 0.25

n = int(RATE * SECS) & ~1
raw = []
for i in range(n):
    t = i / RATE
    v = 0.0
    for r, a, tau in PARTIALS + [BEAT]:
        v += a * math.exp(-t / tau) * math.sin(2 * math.pi * F0 * r * t)
    if t < 0.008:					# the rise: a raised cosine
        v *= 0.5 - 0.5 * math.cos(math.pi * t / 0.008)
    raw.append(v)


def comb(x, ms, g):
    d = int(RATE * ms / 1000)
    y = [0.0] * len(x)
    for i in range(len(x)):
        y[i] = x[i] + (g * y[i - d] if i >= d else 0.0)
    return y


def allpass(x, ms, g):
    d = int(RATE * ms / 1000)
    y = [0.0] * len(x)
    for i in range(len(x)):
        xd = x[i - d] if i >= d else 0.0
        yd = y[i - d] if i >= d else 0.0
        y[i] = -g * x[i] + xd + g * yd
    return y


combs = [comb(raw, ms, g) for ms, g in ((29.7, 0.78), (37.1, 0.76), (41.1, 0.74), (43.7, 0.72))]
wet = [sum(c[i] for c in combs) / 4 for i in range(n)]
wet = allpass(allpass(wet, 5.0, 0.7), 1.7, 0.7)
for i in range(n):
    t = i / RATE
    v = (1 - WET) * raw[i] + WET * wet[i]
    tail = SECS - t
    if tail < 0.3:					# the fade to silence
        v *= 0.5 - 0.5 * math.cos(math.pi * max(tail, 0) / 0.3)
    raw[i] = v
top = max(abs(v) for v in raw)
data = [max(-127, min(127, int(round(v / top * PEAK * 127)))) for v in raw]
data[-2:] = [0, 0]

with open(sys.argv[1], "w") as f:
    f.write("/* SPDX-License-Identifier: GPL-2.0-or-later\n * Copyright (C) 2026 AERA | Erez Yaary */\n")
    f.write("/* made by tools/mkbell.py: the new-mail bell, 8-bit signed mono at %d Hz */\n" % RATE)
    f.write("const signed char bell_snd[%d] __attribute__((aligned(2))) = {\n" % n)
    for i in range(0, n, 20):
        f.write("\t" + ",".join(str(x) for x in data[i:i + 20]) + ",\n")
    f.write("};\nconst long bell_len = %d;\n" % n)
