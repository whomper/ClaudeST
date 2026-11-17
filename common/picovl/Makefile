# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
# PIC.OVL: the Falcon's DSP for pictures (src/picovl.h), 68030 code: pic.c's
# dither on the DSP (dsp/dither.a56) through atari/dspjpeg.c. As tlsovl:
# pic.ovl here is the committed one, the same bytes in every app that
# carries it.
#   make          build/pic.ovl from the sources
#   make check    build/pic.ovl is pic.ovl byte for byte
#   make release  after raising VERSION: build/pic.ovl becomes pic.ovl
CROSS   ?= m68k-linux-gnu-
CC      := $(CROSS)gcc
GCCINC  := $(shell $(CC) -print-file-name=include 2>/dev/null)
C       := ..
B       := build
VERSION := $(shell cat VERSION)

CFLAGS  := -m68030 -Os -fomit-frame-pointer -ffreestanding -fno-builtin -fno-pic -fno-pie \
           -fno-tree-loop-distribute-patterns -fno-asynchronous-unwind-tables -Wall -Wno-pointer-sign \
           -ffunction-sections -fdata-sections \
           -nostdinc -isystem $(GCCINC) -Iinclude -I$(C)/atari/include -I$(C)/src -I$(C)/atari \
           -I$(C)/third_party/tjpgd -I$(C)/third_party/nsgif/include \
           -DPIC_TJPGD_DSP -DPIC_OVL_BUILD -DPIC_NO_PNG -DPIC_NO_GIF -DLIBC_NO_HEAP \
           -DPICOVL_VERSION=\"$(VERSION)\"
LDFLAGS := -m68030 -nostdlib -static -no-pie -Wl,--emit-relocs -Wl,-T,ovl.ld -Wl,--gc-sections \
           -Wl,--build-id=none -Wl,-z,noexecstack -Wl,--no-warn-rwx-segments

SRC     := head.c $(C)/src/pic.c $(C)/atari/dspjpeg.c $(C)/atari/libc.c
OBJS    := $(addprefix $(B)/,$(notdir $(SRC:.c=.o)))
HDRS    := $(wildcard $(C)/src/*.h $(C)/atari/*.h $(C)/atari/include/*.h $(C)/dsp/*.h)

all: $(B)/pic.ovl

$(B)/pic.ovl: $(B)/picovl.elf $(C)/tools/elf2tos.py
	python3 $(C)/tools/elf2tos.py $< $@

$(B)/picovl.elf: $(OBJS) ovl.ld
	$(CC) $(LDFLAGS) -o $@ $(OBJS) -lgcc

$(B)/head.o: head.c $(HDRS) | $(B)
	$(CC) $(CFLAGS) -c -o $@ $<
$(B)/%.o: $(C)/src/%.c $(HDRS) | $(B)
	$(CC) $(CFLAGS) -c -o $@ $<
$(B)/%.o: $(C)/atari/%.c $(HDRS) | $(B)
	$(CC) $(CFLAGS) -c -o $@ $<

$(B):
	mkdir -p $@

check: $(B)/pic.ovl
	cmp $(B)/pic.ovl pic.ovl

release: $(B)/pic.ovl
	cp $(B)/pic.ovl pic.ovl

clean:
	rm -rf $(B)

.PHONY: all check release clean
