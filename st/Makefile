# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
# ClaudeST - builds ../binary/CLAUDEST.PRG with a stock m68k ELF cross compiler
# (Debian/Ubuntu: apt install gcc-m68k-linux-gnu). No MiNTLib needed:
# the program is freestanding and elf2tos.py writes the TOS header.
# ../binary/ is build output, never committed: releases carry the program
# (../.github/workflows/release.yml).
#
# The Atari runtime (start-up, C library, GEM and TOS calls, STinG) and the
# right-to-left layout are the AERA programs' shared code in ../common/, a
# git subtree of github.com/whomper/AERA-Atari: change it there, then
# ../tools/sync-common.sh.

CROSS   ?= m68k-linux-gnu-
CC      := $(CROSS)gcc
GCCINC  := $(shell $(CC) -print-file-name=include 2>/dev/null)
# O and S: where objects go and sources are, for a build from the repository's
# root (common's tools/mkfirebee.py: make -f st/Makefile O=build/ S=st/
# COMMON=common build/claudest.elf); empty for the usual make -C st
O       ?=
S       ?=
COMMON  ?= ../common
CFLAGS  := -m68000 -Os -fomit-frame-pointer -ffreestanding -fno-builtin -ffunction-sections -fdata-sections \
           -fno-pic -fno-pie -fno-tree-loop-distribute-patterns -fno-store-merging -fdisable-tree-bswap \
           -fno-asynchronous-unwind-tables -Wall -Wno-pointer-sign \
           -nostdinc -isystem $(GCCINC) -I$(COMMON)/atari/include -I$(COMMON)/atari -I$(COMMON)/src -I$(COMMON)/ui -I$(S).
# dialogs drawn by the CPU on a 68000 (common dialog.c, DLG_CPU): about a
# third faster on an 8 MHz ST (EFTP, the hub, 1 Nov 2026)
CFLAGS  += -DDLG_CPU
# dialog loops ask the AES for one click, so a press comes at once
# (common dialog.h; the guidelines, 5.8; C16, the hub, 10 Nov 2026)
CFLAGS  += -DDLG_CLICK
# the speed rig's build (../tests/hatari/speed.py): EXTRA_CFLAGS=-DAES_PROFILE=20
CFLAGS  += $(EXTRA_CFLAGS)
LDFLAGS := -m68000 -nostdlib -static -no-pie -Wl,--emit-relocs -Wl,-T,$(COMMON)/atari/link.ld \
           -Wl,--build-id=none -Wl,-z,noexecstack -Wl,--no-warn-rwx-segments -Wl,--gc-sections

OBJS := $(addprefix $(O),crt0.o cpucheck.o claudest.o gem.o tos.o libc.o sting.o sting_s.o nf.o bidi.o crash.o crash_s.o help.o av.o alert.o dialog.o \
        mbar.o popup.o aboutbox.o iconify.o clip.o dd.o bigbuf.o fontsel.o futil.o panebar.o plat_tos.o text.o notify.o aboutline.o offdraw.o click.o hebkey.o arrowed.o theme.o tooltip.o dlgbuild.o field.o eye.o settings.o)
HDRS := $(addprefix $(S),icon.h icon16.h ui.h grect.h plat.h textwrap.h) $(wildcard $(COMMON)/atari/*.h $(COMMON)/atari/include/*.h \
        $(COMMON)/ui/*.h $(COMMON)/src/bidi.h)

all: ../binary/CLAUDEST.PRG

../binary/CLAUDEST.PRG: claudest.elf $(COMMON)/tools/elf2tos.py
	mkdir -p ../binary
	python3 $(COMMON)/tools/elf2tos.py $< $@

$(O)claudest.elf: $(OBJS) $(COMMON)/atari/link.ld
	@mkdir -p $(dir $@)
	$(CC) $(LDFLAGS) -Wl,-Map,$@.map -o $@ $(OBJS) -lgcc

# the speed rig's timed build (-DAES_PROFILE=20 in gem.c and claudest.c),
# linked at the shipped build's places modulo 256 (common/tools/profpad.py;
# the guidelines 1.2, the regression gate): ../binary/prof/CLAUDEST.PRG
PROF_OBJS := $(patsubst gem.o,prof/gem.o,$(patsubst claudest.o,prof/claudest.o,$(OBJS)))
prof/claudest.o: claudest.c $(HDRS)
	mkdir -p prof
	$(CC) $(CFLAGS) -DAES_PROFILE=20 -c -o $@ $<
prof/gem.o: $(COMMON)/atari/gem.c $(HDRS)
	mkdir -p prof
	$(CC) $(CFLAGS) -DAES_PROFILE=20 -c -o $@ $<
prof/claudest.elf: claudest.elf $(PROF_OBJS) $(COMMON)/tools/profpad.py $(COMMON)/atari/codepad.S
	python3 $(COMMON)/tools/profpad.py --ship claudest.elf --out $@ --cpu=-m68000 --dir prof/pads \
	    --exclude claudest.o --exclude prof/claudest.o --exclude gem.o --exclude prof/gem.o \
	    -- $(CC) $(LDFLAGS) $(PROF_OBJS) -lgcc
../binary/prof/CLAUDEST.PRG: prof/claudest.elf
	mkdir -p ../binary/prof
	python3 $(COMMON)/tools/elf2tos.py $< $@
prof: ../binary/prof/CLAUDEST.PRG

$(O)claudest.o: $(S)claudest.c $(HDRS)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -c -o $@ $<
$(O)plat_tos.o: $(S)plat_tos.c $(HDRS)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -c -o $@ $<
$(O)%.o: $(COMMON)/atari/%.c $(HDRS)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -c -o $@ $<
$(O)%.o: $(COMMON)/src/%.c $(HDRS)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -c -o $@ $<
$(O)%.o: $(COMMON)/ui/%.c $(HDRS)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -c -o $@ $<

# a 24 KB stack (48 KB by default): enough for ClaudeST
$(O)crt0.o: $(COMMON)/atari/crt0.S
	@mkdir -p $(@D)
	$(CC) -m68000 -DSTACK_SIZE=24576 -c -o $@ $<
$(O)sting_s.o: $(COMMON)/atari/sting.S
	@mkdir -p $(@D)
	$(CC) -m68000 -c -o $@ $<
$(O)nf.o: $(COMMON)/atari/nf.S
	@mkdir -p $(@D)
	$(CC) -m68000 -c -o $@ $<
$(O)crash_s.o: $(COMMON)/atari/crash.S
	@mkdir -p $(@D)
	$(CC) -m68000 -c -o $@ $<

clean:
	rm -rf *.o claudest.elf claudest.elf.map build.h ../binary/CLAUDEST.PRG prof ../binary/prof

# host-side unit test for the right-to-left layout
test: bidi_test.c $(COMMON)/src/bidi.c $(COMMON)/src/bidi.h
	cc -O2 -Wall -I$(COMMON)/src -o /tmp/bidi_test bidi_test.c $(COMMON)/src/bidi.c && /tmp/bidi_test

.PHONY: all clean test prof FORCE
