# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Erez Yaary
# The ST-Guide help: CLAUDEST.STG -> ../binary/CLAUDEST.HYP
# Needs HCP, ST-Guide's compiler: on the Atari HCP.TTP, elsewhere hcp from
# https://github.com/th-otto/hypview (make HCP=/path/to/hcp).
HCP ?= hcp

../binary/CLAUDEST.HYP: CLAUDEST.STG
	$(HCP) -q -o $@ $<

clean:
	rm -f ../binary/CLAUDEST.HYP

.PHONY: clean
