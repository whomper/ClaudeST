# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
# printing on the family's machines (68000 code, every Atari):
#   make prg, then python3 run.py WORK MACHINE [--nvdi]
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

prg: $(T)/PRT.PRG
$(T)/PRT.PRG: $(T)/prt.elf ../../tools/elf2tos.py
	python3 ../../tools/elf2tos.py $< $@
$(T)/prt.elf: $(T)/crt0.o $(T)/prt.o $(T)/gem.o $(T)/libc.o $(T)/tos.o $(T)/nf.o $(T)/print.o
	$(CROSS)gcc $(TLDFLAGS) -o $@ $^ -lgcc
$(T)/prt.o: prt.c | $(T)
	$(CROSS)gcc $(TCFLAGS) -c -o $@ $< 2>&1 | grep -v "disable pass" || true
$(T)/%.o: $(A)/%.c | $(T)
	$(CROSS)gcc $(TCFLAGS) -c -o $@ $< 2>&1 | grep -v "disable pass" || true
$(T)/%.o: $(U)/%.c | $(T)
	$(CROSS)gcc $(TCFLAGS) -c -o $@ $< 2>&1 | grep -v "disable pass" || true
$(T)/%.o: $(A)/%.S | $(T)
	$(CROSS)gcc -m68000 -c -o $@ $<
$(T):
	mkdir -p $@
clean:
	rm -rf build
.PHONY: prg clean
