# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
# PICSTEPS.PRG: PIC.OVL's check and chunky8's copies a step at a time
# (atari/pic_ovl.c), timed and compared: make, then
#   python3 run.py WORK MACHINE [--nvdi] [--before OLD_PIC.OVL]
CROSS   ?= m68k-linux-gnu-
CC      := $(CROSS)gcc
GCCINC  := $(shell $(CC) -print-file-name=include 2>/dev/null)
A       := ../../atari
S       := ../../src
CFLAGS  := -m68000 -Os -fomit-frame-pointer -ffreestanding -fno-builtin -fno-pic -fno-pie \
           -fno-tree-loop-distribute-patterns -fno-store-merging -fdisable-tree-bswap -fno-asynchronous-unwind-tables \
           -Wall -nostdinc -isystem $(GCCINC) -Istub -I$(A)/include -I$(A) -I$(S)
LDFLAGS := -m68000 -nostdlib -static -no-pie -Wl,--emit-relocs -Wl,-T,$(A)/link.ld \
           -Wl,--build-id=none -Wl,-z,noexecstack -Wl,--no-warn-rwx-segments
B       := build
RT      := $(B)/crt0.o $(B)/tos.o $(B)/libc.o $(B)/nf.o

all: $(B)/PICSTEPS.PRG
$(B)/%.PRG: $(B)/%.elf ../../tools/elf2tos.py
	python3 ../../tools/elf2tos.py $< $@
$(B)/PICSTEPS.elf: $(RT) $(B)/picsteps.o $(B)/pic_ovl.o $(B)/ovl.o $(B)/util.o $(B)/gem.o
	$(CC) $(LDFLAGS) -o $@ $^ -lgcc
$(B)/%.o: %.c stub/plat.h | $(B)
	$(CC) $(CFLAGS) -c -o $@ $<
$(B)/%.o: $(A)/%.c stub/plat.h | $(B)
	$(CC) $(CFLAGS) -c -o $@ $<
$(B)/%.o: $(S)/%.c stub/plat.h | $(B)
	$(CC) $(CFLAGS) -c -o $@ $<
$(B)/crt0.o: $(A)/crt0.S | $(B)
	$(CC) -m68000 -DSTACK_SIZE=8192 -c -o $@ $<
$(B)/nf.o: $(A)/nf.S | $(B)
	$(CC) -m68000 -c -o $@ $<
$(B):
	mkdir -p $@
clean:
	rm -rf $(B)
.PHONY: all clean
