# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
# ui/aboutline.c (the About box's status lines) and ui/aboutpen.h (its icon's nearest pen) on the host: make test
#
# ABOUT.PRG, the About box itself in an emulator (abouttest.c): make prg builds it twice, from this tree's
# ui/aboutbox.c and aboutpen.h (build/new) and from git's at REF (build/ref, REF=HEAD unless given), each with
# -DAES_PROFILE=20 (the box's stretches as AESGAP lines) and the icon of ICON (common/icons/ICON.txt, EPDF unless
# given); then python3 run.py WORK [MACHINE...] compares the two boxes' shots pixel for pixel (run.py's own
# machines unless given: ST:low ST:med ST:mono TT:ttlow Falcon-4:16 Falcon-4:256 with NVDI)
B := build
$(B)/test_aboutline: test_aboutline.c ../../ui/aboutline.c ../../ui/aboutline.h | $(B)
	cc -O2 -Wall -Wextra -fsanitize=address,undefined -DABOUTLINE_HOST -I../../ui -o $@ test_aboutline.c ../../ui/aboutline.c
$(B)/test_aboutpen: test_aboutpen.c ../../ui/aboutpen.h | $(B)
	cc -O2 -Wall -Wextra -fsanitize=address,undefined -I../../ui -o $@ test_aboutpen.c
test: $(B)/test_aboutline $(B)/test_aboutpen
	./$(B)/test_aboutline
	./$(B)/test_aboutpen
$(B):
	mkdir -p $@
clean:
	rm -rf $(B)
.PHONY: test clean prg

# ---- ABOUT.PRG ----
CROSS   ?= m68k-linux-gnu-
GCCINC  := $(shell $(CROSS)gcc -print-file-name=include 2>/dev/null)
A       := ../../atari
U       := ../../ui
REF     ?= HEAD
ICON    ?= EPDF
TCFLAGS := -m68000 -Os -fomit-frame-pointer -ffreestanding -fno-builtin -fno-pic -fno-pie \
           -fno-tree-loop-distribute-patterns -fno-store-merging -fdisable-tree-bswap -fno-asynchronous-unwind-tables \
           -Wall -nostdinc -isystem $(GCCINC) -I$(A)/include -I$(A) -I../langs/stub \
           -I$(U) -I../../src -I$(B)/icons -DAES_PROFILE=20
# the box and the program kept to -Wextra -Werror (the rest as the apps build it)
STRICT  := -Wextra -Wno-unused-parameter -Werror
TLDFLAGS := -m68000 -nostdlib -static -no-pie -Wl,--emit-relocs -Wl,-T,$(A)/link.ld \
            -Wl,--build-id=none -Wl,-z,noexecstack -Wl,--no-warn-rwx-segments
# what an app links the box with; draw.c is LANGT.PRG's stand-in for an app's drawing (../langs/stub)
LIB     := dialog alert crash_none text bidi gem libc tos
L       := $(B)/lib
LOBJS   := $(L)/crt0.o $(L)/nf.o $(L)/draw.o $(addprefix $(L)/,$(addsuffix .o,$(LIB)))

prg: $(B)/new/ABOUT.PRG $(B)/ref/ABOUT.PRG
$(B)/%/ABOUT.PRG: $(B)/%/about.elf ../../tools/elf2tos.py
	python3 ../../tools/elf2tos.py $< $@
$(B)/%/about.elf: $(B)/%/aboutbox.o $(L)/abouttest.o $(LOBJS)
	$(CROSS)gcc $(TLDFLAGS) -o $@ $^ -lgcc
# the box from this tree, and from git's REF (its aboutbox.c with its own aboutpen.h beside it)
$(B)/new/aboutbox.o: $(U)/aboutbox.c $(U)/aboutbox.h $(U)/aboutpen.h | $(B)/icons/icon.h
	mkdir -p $(B)/new
	$(CROSS)gcc $(TCFLAGS) $(STRICT) -c -o $@ $<
$(B)/ref/aboutbox.o: FORCE | $(B)/icons/icon.h
	mkdir -p $(B)/ref
	git show $(REF):./../../ui/aboutbox.c > $(B)/ref/aboutbox.c
	git show $(REF):./../../ui/aboutpen.h > $(B)/ref/aboutpen.h
	$(CROSS)gcc $(TCFLAGS) -c -o $@ $(B)/ref/aboutbox.c
$(B)/icons/icon.h: ../../icons/$(ICON).txt ../../tools/appicon.py
	mkdir -p $(B)/icons
	python3 ../../tools/appicon.py $(ICON) --headers $(B)/icons
$(L)/abouttest.o: abouttest.c $(U)/aboutbox.h | $(B)/icons/icon.h
	mkdir -p $(L)
	$(CROSS)gcc $(TCFLAGS) $(STRICT) -c -o $@ $<
$(L)/draw.o: ../langs/stub/draw.c ../langs/stub/ui.h
	mkdir -p $(L)
	$(CROSS)gcc $(TCFLAGS) -c -o $@ $<
$(L)/%.o: ../../src/%.c
	mkdir -p $(L)
	$(CROSS)gcc $(TCFLAGS) -c -o $@ $<
$(L)/%.o: $(U)/%.c
	mkdir -p $(L)
	$(CROSS)gcc $(TCFLAGS) -c -o $@ $<
$(L)/%.o: $(A)/%.c
	mkdir -p $(L)
	$(CROSS)gcc $(TCFLAGS) -c -o $@ $<
$(L)/%.o: $(A)/%.S
	mkdir -p $(L)
	$(CROSS)gcc -m68000 -c -o $@ $<
FORCE:
.PHONY: FORCE
.SECONDARY:
