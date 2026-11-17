# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
# conn.c keeps the hosts that need ECDSA or every cipher across sessions: make test
CFLAGS := -O1 -Wall -Wextra -Wno-unused-parameter -Istub -I../../src
B      := build

$(B)/test_connhosts: test_connhosts.c ../../src/conn.c ../../src/util.c stub/plat.h
	@mkdir -p $(B)
	cc $(CFLAGS) -fsanitize=address,undefined -o $@ test_connhosts.c ../../src/conn.c ../../src/util.c
test: $(B)/test_connhosts
	./$(B)/test_connhosts 1
	./$(B)/test_connhosts 2
	./$(B)/test_connhosts 3
	./$(B)/test_connhosts 4
clean:
	rm -rf $(B)
.PHONY: test clean
