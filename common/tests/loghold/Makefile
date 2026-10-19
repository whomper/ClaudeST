# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
# conn_log_hold's buffer (the guidelines, 6.5), on the build machine:
# make test
CFLAGS := -O1 -Wall -Wextra -Wno-unused-parameter -Istub -I../../src
B      := build

$(B)/test_loghold: test_loghold.c ../../src/conn.c ../../src/tls_none.c ../../src/util.c stub/plat.h
	@mkdir -p $(B)
	cc $(CFLAGS) -fsanitize=address,undefined -Wl,--wrap=malloc -o $@ test_loghold.c ../../src/conn.c ../../src/tls_none.c ../../src/util.c
test: $(B)/test_loghold
	./$(B)/test_loghold
clean:
	rm -rf $(B)
.PHONY: test clean
