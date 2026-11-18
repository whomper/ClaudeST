# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
# ui/textcells.c on the host, the VDI stood in for: make test
CFLAGS := -O1 -g -Wall -Wextra -Wno-unused-parameter -fsanitize=address,undefined -I.
B      := build

$(B)/test_textcells: test_textcells.c ../../ui/textcells.c ../../ui/textcells.h gem.h text.h
	@mkdir -p $(B)
	cc $(CFLAGS) -o $@ test_textcells.c ../../ui/textcells.c
test: $(B)/test_textcells
	./$(B)/test_textcells
clean:
	rm -rf $(B)
.PHONY: test clean
