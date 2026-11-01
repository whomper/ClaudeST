# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
# OFFCMP.PRG, offdraw.c against drawing on the screen: make, then
#   python3 run.py WORK MACHINE
CROSS   ?= m68k-linux-gnu-
GCCINC  := $(shell $(CROSS)gcc -print-file-name=include 2>/dev/null)
A       := ../../atari
U       := ../../ui
TCFLAGS := -m68000 -Os -fomit-frame-pointer -ffreestanding -fno-builtin -fno-pic -fno-pie \
           -fno-tree-loop-distribute-patterns -fno-store-merging -fdisable-tree-bswap -fno-asynchronous-unwind-tables \
           -Wall -nostdinc -isystem $(GCCINC) -I$(A)/include -I$(A) -I$(U)
TLDFLAGS := -m68000 -nostdlib -static -no-pie -Wl,--emit-relocs -Wl,-T,$(A)/link.ld \
            -Wl,--build-id=none -Wl,-z,noexecstack -Wl,--no-warn-rwx-segments
B       := build

all: $(B)/OFFCMP.PRG
$(B)/OFFCMP.PRG: $(B)/offcmp.elf ../../tools/elf2tos.py
	python3 ../../tools/elf2tos.py $< $@
$(B)/offcmp.elf: $(B)/crt0.o $(B)/offcmp.o $(B)/offdraw.o $(B)/text.o $(B)/gem.o $(B)/libc.o $(B)/tos.o $(B)/nf.o
	$(CROSS)gcc $(TLDFLAGS) -o $@ $^ -lgcc
$(B)/offcmp.o: offcmp.c $(U)/offdraw.h $(U)/text.h | $(B)
	$(CROSS)gcc $(TCFLAGS) -c -o $@ $<
$(B)/%.o: $(U)/%.c $(U)/offdraw.h $(U)/text.h | $(B)
	$(CROSS)gcc $(TCFLAGS) -c -o $@ $<
$(B)/%.o: $(A)/%.c | $(B)
	$(CROSS)gcc $(TCFLAGS) -c -o $@ $<
$(B)/%.o: $(A)/%.S | $(B)
	$(CROSS)gcc -m68000 -c -o $@ $<
$(B):
	mkdir -p $@
clean:
	rm -rf $(B)
.PHONY: all clean
