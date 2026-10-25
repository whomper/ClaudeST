# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
# conn.c gives the machine a turn while a reply trickles in (2.2), on the build machine:
# make test
CFLAGS := -O1 -Wall -Wextra -Wno-unused-parameter -Istub -I../../src
B      := build

$(B)/test_connslice: test_connslice.c ../../src/conn.c ../../src/tls_none.c ../../src/util.c stub/plat.h
	@mkdir -p $(B)
	cc $(CFLAGS) -fsanitize=address,undefined -o $@ test_connslice.c ../../src/conn.c ../../src/tls_none.c ../../src/util.c
test: $(B)/test_connslice
	./$(B)/test_connslice
clean:
	rm -rf $(B)
.PHONY: test clean
