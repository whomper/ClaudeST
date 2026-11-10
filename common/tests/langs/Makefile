# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
# ui/langs.c, the Languages dialog: everything that has no GEM (the rows' text, the total, the tick
# rules, the scroll arithmetic, the job's words and its polling against a scripted network): make test
#
# LANGT.PRG, the dialog itself in an emulator against a fake translategate (fakegate.py):
#   make prg, then python3 run.py WORK MACHINE [--nvdi]     (MACHINE as common/tools/machines.py: ST:mono,
#   ST:med, Falcon:16, Falcon:256 ...); the speed numbers: python3 speed_test.py WORK MACHINE [--nvdi]
CFLAGS := -O1 -g -Wall -Wextra -Wno-unused-parameter -DLANGS_PURE_ONLY -I../connslice/stub -I../../src -I../../ui
B      := build
SRC    := ../../ui/langs.c ../../src/translate.c ../../src/conn.c ../../src/tls_none.c ../../src/util.c \
          ../../src/charset.c ../../src/bidi.c

$(B)/test_langs: test_langs.c $(SRC) ../../ui/langs.h ../../src/translate.h | $(B)
	cc $(CFLAGS) -fsanitize=address,undefined -fno-sanitize-recover=all -o $@ test_langs.c $(SRC)
$(B):
	mkdir -p $@
test: $(B)/test_langs
	./$(B)/test_langs
clean:
	rm -rf $(B)
.PHONY: test clean

# ---- LANGT.PRG ----
CROSS   ?= m68k-linux-gnu-
GCCINC  := $(shell $(CROSS)gcc -print-file-name=include 2>/dev/null)
A       := ../../atari
U       := ../../ui
S       := ../../src
TCFLAGS := -m68000 -Os -fomit-frame-pointer -ffreestanding -fno-builtin -fno-pic -fno-pie \
           -fno-tree-loop-distribute-patterns -fno-store-merging -fdisable-tree-bswap -fno-asynchronous-unwind-tables \
           -Wall -Wextra -Wno-unused-parameter -nostdinc -isystem $(GCCINC) -I$(A)/include -I$(A) -Istub -Istub/src -I$(U) -I$(S) -DLANGS_TRACE -DDLG_CPU $(EXTRA)
TLDFLAGS := -m68000 -nostdlib -static -no-pie -Wl,--emit-relocs -Wl,-T,$(A)/link.ld \
            -Wl,--build-id=none -Wl,-z,noexecstack -Wl,--no-warn-rwx-segments
T       := $(B)/atari
# the dialog's own parts, and the rest of what an app links it with
LIB     := dialog alert click panebar progbar text bidi gem libc tos
NET     := translate conn tls_none util charset
NETOBJS := $(T)/net_tos.o $(T)/sting.o $(T)/sting_asm.o
OBJS    := $(T)/crt0.o $(T)/nf.o $(T)/langt.o $(T)/langs.o $(T)/draw.o $(T)/plat.o $(NETOBJS) \
           $(addprefix $(T)/,$(addsuffix .o,$(LIB) $(NET)))

prg: $(T)/LANGT.PRG
# LANGT.PRG with the Gateway's answers made in the program (stub/fakenet.c, no network to the emulator needed): for
# FreeMiNT with XaAES (../xaaes) and the window forced on TOS (a file WINDOW beside it)
fake:
	$(MAKE) T=$(B)/fake EXTRA=-DLANGT_FAKENET NETOBJS=$(B)/fake/fakenet.o $(B)/fake/LANGT.PRG
$(T)/fakenet.o: stub/fakenet.c stub/src/plat.h | $(T)
	$(CROSS)gcc $(TCFLAGS) -c -o $@ $<
# the same with the AES's stretches on the console (AESGAP lines: gem.c), for speed_test.py
prof:
	$(MAKE) T=$(B)/prof EXTRA=-DAES_PROFILE=20 $(B)/prof/LANGT.PRG
$(T)/LANGT.PRG: $(T)/langt.elf ../../tools/elf2tos.py
	python3 ../../tools/elf2tos.py $< $@
$(T)/langt.elf: $(OBJS)
	$(CROSS)gcc $(TLDFLAGS) -o $@ $^ -lgcc
$(T)/langt.o: langt.c $(U)/langs.h | $(T)
	$(CROSS)gcc $(TCFLAGS) -c -o $@ $<
$(T)/langs.o: $(U)/langs.c $(U)/langs.h $(S)/translate.h | $(T)
	$(CROSS)gcc $(TCFLAGS) -Werror -c -o $@ $<
$(T)/draw.o: stub/draw.c stub/ui.h | $(T)
	$(CROSS)gcc $(TCFLAGS) -c -o $@ $<
$(T)/plat.o: stub/plat.c stub/src/plat.h | $(T)
	$(CROSS)gcc $(TCFLAGS) -c -o $@ $<
$(T)/sting_asm.o: $(A)/sting.S | $(T)
	$(CROSS)gcc -m68000 -c -o $@ $<
$(T)/net_tos.o: $(A)/net_tos.c | $(T)
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
# no word or long at an odd stack offset in the 68000 code (an address error on a 68000)
odd: prg
	python3 ../../tools/oddaccess.py $(T)/langt.elf
.PHONY: prg prof fake odd
