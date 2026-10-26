# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
# the AV protocol (ui/av.c) on the build machine: make test. Not a
# position-independent program, so the static buffers have the 32-bit
# addresses a message carries. THING.ACC, the stand-in AV server for
# app_av.py: make
CFLAGS := -O2 -Wall -Wextra -no-pie -fsanitize=address,undefined -Istub -I../../ui
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

$(B)/THING.ACC: $(B)/fakeserver.elf ../../tools/elf2tos.py
	python3 ../../tools/elf2tos.py $< $@
$(B)/fakeserver.elf: $(B)/crt0.o $(B)/fakeserver.o $(B)/gem.o $(B)/tos.o $(B)/libc.o $(B)/nf.o $(B)/clip.o
	$(CC) $(LDFLAGS) -o $@ $^ -lgcc
$(B)/fakeserver.o: fakeserver.c | $(B)
	$(CC) $(ACFLAGS) -I../../ui -c -o $@ $<
$(B)/clip.o: ../../ui/clip.c ../../ui/clip.h | $(B)
	$(CC) $(ACFLAGS) -I../../ui -c -o $@ $<
$(B)/%.o: $(A)/%.c | $(B)
	$(CC) $(ACFLAGS) -c -o $@ $<
$(B)/crt0.o: $(A)/crt0.S | $(B)
	$(CC) -m68000 -DSTACK_SIZE=4096 -c -o $@ $<
$(B)/nf.o: $(A)/nf.S | $(B)
	$(CC) -m68000 -c -o $@ $<
$(B)/test_av: test_av.c ../../ui/av.c ../../ui/av_people.c ../../ui/av.h $(B)/cmdline.c | $(B)
	cc $(CFLAGS) -o $@ test_av.c ../../ui/av.c ../../ui/av_people.c $(B)/cmdline.c
# cmd_line() as tos.c has it, for the build machine (the rest of tos.c is TOS's)
$(B)/cmdline.c: $(A)/tos.c | $(B)
	sed -n '/^extern void \*_BasePage/,/^}$$/p;/^const char \*cmd_line(void)$$/,/^}$$/p' $< > $@
$(B):
	mkdir -p $@
test: $(B)/test_av
	./$(B)/test_av
clean:
	rm -rf build
.PHONY: test clean
