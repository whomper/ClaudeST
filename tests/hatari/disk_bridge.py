#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""The demo bridge for disk_budget.sh: every artifact is a 50 KB file,
BIG.BIN, so Save to disk has something big enough to count its writes.
The arguments are claude_bridge.py's (the rig gives --tcp: ClaudeST
reaches it over STinG, through common's FAKESTNG.PRG, whose tunnel paces
what goes to the Atari)."""
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "gateway", "claude-st", "bridge"))
import backends  # noqa: E402
import claude_bridge  # noqa: E402

SIZE = 50 * 1024

backends.DemoBackend.artifact_name = lambda self, aid: "BIG.BIN"
backends.DemoBackend.artifact_file = lambda self, aid: ("BIG.BIN", bytes(i & 255 for i in range(SIZE)))


if __name__ == "__main__":
    claude_bridge.main()
