# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
# TLS.OVL's 68030 assembly (atari/mont030.S, tlsovl/mont31_030.S) against
# BearSSL's C, as 68030 code under qemu-m68k: make test
CROSS  ?= m68k-linux-gnu-
CC     := $(CROSS)gcc
GCCINC := $(shell $(CC) -print-file-name=include 2>/dev/null)
C      := ../..
BSSL   := $(C)/third_party/bearssl
B      := build
CF     := -m68030 -O2 -fomit-frame-pointer -ffreestanding -fno-builtin -fno-pic -fno-pie \
          -fno-tree-loop-distribute-patterns -nostdinc -isystem $(GCCINC) -I$(C)/atari/include \
          -I$(BSSL)/inc -I$(BSSL)/src -I$(C)/src -Wno-stringop-overflow
OBJS   := $(B)/test_030.o $(B)/rsa030.o $(B)/mont030.o $(B)/i31_montmul_030.o $(B)/mont31_030.o \
          $(B)/i31_montmul_c.o

$(B)/test_030: $(OBJS) $(C)/tlsovl/build/libbearssl.a
	$(CC) -m68030 -nostdlib -static -no-pie -Wl,-z,noexecstack -o $@ $(OBJS) $(C)/tlsovl/build/libbearssl.a -lgcc
$(C)/tlsovl/build/libbearssl.a:
	$(MAKE) -C $(C)/tlsovl build/libbearssl.a
$(B)/test_030.o: test_030.c | $(B)
	$(CC) $(CF) -Wall -c -o $@ $<
$(B)/rsa030.o: $(C)/src/rsa030.c $(C)/src/rsa030.h | $(B)
	$(CC) $(CF) -Wall -c -o $@ $<
$(B)/i31_montmul_030.o: $(C)/tlsovl/i31_montmul_030.c | $(B)
	$(CC) $(CF) -w -c -o $@ $<
$(B)/i31_montmul_c.o: $(BSSL)/src/int/i31_montmul.c | $(B)
	$(CC) $(CF) -w -Dbr_i31_montymul=br_i31_montymul_c -c -o $@ $<
$(B)/mont030.o: $(C)/atari/mont030.S | $(B)
	$(CC) -m68030 -c -o $@ $<
$(B)/mont31_030.o: $(C)/tlsovl/mont31_030.S | $(B)
	$(CC) -m68030 -c -o $@ $<
$(B):
	mkdir -p $@
test: $(B)/test_030
	qemu-m68k -cpu m68030 ./$(B)/test_030 2>/dev/null || qemu-m68k ./$(B)/test_030
clean:
	rm -rf $(B)
.PHONY: test clean
