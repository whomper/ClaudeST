# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
# tz_offset's kept zone-years against the rules (src/tz.c): make test
CFLAGS := -O2 -Wall -Wextra -fsanitize=address,undefined -I../../src -Istub
B      := build
SRC    := ../../src/tz.c ../../src/ctime.c ../../src/util.c

$(B)/test_tz: test_tz.c $(SRC) | $(B)
	cc $(CFLAGS) -o $@ test_tz.c $(SRC)
$(B):
	mkdir -p $@
test: $(B)/test_tz
	./$(B)/test_tz
clean:
	rm -rf build
.PHONY: test clean
