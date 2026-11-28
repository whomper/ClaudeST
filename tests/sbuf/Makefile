# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
# sb_printf (src/util.c) on the build machine: make test
CFLAGS := -O2 -Wall -Wextra -fsanitize=address,undefined -fno-sanitize-recover=all -Istub -I../../src
B      := build

$(B)/test_sbuf: test_sbuf.c ../../src/util.c ../../src/util.h stub/plat.h | $(B)
	cc $(CFLAGS) -o $@ test_sbuf.c ../../src/util.c
$(B):
	mkdir -p $@
test: $(B)/test_sbuf
	./$(B)/test_sbuf
clean:
	rm -rf build
.PHONY: test clean
