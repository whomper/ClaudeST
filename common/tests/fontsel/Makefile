# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
# the Font dialog's names (ui/fontsel.c, its part without GEM) on the build
# machine, with AddressSanitizer: make test
CFLAGS := -O1 -g -Wall -Wextra -fsanitize=address,undefined -fno-sanitize-recover=all \
          -DFONTSEL_NAMES_ONLY -I../../ui
build/test_fontsel: test_fontsel.c ../../ui/fontsel.c ../../ui/fontsel.h | build
	cc $(CFLAGS) -o $@ test_fontsel.c ../../ui/fontsel.c
build:
	mkdir -p $@
test: build/test_fontsel
	./build/test_fontsel
clean:
	rm -rf build
.PHONY: test clean
