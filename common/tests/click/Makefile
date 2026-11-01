# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
# click.c's state machine on the build machine, with AddressSanitizer:
# make test
CFLAGS := -O1 -g -Wall -Wextra -fsanitize=address,undefined -fno-sanitize-recover=all -Istub -I../../ui
build/test_click: test_click.c ../../ui/click.c ../../ui/click.h | build
	cc $(CFLAGS) -o $@ test_click.c ../../ui/click.c
build:
	mkdir -p $@
test: build/test_click
	./build/test_click
clean:
	rm -rf build
.PHONY: test clean
