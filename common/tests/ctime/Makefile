# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
# src/ctime.c's quick ways against the plain arithmetic, here and as
# 68000 code under qemu-m68k: make test
CROSS ?= m68k-linux-gnu-
B     := build
SRC   := test_ctime.c ../../src/ctime.c
CF    := -O2 -Wall -Wextra -I../../src

$(B)/test_host: $(SRC) | $(B)
	cc $(CF) -o $@ $(SRC)
$(B)/test_000: $(SRC) | $(B)
	$(CROSS)gcc -m68000 $(CF) -static -o $@ $(SRC)
$(B):
	mkdir -p $@
test: $(B)/test_host $(B)/test_000
	./$(B)/test_host
	qemu-m68k ./$(B)/test_000
clean:
	rm -rf $(B)
.PHONY: test clean
