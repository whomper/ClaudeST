# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
# ClaudeST: the Atari program (st/), its help (help/) and the Pi bridge
# (bridge/, pi/).
#
#   make            binary/CLAUDEST.PRG (make -C st; apt install gcc-m68k-linux-gnu)
#   make help       binary/CLAUDEST.HYP and .REF (needs hcp:
#                   common/tools/build-hcp.sh builds it; HCP=/path/to/hcp)
#   make test       the bridge's tests and the right-to-left layout's
#   make icons      the icon again, from common/icons/CLAUDEST.txt: icons/, st/icon*.h, docs/icon*.png
#   make dist       a release's files in dist/: each file, and CLAUDEST-x.y.zip
#
# binary/ is what make builds, never committed: releases carry the
# program (.github/workflows/release.yml). The version is VERSION in
# st/claudest.c: the last release, which every build shows as it is.
# tools/bump.py starts a release.

HCP     ?= hcp
VERSION := $(shell sed -n 's/^\#define VERSION "\(.*\)"/\1/p' st/claudest.c)
VERSION_STR := $(VERSION)

all:
	$(MAKE) -C st

help:
	$(MAKE) -C help HCP=$(HCP)

test:
	cd bridge && python3 -m unittest test_bridge
	$(MAKE) -C st test

icons:
	python3 common/tools/appicon.py CLAUDEST --rsc icons/CLAUDEST.RSC --rscnv icons/CLSTNV.RSC \
	    --icn icons/CLAUDEST.ICN --mask icons/CLAUDSMK.ICN --headers st --docs docs

# a release's files: each one in dist/, and all of them in
# dist/CLAUDEST-x.y.zip (the icons in icons/), for the release workflow.
DIST    := binary/CLAUDEST.PRG binary/CLAUDEST.HYP binary/CLAUDEST.REF
ICONS   := icons/CLAUDEST.RSC icons/CLSTNV.RSC icons/CLAUDEST.ICN icons/CLAUDSMK.ICN
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
