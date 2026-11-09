# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
# ClaudeST: the Atari program (st/) and its help (help/). Its bridge lives
# in AERA-Gateway (gateway/claude-st/, a read-only copy); its tests are
# ClaudeST's and run here too.
#
#   make            binary/CLAUDEST.PRG (make -C st; apt install gcc-m68k-linux-gnu)
#   make help       binary/CLAUDEST.HYP and .REF (needs hcp:
#                   common/tools/build-hcp.sh builds it; HCP=/path/to/hcp)
#   make test       the bridge's tests and the right-to-left layout's, and
#                   the 68000 build checked for word and long accesses at an
#                   odd stack offset (common/tools/oddaccess.py; The machines)
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
	cd gateway/claude-st/bridge && python3 -m unittest test_bridge
	$(MAKE) -C st test
	$(MAKE) -C st
	python3 common/tools/oddaccess.py st/claudest.elf

icons:
	python3 common/tools/appicon.py CLAUDEST --rsc icons/CLAUDEST.RSC --rscnv icons/CLSTNV.RSC \
	    --icn icons/CLAUDEST.ICN --mask icons/CLAUDSMK.ICN --headers st --docs docs

# a release's files: each one in dist/, and all of them in
# dist/CLAUDEST-x.y.zip (the icons in icons/), for the release workflow.
DIST    := binary/CLAUDEST.PRG binary/CLAUDEST.HYP binary/CLAUDEST.REF
ICONS   := icons/CLAUDEST.RSC icons/CLSTNV.RSC icons/CLAUDEST.ICN icons/CLAUDSMK.ICN
# the release: every file, the zip and its folder named in lowercase (the
# guidelines' 6.6: on MiNT's ext2 CLAUDEST.PRG and claudest.prg are two
# files); the build's own names stay as they are
lc = $(shell echo $(notdir $(1)) | tr A-Z a-z)
dist: all help
	rm -rf dist
	mkdir -p dist/zip/icons
	$(foreach f,$(DIST) $(ICONS),cp $(f) dist/$(call lc,$(f)) &&) true
	$(foreach f,$(DIST),cp $(f) dist/zip/$(call lc,$(f)) &&) true
	$(foreach f,$(ICONS),cp $(f) dist/zip/icons/$(call lc,$(f)) &&) true
	cd dist/zip && zip -q -9 -r ../claudest-$(VERSION_STR).zip .
	rm -rf dist/zip

clean:
	$(MAKE) -C st clean
	rm -rf binary dist

.PHONY: all help test icons dist clean
