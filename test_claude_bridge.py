# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""ClaudeST's bridge's own tests (claude-st/bridge/test_bridge.py, moved
here from ClaudeST with the bridge, Erez 9 Nov 2026), run with the
others: python3 -m unittest discover -s gateway -p 'test_*.py'"""

import os
import sys

BRIDGE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "claude-st", "bridge")


def load_tests(loader, tests, pattern):
    sys.path.insert(0, BRIDGE)
    import test_bridge
    return loader.loadTestsFromModule(test_bridge)
