# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
# The family's speed rig (the guidelines' Speed): speed.py, and SPT.PRG, its
# own check: make prg, then python3 speed.py --selftest WORK [MACHINE]
CROSS   ?= m68k-linux-gnu-
GCCINC  := $(shell $(CROSS)gcc -print-file-name=include 2>/dev/null)
A       := ../../atari
B       := build
T       := $(B)/atari
TCFLAGS := -m68000 -Os -fomit-frame-pointer -ffreestanding -fno-builtin -fno-pic -fno-pie \
           -fno-tree-loop-distribute-patterns -fno-store-merging -fdisable-tree-bswap -fno-asynchronous-unwind-tables \
           -Wall -nostdinc -isystem $(GCCINC) -I$(A)/include -I$(A) -DAES_PROFILE=20
TLDFLAGS := -m68000 -nostdlib -static -no-pie -Wl,--emit-relocs -Wl,-T,$(A)/link.ld \
            -Wl,--build-id=none -Wl,-z,noexecstack -Wl,--no-warn-rwx-segments

prg: $(T)/SPT.PRG $(T)/VDT.PRG
$(T)/SPT.PRG: $(T)/spt.elf ../../tools/elf2tos.py
	python3 ../../tools/elf2tos.py $< $@
$(T)/spt.elf: $(T)/crt0.o $(T)/spt.o $(T)/gem.o $(T)/libc.o $(T)/tos.o $(T)/nf.o
	$(CROSS)gcc $(TLDFLAGS) -o $@ $^ -lgcc
$(T)/spt.o: spt.c | $(T)
	$(CROSS)gcc $(TCFLAGS) -c -o $@ $<
$(T)/VDT.PRG: $(T)/vdt.elf ../../tools/elf2tos.py
	python3 ../../tools/elf2tos.py $< $@
$(T)/vdt.elf: $(T)/crt0.o $(T)/vdt.o $(T)/gem.o $(T)/libc.o $(T)/tos.o $(T)/nf.o
	$(CROSS)gcc $(TLDFLAGS) -o $@ $^ -lgcc
$(T)/vdt.o: vdt.c | $(T)
	$(CROSS)gcc $(TCFLAGS) -c -o $@ $<
$(T)/%.o: $(A)/%.c | $(T)
	$(CROSS)gcc $(TCFLAGS) -c -o $@ $<
$(T)/%.o: $(A)/%.S | $(T)
	$(CROSS)gcc -m68000 -c -o $@ $<
$(T):
	mkdir -p $@
clean:
	rm -rf $(B)
.PHONY: prg clean
