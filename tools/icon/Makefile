# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Erez Yaary
# RSCTEST.PRG: loads ../../binary/CLAUDEST.RSC and draws it (a check, not shipped)
CROSS  ?= m68k-linux-gnu-
CC     := $(CROSS)gcc
GCCINC := $(shell $(CC) -print-file-name=include 2>/dev/null)
COMMON := ../../common
CFLAGS := -m68000 -Os -fomit-frame-pointer -ffreestanding -fno-builtin -fno-pic -fno-pie \
          -fno-tree-loop-distribute-patterns -fno-store-merging -fno-asynchronous-unwind-tables -Wall \
          -nostdinc -isystem $(GCCINC) -I$(COMMON)/atari/include -I$(COMMON)/atari
LDFLAGS := -m68000 -nostdlib -static -no-pie -Wl,--emit-relocs -Wl,-T,$(COMMON)/atari/link.ld \
          -Wl,--build-id=none -Wl,-z,noexecstack -Wl,--no-warn-rwx-segments

RSCTEST.PRG: rsctest.elf
	python3 $(COMMON)/tools/elf2tos.py $< $@
rsctest.elf: crt0.o rsctest.o gem.o tos.o libc.o nf.o
	$(CC) $(LDFLAGS) -o $@ $^ -lgcc
crt0.o: $(COMMON)/atari/crt0.S
	$(CC) -m68000 -c -o $@ $<
nf.o: $(COMMON)/atari/nf.S
	$(CC) -m68000 -c -o $@ $<
%.o: $(COMMON)/atari/%.c
	$(CC) $(CFLAGS) -c -o $@ $<
rsctest.o: rsctest.c
	$(CC) $(CFLAGS) -c -o $@ $<
clean:
	rm -f *.o *.elf RSCTEST.PRG
