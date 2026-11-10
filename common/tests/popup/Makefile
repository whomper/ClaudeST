# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
# ui/popup.c (the two label contracts), ui/tooltip.c and ui/theme.c and ui/arrowed.c on the host, against a stand-in AES and drawing
# that record what they are asked, with AddressSanitizer and UBSan: make test
# make size: tooltip.o and popup.o for the 68000 with the family's flags, -Werror, oddaccess.py
B      := build
CFLAGS := -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-sanitize-recover=all \
          -Istub -I. -I../../ui -I../../atari
COMMON := stub.c stub.h stub/ui.h stub/tos.h

test: $(B)/test_popup $(B)/test_tooltip $(B)/test_theme $(B)/test_arrowed
	./$(B)/test_popup
	./$(B)/test_tooltip
	./$(B)/test_theme
	./$(B)/test_arrowed
$(B)/test_popup: test_popup.c $(COMMON) ../../ui/popup.c ../../ui/popup.h | $(B)
	cc $(CFLAGS) -o $@ test_popup.c stub.c ../../ui/popup.c
$(B)/test_tooltip: test_tooltip.c $(COMMON) ../../ui/tooltip.c ../../ui/tooltip.h | $(B)
	cc $(CFLAGS) -o $@ test_tooltip.c stub.c ../../ui/tooltip.c
$(B)/test_theme: test_theme.c $(COMMON) ../../ui/theme.c ../../ui/theme.h | $(B)
	cc $(CFLAGS) -o $@ test_theme.c stub.c ../../ui/theme.c
$(B)/test_arrowed: test_arrowed.c $(COMMON) ../../ui/arrowed.c ../../ui/arrowed.h | $(B)
	cc $(CFLAGS) -o $@ test_arrowed.c stub.c ../../ui/arrowed.c
$(B):
	mkdir -p $@
clean:
	rm -rf $(B)

CROSS   ?= m68k-linux-gnu-
GCCINC  := $(shell $(CROSS)gcc -print-file-name=include 2>/dev/null)
A       := ../../atari
U       := ../../ui
TCFLAGS := -m68000 -Os -fomit-frame-pointer -ffreestanding -fno-builtin -fno-pic -fno-pie \
           -fno-tree-loop-distribute-patterns -fno-store-merging -fdisable-tree-bswap -fno-asynchronous-unwind-tables \
           -Wall -Wextra -Werror -nostdinc -isystem $(GCCINC) -I$(A)/include -I$(A) -Istub -I$(U)
size: | $(B)
	for f in popup tooltip theme arrowed; do $(CROSS)gcc $(TCFLAGS) -c -o $(B)/$$f.o $(U)/$$f.c || exit 1; done
	$(CROSS)size $(B)/popup.o $(B)/tooltip.o $(B)/theme.o $(B)/arrowed.o
	python3 ../../tools/oddaccess.py $(B)/popup.o $(B)/tooltip.o $(B)/theme.o $(B)/arrowed.o
.PHONY: test clean size
