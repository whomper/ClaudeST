# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
# FAKEVIEW.ACC, the stand-in viewer for app_help.py: make, then
#   python3 app_help.py WORK etos512us.img APP.PRG NAME.HYP
CROSS   ?= m68k-linux-gnu-
CC      := $(CROSS)gcc
GCCINC  := $(shell $(CC) -print-file-name=include 2>/dev/null)
A       := ../../atari
CFLAGS  := -m68000 -Os -fomit-frame-pointer -ffreestanding -fno-builtin -fno-pic -fno-pie \
           -fno-tree-loop-distribute-patterns -fno-store-merging -fdisable-tree-bswap -fno-asynchronous-unwind-tables \
           -Wall -nostdinc -isystem $(GCCINC) -I$(A)/include -I$(A)
LDFLAGS := -m68000 -nostdlib -static -no-pie -Wl,--emit-relocs -Wl,-T,$(A)/link.ld \
           -Wl,--build-id=none -Wl,-z,noexecstack -Wl,--no-warn-rwx-segments
B       := build

$(B)/FAKEVIEW.ACC: $(B)/fakeview.elf ../../tools/elf2tos.py
	python3 ../../tools/elf2tos.py $< $@
$(B)/fakeview.elf: $(B)/crt0.o $(B)/fakeview.o $(B)/gem.o $(B)/tos.o $(B)/libc.o $(B)/nf.o
	$(CC) $(LDFLAGS) -o $@ $^ -lgcc
$(B)/fakeview.o: fakeview.c | $(B)
	$(CC) $(CFLAGS) -c -o $@ $<
# for prg_help.py: an app pressing Help (HELPT.PRG) and a stand-in
# EHYPVIEW.PRG (FAKEPRG.PRG) that reports its command line
U       := ../../ui
S       := ../../src
prg: $(B)/HELPT.PRG $(B)/FAKEPRG.PRG
$(B)/HELPT.PRG: $(B)/helpt.elf ../../tools/elf2tos.py
	python3 ../../tools/elf2tos.py $< $@
$(B)/FAKEPRG.PRG: $(B)/fakeprg.elf ../../tools/elf2tos.py
	python3 ../../tools/elf2tos.py $< $@
$(B)/helpt.elf: $(B)/crt0.o $(B)/helpt.o $(B)/help.o $(B)/av.o $(B)/alert.o $(B)/dialog.o $(B)/bidi.o $(B)/gem.o $(B)/tos.o $(B)/libc.o $(B)/nf.o
	$(CC) $(LDFLAGS) -o $@ $^ -lgcc
$(B)/fakeprg.elf: $(B)/crt0.o $(B)/fakeprg.o $(B)/gem.o $(B)/tos.o $(B)/libc.o $(B)/nf.o
	$(CC) $(LDFLAGS) -o $@ $^ -lgcc
$(B)/helpt.o: helpt.c | $(B)
	$(CC) $(CFLAGS) -I$(U) -I$(S) -c -o $@ $<
$(B)/fakeprg.o: fakeprg.c | $(B)
	$(CC) $(CFLAGS) -c -o $@ $<
$(B)/%.o: $(U)/%.c | $(B)
	$(CC) $(CFLAGS) -I$(U) -I$(S) -c -o $@ $<
$(B)/bidi.o: $(S)/bidi.c | $(B)
	$(CC) $(CFLAGS) -I$(S) -c -o $@ $<
$(B)/%.o: $(A)/%.c | $(B)
	$(CC) $(CFLAGS) -c -o $@ $<
$(B)/crt0.o: $(A)/crt0.S | $(B)
	$(CC) -m68000 -DSTACK_SIZE=4096 -c -o $@ $<
$(B)/nf.o: $(A)/nf.S | $(B)
	$(CC) -m68000 -c -o $@ $<
$(B):
	mkdir -p $@
clean:
	rm -rf $(B)
.PHONY: clean prg
