# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
# ui/scrpens.c (the screen's colours by hardware register) on the build
# machine, against a stand-in VDI, with AddressSanitizer: make test
CFLAGS := -O1 -g -Wall -Wextra -fsanitize=address,undefined -fno-sanitize-recover=all \
          -Istub -I../../ui
build/test_scrpens: test_scrpens.c ../../ui/scrpens.c ../../ui/scrpens.h stub/gem.h | build
	cc $(CFLAGS) -o $@ test_scrpens.c ../../ui/scrpens.c
build:
	mkdir -p $@
test: build/test_scrpens
	./build/test_scrpens
clean:
	rm -rf build
.PHONY: test clean
