#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# Bring common/ up to date with the shared AERA code
# (github.com/whomper/AERA-Atari, folder common/). Change shared code
# there, never here: a change made only in this copy drifts again.
set -e
cd "$(dirname "$0")/.."
git subtree pull --prefix=common https://github.com/whomper/AERA-Atari common --squash \
    -m "Update common/ from AERA-Atari"
