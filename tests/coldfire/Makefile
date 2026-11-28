# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
# common's assembly built for the ColdFire (-mcpu=5475, the FireBee) and run
# under qemu-m68k -cpu cfv4e, as Linux programs with no C library (the m68k
# one here is 68020 code): make test. make same: each .S in atari/ and
# tlsovl/ built for the 68000 and the 68030 is byte for byte what BASE
# (origin/main) builds.
CROSS  ?= m68k-linux-gnu-
CC     := $(CROSS)gcc
GCCINC := $(shell $(CC) -print-file-name=include 2>/dev/null)
C      := ../..
B      := build
QEMU   ?= qemu-m68k -cpu cfv4e
BASE   ?= origin/main
CF     := -mcpu=5475 -O2 -fomit-frame-pointer -ffreestanding -fno-builtin -fno-pic -fno-pie \
          -fno-tree-loop-distribute-patterns -nostdinc -isystem $(GCCINC) -I. -I$(C)/src -Wall
LD     := $(CC) -mcpu=5475 -nostdlib -static -no-pie -Wl,-z,noexecstack
TESTS  := $(B)/test_crt0 $(B)/test_crt0_acc $(B)/test_sting $(B)/test_nf $(B)/test_inflate $(B)/test_inflate_c $(B)/test_mont $(B)/test_mont_host $(B)/test_crash $(B)/test_tls $(B)/test_cpucheck_cf $(B)/test_cpucheck_030 $(B)/test_cpucheck_c_cf $(B)/test_cpucheck_c_none $(B)/test_cpucheck_acc1_030 $(B)/test_cpucheck_acc0_030 $(B)/test_cpucheck_acc2_030 $(B)/test_cpucheck_c_acc $(B)/test_cpucheck_c_acc0 $(B)/test_cpucheck_c_acc2 $(B)/test_rows_cf $(B)/test_rows_030 $(B)/test_ll64 $(B)/test_ll64_host $(B)/test_tlsfb

all: $(TESTS)

$(B)/rt.o: rt.S | $(B)
	$(CC) -mcpu=5475 -c -o $@ $<
$(B)/rt_nostart.o: rt.S | $(B)
	$(CC) -mcpu=5475 -DNO_START -c -o $@ $<
$(B)/%.o: %.c rt.h gemdos.h | $(B)
	$(CC) $(CF) -c -o $@ $<

# crt0.S: its _start renamed, entered by crt0_start.S after the GEMDOS stand-in
$(B)/crt0.o: $(C)/atari/crt0.S | $(B)
	$(CC) -mcpu=5475 -c -o $@ $<
	$(CROSS)objcopy --redefine-sym _start=crt0_start $@
$(B)/crt0_start.o: crt0_start.S | $(B)
	$(CC) -mcpu=5475 -c -o $@ $<
$(B)/crt0_start_acc.o: crt0_start.S | $(B)
	$(CC) -mcpu=5475 -DACC -c -o $@ $<
$(B)/test_crt0_acc.o: test_crt0.c rt.h gemdos.h | $(B)
	$(CC) $(CF) -DACC -c -o $@ $<
$(B)/test_crt0: $(B)/crt0_start.o $(B)/crt0.o $(B)/test_crt0.o $(B)/gemdos.o $(B)/libc.o $(B)/rt_nostart.o
	$(LD) -o $@ $^
$(B)/test_crt0_acc: $(B)/crt0_start_acc.o $(B)/crt0.o $(B)/test_crt0_acc.o $(B)/gemdos.o $(B)/libc.o $(B)/rt_nostart.o
	$(LD) -o $@ $^

# sting.S: sting_call against Pure C stand-ins (sting_fn.S)
$(B)/sting.o: $(C)/atari/sting.S | $(B)
	$(CC) -mcpu=5475 -c -o $@ $<
$(B)/sting_fn.o: sting_fn.S | $(B)
	$(CC) -mcpu=5475 -c -o $@ $<
