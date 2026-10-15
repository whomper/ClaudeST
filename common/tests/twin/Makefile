# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
# NAME.PRG and NAME.ACC (ui/twin.c) on the build machine: make test. Not
# position-independent, so a string's address fits a message's two words
CFLAGS := -O2 -Wall -Wextra -no-pie -fsanitize=address,undefined -Istub -I../../ui
B      := build

$(B)/test_twin: test_twin.c ../../ui/twin.c ../../ui/twin.h | $(B)
	cc $(CFLAGS) -o $@ test_twin.c ../../ui/twin.c
$(B):
	mkdir -p $@
test: $(B)/test_twin
	./$(B)/test_twin
clean:
	rm -rf build
.PHONY: test clean
