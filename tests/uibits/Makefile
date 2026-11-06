# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
# The small UI helpers (ui/fsel.c, ui/scrsize.c, ui/enlarge.c) on the build machine, against a
# stand-in AES and VDI, with AddressSanitizer: make test
CFLAGS := -O1 -g -Wall -Wextra -fsanitize=address,undefined -fno-sanitize-recover=all \
          -I../dialog/stub -I../../ui -I../../atari
B      := build
U      := ../../ui

$(B)/test_uibits: test_uibits.c $(U)/fsel.c $(U)/fsel.h $(U)/scrsize.c $(U)/scrsize.h $(U)/enlarge.c $(U)/enlarge.h | $(B)
	cc $(CFLAGS) -o $@ test_uibits.c $(U)/fsel.c $(U)/scrsize.c $(U)/enlarge.c
$(B):
	mkdir -p $@
test: $(B)/test_uibits
	./$(B)/test_uibits
clean:
	rm -rf $(B)
.PHONY: test clean
