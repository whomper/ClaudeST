# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
# src/charset.c: cs_to_atari's two-byte UTF-8 fast path gives uni_to_atari's answer for every character: make test
CFLAGS := -O1 -g -Wall -Wextra -Wno-unused-parameter -fsanitize=address,undefined -I../../src
B      := build

$(B)/test_charset: test_charset.c ../../src/charset.c ../../src/charset.h
	@mkdir -p $(B)
	cc $(CFLAGS) -o $@ test_charset.c ../../src/charset.c
test: $(B)/test_charset
	./$(B)/test_charset
clean:
	rm -rf $(B)
.PHONY: test clean
