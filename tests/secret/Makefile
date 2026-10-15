# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
# passwords in NAME.INF (src/secret.c) on the build machine: make test
CFLAGS := -O2 -Wall -Wextra -fsanitize=address,undefined -Istub -I../../src
B      := build

$(B)/test_secret: test_secret.c ../../src/secret.c ../../src/secret.h stub/plat.h | $(B)
	cc $(CFLAGS) -o $@ test_secret.c ../../src/secret.c
$(B):
	mkdir -p $@
test: $(B)/test_secret
	./$(B)/test_secret
clean:
	rm -rf build
.PHONY: test clean
