#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""The demo bridge for long_field.sh: it logs each rename with the whole
name, so the rig reads back what was typed into the field. The arguments
are claude_bridge.py's."""
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "bridge"))
import backends  # noqa: E402
import claude_bridge  # noqa: E402

rename = backends.DemoBackend.rename


def logged_rename(self, kind, item_id, name):
    claude_bridge.log.info("renamed %s to [%s]", kind, name)
    return rename(self, kind, item_id, name)


backends.DemoBackend.rename = logged_rename

if __name__ == "__main__":
    claude_bridge.main()
