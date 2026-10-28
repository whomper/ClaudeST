# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
# ARTEST.PRG, a window with the AES's slider that says what each click on
# its track sends: make, then python3 run.py WORK [MACHINE] [--busy MS]
CROSS   ?= m68k-linux-gnu-
GCCINC  := $(shell $(CROSS)gcc -print-file-name=include 2>/dev/null)
A       := ../../atari
TCFLAGS := -m68000 -Os -fomit-frame-pointer -ffreestanding -fno-builtin -fno-pic -fno-pie \
           -fno-tree-loop-distribute-patterns -fno-store-merging -fdisable-tree-bswap -fno-asynchronous-unwind-tables \
           -Wall -nostdinc -isystem $(GCCINC) -I$(A)/include -I$(A)
TLDFLAGS := -m68000 -nostdlib -static -no-pie -Wl,--emit-relocs -Wl,-T,$(A)/link.ld \
            -Wl,--build-id=none -Wl,-z,noexecstack -Wl,--no-warn-rwx-segments
T       := build

$(T)/ARTEST.PRG: $(T)/artest.elf ../../tools/elf2tos.py
	python3 ../../tools/elf2tos.py $< $@
$(T)/artest.elf: $(T)/crt0.o $(T)/artest.o $(T)/gem.o $(T)/libc.o $(T)/tos.o $(T)/nf.o
	$(CROSS)gcc $(TLDFLAGS) -o $@ $^ -lgcc
$(T)/artest.o: artest.c | $(T)
	$(CROSS)gcc $(TCFLAGS) -c -o $@ $<
$(T)/%.o: $(A)/%.c | $(T)
	$(CROSS)gcc $(TCFLAGS) -c -o $@ $<
$(T)/crt0.o: $(A)/crt0.S | $(T)
	$(CROSS)gcc -m68000 -DSTACK_SIZE=8192 -c -o $@ $<
$(T)/nf.o: $(A)/nf.S | $(T)
	$(CROSS)gcc -m68000 -c -o $@ $<
$(T):
	mkdir -p $@
clean:
	rm -rf $(T)
.PHONY: clean
