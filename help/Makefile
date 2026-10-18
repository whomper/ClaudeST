# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
# The ST-Guide help: CLAUDEST.STG -> ../binary/CLAUDEST.HYP
# Needs HCP, ST-Guide's compiler: on the Atari HCP.TTP, elsewhere hcp from
# https://github.com/th-otto/hypview (make HCP=/path/to/hcp).
#
# CLAUDEST.STG is the source. The build works on a copy in build/: its
# @$VER and the About page take VERSION from ../st/claudest.c, so the
# help can't go stale, and its Main page becomes the AERA front page (guidelines 4.3), made by
# common/tools/helpfront.py from ../docs/front.json (standard Python only).
HCP ?= hcp
VERSION := $(shell sed -n 's/^.define VERSION "\(.*\)"/\1/p' ../st/claudest.c)
TODAY   := $(shell date +%d.%m.%Y)
FRONT   := ../docs/front.json ../docs/help/WINDOW.IMG ../common/help/CLAUDEST.IMG \
           ../common/tools/helpfront.py ../common/tools/gemimg.py
HYP     := $(abspath ../binary/CLAUDEST.HYP)

$(HYP): CLAUDEST.STG ../st/claudest.c $(FRONT)
	@test -n "$(VERSION)" || { echo "no VERSION in ../st/claudest.c" >&2; exit 1; }
	mkdir -p ../binary build
	sed -e 's/^@\$$VER: CLAUDEST\.HYP .*/@$$VER: CLAUDEST.HYP $(VERSION) ($(TODAY))/' \
	    -e 's/^  ClaudeST x\.y$$/  ClaudeST $(VERSION)/' CLAUDEST.STG > build/SOURCE.STG
	python3 ../common/tools/helpfront.py page ../docs/front.json build/SOURCE.STG build/CLAUDEST.STG
	cd build && $(HCP) -q -o $@ CLAUDEST.STG

clean:
	rm -rf build ../binary/CLAUDEST.HYP ../binary/CLAUDEST.REF

.PHONY: clean
