# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""Only the hub tests with other AERA programs and accessories (Erez, 8 Nov 2026;
guidelines 1.2). An app's own tests run the app alone, against stand-ins its repo
owns. The tools that fetch or load another AERA program (getrelease.py,
machines.py's accs(), the family check acc_all.py) call require() and stop
anywhere but in AERA-Atari, where common/ has standards/ beside it; an app's
copy of common/ has not."""
import os
import sys


def require(what):
    here = os.path.dirname(os.path.abspath(__file__))
    if not os.path.isdir(os.path.join(here, "..", "..", "standards")):
        sys.exit("%s: only the hub (AERA-Atari) tests with other AERA programs or accessories "
                 "(guidelines 1.2, Erez 8 Nov 2026); an app's tests run the app alone, "
                 "against a stand-in its repo owns" % what)
