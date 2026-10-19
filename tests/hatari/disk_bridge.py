#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""The demo bridge for disk_budget.sh: every artifact is a 50 KB file,
BIG.BIN, so Save to disk has something big enough to count its writes.
What it sends goes at 19200 baud, as on the cable: a named pipe has no
speed, and Hatari drops what the Atari can't take in time. The arguments
are claude_bridge.py's."""
import os
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "bridge"))
import backends  # noqa: E402
import claude_bridge  # noqa: E402

SIZE = 50 * 1024

backends.DemoBackend.artifact_name = lambda self, aid: "BIG.BIN"
backends.DemoBackend.artifact_file = lambda self, aid: ("BIG.BIN", bytes(i & 255 for i in range(SIZE)))


def paced_write(self, data, write=claude_bridge.PipeLink.write):
    for i in range(0, len(data), 192):
        write(self, data[i:i + 192])
        time.sleep(0.1)                 # 1920 bytes a second: 19200 baud, 8N1


claude_bridge.PipeLink.write = paced_write

if __name__ == "__main__":
    claude_bridge.main()
