# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
# src/inflate.c's streaming decoder against inflate_raw and zlib: make test
S      := ../../src
B      := build
CFLAGS := -O2 -g -Wall -Wextra -Wno-sign-compare -Wno-unused-parameter -fsanitize=address,undefined -I$(S) -Istub
$(B)/test_inflate: test_inflate.c $(S)/inflate.c $(S)/inflate.h $(S)/util.c | $(B)
	cc $(CFLAGS) -o $@ test_inflate.c $(S)/inflate.c $(S)/util.c -lz
$(B):
	mkdir -p $@
test: $(B)/test_inflate
	./$(B)/test_inflate
clean:
	rm -rf $(B)
.PHONY: test clean
