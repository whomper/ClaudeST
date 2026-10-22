# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
# The shared dialog loop (ui/dialog.c) and its long fields (ui/field.c) on the build machine, against a
# stand-in AES, with AddressSanitizer: make test
CFLAGS := -O1 -g -Wall -Wextra -fsanitize=address,undefined -fno-sanitize-recover=all \
          -Istub -I../../ui -I../../atari
B      := build

$(B)/test_dialog: test_dialog.c ../../ui/dialog.c ../../ui/dialog.h ../../ui/field.c ../../ui/field.h | $(B)
	cc $(CFLAGS) -o $@ test_dialog.c ../../ui/dialog.c ../../ui/field.c
$(B):
	mkdir -p $@
test: $(B)/test_dialog
	./$(B)/test_dialog
clean:
	rm -rf $(B)
.PHONY: test clean

# DLGT.PRG, alert() and a dialog in an emulator: make prg, then
#   python3 run.py etos512us.img            EmuTOS: ST high, medium, low, Falcon
#   python3 run.py etos512us.img --xaaes    FreeMiNT and XaAES (../xaaes, make base)
CROSS   ?= m68k-linux-gnu-
GCCINC  := $(shell $(CROSS)gcc -print-file-name=include 2>/dev/null)
A       := ../../atari
U       := ../../ui
S       := ../../src
TCFLAGS := -m68000 -Os -fomit-frame-pointer -ffreestanding -fno-builtin -fno-pic -fno-pie \
           -fno-tree-loop-distribute-patterns -fno-store-merging -fdisable-tree-bswap -fno-asynchronous-unwind-tables \
           -Wall -nostdinc -isystem $(GCCINC) -I$(A)/include -I$(A) -I$(U) -I$(S) -DDLG_TRACE
TLDFLAGS := -m68000 -nostdlib -static -no-pie -Wl,--emit-relocs -Wl,-T,$(A)/link.ld \
            -Wl,--build-id=none -Wl,-z,noexecstack -Wl,--no-warn-rwx-segments
T       := $(B)/atari

prg: $(T)/DLGT.PRG
$(T)/DLGT.PRG: $(T)/dlgt.elf ../../tools/elf2tos.py
	python3 ../../tools/elf2tos.py $< $@
$(T)/dlgt.elf: $(T)/crt0.o $(T)/dlgtest.o $(T)/dialog.o $(T)/alert.o $(T)/bidi.o $(T)/gem.o $(T)/libc.o $(T)/tos.o $(T)/nf.o
	$(CROSS)gcc $(TLDFLAGS) -o $@ $^ -lgcc
$(T)/dlgtest.o: dlgtest.c $(U)/dialog.h $(U)/alert.h | $(T)
	$(CROSS)gcc $(TCFLAGS) -c -o $@ $<
$(T)/%.o: $(U)/%.c $(U)/dialog.h | $(T)
	$(CROSS)gcc $(TCFLAGS) -c -o $@ $<
$(T)/%.o: $(S)/%.c | $(T)
	$(CROSS)gcc $(TCFLAGS) -c -o $@ $<
$(T)/%.o: $(A)/%.c | $(T)
	$(CROSS)gcc $(TCFLAGS) -c -o $@ $<
$(T)/%.o: $(A)/%.S | $(T)
	$(CROSS)gcc -m68000 -c -o $@ $<
$(T):
	mkdir -p $@
.PHONY: prg
