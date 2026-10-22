# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
# the pictures (src/pic.c, png.c, jpegdc.c, inflate.c, TJpgDec, libnsgif)
# on the build machine, against Pillow: make test (fetch.sh first);
# make test68k runs the same as 68030 code under qemu-m68k. Both JPEG
# decoders: TJpgDec (test_pic, the 68000 programs') and libjpeg-turbo
# (test_pic_lj, -DPIC_LIBJPEG: EWeb's); make testdsp, EWeb's with the
# transform and colours on the Falcon's DSP (dsp/jpeg.a56 on Hatari's core),
# and the dither on it (dsp/dither.a56) against the 68030's; make pens,
# aerapens (tools/pens) against the Atari's own code
S      := ../../src
T      := ../../third_party
SRC    := test_pic.c $(S)/pic.c $(S)/png.c $(S)/jpegdc.c $(S)/inflate.c $(S)/util.c \
          $(T)/nsgif/src/gif.c $(T)/nsgif/src/lzw.c
# TJpgDec's IDCT overflows on damaged data, as it is meant to on a 68000
# (the sums wrap): -fwrapv, as EContact builds it
TJPGD  := $(T)/tjpgd/tjpgd.c
HDR    := $(wildcard $(S)/*.h) $(wildcard $(T)/tjpgd/*.h) $(T)/nsgif/include/nsgif.h
INC    := -I$(S) -I$(T)/tjpgd -I$(T)/nsgif/include -Istub
CFLAGS := -O2 -g -Wall -Wextra -Wno-sign-compare -Wno-unused-parameter
B      := build

SAN    := -fsanitize=address,undefined -fno-sanitize-recover=undefined
LJ     := $(T)/libjpeg-turbo
LJ_C   := jaricom jcomapi jdapimin jdapistd jdarith jdcoefct jdcolor jddctmgr jdhuff jdinput jdmainct jdmarker \
          jdmaster jdmerge jdphuff jdpostct jdsample jdtrans jidctfst jidctint jidctred jmemmgr jmemnobs \
          jquant1 jquant2 jutils
LJ_SRC := $(addprefix $(LJ)/src/,$(addsuffix .c,$(LJ_C)))
LJ_INC := -DPIC_LIBJPEG -DNO_GETENV -DNO_PUTENV -DAERA_NO_FLOAT -I$(LJ)/src -I$(LJ)/config
SRC_LJ := $(filter-out $(S)/jpegdc.c,$(SRC)) $(S)/pic_lj8.c

$(B)/test_pic: $(SRC) $(HDR) $(B)/tjpgd.o | $(B)
	cc $(CFLAGS) $(SAN) $(INC) -o $@ $(SRC) $(B)/tjpgd.o
$(B)/tjpgd.o: $(TJPGD) $(HDR) | $(B)
	cc $(CFLAGS) $(SAN) -fwrapv -w $(INC) -c -o $@ $(TJPGD)
$(B)/test_pic68k: $(SRC) $(TJPGD) $(HDR) | $(B)
	m68k-linux-gnu-gcc $(CFLAGS) -fwrapv -m68030 -static $(INC) -o $@ $(SRC) $(TJPGD)
$(B)/libjpeg.a: $(LJ_SRC) | $(B)
	mkdir -p $(B)/lj && cd $(B)/lj && cc -O2 -g $(SAN) -w -DNO_GETENV -DNO_PUTENV -DAERA_NO_FLOAT -I../../$(LJ)/src -I../../$(LJ)/config \
		-c $(addprefix ../../,$(LJ_SRC))
	rm -f $@ && ar rcs $@ $(B)/lj/*.o
$(B)/test_pic_lj: $(SRC_LJ) $(HDR) $(B)/libjpeg.a | $(B)
	cc $(CFLAGS) $(SAN) $(INC) $(LJ_INC) -o $@ $(SRC_LJ) $(B)/libjpeg.a
$(B)/test_pic_lj68k: $(SRC_LJ) $(LJ_SRC) $(HDR) | $(B)
	m68k-linux-gnu-gcc $(CFLAGS) -w -m68030 -static $(INC) $(LJ_INC) -o $@ $(SRC_LJ) $(LJ_SRC)
# the same with jpeg.a56 on Hatari's DSP core (../ec/fetch-hatari.sh)
HAT    := ../ec/build/hatari
$(B)/hatari.a: | $(B)
	test -f $(HAT)/dsp_core.c || ../ec/fetch-hatari.sh
	mkdir -p $(B)/hat && cd $(B)/hat && cc -O2 -w -I../../$(HAT) -I../../$(HAT)/stub \
		-c ../../$(HAT)/dsp_core.c ../../$(HAT)/dsp_cpu.c
	rm -f $@ && ar rcs $@ $(B)/hat/*.o
$(B)/test_pic_dsp: $(SRC_LJ) dsp_sim.c ../../dsp/jpeg_prog.h ../../dsp/dither_prog.h $(HDR) $(B)/libjpeg.a $(B)/hatari.a | $(B)
	cc $(CFLAGS) $(SAN) $(INC) $(LJ_INC) -DWITH_DSP_SIM -I$(HAT) -I$(HAT)/stub -o $@ $(SRC_LJ) dsp_sim.c \
		$(B)/libjpeg.a $(B)/hatari.a
$(B):
	mkdir -p $@
# aerapens (../../tools/pens), the Pi gateway's pens, against the Atari's
# code under qemu-m68k: here, arm64 and armhf, every picture and screen
pens:
	python3 pens.py -q
# aerapens' requests fuzzed (clang's libFuzzer, AddressSanitizer, UBSan),
# from fuzz/pens (its seeds: requests for the pictures here, both builds,
# each depth): FUZZ_TIME seconds; what it finds goes in fuzz/pens too
FUZZ_TIME ?= 600
$(B)/fuzz/aerapens: ../../tools/pens/aerapens.c $(wildcard $(S)/*.c) | $(B)
	$(MAKE) -s -C ../../tools/pens CC=clang B=$(CURDIR)/$(B)/fuzz PENS_BUILD=fuzz \
		EXTRA="-g -fsanitize=fuzzer-no-link,address,undefined -fno-sanitize-recover=undefined -DPENS_FUZZ" \
		LDFLAGS="-fsanitize=fuzzer,address,undefined"
fuzzpens: $(B)/fuzz/aerapens
	mkdir -p $(B)/fuzz/corpus
	$(B)/fuzz/aerapens -max_total_time=$(FUZZ_TIME) -max_len=65536 -rss_limit_mb=1024 \
		-artifact_prefix=fuzz/pens/crash- $(B)/fuzz/corpus fuzz/pens
# the seeds again on any build (each must run clean): make pensseeds
pensseeds: $(B)/fuzz/aerapens
	$(B)/fuzz/aerapens -runs=0 fuzz/pens
testdsp: $(B)/test_pic_dsp
	python3 run.py $(B)/test_pic_dsp
test: $(B)/test_pic $(B)/test_pic_lj
	python3 run.py $(B)/test_pic
	python3 run.py $(B)/test_pic_lj
test68k: $(B)/test_pic $(B)/test_pic68k $(B)/test_pic_lj $(B)/test_pic_lj68k
	python3 run.py --same $(B)/test_pic "qemu-m68k -cpu m68030 $(B)/test_pic68k"
	python3 run.py --same $(B)/test_pic_lj "qemu-m68k -cpu m68030 $(B)/test_pic_lj68k"
clean:
	rm -rf $(B)
.PHONY: test test68k testdsp pens fuzzpens pensseeds clean
