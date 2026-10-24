# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
# a pane's scroll bar (ui/panebar.c) on the build machine, against a
# stand-in AES that keeps what objc_draw was given and plays a mouse, with
# AddressSanitizer: make test
CFLAGS := -O1 -g -Wall -Wextra -fsanitize=address,undefined -fno-sanitize-recover=all \
          -Istub -I../../ui -I../../atari
build/test_panebar: test_panebar.c ../../ui/panebar.c ../../ui/panebar.h | build
	cc $(CFLAGS) -o $@ test_panebar.c ../../ui/panebar.c
build:
	mkdir -p $@
test: build/test_panebar
	./build/test_panebar
clean:
	rm -rf build
.PHONY: test clean

# PBTEST.PRG, two panes with their bars in a window: make prg, then
#   python3 run.py WORK MACHINE [--flat]     a screenshot of it (machines.py)
CROSS   ?= m68k-linux-gnu-
GCCINC  := $(shell $(CROSS)gcc -print-file-name=include 2>/dev/null)
A       := ../../atari
U       := ../../ui
TCFLAGS := -m68000 -Os -fomit-frame-pointer -ffreestanding -fno-builtin -fno-pic -fno-pie \
           -fno-tree-loop-distribute-patterns -fno-store-merging -fdisable-tree-bswap -fno-asynchronous-unwind-tables \
           -Wall -nostdinc -isystem $(GCCINC) -I$(A)/include -I$(A) -Iatari -I$(U)
TLDFLAGS := -m68000 -nostdlib -static -no-pie -Wl,--emit-relocs -Wl,-T,$(A)/link.ld \
            -Wl,--build-id=none -Wl,-z,noexecstack -Wl,--no-warn-rwx-segments
T       := build/atari

prg: $(T)/PBTEST.PRG
$(T)/PBTEST.PRG: $(T)/pbtest.elf ../../tools/elf2tos.py
	python3 ../../tools/elf2tos.py $< $@
$(T)/pbtest.elf: $(T)/crt0.o $(T)/pbtest.o $(T)/panebar.o $(T)/dialog.o $(T)/gem.o $(T)/libc.o $(T)/tos.o $(T)/nf.o
	$(CROSS)gcc $(TLDFLAGS) -o $@ $^ -lgcc
$(T)/pbtest.o: pbtest.c $(U)/panebar.h | $(T)
	$(CROSS)gcc $(TCFLAGS) -c -o $@ $<
$(T)/%.o: $(U)/%.c $(U)/panebar.h $(U)/dialog.h | $(T)
	$(CROSS)gcc $(TCFLAGS) -c -o $@ $<
$(T)/%.o: $(A)/%.c | $(T)
	$(CROSS)gcc $(TCFLAGS) -c -o $@ $<
$(T)/%.o: $(A)/%.S | $(T)
	$(CROSS)gcc -m68000 -c -o $@ $<
$(T):
	mkdir -p $@
.PHONY: prg
