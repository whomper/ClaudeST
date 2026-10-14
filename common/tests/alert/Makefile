# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Erez Yaary
# the shared alert() on the build machine: make test
CFLAGS := -O2 -Wall -Wextra -Istub -I../../ui -I../../src
build/test_alert: test_alert.c ../../ui/alert.c ../../src/bidi.c ../../ui/alert.h | build
	cc $(CFLAGS) -o $@ test_alert.c ../../ui/alert.c ../../src/bidi.c
build:
	mkdir -p $@
test: build/test_alert
	./build/test_alert
clean:
	rm -rf build
.PHONY: test clean
