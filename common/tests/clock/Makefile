# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
# tos_ms() (atari/tos.c) on an Atari whose 200 Hz clock runs fast:
#   make prg, then python3 run.py etos512us.img
CROSS   ?= m68k-linux-gnu-
GCCINC  := $(shell $(CROSS)gcc -print-file-name=include 2>/dev/null)
A       := ../../atari
B       := build
TCFLAGS := -m68000 -Os -fomit-frame-pointer -ffreestanding -fno-builtin -fno-pic -fno-pie \
           -fno-tree-loop-distribute-patterns -fno-store-merging -fno-expensive-optimizations -fno-asynchronous-unwind-tables \
           -Wall -nostdinc -isystem $(GCCINC) -I$(A)/include -I$(A)
TLDFLAGS := -m68000 -nostdlib -static -no-pie -Wl,--emit-relocs -Wl,-T,$(A)/link.ld \
            -Wl,--build-id=none -Wl,-z,noexecstack -Wl,--no-warn-rwx-segments

prg: $(B)/CLOCKT.PRG
$(B)/CLOCKT.PRG: $(B)/clockt.elf ../../tools/elf2tos.py
	python3 ../../tools/elf2tos.py $< $@
$(B)/clockt.elf: $(B)/crt0.o $(B)/clocktest.o $(B)/libc.o $(B)/tos.o $(B)/nf.o
	$(CROSS)gcc $(TLDFLAGS) -o $@ $^ -lgcc
$(B)/clocktest.o: clocktest.c | $(B)
	$(CROSS)gcc $(TCFLAGS) -c -o $@ $<
$(B)/%.o: $(A)/%.c | $(B)
	$(CROSS)gcc $(TCFLAGS) -c -o $@ $<
$(B)/%.o: $(A)/%.S | $(B)
	$(CROSS)gcc -m68000 -c -o $@ $<
$(B):
	mkdir -p $@
clean:
	rm -rf $(B)
.PHONY: prg clean
