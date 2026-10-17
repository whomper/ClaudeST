# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
# the shared alert() on the build machine, with AddressSanitizer: make test
CFLAGS := -O1 -g -Wall -Wextra -fsanitize=address,undefined -fno-sanitize-recover=all \
          -I../../ui -I../../src -I../../atari
build/test_alert: test_alert.c ../../ui/alert.c ../../src/bidi.c ../../ui/alert.h ../../ui/dialog.h | build
	cc $(CFLAGS) -o $@ test_alert.c ../../ui/alert.c ../../src/bidi.c
build:
	mkdir -p $@
test: build/test_alert
	./build/test_alert
clean:
	rm -rf build
.PHONY: test clean
