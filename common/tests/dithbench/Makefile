# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
# DITHBENCH.PRG: pic.c's dither on the CPU timed (dithbench.c), built from
# this tree's pic.c (DITHA.PRG, with pic_pal_soft's shares) and from
# another's (DITHB.PRG: BEFORE=path/to/common/src, its pic.c and pic.h):
#   make BEFORE=...; python3 run.py WORK MACHINE
CROSS   ?= m68k-linux-gnu-
CC      := $(CROSS)gcc
GCCINC  := $(shell $(CC) -print-file-name=include 2>/dev/null)
A       := ../../atari
S       := ../../src
BEFORE  ?= $(S)
CFLAGS  := -m68000 -Os -fomit-frame-pointer -ffreestanding -fno-builtin -fno-pic -fno-pie \
           -fno-tree-loop-distribute-patterns -fno-store-merging -fdisable-tree-bswap -fno-asynchronous-unwind-tables \
           -ffunction-sections -fdata-sections -Wall -nostdinc -isystem $(GCCINC) -Istub -I$(A)/include -I../../picovl/include -I$(A) \
           -DPIC_NO_PNG -DPIC_NO_GIF -I../../third_party/nsgif/include -I../../third_party/tjpgd
LDFLAGS := -m68000 -nostdlib -static -no-pie -Wl,--emit-relocs -Wl,-T,$(A)/link.ld -Wl,--gc-sections \
           -Wl,--build-id=none -Wl,-z,noexecstack -Wl,--no-warn-rwx-segments
B       := build
RT      := $(B)/crt0.o $(B)/tos.o $(B)/libc.o $(B)/nf.o $(B)/util.o

all: $(B)/DITHA.PRG $(B)/DITHB.PRG
$(B)/%.PRG: $(B)/%.elf ../../tools/elf2tos.py
	python3 ../../tools/elf2tos.py $< $@
# PAD=n: n bytes before the after build's pic.c, to set its loops at the
# before build's places modulo 256 (the 68030's cache: a loop moved costs
# a few ms either way, as any code moved does)
PAD     ?= 0
$(B)/pad.o: Makefile | $(B)
	printf '\t.section .text.dtbpad,"ax"\n\t.globl dtb_pad\ndtb_pad:\n\t.space %d\n' $(PAD) | $(CC) -m68000 -x assembler -c -o $@ -
$(B)/DITHA.elf: $(RT) $(B)/a.o $(B)/pad.o $(B)/pic_a.o
	$(CC) $(LDFLAGS) -Wl,--undefined=dtb_pad -o $@ $^ -lgcc
$(B)/DITHB.elf: $(RT) $(B)/b.o $(B)/pic_b.o
	$(CC) $(LDFLAGS) -o $@ $^ -lgcc
$(B)/a.o: dithbench.c $(S)/pic.h | $(B)
	$(CC) $(CFLAGS) -I$(S) -DDTB_SOFT -DDTB_NAME='"after"' -c -o $@ $<
$(B)/b.o: dithbench.c $(BEFORE)/pic.h | $(B)
	$(CC) $(CFLAGS) -I$(BEFORE) -I$(S) -DDTB_NAME='"before"' -c -o $@ $<
$(B)/pic_a.o: $(S)/pic.c $(S)/pic.h | $(B)
	$(CC) $(CFLAGS) -I$(S) -c -o $@ $<
$(B)/pic_b.o: $(BEFORE)/pic.c $(BEFORE)/pic.h | $(B)
	$(CC) $(CFLAGS) -I$(BEFORE) -I$(S) -c -o $@ $<
$(B)/%.o: $(A)/%.c stub/plat.h | $(B)
	$(CC) $(CFLAGS) -I$(S) -c -o $@ $<
$(B)/%.o: $(S)/%.c stub/plat.h | $(B)
	$(CC) $(CFLAGS) -I$(S) -c -o $@ $<
$(B)/crt0.o: $(A)/crt0.S | $(B)
	$(CC) -m68000 -DSTACK_SIZE=8192 -c -o $@ $<
$(B)/nf.o: $(A)/nf.S | $(B)
	$(CC) -m68000 -c -o $@ $<
$(B):
	mkdir -p $@
clean:
	rm -rf $(B)
.PHONY: all clean
