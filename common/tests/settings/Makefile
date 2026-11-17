# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
# The family's Settings dialog (ui/settings.c, ui/dlgbuild.c) with the shared dialog loop (ui/dialog.c) and
# long fields (ui/field.c) on the build machine, against a stand-in AES, with AddressSanitizer and
# UndefinedBehaviorSanitizer: make test
CFLAGS := -O1 -g -Wall -Wextra -fsanitize=address,undefined -fno-sanitize-recover=all \
          -Istub -I../../ui -I../../atari
B      := build
SRC    := test_settings.c aes_stub.c ../../ui/settings.c ../../ui/dlgbuild.c ../../ui/dialog.c ../../ui/field.c ../../ui/eye.c
HDR    := aes_stub.h ../../ui/settings.h ../../ui/dlgbuild.h ../../ui/dialog.h ../../ui/field.h

$(B)/test_settings: $(SRC) $(HDR) | $(B)
	cc $(CFLAGS) -o $@ $(SRC)
$(B):
	mkdir -p $@
test: $(B)/test_settings
	./$(B)/test_settings
clean:
	rm -rf $(B)
.PHONY: test clean

# SETDEMO.PRG: EMail 2.14's and EHypView 1.39's Settings pages built with the shared dialog, in an emulator:
#   make prg           (and make prof: the AES's stretches on the console, for speed)
#   python3 run.py WORK MACHINE [--nvdi]     (MACHINE as common/tools/machines.py: ST:mono, Falcon:16 ...)
#   python3 run.py WORK --xaaes TOS_IMAGE    FreeMiNT and XaAES (../xaaes: make base)
CROSS   ?= m68k-linux-gnu-
GCCINC  := $(shell $(CROSS)gcc -print-file-name=include 2>/dev/null)
A       := ../../atari
U       := ../../ui
S       := ../../src
TCFLAGS := -m68000 -Os -fomit-frame-pointer -ffreestanding -fno-builtin -fno-pic -fno-pie \
           -fno-tree-loop-distribute-patterns -fno-store-merging -fdisable-tree-bswap -fno-asynchronous-unwind-tables \
           -Wall -Wextra -nostdinc -isystem $(GCCINC) -I$(A)/include -I$(A) -Iatari -I$(U) -I$(S) $(EXTRA)
TLDFLAGS := -m68000 -nostdlib -static -no-pie -Wl,--emit-relocs -Wl,-T,$(A)/link.ld \
            -Wl,--build-id=none -Wl,-z,noexecstack -Wl,--no-warn-rwx-segments
T       := $(B)/atari
LIB     := fontsel mbar dialog alert popup click text bidi clip field eye dlgbuild settings gem libc tos
OBJS    := $(T)/crt0.o $(T)/nf.o $(T)/setdemo.o $(T)/draw.o $(addprefix $(T)/,$(addsuffix .o,$(LIB)))

prg: $(T)/SETDEMO.PRG
prof:
	$(MAKE) T=$(B)/prof EXTRA="-DAES_PROFILE=20 $(XTRA)" $(B)/prof/SETDEMO.PRG
# the programs' own build on an ST: dialog.c's DLG_CPU (the guidelines' Speed; never in an accessory) and click.c's
# DLG_CLICK; the name keeps it apart from the plain demo: make cpu, python3 run.py WORK MACHINE --speed --prg cpu
cpu:
	$(MAKE) T=$(B)/cpu EXTRA="-DAES_PROFILE=20 -DDLG_CPU -DDLG_CLICK" $(B)/cpu/SETDEMO.PRG
$(T)/SETDEMO.PRG: $(T)/setdemo.elf ../../tools/elf2tos.py
	python3 ../../tools/elf2tos.py $< $@
$(T)/setdemo.elf: $(OBJS)
	$(CROSS)gcc $(TLDFLAGS) -o $@ $^ -lgcc
$(T)/setdemo.o: setdemo.c $(U)/settings.h $(U)/dlgbuild.h | $(T)
	$(CROSS)gcc $(TCFLAGS) -Wno-unused-parameter -c -o $@ $<
$(T)/draw.o: atari/draw.c atari/ui.h | $(T)
	$(CROSS)gcc $(TCFLAGS) -c -o $@ $<
# the family's flags and warnings as errors for the two new files
$(T)/dlgbuild.o: $(U)/dlgbuild.c $(U)/dlgbuild.h | $(T)
	$(CROSS)gcc $(TCFLAGS) -Werror -c -o $@ $<
$(T)/settings.o: $(U)/settings.c $(U)/settings.h $(U)/dlgbuild.h | $(T)
	$(CROSS)gcc $(TCFLAGS) -Werror -c -o $@ $<
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
.PHONY: prg prof cpu
