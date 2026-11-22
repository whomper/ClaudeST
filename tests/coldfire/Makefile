# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
# ClaudeST's FireBee edition, its core as ColdFire code (-mcpu=5475) under
# qemu-m68k -cpu cfv4e against the same source on the host (the guidelines,
# 7.4): make test. The runtime (rt.S, libc.c: Linux calls, no C library)
# is common's tests/coldfire's.
CROSS  ?= m68k-linux-gnu-
CC     := $(CROSS)gcc
GCCINC := $(shell $(CC) -print-file-name=include 2>/dev/null)
C      := ../../common
CFT    := $(C)/tests/coldfire
B      := build
QEMU   ?= qemu-m68k -cpu cfv4e
CF     := -mcpu=5475 -O2 -fomit-frame-pointer -ffreestanding -fno-builtin -fno-pic -fno-pie \
          -fno-tree-loop-distribute-patterns -nostdinc -isystem $(GCCINC) -I$(CFT) -I$(C)/src -I../../st -Wall
LD     := $(CC) -mcpu=5475 -nostdlib -static -no-pie -Wl,-z,noexecstack

all: $(B)/test_wrap $(B)/test_wrap_host

$(B):
	mkdir -p $(B)
$(B)/rt.o: $(CFT)/rt.S | $(B)
	$(CC) -mcpu=5475 -c -o $@ $<
$(B)/libc.o: $(CFT)/libc.c | $(B)
	$(CC) $(CF) -c -o $@ $<
$(B)/bidi.o: $(C)/src/bidi.c | $(B)
	$(CC) $(CF) -c -o $@ $<
$(B)/test_wrap.o: test_wrap.c ../../st/textwrap.h | $(B)
	$(CC) $(CF) -c -o $@ $<
$(B)/test_wrap: $(B)/test_wrap.o $(B)/bidi.o $(B)/libc.o $(B)/rt.o
	$(LD) -o $@ $^
$(B)/test_wrap_host: test_wrap.c ../../st/textwrap.h $(C)/src/bidi.c | $(B)
	cc -O2 -Wall -DHOST -I$(C)/src -I../../st -o $@ test_wrap.c $(C)/src/bidi.c

test: all
	./$(B)/test_wrap_host > $(B)/wrap_host.txt
	$(QEMU) $(B)/test_wrap > $(B)/wrap_cf.txt
	cat $(B)/wrap_cf.txt
	cmp $(B)/wrap_host.txt $(B)/wrap_cf.txt && echo "wrap: the ColdFire's layout is the host's, line for line"

clean:
	rm -rf $(B)

.PHONY: all test clean
