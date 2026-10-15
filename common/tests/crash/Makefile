# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
# CRASHT.PRG, the crash catcher's check: make, then
#   python3 run.py etos512us.img
CROSS   ?= m68k-linux-gnu-
CC      := $(CROSS)gcc
GCCINC  := $(shell $(CC) -print-file-name=include 2>/dev/null)
A       := ../../atari
CFLAGS  := -m68000 -Os -fomit-frame-pointer -ffreestanding -fno-builtin -fno-pic -fno-pie \
           -fno-tree-loop-distribute-patterns -fno-store-merging -fno-asynchronous-unwind-tables \
           -Wall -DCRASH_TEST -nostdinc -isystem $(GCCINC) -I$(A)/include -I$(A)
LDFLAGS := -m68000 -nostdlib -static -no-pie -Wl,--emit-relocs -Wl,-T,$(A)/link.ld \
           -Wl,--build-id=none -Wl,-z,noexecstack -Wl,--no-warn-rwx-segments
B       := build

$(B)/CRASHT.PRG: $(B)/crasht.elf ../../tools/elf2tos.py
	python3 ../../tools/elf2tos.py $< $@
$(B)/crasht.elf: $(B)/crt0.o $(B)/crashtest.o $(B)/crash.o $(B)/crash_s.o $(B)/tos.o $(B)/libc.o $(B)/nf.o
	$(CC) $(LDFLAGS) -o $@ $^ -lgcc
$(B)/crashtest.o: crashtest.c $(A)/crash.h | $(B)
	$(CC) $(CFLAGS) -c -o $@ $<
$(B)/%.o: $(A)/%.c | $(B)
	$(CC) $(CFLAGS) -c -o $@ $<
$(B)/crash_s.o: $(A)/crash.S | $(B)
	$(CC) -m68000 -c -o $@ $<
$(B)/crt0.o: $(A)/crt0.S | $(B)
	$(CC) -m68000 -c -o $@ $<
$(B)/nf.o: $(A)/nf.S | $(B)
	$(CC) -m68000 -c -o $@ $<
$(B):
	mkdir -p $@
clean:
	rm -rf $(B)
.PHONY: clean
