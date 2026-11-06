# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
# the family's progress bar (ui/progbar.c) on the build machine, against a
# stand-in VDI with TOS's, NVDI's and EWeb's palettes, with
# AddressSanitizer: make test
CFLAGS := -O1 -g -Wall -Wextra -fsanitize=address,undefined -fno-sanitize-recover=all \
          -Istub -I../../ui
build/test_progbar: test_progbar.c ../../ui/progbar.c ../../ui/progbar.h stub/gem.h stub/ui.h | build
	cc $(CFLAGS) -o $@ test_progbar.c ../../ui/progbar.c
build:
	mkdir -p $@
test: build/test_progbar
	./build/test_progbar
clean:
	rm -rf build
.PHONY: test clean

# PRBTEST.PRG, the bar in every screen as the machine draws it: make prg,
# then python3 run.py WORK MACHINE [--nvdi]
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

prg: $(T)/PRBTEST.PRG
$(T)/PRBTEST.PRG: $(T)/prbtest.elf ../../tools/elf2tos.py
	python3 ../../tools/elf2tos.py $< $@
$(T)/prbtest.elf: $(T)/crt0.o $(T)/prbtest.o $(T)/progbar.o $(T)/dialog.o $(T)/gem.o $(T)/libc.o $(T)/tos.o $(T)/nf.o
	$(CROSS)gcc $(TLDFLAGS) -o $@ $^ -lgcc
$(T)/prbtest.o: prbtest.c $(U)/progbar.h | $(T)
	$(CROSS)gcc $(TCFLAGS) -c -o $@ $<
$(T)/%.o: $(U)/%.c $(U)/progbar.h $(U)/dialog.h | $(T)
	$(CROSS)gcc $(TCFLAGS) -c -o $@ $<
$(T)/%.o: $(A)/%.c | $(T)
	$(CROSS)gcc $(TCFLAGS) -c -o $@ $<
$(T)/%.o: $(A)/%.S | $(T)
	$(CROSS)gcc -m68000 -c -o $@ $<
$(T):
	mkdir -p $@
.PHONY: prg
