# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
# src/dirlist.c, the file dialog's folder, on the build machine, with
# AddressSanitizer: make test
CFLAGS := -O1 -g -Wall -Wextra -fsanitize=address,undefined -fno-sanitize-recover=all -I../../src
build/test_dirlist: test_dirlist.c ../../src/dirlist.c ../../src/dirlist.h | build
	cc $(CFLAGS) -o $@ test_dirlist.c ../../src/dirlist.c
build:
	mkdir -p $@
test: build/test_dirlist
	./build/test_dirlist
clean:
	rm -rf build
.PHONY: test clean
