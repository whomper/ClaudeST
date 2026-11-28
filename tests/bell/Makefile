# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
# The bell (ui/bell.c) on the build machine: what it rings, what it leaves
# alone and what it puts back, on each kind of machine: make test
CFLAGS := -O2 -Wall -Wextra -fsanitize=address,undefined -DBELL_HOST -Istub -I../../ui
B      := build

$(B)/test_bell: test_bell.c ../../ui/bell.c ../../ui/bell.h | $(B)
	cc $(CFLAGS) -o $@ test_bell.c ../../ui/bell.c
$(B):
	mkdir -p $@
test: $(B)/test_bell
	./$(B)/test_bell
clean:
	rm -rf build
.PHONY: test clean
