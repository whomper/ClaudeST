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
COMMON  := ../common
CFLAGS  := -m68000 -Os -fomit-frame-pointer -ffreestanding -fno-builtin \
           -fno-pic -fno-pie -fno-tree-loop-distribute-patterns -fno-store-merging -fdisable-tree-bswap \
           -fno-asynchronous-unwind-tables -Wall -Wno-pointer-sign \
           -nostdinc -isystem $(GCCINC) -I$(COMMON)/atari/include -I$(COMMON)/atari -I$(COMMON)/src -I$(COMMON)/ui -I.
# the speed rig's build (../tests/hatari/speed.py): EXTRA_CFLAGS=-DAES_PROFILE=20
CFLAGS  += $(EXTRA_CFLAGS)
LDFLAGS := -m68000 -nostdlib -static -no-pie -Wl,--emit-relocs -Wl,-T,$(COMMON)/atari/link.ld \
           -Wl,--build-id=none -Wl,-z,noexecstack -Wl,--no-warn-rwx-segments

OBJS := crt0.o claudest.o gem.o tos.o libc.o sting.o sting_s.o nf.o bidi.o crash.o crash_s.o help.o av.o alert.o dialog.o \
        mbar.o popup.o aboutbox.o iconify.o clip.o dd.o bigbuf.o fontsel.o futil.o panebar.o plat_tos.o
HDRS := icon.h icon16.h ui.h grect.h plat.h $(wildcard $(COMMON)/atari/*.h $(COMMON)/atari/include/*.h \
        $(COMMON)/ui/*.h $(COMMON)/src/bidi.h)

all: ../binary/CLAUDEST.PRG

../binary/CLAUDEST.PRG: claudest.elf $(COMMON)/tools/elf2tos.py
	mkdir -p ../binary
	python3 $(COMMON)/tools/elf2tos.py $< $@

claudest.elf: $(OBJS) $(COMMON)/atari/link.ld
	$(CC) $(LDFLAGS) -o $@ $(OBJS) -lgcc

claudest.o: claudest.c $(HDRS)
	$(CC) $(CFLAGS) -c -o $@ $<
%.o: $(COMMON)/atari/%.c $(HDRS)
	$(CC) $(CFLAGS) -c -o $@ $<
%.o: $(COMMON)/src/%.c $(HDRS)
	$(CC) $(CFLAGS) -c -o $@ $<
%.o: $(COMMON)/ui/%.c $(HDRS)
	$(CC) $(CFLAGS) -c -o $@ $<

# a 24 KB stack (48 KB by default): enough for ClaudeST
crt0.o: $(COMMON)/atari/crt0.S
	$(CC) -m68000 -DSTACK_SIZE=24576 -c -o $@ $<
sting_s.o: $(COMMON)/atari/sting.S
	$(CC) -m68000 -c -o $@ $<
nf.o: $(COMMON)/atari/nf.S
	$(CC) -m68000 -c -o $@ $<
crash_s.o: $(COMMON)/atari/crash.S
	$(CC) -m68000 -c -o $@ $<

clean:
	rm -f *.o claudest.elf build.h ../binary/CLAUDEST.PRG

# host-side unit test for the right-to-left layout
test: bidi_test.c $(COMMON)/src/bidi.c $(COMMON)/src/bidi.h
	cc -O2 -Wall -I$(COMMON)/src -o /tmp/bidi_test bidi_test.c $(COMMON)/src/bidi.c && /tmp/bidi_test

.PHONY: all clean test FORCE
