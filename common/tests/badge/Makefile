# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
# ui/badge.c: its form on the build machine, with AddressSanitizer: make test;
# BADGECMP.PRG, badge_draw against the VDI's own drawing on the Atari:
#   make prg, then python3 run.py WORK MACHINE [--nvdi]
CFLAGS := -O1 -g -Wall -Wextra -fsanitize=address,undefined -fno-sanitize-recover=all \
          -DBADGE_HOST -I../../ui
build/test_badge: test_badge.c ../../ui/badge.c ../../ui/badge.h | build
	cc $(CFLAGS) -o $@ test_badge.c ../../ui/badge.c
build:
	mkdir -p $@
test: build/test_badge
	./build/test_badge
clean:
	rm -rf build
.PHONY: test clean

CROSS   ?= m68k-linux-gnu-
GCCINC  := $(shell $(CROSS)gcc -print-file-name=include 2>/dev/null)
A       := ../../atari
U       := ../../ui
TCFLAGS := -m68000 -Os -fomit-frame-pointer -ffreestanding -fno-builtin -fno-pic -fno-pie \
           -fno-tree-loop-distribute-patterns -fno-store-merging -fdisable-tree-bswap -fno-asynchronous-unwind-tables \
           -Wall -nostdinc -isystem $(GCCINC) -I$(A)/include -I$(A) -I$(U)
TLDFLAGS := -m68000 -nostdlib -static -no-pie -Wl,--emit-relocs -Wl,-T,$(A)/link.ld \
            -Wl,--build-id=none -Wl,-z,noexecstack -Wl,--no-warn-rwx-segments
T       := build/atari

prg: $(T)/BADGECMP.PRG
$(T)/BADGECMP.PRG: $(T)/badgecmp.elf ../../tools/elf2tos.py
	python3 ../../tools/elf2tos.py $< $@
$(T)/badgecmp.elf: $(T)/crt0.o $(T)/badgecmp.o $(T)/badge.o $(T)/gem.o $(T)/libc.o $(T)/tos.o $(T)/nf.o
	$(CROSS)gcc $(TLDFLAGS) -o $@ $^ -lgcc
$(T)/badgecmp.o: badgecmp.c $(U)/badge.h | $(T)
	$(CROSS)gcc $(TCFLAGS) -Wextra -Werror -c -o $@ $<
$(T)/badge.o: $(U)/badge.c $(U)/badge.h | $(T)
	$(CROSS)gcc $(TCFLAGS) -Wextra -Werror -c -o $@ $<
$(T)/%.o: $(A)/%.c | $(T)
	$(CROSS)gcc $(TCFLAGS) -c -o $@ $<
$(T)/%.o: $(A)/%.S | $(T)
	$(CROSS)gcc -m68000 -c -o $@ $<
$(T):
	mkdir -p $@
.PHONY: prg
