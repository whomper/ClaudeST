# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
# Background work in slices (ui/bgwork.c) on the build machine: make test
CFLAGS := -O2 -Wall -Wextra -fsanitize=address,undefined -Istub -I../../ui
B      := build

$(B)/test_bgwork: test_bgwork.c ../../ui/bgwork.c ../../ui/bgwork.h | $(B)
	cc $(CFLAGS) -o $@ test_bgwork.c ../../ui/bgwork.c
$(B):
	mkdir -p $@
test: $(B)/test_bgwork
	./$(B)/test_bgwork
clean:
	rm -rf build
.PHONY: test clean

# BGT.PRG, the same in an emulator: make prg, then python3 run.py etos512us.img
CROSS   ?= m68k-linux-gnu-
GCCINC  := $(shell $(CROSS)gcc -print-file-name=include 2>/dev/null)
A       := ../../atari
U       := ../../ui
TCFLAGS := -m68000 -Os -fomit-frame-pointer -ffreestanding -fno-builtin -fno-pic -fno-pie \
           -fno-tree-loop-distribute-patterns -fno-store-merging -fdisable-tree-bswap -fno-asynchronous-unwind-tables \
           -Wall -nostdinc -isystem $(GCCINC) -I$(A)/include -I$(A) -I$(U) -DAES_PROFILE=40
TLDFLAGS := -m68000 -nostdlib -static -no-pie -Wl,--emit-relocs -Wl,-T,$(A)/link.ld \
            -Wl,--build-id=none -Wl,-z,noexecstack -Wl,--no-warn-rwx-segments
T       := $(B)/atari

prg: $(T)/BGT.PRG
$(T)/BGT.PRG: $(T)/bgt.elf ../../tools/elf2tos.py
	python3 ../../tools/elf2tos.py $< $@
$(T)/bgt.elf: $(T)/crt0.o $(T)/bgtest.o $(T)/bgwork.o $(T)/twin.o $(T)/av.o $(T)/gem.o $(T)/libc.o $(T)/tos.o $(T)/nf.o
	$(CROSS)gcc $(TLDFLAGS) -o $@ $^ -lgcc
$(T)/bgtest.o: bgtest.c | $(T)
	$(CROSS)gcc $(TCFLAGS) -c -o $@ $<
$(T)/%.o: $(U)/%.c | $(T)
	$(CROSS)gcc $(TCFLAGS) -c -o $@ $<
$(T)/%.o: $(A)/%.c | $(T)
	$(CROSS)gcc $(TCFLAGS) -c -o $@ $<
$(T)/%.o: $(A)/%.S | $(T)
	$(CROSS)gcc -m68000 -c -o $@ $<
$(T):
	mkdir -p $@
.PHONY: prg
