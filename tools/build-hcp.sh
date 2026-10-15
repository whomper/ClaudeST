#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
# Builds hcp, ST-Guide's help compiler, from Thorsten Otto's hypview (a
# fixed commit), for `make help` in the release workflows and on a new
# machine. Ubuntu doesn't package it.
#
#   common/tools/build-hcp.sh DIR        leaves DIR/hcp
#
# Needs git, a C compiler, autoconf, automake, libtool, gettext, autopoint
# and pkg-config (Debian/Ubuntu: apt install those names).
set -e
DIR=${1:?usage: build-hcp.sh DIR}
REV=44f87206970f7862ac0a6c9919a62625cc2e5490
mkdir -p "$DIR"
DIR=$(cd "$DIR" && pwd)
SRC="$DIR/hypview"
if [ ! -d "$SRC/.git" ]; then
  git init -q "$SRC"
  git -C "$SRC" remote add origin https://github.com/th-otto/hypview
fi
git -C "$SRC" fetch -q --depth 1 origin "$REV"
git -C "$SRC" checkout -q FETCH_HEAD
cd "$SRC"
./autogen.sh >/dev/null 2>&1
./configure --disable-gtk --without-gtk --without-x >/dev/null 2>&1 || ./configure >/dev/null
cd hyp
if ! make hcp >/dev/null 2>&1; then
  # the link line names include/linux/libc.so, which isn't a library on
  # a Linux build machine: link without it
  cmd=$(make -n hcp 2>/dev/null | grep -E '(^| )-o hcp( |$)' | tail -1)
  eval "$(echo "$cmd" | sed 's#[^ ]*include/linux/libc\.so##g; s#[^ ]*/libc\.so[^ ]*##g')"
fi
cp hcp "$DIR/hcp"
"$DIR/hcp" --version 2>&1 | head -1
