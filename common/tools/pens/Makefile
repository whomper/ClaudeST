# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
# aerapens (aerapens.c): a picture made into an Atari screen's pens on the
# Pi's gateway, both of EWeb's builds in one program: pic.c with
# libjpeg-turbo (EWEB.PRG's) and pic.c again with TJpgDec (EWEBST.PRG's,
# its names made tj_ by pens_tj.h; nm checks none is left). make builds
# build/aerapens for this machine; make pi, static ones for a Pi
# (build/arm64, build/armhf: Pi 2 and later). PENS_COMMON= and PENS_BUILD=
# name the AERA-Atari commit and the build (its -v line, every answer).
# -fsigned-char: an ARM's char is unsigned, the 68000's signed; -fwrapv:
# TJpgDec's sums wrap, as on a 68000. ../../tests/pic: make pens checks it
# against the Atari's code, make fuzzpens fuzzes it.
CC     ?= cc
NM     ?= nm
B      ?= build
S      := ../../src
T      := ../../third_party
PENS_COMMON ?= $(shell git -C $(CURDIR) rev-parse --short=12 HEAD 2>/dev/null || echo unknown)
PENS_BUILD  ?= local
CFLAGS := $(ARCH) -O2 -std=gnu99 -fsigned-char -fwrapv
WARN   := -Wall -Wextra -Wno-sign-compare -Wno-unused-parameter
DEFS   := -DNO_GETENV -DNO_PUTENV -DAERA_NO_FLOAT
LJ     := $(T)/libjpeg-turbo
LJ_C   := jaricom jcomapi jdapimin jdapistd jdarith jdcoefct jdcolor jddctmgr jdhuff jdinput jdmainct jdmarker \
          jdmaster jdmerge jdphuff jdpostct jdsample jdtrans jidctfst jidctint jidctred jmemmgr jmemnobs \
          jquant1 jquant2 jutils
INC    := -I$(S) -I$(T)/nsgif/include -Istub
LJ_INC := -DPIC_LIBJPEG -I$(LJ)/src -I$(LJ)/config
HDR    := $(wildcard $(S)/*.h) $(wildcard $(LJ)/src/*.h) $(wildcard $(T)/tjpgd/*.h) pens_tj.h stub/plat.h
OBJ    := main pic_lj pic_tj pic_lj8 png inflate util jpegdc tjpgd gif lzw $(LJ_C)

all: $(B)/aerapens

# the third parties' warnings are theirs (-w), as the Atari builds have them
$(B)/main.o: aerapens.c $(HDR) | $(B)
	$(CC) $(CFLAGS) $(WARN) $(DEFS) $(INC) -DPENS_COMMON='"$(PENS_COMMON)"' -DPENS_BUILD='"$(PENS_BUILD)"' \
		$(EXTRA) -c -o $@ $<
$(B)/pic_lj.o: $(S)/pic.c $(HDR) | $(B)
	$(CC) $(CFLAGS) $(WARN) $(DEFS) $(INC) $(LJ_INC) $(EXTRA) -c -o $@ $<
$(B)/pic_tj.o: $(S)/pic.c $(HDR) | $(B)
	$(CC) $(CFLAGS) $(WARN) $(DEFS) $(INC) -I$(T)/tjpgd -include pens_tj.h $(EXTRA) -c -o $@ $<
	@left=$$($(NM) -g --defined-only $@ | awk '{print $$3}' | grep -v -e '^tj_' -e '^__' || true); \
	if [ -n "$$left" ]; then echo "pens_tj.h lacks $$left"; rm -f $@; exit 1; fi
$(B)/pic_lj8.o: $(S)/pic_lj8.c $(HDR) | $(B)
	$(CC) $(CFLAGS) $(WARN) $(DEFS) $(INC) $(LJ_INC) $(EXTRA) -c -o $@ $<
$(B)/jpegdc.o: $(S)/jpegdc.c $(HDR) | $(B)
	$(CC) $(CFLAGS) $(WARN) $(DEFS) $(INC) -I$(T)/tjpgd $(EXTRA) -c -o $@ $<
$(B)/%.o: $(S)/%.c $(HDR) | $(B)
	$(CC) $(CFLAGS) $(WARN) $(DEFS) $(INC) $(EXTRA) -c -o $@ $<
$(B)/tjpgd.o: $(T)/tjpgd/tjpgd.c $(HDR) | $(B)
	$(CC) $(CFLAGS) -w $(DEFS) $(INC) -I$(T)/tjpgd $(EXTRA) -c -o $@ $<
$(B)/%.o: $(T)/nsgif/src/%.c $(HDR) | $(B)
	$(CC) $(CFLAGS) -w $(DEFS) -DNDEBUG $(INC) $(EXTRA) -c -o $@ $<
$(B)/%.o: $(LJ)/src/%.c $(HDR) | $(B)
	$(CC) $(CFLAGS) -w $(DEFS) $(INC) $(LJ_INC) $(EXTRA) -c -o $@ $<
$(B)/aerapens: $(addprefix $(B)/,$(addsuffix .o,$(OBJ)))
	$(CC) $(CFLAGS) $(EXTRA) -o $@ $^ $(LDFLAGS)

pi:
	$(MAKE) CC=aarch64-linux-gnu-gcc NM=aarch64-linux-gnu-nm B=build/arm64 LDFLAGS=-static
	$(MAKE) CC=arm-linux-gnueabihf-gcc NM=arm-linux-gnueabihf-nm B=build/armhf LDFLAGS=-static
$(B):
	mkdir -p $@
clean:
	rm -rf build
.PHONY: all pi clean
