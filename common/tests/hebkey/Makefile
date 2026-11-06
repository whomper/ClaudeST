# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
# hebkey.c on the build machine, with AddressSanitizer:
# make test
CFLAGS := -O1 -g -Wall -Wextra -fsanitize=address,undefined -fno-sanitize-recover=all -Istub -I../../ui
build/test_hebkey: test_hebkey.c ../../ui/hebkey.c ../../ui/hebkey.h stub/gem.h | build
	cc $(CFLAGS) -o $@ test_hebkey.c ../../ui/hebkey.c
build:
	mkdir -p $@
test: build/test_hebkey
	./build/test_hebkey
clean:
	rm -rf build
.PHONY: test clean
