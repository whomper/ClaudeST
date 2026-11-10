# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
# src/translate.c, the client of the Pi's translation service, against a scripted stand-in
# network (the plat.h of tests/connslice): make test
CFLAGS := -O1 -Wall -Wextra -Wno-unused-parameter -I../connslice/stub -I../../src
B      := build
SRC    := ../../src/translate.c ../../src/conn.c ../../src/tls_none.c ../../src/util.c ../../src/charset.c

$(B)/test_translate: test_translate.c $(SRC) ../../src/translate.h
	@mkdir -p $(B)
	cc $(CFLAGS) -fsanitize=address,undefined -o $@ test_translate.c $(SRC)
test: $(B)/test_translate
	./$(B)/test_translate
clean:
	rm -rf $(B)
.PHONY: test clean