$(B)/test_sting: $(B)/test_sting.o $(B)/sting.o $(B)/sting_fn.o $(B)/libc.o $(B)/rt.o
	$(LD) -o $@ $^

$(B)/nf.o: $(C)/atari/nf.S | $(B)
	$(CC) -mcpu=5475 -c -o $@ $<
$(B)/test_nf: $(B)/test_nf.o $(B)/nf.o $(B)/libc.o $(B)/rt.o
	$(LD) -o $@ $^

# inflate_fast.S: tests/inflate's asm_check corpus through inflate_piece
# with the ColdFire loop (-DINFLATE_ASM) and with the C's, both as ColdFire
# code, each compared with the host's C byte for byte
INF    := $(C)/tests/inflate
$(B)/inflate_fast.o: $(C)/atari/inflate_fast.S | $(B)
	$(CC) -mcpu=5475 -c -o $@ $<
$(B)/inflate_asm.o: $(C)/src/inflate.c | $(B)
	$(CC) $(CF) -Iinc -I$(INF)/stub -DINFLATE_ASM -c -o $@ $<
$(B)/inflate_c.o: $(C)/src/inflate.c | $(B)
	$(CC) $(CF) -Iinc -I$(INF)/stub -c -o $@ $<
$(B)/test_inflate.o: test_inflate.c rt.h | $(B)
	$(CC) $(CF) -Iinc -I$(INF)/stub -c -o $@ $<
$(B)/test_inflate: $(B)/test_inflate.o $(B)/inflate_asm.o $(B)/inflate_fast.o $(B)/libc.o $(B)/rt.o
	$(LD) -o $@ $^
$(B)/test_inflate_c: $(B)/test_inflate.o $(B)/inflate_c.o $(B)/libc.o $(B)/rt.o
	$(LD) -o $@ $^
$(INF)/build/corpus.bin $(INF)/build/out_c.bin:
	$(MAKE) -C $(INF) build/asm_check_c
	$(INF)/build/asm_check_c gen $(INF)/build/corpus.bin
	$(INF)/build/asm_check_c run $(INF)/build/corpus.bin $(INF)/build/out_c.bin

# mont030.S: Montgomery products, doublings and rsa030.c's RSA against the
# same program on the host with the two in 64-bit C
$(B)/mont030.o: $(C)/atari/mont030.S | $(B)
	$(CC) -mcpu=5475 -c -o $@ $<
$(B)/rsa030.o: $(C)/src/rsa030.c | $(B)
	$(CC) $(CF) -Iinc -c -o $@ $<
$(B)/test_mont.o: test_mont.c rt.h | $(B)
	$(CC) $(CF) -Iinc -c -o $@ $<
$(B)/test_mont: $(B)/test_mont.o $(B)/rsa030.o $(B)/mont030.o $(B)/libc.o $(B)/rt.o
	$(LD) -o $@ $^
$(B)/test_mont_host: test_mont.c $(C)/src/rsa030.c | $(B)
	cc -O2 -Wall -DHOST -I$(C)/src -o $@ test_mont.c $(C)/src/rsa030.c

# crash.S: its entries through a ColdFire frame (crash_fake.S), crash.c's
# part stood in for in test_crash.c
$(B)/crash.o: $(C)/atari/crash.S | $(B)
	$(CC) -mcpu=5475 -c -o $@ $<
$(B)/crash_fake.o: crash_fake.S | $(B)
	$(CC) -mcpu=5475 -c -o $@ $<
$(B)/test_crash: $(B)/test_crash.o $(B)/crash.o $(B)/crash_fake.o $(B)/gemdos.o $(B)/libc.o $(B)/rt.o
	$(LD) -Wl,-N -Wl,--no-warn-rwx-segments -o $@ $^

