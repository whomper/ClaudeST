# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
# FreeMiNT 1.17.0 with XaAES in Hatari: an AES that iconifies windows,
# multitasks and does drag and drop, for checking the apps where EmuTOS's
# AES can't (see run.py).
#   make base             fetch FreeMiNT (checked against its SHA-256) and
#                         make build/base, the C: drive run.py starts from
#   make check TOS=IMG    the rig's own check: ICONTEST.PRG iconified and
#                         restored with XaAES's gadgets (EmuTOS 512k image)
CROSS   ?= m68k-linux-gnu-
CC      := $(CROSS)gcc
GCCINC  := $(shell $(CC) -print-file-name=include 2>/dev/null)
A       := ../../atari
ACFLAGS := -m68000 -Os -fomit-frame-pointer -ffreestanding -fno-builtin -fno-pic -fno-pie \
           -fno-tree-loop-distribute-patterns -fno-store-merging -fdisable-tree-bswap -fno-asynchronous-unwind-tables \
           -Wall -nostdinc -isystem $(GCCINC) -I$(A)/include -I$(A)
LDFLAGS := -m68000 -nostdlib -static -no-pie -Wl,--emit-relocs -Wl,-T,$(A)/link.ld \
           -Wl,--build-id=none -Wl,-z,noexecstack -Wl,--no-warn-rwx-segments
B       := build

all: $(B)/ICONTEST.PRG $(B)/OPNWK.PRG $(B)/LATER.PRG
base: $(B)/OPNWK.PRG
	python3 fetch.py
$(B)/%.PRG: $(B)/%.elf ../../tools/elf2tos.py
	python3 ../../tools/elf2tos.py $< $@
$(B)/ICONTEST.elf: $(B)/crt0.o $(B)/icontest.o $(B)/gem.o $(B)/tos.o $(B)/libc.o $(B)/nf.o
	$(CC) $(LDFLAGS) -o $@ $^ -lgcc
$(B)/LATER.elf: $(B)/crt0.o $(B)/later.o $(B)/gem.o $(B)/tos.o $(B)/libc.o $(B)/nf.o
	$(CC) $(LDFLAGS) -o $@ $^ -lgcc
$(B)/OPNWK.elf: $(B)/crt0.o $(B)/opnwk.o $(B)/tos.o $(B)/libc.o $(B)/nf.o
	$(CC) $(LDFLAGS) -o $@ $^ -lgcc
$(B)/opnwk.o: opnwk.c | $(B)
	$(CC) $(ACFLAGS) -c -o $@ $<
$(B)/later.o: later.c | $(B)
	$(CC) $(ACFLAGS) -c -o $@ $<
$(B)/icontest.o: icontest.c | $(B)
	$(CC) $(ACFLAGS) -c -o $@ $<
$(B)/%.o: $(A)/%.c | $(B)
	$(CC) $(ACFLAGS) -c -o $@ $<
$(B)/crt0.o: $(A)/crt0.S | $(B)
	$(CC) -m68000 -DSTACK_SIZE=8192 -c -o $@ $<
$(B)/nf.o: $(A)/nf.S | $(B)
	$(CC) -m68000 -c -o $@ $<
$(B):
	mkdir -p $@
check: all base
	@test -n "$(TOS)" || { echo "make check TOS=/path/to/etos512us.img"; exit 1; }
	python3 run.py $(B)/check $(TOS) $(B)/ICONTEST.PRG --steps "until ICT opened 90; wait 1; shot; \
	  click 447 112; until ICT iconified 20; wait 1; shot; click 58 419; until ICT uniconified 20; \
	  wait 1; shot; click 131 112; until ICT done 20"
	@echo "xaaes: ICONTEST.PRG iconified and restored"
clean:
	rm -rf $(B)
.PHONY: all base check clean
.PRECIOUS: $(B)/%.elf
