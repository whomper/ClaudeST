# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
# ui/notify.c (BGEM.NG's notifications) on the host: make test. In Hatari
# with FreeMiNT, XaAES and a stand-in for BGEM.NG: make prg, then notify.py.
B := build
$(B)/test_notify: test_notify.c ../../ui/notify.c ../../ui/notify.h | $(B)
	cc -O2 -Wall -Wextra -fsanitize=address,undefined -Istub -I../../ui -o $@ test_notify.c ../../ui/notify.c
test: $(B)/test_notify
	./$(B)/test_notify
$(B):
	mkdir -p $@
clean:
	rm -rf $(B)
.PHONY: test clean

# NOTET.PRG and the stand-in BUBBLE.PRG for notify.py
CROSS   ?= m68k-linux-gnu-
CC68    := $(CROSS)gcc
GCCINC  := $(shell $(CC68) -print-file-name=include 2>/dev/null)
A       := ../../atari
U       := ../../ui
CFLAGS  := -m68000 -Os -fomit-frame-pointer -ffreestanding -fno-builtin -fno-pic -fno-pie \
           -fno-tree-loop-distribute-patterns -fno-store-merging -fdisable-tree-bswap -fno-asynchronous-unwind-tables \
           -Wall -nostdinc -isystem $(GCCINC) -I$(A)/include -I$(A)
LDFLAGS := -m68000 -nostdlib -static -no-pie -Wl,--emit-relocs -Wl,-T,$(A)/link.ld \
           -Wl,--build-id=none -Wl,-z,noexecstack -Wl,--no-warn-rwx-segments
prg: $(B)/NOTET.PRG $(B)/BUBBLE.PRG
$(B)/BUBBLE.PRG: $(B)/fakebgem.elf ../../tools/elf2tos.py
	python3 ../../tools/elf2tos.py $< $@
$(B)/fakebgem.elf: $(B)/crt0.o $(B)/fakebgem.o $(B)/gem.o $(B)/tos.o $(B)/libc.o $(B)/nf.o
	$(CC68) $(LDFLAGS) -o $@ $^ -lgcc
$(B)/fakebgem.o: fakebgem.c | $(B)
	$(CC68) $(CFLAGS) -I$(U) -c -o $@ $<
$(B)/NOTET.PRG: $(B)/notet.elf ../../tools/elf2tos.py
	python3 ../../tools/elf2tos.py $< $@
$(B)/notet.elf: $(B)/crt0.o $(B)/notet.o $(B)/notify.o $(B)/av.o $(B)/gem.o $(B)/tos.o $(B)/libc.o $(B)/nf.o
	$(CC68) $(LDFLAGS) -o $@ $^ -lgcc
$(B)/notet.o: notet.c | $(B)
	$(CC68) $(CFLAGS) -I$(U) -c -o $@ $<
$(B)/%.o: $(U)/%.c | $(B)
	$(CC68) $(CFLAGS) -I$(U) -c -o $@ $<
$(B)/%.o: $(A)/%.c | $(B)
	$(CC68) $(CFLAGS) -c -o $@ $<
$(B)/crt0.o: $(A)/crt0.S | $(B)
	$(CC68) -m68000 -DSTACK_SIZE=4096 -c -o $@ $<
$(B)/nf.o: $(A)/nf.S | $(B)
	$(CC68) -m68000 -c -o $@ $<
.PHONY: prg
