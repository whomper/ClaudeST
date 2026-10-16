#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
# Bring gateway/ up to date with the AERA gateways for the Raspberry Pi
# (github.com/whomper/AERA-Gateway, branch gateway: its gateway/ folder).
# Change them there, never here: a change made only in this copy drifts
# again.
set -e
cd "$(dirname "$0")/.."
before=$(git rev-parse HEAD)
git subtree pull --prefix=gateway https://github.com/whomper/AERA-Gateway gateway --squash \
    -m "Update gateway/ from AERA-Gateway"
# git subtree leaves its squash commit unsigned (Unverified on GitHub):
# make it again signed, with the merge on top
[ ! -f common/tools/sign-subtree.sh ] || sh common/tools/sign-subtree.sh "$before"
