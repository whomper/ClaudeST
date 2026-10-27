# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
# The heap (atari/libc.c) on the build machine: make test
# Twice: here with ASan and UBSan, and as 68000 code under QEMU, where
# sizes and pointers are four bytes, as on the Atari (QEMU lets odd
# addresses pass; tests/heap/run.py has a real 68000, in Hatari).
CFLAGS := -O2 -Wall -Wextra -Wno-sign-compare -DHEAP_TEST -Istub
M68K   ?= m68k-linux-gnu-gcc
QEMU   ?= qemu-m68k-static
SRC    := test_heap.c ../../atari/libc.c
B      := build
CASES  := fixed accearly frag grow trim limited tos1 st1 maxblocks exact bins dv1 dv2

$(B)/test_heap: $(SRC) stub/tos.h | $(B)
	cc $(CFLAGS) -fsanitize=address,undefined -fno-sanitize-recover=undefined -o $@ $(SRC)
$(B)/test_heap.m68k: $(SRC) stub/tos.h | $(B)
	$(M68K) -m68000 $(CFLAGS) -static -o $@ $(SRC)
$(B):
	mkdir -p $@
TRACE  := $(B)/ecal-falconmode-3x500.trace

$(TRACE): fixtures/ecal-falconmode-3x500.trace.gz | $(B)
	gzip -dc $< > $@
test: $(B)/test_heap $(TRACE)
	@for c in $(CASES); do ./$(B)/test_heap $$c || exit 1; done
	./$(B)/test_heap replay $(TRACE)
	./$(B)/test_heap random 3000000 1
	./$(B)/test_heap random 3000000 2
	@if command -v $(firstword $(QEMU)) >/dev/null && command -v $(M68K) >/dev/null; then \
	    $(MAKE) --no-print-directory $(B)/test_heap.m68k && \
	    for c in $(CASES); do $(QEMU) ./$(B)/test_heap.m68k $$c || exit 1; done && \
	    $(QEMU) ./$(B)/test_heap.m68k replay $(TRACE) && \
	    $(QEMU) ./$(B)/test_heap.m68k random 1000000 3; \
	else \
	    echo "skipped: the 68000 half (apt install qemu-user-static gcc-m68k-linux-gnu libc6-dev-m68k-cross)"; \
	fi
clean:
	rm -rf $(B)
.PHONY: test clean

# HEAPT.PRG, the same heap on an Atari: make prg, then
#   python3 run.py etos512us.img
CROSS   ?= m68k-linux-gnu-
GCCINC  := $(shell $(CROSS)gcc -print-file-name=include 2>/dev/null)
A       := ../../atari
TCFLAGS := -m68000 -Os -fomit-frame-pointer -ffreestanding -fno-builtin -fno-pic -fno-pie \
           -fno-tree-loop-distribute-patterns -fno-store-merging -fdisable-tree-bswap -fno-asynchronous-unwind-tables \
           -Wall -nostdinc -isystem $(GCCINC) -I$(A)/include -I$(A)
TLDFLAGS := -m68000 -nostdlib -static -no-pie -Wl,--emit-relocs -Wl,-T,$(A)/link.ld \
            -Wl,--build-id=none -Wl,-z,noexecstack -Wl,--no-warn-rwx-segments
T       := $(B)/atari

prg: $(T)/HEAPT.PRG
$(T)/HEAPT.PRG: $(T)/heapt.elf ../../tools/elf2tos.py
	python3 ../../tools/elf2tos.py $< $@
$(T)/heapt.elf: $(T)/crt0.o $(T)/heaptest.o $(T)/libc.o $(T)/tos.o $(T)/nf.o
	$(CROSS)gcc $(TLDFLAGS) -o $@ $^ -lgcc
$(T)/heaptest.o: heaptest.c | $(T)
	$(CROSS)gcc $(TCFLAGS) -c -o $@ $<
$(T)/%.o: $(A)/%.c | $(T)
	$(CROSS)gcc $(TCFLAGS) -c -o $@ $<
$(T)/%.o: $(A)/%.S | $(T)
	$(CROSS)gcc -m68000 -c -o $@ $<
$(T):
	mkdir -p $@
.PHONY: prg