# TLS without the DSP: tests/tls030's test_030.c (br_i31_montymul with
# mont31_030.S against BearSSL's C, rsa030 with mont030.S against
# br_rsa_i31_public) with BearSSL, the assembly and ll64.c (in place of the
# 68020 libgcc) all ColdFire code
BSSL    := $(C)/third_party/bearssl
BSSLSRC := $(filter-out $(BSSL)/src/int/i31_montmul.c,$(wildcard $(BSSL)/src/*/*.c))
BSSLOBJ := $(patsubst $(BSSL)/src/%.c,$(B)/bssl/%.o,$(BSSLSRC))
BSSLCF  := -mcpu=5475 -O2 -fomit-frame-pointer -ffreestanding -fno-builtin -fno-pic -fno-pie \
           -fno-tree-loop-distribute-patterns -w -nostdinc -isystem $(GCCINC) -I$(C)/atari/include \
           -I$(BSSL)/inc -I$(BSSL)/src -DBR_USE_URANDOM=0 -DBR_USE_GETENTROPY=0 -DBR_USE_UNIX_TIME=0 \
           -DBR_USE_WIN32_TIME=0 -DBR_USE_WIN32_RAND=0 -DBR_RDRAND=0
$(B)/bssl/%.o: $(BSSL)/src/%.c
	@mkdir -p $(dir $@)
	@$(CC) $(BSSLCF) -c -o $@ $<
$(B)/libbearssl.a: $(BSSLOBJ)
	rm -f $@
	$(CROSS)ar rcs $@ $^
$(B)/tls_test_030.o: $(C)/tests/tls030/test_030.c | $(B)
	$(CC) $(BSSLCF) -Wall -I$(C)/src -c -o $@ $<
$(B)/tls_rsa030.o: $(C)/src/rsa030.c | $(B)
	$(CC) $(BSSLCF) -I$(C)/src -c -o $@ $<
$(B)/tls_i31_030.o: $(C)/tlsovl/i31_montmul_030.c | $(B)
	$(CC) $(BSSLCF) -c -o $@ $<
$(B)/tls_i31_c.o: $(BSSL)/src/int/i31_montmul.c | $(B)
	$(CC) $(BSSLCF) -Dbr_i31_montymul=br_i31_montymul_c -c -o $@ $<
$(B)/mont31_030.o: $(C)/tlsovl/mont31_030.S | $(B)
	$(CC) -mcpu=5475 -c -o $@ $<
$(B)/ll64.o: $(C)/atari/ll64.c | $(B)
	$(CC) $(BSSLCF) -c -o $@ $<
$(B)/test_tls: $(B)/tls_test_030.o $(B)/tls_rsa030.o $(B)/mont030.o $(B)/tls_i31_030.o $(B)/mont31_030.o \
               $(B)/tls_i31_c.o $(B)/libbearssl.a $(B)/ll64.o
	$(LD) -o $@ $(filter-out $(B)/libbearssl.a $(B)/ll64.o,$^) $(B)/libbearssl.a $(B)/ll64.o

# cpucheck_cf.S: the same bytes for the 68000 and the ColdFire, then run
# as ColdFire code (_CF_ in the jar) and in a 68030 program (no _CF_)
$(B)/cpucheck_cf.o: $(C)/atari/cpucheck_cf.S | $(B)
	$(CC) -mcpu=5475 -c -o $@ $<
	$(CC) -m68000 -c -o $(B)/cpucheck_000.o $<
	$(CROSS)objcopy -O binary -j .text -j .data $@ $(B)/k_cf.bin
	$(CROSS)objcopy -O binary -j .text -j .data $(B)/cpucheck_000.o $(B)/k_000.bin
	cmp $(B)/k_cf.bin $(B)/k_000.bin
	$(CROSS)objcopy --redefine-sym main=main_check $@
$(B)/test_cpucheck_cf: test_cpucheck.c $(B)/cpucheck_cf.o $(B)/gemdos.o $(B)/libc.o $(B)/rt.o
	$(CC) $(CF) -DWANT_CF -c -o $(B)/test_cpucheck_cf.o test_cpucheck.c
	$(LD) -o $@ $(B)/test_cpucheck_cf.o $(B)/cpucheck_cf.o $(B)/gemdos.o $(B)/libc.o $(B)/rt.o
# (the harness as 68030 code: rt.S is both; libgcc for its sums)
$(B)/test_cpucheck_030: test_cpucheck.c $(B)/cpucheck_cf.o gemdos.c libc.c rt.S
	$(CC) $(subst -mcpu=5475,-m68030,$(CF)) -c -o $(B)/t030.o test_cpucheck.c
	$(CC) $(subst -mcpu=5475,-m68030,$(CF)) -c -o $(B)/gemdos030.o gemdos.c
	$(CC) $(subst -mcpu=5475,-m68030,$(CF)) -c -o $(B)/libc030.o libc.c
	$(CC) -m68030 -c -o $(B)/rt030.o rt.S
	$(CC) -m68030 -nostdlib -static -no-pie -Wl,-z,noexecstack -o $@ $(B)/t030.o $(B)/cpucheck_cf.o \
		$(B)/gemdos030.o $(B)/libc030.o $(B)/rt030.o -lgcc

# an accessory on the wrong machine (cpucheck_cf.S as a 68030 runs it): it waits on evnt_mesag, its
# entry registered and its alert at AC_OPEN (cpu_acc_name), or silent without one (decided 22 Nov 2026)
$(B)/test_cpucheck_acc%_030: test_cpucheck.c $(B)/cpucheck_cf.o $(B)/test_cpucheck_030
	$(CC) $(subst -mcpu=5475,-m68030,$(CF)) -DACC $(if $(filter 0,$*),,-DACC_NAME) $(if $(filter 2,$*),-DACC_ALERT) -c -o $@.o test_cpucheck.c
	$(CC) -m68030 -nostdlib -static -no-pie -Wl,-z,noexecstack -o $@ $@.o $(B)/cpucheck_cf.o \
		$(B)/gemdos030.o $(B)/libc030.o $(B)/rt030.o -lgcc

# cpucheck.c: as an app builds it (68000 code, its main renamed), run in a
# 68030 harness with and without cpu_cf_alert
$(B)/cpucheck_c.o: $(C)/atari/cpucheck.c | $(B)
	$(CC) $(subst -mcpu=5475,-m68000,$(CF)) -Os -c -o $@ $<
	$(CROSS)objcopy --redefine-sym main=main_check $@
$(B)/test_cpucheck_c_%: test_cpucheck_c.c $(B)/cpucheck_c.o $(B)/test_cpucheck_030
	$(CC) $(subst -mcpu=5475,-m68030,$(CF)) $(if $(filter cf,$*),-DCF_ALERT) \
		$(if $(filter acc acc0 acc2,$*),-DACC) $(if $(filter acc acc2,$*),-DACC_NAME) \
		$(if $(filter acc2,$*),-DACC_ALERT) -c -o $@.o test_cpucheck_c.c
	$(CC) -m68030 -nostdlib -static -no-pie -Wl,-z,noexecstack -o $@ $@.o $(B)/cpucheck_c.o \
		$(B)/gemdos030.o $(B)/libc030.o $(B)/rt030.o -lgcc

# atari/ll64.c: as ColdFire code against the host's own 64-bit arithmetic
$(B)/test_ll64: test_ll64.c $(B)/ll64.o $(B)/libc.o $(B)/rt.o
	$(CC) $(CF) -o $@.o -c test_ll64.c
	$(LD) -o $@ $@.o $(B)/ll64.o $(B)/libc.o $(B)/rt.o
$(B)/test_ll64_host: test_ll64.c | $(B)
	cc -O2 -Wall -DHOST -o $@ test_ll64.c

# ui/text.c's row loops: C on the ColdFire, the assembly on a 68030, one sum
$(B)/rows.c: $(C)/ui/text.c | $(B)
	(echo '#ifdef __mcoldfire__'; sed -n "/^\/\* (a ColdFire's: the same rows in C/,/^static void rows_copy_mid(/p" $< | sed '$$d') > $@
$(B)/test_rows_cf: test_rows.c $(B)/rows.c $(B)/libc.o $(B)/rt.o
	$(CC) $(CF) -I$(B) -o $@.o -c test_rows.c
	$(LD) -o $@ $@.o $(B)/libc.o $(B)/rt.o
$(B)/test_rows_030: test_rows.c $(B)/rows.c libc.c rt.S
	$(CC) $(subst -mcpu=5475,-m68030,$(CF)) -I$(B) -o $@.o -c test_rows.c
	$(CC) $(subst -mcpu=5475,-m68030,$(CF)) -c -o $(B)/libc030r.o libc.c
	$(CC) -m68030 -c -o $(B)/rt030r.o rt.S
	$(CC) -m68030 -nostdlib -static -no-pie -Wl,-z,noexecstack -o $@ $@.o $(B)/libc030r.o $(B)/rt030r.o -lgcc

# tlsovl/tlsfb.ovl, the shipped file: loaded by src/ovl.c (ColdFire code),
# its table checked, init() given a TLS_HOST of Linux calls, then TLS 1.2
# to tlsfb_run.py's servers on 127.0.0.1 (certificates made by openssl)
TLSVER := $(shell cat $(C)/tlsovl/VERSION)
$(B)/ovl_cf.o: $(C)/src/ovl.c $(C)/src/ovl.h | $(B)
	$(CC) $(CF) -Iinc -I$(C)/tlsovl/src -c -o $@ $<
$(B)/test_tlsfb.o: test_tlsfb.c rt.h $(C)/src/tlsovl.h $(C)/tlsovl/VERSION | $(B)
	$(CC) $(CF) -Iinc -DWANT_VERSION=\"$(TLSVER)\" -c -o $@ $<
# (-N: the heap the overlay is loaded into executable, as all of TOS's memory is)
$(B)/test_tlsfb: $(B)/test_tlsfb.o $(B)/ovl_cf.o $(B)/libc.o $(B)/rt.o
	$(LD) -Wl,-N -Wl,--no-warn-rwx-segments -o $@ $^

# the C files given a ColdFire path, for the 68000 and the 68030 against
# BASE's (make same)
CSAME  := atari/gem.c src/ovl.c ui/text.c atari/libc.c src/inflate.c atari/crash.c tlsovl/head.c
CSINC  := -O2 -ffreestanding -fno-builtin -nostdinc -isystem $(GCCINC) -I$(C)/atari/include -I$(C)/src \
          -I$(C)/atari -I$(C)/ui -I$(C)/tlsovl/src -I$(C)/tests/tlsovl/stub -I$(C)/third_party/bearssl/inc -w

$(B):
	mkdir -p $@

test: $(TESTS)
	$(QEMU) $(B)/test_crt0
	$(QEMU) $(B)/test_crt0_acc
	$(QEMU) $(B)/test_sting
	$(QEMU) $(B)/test_nf
	$(MAKE) $(INF)/build/corpus.bin
	$(QEMU) $(B)/test_inflate $(INF)/build/corpus.bin $(B)/out_asm.bin
	cmp $(INF)/build/out_c.bin $(B)/out_asm.bin
	$(QEMU) $(B)/test_inflate_c $(INF)/build/corpus.bin $(B)/out_c.bin
	cmp $(INF)/build/out_c.bin $(B)/out_c.bin
	@echo "inflate: the ColdFire loop and the C as ColdFire code give the host's C every byte and answer"
	./$(B)/test_mont_host > $(B)/mont_host.txt
	$(QEMU) $(B)/test_mont > $(B)/mont_cf.txt
	cat $(B)/mont_cf.txt
	$(QEMU) $(B)/test_tls
	python3 tlsfb_run.py $(B)/tlsfb $(C)/tlsovl/tlsfb.ovl $(C)/certs/CACERT.PEM $(QEMU) $(B)/test_tlsfb
	./$(B)/test_ll64_host > $(B)/ll64_host.txt
	$(QEMU) $(B)/test_ll64 > $(B)/ll64_cf.txt
	cat $(B)/ll64_cf.txt
	cmp $(B)/ll64_host.txt $(B)/ll64_cf.txt && echo "ll64: the ColdFire's 64-bit arithmetic gives the host's every answer"
	$(QEMU) $(B)/test_rows_cf > $(B)/rows_cf.txt
	qemu-m68k -cpu m68030 $(B)/test_rows_030 > $(B)/rows_030.txt
	cat $(B)/rows_cf.txt
	cmp $(B)/rows_cf.txt $(B)/rows_030.txt && echo "rows: text.c's C rows on the ColdFire give its assembly's every byte"
	$(QEMU) $(B)/test_cpucheck_cf
	qemu-m68k -cpu m68030 $(B)/test_cpucheck_030
	qemu-m68k -cpu m68030 $(B)/test_cpucheck_c_cf
	qemu-m68k -cpu m68030 $(B)/test_cpucheck_c_none
	qemu-m68k -cpu m68030 $(B)/test_cpucheck_acc1_030
	qemu-m68k -cpu m68030 $(B)/test_cpucheck_acc0_030
	qemu-m68k -cpu m68030 $(B)/test_cpucheck_c_acc
	qemu-m68k -cpu m68030 $(B)/test_cpucheck_c_acc0
	qemu-m68k -cpu m68030 $(B)/test_cpucheck_acc2_030
	qemu-m68k -cpu m68030 $(B)/test_cpucheck_c_acc2
	$(QEMU) $(B)/test_crash
	$(QEMU) $(B)/test_crash take
	$(QEMU) $(B)/test_crash linef
	cmp $(B)/mont_host.txt $(B)/mont_cf.txt && echo "mont: the ColdFire's mont030, dbl030 and RSA give the host's C every word"

# every .S in atari/ and tlsovl/ for the 68000 and the 68030, against BASE's
same: | $(B)
	@ok=1; for f in $(C)/atari/*.S $(C)/tlsovl/*.S; do n=$$(basename $$f .S); d=$$(basename $$(dirname $$f)); \
	  git show $(BASE):common/$$d/$$n.S > $(B)/base.S 2>/dev/null || continue; \
	  for cpu in 68000 68030; do \
	    $(CC) -m$$cpu -c -o $(B)/base.o $(B)/base.S 2>/dev/null || continue; \
	    $(CC) -m$$cpu -c -o $(B)/new.o $$f && \
	    $(CROSS)objcopy -O binary -j .text -j .data $(B)/new.o $(B)/new.bin && \
	    $(CROSS)objcopy -O binary -j .text -j .data $(B)/base.o $(B)/base.bin && \
	    cmp -s $(B)/new.bin $(B)/base.bin || { echo "same: $$n.S for the $$cpu differs from $(BASE)'s"; ok=0; }; \
	  done; done; [ $$ok = 1 ] && echo "same: every .S in atari/ and tlsovl/ is $(BASE)'s, byte for byte, for the 68000 and the 68030"
	@ok=1; for f in $(CSAME); do \
	  git show $(BASE):common/$$f > $(B)/base.c || { ok=0; continue; }; \
	  for cpu in 68000 68030; do \
	    $(CC) -m$$cpu $(CSINC) -I$(C)/$$(dirname $$f) -c -o $(B)/base.o $(B)/base.c && \
	    $(CC) -m$$cpu $(CSINC) -I$(C)/$$(dirname $$f) -c -o $(B)/new.o $(C)/$$f && \
	    $(CROSS)objcopy -O binary -j .text -j .data -j .rodata $(B)/new.o $(B)/new.bin && \
	    $(CROSS)objcopy -O binary -j .text -j .data -j .rodata $(B)/base.o $(B)/base.bin && \
	    cmp -s $(B)/new.bin $(B)/base.bin || { echo "same: $$f for the $$cpu differs from $(BASE)'s"; ok=0; }; \
	  done; done; [ $$ok = 1 ] && echo "same: $(CSAME) as $(BASE)'s, byte for byte, for the 68000 and the 68030"

clean:
	rm -rf build
.PHONY: all test same clean
