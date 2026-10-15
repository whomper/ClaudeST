# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
# the shared menu bar builder on the build machine, with AddressSanitizer:
# make test
CFLAGS := -O1 -g -Wall -Wextra -fsanitize=address,undefined -fno-sanitize-recover=all \
          -I../../ui -I../../atari
build/test_mbar: test_mbar.c ../../ui/mbar.c ../../ui/mbar.h | build
	cc $(CFLAGS) -o $@ test_mbar.c ../../ui/mbar.c
build:
	mkdir -p $@
test: build/test_mbar
	./build/test_mbar
clean:
	rm -rf build
.PHONY: test clean
