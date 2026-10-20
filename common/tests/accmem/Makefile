# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
# An accessory's memory (ui/accmem.c) on the build machine: make test
CFLAGS := -O2 -Wall -Wextra -fsanitize=address,undefined -Istub -I../../ui
B      := build

$(B)/test_accmem: test_accmem.c ../../ui/accmem.c ../../ui/accmem.h | $(B)
	cc $(CFLAGS) -o $@ test_accmem.c ../../ui/accmem.c
$(B):
	mkdir -p $@
test: $(B)/test_accmem
	./$(B)/test_accmem
clean:
	rm -rf build
.PHONY: test clean
