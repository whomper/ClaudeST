# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
# ui/textsel.c, the choosing of text in a read-only view, on the build machine: make test
B := build
$(B)/test_textsel: test_textsel.c ../../ui/textsel.c ../../ui/textsel.h
	@mkdir -p $(B)
	cc -O1 -Wall -Wextra -Wno-unused-parameter -fsanitize=address,undefined -I../../ui -o $@ test_textsel.c ../../ui/textsel.c
test: $(B)/test_textsel
	./$(B)/test_textsel
clean:
	rm -rf $(B)
.PHONY: test clean
