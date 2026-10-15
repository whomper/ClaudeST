#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""Start a release: raise VERSION in st/claudest.c and say what to do next.

A normal build never changes VERSION; only this does.
The release workflow (.github/workflows/release.yml), run by hand on main
or started by a tag vX.Y, builds the program and publishes it as a
GitHub Release.

    tools/bump.py          1.34 -> 1.35
    tools/bump.py major    1.34 -> 2.0
"""
import os
import re
import sys

SRC = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "st", "claudest.c")
text = open(SRC).read()
m = re.search(r'^#define VERSION "(\d+)\.(\d+)"$', text, re.M)
if not m:
    sys.exit("no #define VERSION in st/claudest.c")
major, minor = int(m.group(1)), int(m.group(2))
if sys.argv[1:] == ["major"]:
    version = "%d.0" % (major + 1)
elif sys.argv[1:] in ([], ["minor"]):
    version = "%d.%d" % (major, minor + 1)
else:
    sys.exit(__doc__)
open(SRC, "w").write(text[:m.start()] + '#define VERSION "%s"' % version + text[m.end():])
print("VERSION in st/claudest.c is now %s. Then:" % version)
print('  git commit -am "Release %s" && git push' % version)
print("  and run the Release workflow on main (Actions > Release > Run workflow)")
print('  with what is new; or: git tag -a v%s -m "What is new" && git push origin v%s'
      % (version, version))
