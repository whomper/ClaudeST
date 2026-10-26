# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
# src/util.c's str_istr against the plain way: make test
B := build
$(B)/istr.c: ../../src/util.c | $(B)
	sed -n '/^const char \*str_istr(const char \*h, const char \*n)$$/,/^}$$/p' $< > $@
$(B)/test_istr: test_istr.c $(B)/istr.c
	cc -O2 -Wall -Wextra -fsanitize=address,undefined -o $@ test_istr.c
$(B):
	mkdir -p $@
test: $(B)/test_istr
	./$(B)/test_istr
clean:
	rm -rf $(B)
.PHONY: test clean
