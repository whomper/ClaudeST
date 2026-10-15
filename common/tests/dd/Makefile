# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
# drops (ui/dd.c) and the clipboard (ui/clip.c) on the build machine, the
# pipe and the files standing in: make test
CFLAGS := -O2 -Wall -Wextra -no-pie -fsanitize=address,undefined -Istub -I../../ui
B      := build

$(B)/test_dd: test_dd.c ../../ui/dd.c ../../ui/dd.h ../../ui/clip.c ../../ui/clip.h | $(B)
	cc $(CFLAGS) -o $@ test_dd.c ../../ui/dd.c ../../ui/clip.c
$(B):
	mkdir -p $@
test: $(B)/test_dd
	./$(B)/test_dd
clean:
	rm -rf $(B)
.PHONY: test clean
