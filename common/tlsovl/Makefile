# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
# TLS.OVL: the family's Direct connection (src/tlsovl.h), 68030 code.
# TLS.OVL here is the committed one, the same bytes in every app: each
# takes it with common/ (as certs/CACERT.PEM) and its release carries it.
#   make          build/TLS.OVL from the sources
#   make check    build/TLS.OVL is TLS.OVL byte for byte (the build is
#                 reproducible: the sources say what the committed file is)
#   make release  after raising VERSION: build/TLS.OVL becomes TLS.OVL
#   make test     the loader on the build machine (tests/tlsovl)
CROSS   ?= m68k-linux-gnu-
CC      := $(CROSS)gcc
GCCINC  := $(shell $(CC) -print-file-name=include 2>/dev/null)
C       := ..
B       := build
VERSION := $(shell cat VERSION)

BSSL    := $(C)/third_party/bearssl
# BearSSL's br_i31_montymul is i31_montmul_030.c here, its loop in 68030
# assembly (mont31_030.S): RSA's i31 code, the prime curves and ECDSA
BSSLSRC := $(filter-out $(BSSL)/src/int/i31_montmul.c,$(wildcard $(BSSL)/src/*/*.c))
BSSLDEF := -DBR_USE_URANDOM=0 -DBR_USE_GETENTROPY=0 -DBR_USE_UNIX_TIME=0 \
           -DBR_USE_WIN32_TIME=0 -DBR_USE_WIN32_RAND=0 -DBR_RDRAND=0
# BearSSL as the programs built it for Falcon mode
BSSLCF  := -m68030 -O2 -fomit-frame-pointer -ffreestanding -fno-builtin -fno-pic -fno-pie \
           -fno-tree-loop-distribute-patterns -fno-asynchronous-unwind-tables -w \
           -nostdinc -isystem $(GCCINC) -I$(C)/atari/include -I$(BSSL)/inc -I$(BSSL)/src $(BSSLDEF)
# the rest small, as the programs' own code is
CFLAGS  := -m68030 -Os -fomit-frame-pointer -ffreestanding -fno-builtin -fno-pic -fno-pie \
           -fno-tree-loop-distribute-patterns -fno-asynchronous-unwind-tables -Wall -Wno-pointer-sign \
           -nostdinc -isystem $(GCCINC) -I$(C)/atari/include -Isrc -I$(C)/src -I$(C)/atari -I$(C)/ui \
           -I$(BSSL)/inc -DTLSOVL_BUILD -DTLS_030 -DLIBC_NO_HEAP -DTLSOVL_VERSION=\"$(VERSION)\"
LDFLAGS := -m68030 -nostdlib -static -no-pie -Wl,--emit-relocs -Wl,-T,ovl.ld \
           -Wl,--build-id=none -Wl,-z,noexecstack -Wl,--no-warn-rwx-segments

SRC     := head.c $(C)/src/tls.c $(C)/src/util.c $(C)/src/ecdsp.c $(C)/ui/dsp.c $(C)/ui/dsp_ec.c \
           $(C)/atari/dsprsa.c $(C)/atari/dspec.c $(C)/atari/libc.c $(C)/src/rsa030.c
OBJS    := $(addprefix $(B)/,$(notdir $(SRC:.c=.o))) $(B)/mont030.o $(B)/i31_montmul_030.o $(B)/mont31_030.o
BSSLOBJ := $(patsubst $(BSSL)/src/%.c,$(B)/bssl/%.o,$(BSSLSRC))
HDRS    := $(wildcard src/*.h $(C)/src/*.h $(C)/atari/*.h $(C)/atari/include/*.h $(C)/dsp/*.h)

all: $(B)/TLS.OVL

$(B)/TLS.OVL: $(B)/tlsovl.elf $(C)/tools/elf2tos.py
	python3 $(C)/tools/elf2tos.py $< $@

$(B)/tlsovl.elf: $(OBJS) $(B)/libbearssl.a ovl.ld
	$(CC) $(LDFLAGS) -o $@ $(OBJS) $(B)/libbearssl.a -lgcc

$(B)/libbearssl.a: $(BSSLOBJ)
	rm -f $@
	$(CROSS)ar rcs $@ $^

$(B)/bssl/%.o: $(BSSL)/src/%.c
	@mkdir -p $(dir $@)
	@$(CC) $(BSSLCF) -c -o $@ $<

$(B)/i31_montmul_030.o: i31_montmul_030.c | $(B)
	$(CC) $(BSSLCF) -Wall -c -o $@ $<
$(B)/mont31_030.o: mont31_030.S | $(B)
	$(CC) -m68030 -c -o $@ $<
$(B)/mont030.o: $(C)/atari/mont030.S | $(B)
	$(CC) -m68030 -c -o $@ $<
$(B)/head.o: head.c $(HDRS) VERSION | $(B)
	$(CC) $(CFLAGS) -c -o $@ $<
$(B)/%.o: $(C)/src/%.c $(HDRS) | $(B)
	$(CC) $(CFLAGS) -c -o $@ $<
$(B)/%.o: $(C)/ui/%.c $(HDRS) | $(B)
	$(CC) $(CFLAGS) -c -o $@ $<
$(B)/%.o: $(C)/atari/%.c $(HDRS) | $(B)
	$(CC) $(CFLAGS) -c -o $@ $<

$(B):
	mkdir -p $@

check: $(B)/TLS.OVL
	@cmp $(B)/TLS.OVL TLS.OVL && echo "TLS.OVL $(VERSION): the committed file is its sources' build"

release: $(B)/TLS.OVL
	@if cmp -s $(B)/TLS.OVL TLS.OVL; then echo "TLS.OVL unchanged"; else \
	  if [ -f TLS.OVL ] && [ "$$(dd if=TLS.OVL bs=1 skip=36 count=7 2>/dev/null | tr -d '\0')" = "$(VERSION)" ]; then \
	    echo "raise VERSION first: TLS.OVL $(VERSION) is out already"; exit 1; fi; \
	  cp $(B)/TLS.OVL TLS.OVL; echo "TLS.OVL $(VERSION) made: commit it with VERSION"; fi

test:
	$(MAKE) -C $(C)/tests/tlsovl test

clean:
	rm -rf $(B)

.PHONY: all check release test clean
