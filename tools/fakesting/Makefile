# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Erez Yaary
# FAKESTNG.PRG: test stand-in for STinG (see fakesting.c). Not for real use.
CROSS  ?= m68k-linux-gnu-
CC     := $(CROSS)gcc
GCCINC := $(shell $(CC) -print-file-name=include)
CFLAGS := -m68000 -Os -fomit-frame-pointer -ffreestanding -fno-builtin -fno-pic -fno-pie \
          -fno-tree-loop-distribute-patterns -fno-store-merging -fno-asynchronous-unwind-tables -Wall \
          -nostdinc -isystem $(GCCINC) -I../../common/atari/include
LDFLAGS := -m68000 -nostdlib -static -no-pie -Wl,--emit-relocs -Wl,-T,link.ld \
          -Wl,--build-id=none -Wl,-z,noexecstack -Wl,--no-warn-rwx-segments

FAKESTNG.PRG: fakesting.elf
	python3 ../../common/tools/elf2tos.py $< $@

fakesting.elf: fstart.o fakesting.o tos.o libc.o nf.o link.ld
	$(CC) $(LDFLAGS) -o $@ fstart.o fakesting.o tos.o libc.o nf.o

fstart.o: fstart.S
	$(CC) -m68000 -c -o $@ $<
nf.o: ../../common/atari/nf.S
	$(CC) -m68000 -c -o $@ $<
libc.o: ../../common/atari/libc.c
	$(CC) $(CFLAGS) -c -o $@ $<
tos.o: ../../common/atari/tos.c
	$(CC) $(CFLAGS) -c -o $@ $<
fakesting.o: fakesting.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f *.o *.elf FAKESTNG.PRG
