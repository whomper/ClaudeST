# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
# atari/libc.c's memcpy, memmove, memset and memchr (68000 code by hand)
# against plain C, as 68000 and 68030 code under qemu-m68k: make test
CROSS  ?= m68k-linux-gnu-
CC     := $(CROSS)gcc
GCCINC := $(shell $(CC) -print-file-name=include 2>/dev/null)
B      := build
CF     := -Os -fomit-frame-pointer -ffreestanding -fno-builtin -fno-pic -fno-pie \
          -fno-tree-loop-distribute-patterns -nostdinc -isystem $(GCCINC) -Wall -Wno-unused-function
LF     := -nostdlib -static -no-pie -Wl,-z,noexecstack

$(B)/mem.c: ../../atari/libc.c | $(B)
	sed -n '/^int mem_unaligned_ok;/,/^size_t strlen/p' $< | sed '$$d' > $@
$(B)/test_000: test_mem.c $(B)/mem.c
	$(CC) -m68000 $(CF) $(LF) -o $@ test_mem.c -lgcc
$(B)/test_030: test_mem.c $(B)/mem.c
	$(CC) -m68030 -DM68020 $(CF) $(LF) -o $@ test_mem.c -lgcc
$(B):
	mkdir -p $@
test: $(B)/test_000 $(B)/test_030
	qemu-m68k -cpu m68000 ./$(B)/test_000
	qemu-m68k -cpu m68030 ./$(B)/test_030
clean:
	rm -rf $(B)
.PHONY: test clean
