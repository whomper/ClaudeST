# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
# FPTEST.PRG, the file dialog (ui/filepick.c) with EPlay's types: make,
# then python3 run.py WORK MACHINE [--nvdi]. With EPLAY=its checkout,
# make old builds FPOLD.PRG from EPlay's own ui/addfiles.c and
# src/dirlist.c, and run.py WORK MACHINE --old compares the two, pixel
# for pixel (the move changes nothing: the hub, 5 Nov 2026)
CROSS   ?= m68k-linux-gnu-
GCCINC  := $(shell $(CROSS)gcc -print-file-name=include 2>/dev/null)
A       := ../../atari
U       := ../../ui
S       := ../../src
TCFLAGS := -m68000 -Os -fomit-frame-pointer -ffreestanding -fno-builtin -fno-pic -fno-pie \
           -fno-tree-loop-distribute-patterns -fno-store-merging -fdisable-tree-bswap -fno-asynchronous-unwind-tables \
           -Wall -nostdinc -isystem $(GCCINC) -I$(A)/include -I$(A) -Istub -I$(U) -I$(S)
TLDFLAGS := -m68000 -nostdlib -static -no-pie -Wl,--emit-relocs -Wl,-T,$(A)/link.ld \
            -Wl,--build-id=none -Wl,-z,noexecstack -Wl,--no-warn-rwx-segments
T       := build/atari
LIB     := dialog popup click fsel text bidi gem libc tos
OBJS    := $(T)/crt0.o $(T)/nf.o $(T)/fptest.o $(T)/filepick.o $(T)/dirlist.o $(T)/draw.o $(addprefix $(T)/,$(addsuffix .o,$(LIB)))

all: $(T)/FPTEST.PRG
$(T)/FPTEST.PRG: $(T)/fptest.elf ../../tools/elf2tos.py
	python3 ../../tools/elf2tos.py $< $@
$(T)/fptest.elf: $(OBJS)
	$(CROSS)gcc $(TLDFLAGS) -o $@ $^ -lgcc
$(T)/fptest.o: fptest.c $(U)/filepick.h $(S)/dirlist.h | $(T)
	$(CROSS)gcc $(TCFLAGS) -c -o $@ $<
$(T)/filepick.o: $(U)/filepick.c $(U)/filepick.h $(S)/dirlist.h | $(T)
	$(CROSS)gcc $(TCFLAGS) -Wextra -Wno-unused-parameter -Werror -c -o $@ $<
$(T)/dirlist.o: $(S)/dirlist.c $(S)/dirlist.h | $(T)
	$(CROSS)gcc $(TCFLAGS) -Wextra -Werror -c -o $@ $<
$(T)/draw.o: stub/draw.c stub/ui.h | $(T)
	$(CROSS)gcc $(TCFLAGS) -c -o $@ $<
$(T)/%.o: $(U)/%.c | $(T)
	$(CROSS)gcc $(TCFLAGS) -c -o $@ $<
$(T)/%.o: $(S)/%.c | $(T)
	$(CROSS)gcc $(TCFLAGS) -c -o $@ $<
$(T)/%.o: $(A)/%.c | $(T)
	$(CROSS)gcc $(TCFLAGS) -c -o $@ $<
$(T)/%.o: $(A)/%.S | $(T)
	$(CROSS)gcc -m68000 -c -o $@ $<
$(T):
	mkdir -p $@

# FPOLD.PRG: EPlay's own, for the comparison
O       := build/old
OFLAGS  := $(subst -I$(S),-I$(EPLAY)/src -I$(EPLAY)/ui -I$(S),$(TCFLAGS)) -DFP_OLD
old: $(O)/FPOLD.PRG
$(O)/FPOLD.PRG: $(O)/fpold.elf ../../tools/elf2tos.py
	python3 ../../tools/elf2tos.py $< $@
$(O)/fpold.elf: $(T)/crt0.o $(T)/nf.o $(O)/fptest.o $(O)/addfiles.o $(O)/dirlist.o $(T)/draw.o \
                $(addprefix $(T)/,$(addsuffix .o,$(LIB)))
	$(CROSS)gcc $(TLDFLAGS) -o $@ $^ -lgcc
$(O)/fptest.o: fptest.c | $(O)
	$(CROSS)gcc $(OFLAGS) -c -o $@ $<
$(O)/addfiles.o: $(EPLAY)/ui/addfiles.c | $(O)
	$(CROSS)gcc $(OFLAGS) -c -o $@ $<
$(O)/dirlist.o: $(EPLAY)/src/dirlist.c | $(O)
	$(CROSS)gcc $(OFLAGS) -c -o $@ $<
$(O):
	mkdir -p $@
clean:
	rm -rf build
.PHONY: all old clean
