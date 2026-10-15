# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
# ClaudeST: the Atari program (st/), its help (help/) and the Pi bridge
# (bridge/, pi/).
#
#   make            binary/CLAUDEST.PRG (make -C st; apt install gcc-m68k-linux-gnu)
#   make help       binary/CLAUDEST.HYP and .REF (needs hcp:
#                   common/tools/build-hcp.sh builds it; HCP=/path/to/hcp)
#   make test       the bridge's tests and the right-to-left layout's
#   make icons      the desktop icon again: icons/ (tools/icon)
#   make dist       a release's files in dist/: each file, and CLAUDEST-x.y.zip
#
# binary/ is what make builds, never committed: releases carry the
# program (.github/workflows/release.yml). The version is VERSION in
# st/claudest.c: the last release, which a release build (RELEASE=1)
# shows; others show "x.y-dev". tools/bump.py starts a release.

HCP     ?= hcp
VERSION := $(shell sed -n 's/^\#define VERSION "\(.*\)"/\1/p' st/claudest.c)
VERSION_STR := $(VERSION)$(if $(RELEASE),,-dev)

all:
	$(MAKE) -C st RELEASE=$(RELEASE)

help:
	$(MAKE) -C help HCP=$(HCP)

test:
	cd bridge && python3 -m unittest test_bridge
	$(MAKE) -C st test

icons:
	python3 tools/icon/make_rsc.py

# a release's files: each one in dist/, and all of them in
# dist/CLAUDEST-x.y.zip (the icons in icons/), for the release workflow.
# CLAUDEST.INF is the example settings file (st/CLAUDEST.INF).
DIST    := binary/CLAUDEST.PRG binary/CLAUDEST.HYP binary/CLAUDEST.REF st/CLAUDEST.INF
ICONS   := icons/CLAUDEST.RSC icons/CLAUDEST.ICN icons/CLAUDSMK.ICN
dist: all help
	rm -rf dist
	mkdir -p dist/zip/icons
	cp $(DIST) $(ICONS) dist/
	cp $(DIST) dist/zip/
	cp $(ICONS) dist/zip/icons/
	cd dist/zip && zip -q -9 -r ../CLAUDEST-$(VERSION_STR).zip .
	rm -rf dist/zip

clean:
	$(MAKE) -C st clean
	rm -rf binary dist

.PHONY: all help test icons dist clean
