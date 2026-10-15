# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
# pf_save and pf_load (src/futil.c) on the build machine: make test
CFLAGS := -O2 -Wall -Wextra -fsanitize=address,undefined -Istub
B      := build

$(B)/test_futil: test_futil.c ../../src/futil.c stub/plat.h | $(B)
	cc $(CFLAGS) -o $@ test_futil.c ../../src/futil.c
$(B):
	mkdir -p $@
test: $(B)/test_futil
	./$(B)/test_futil
clean:
	rm -rf build
.PHONY: test clean
