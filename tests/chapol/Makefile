# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
# src/chapol.c against BearSSL's br_poly1305_ctmul_run, here and as
# 68030 code under qemu-m68k: make test
CROSS ?= m68k-linux-gnu-
BSSL  := ../../third_party/bearssl
B     := build
SRC   := $(BSSL)/src/symcipher/poly1305_ctmul.c $(BSSL)/src/symcipher/chacha20_ct.c
CF    := -O2 -Wall -Wextra -I$(BSSL)/inc -I$(BSSL)/src

$(B)/test_host: test_chapol.c ../../src/chapol.c | $(B)
	cc $(CF) -o $@ test_chapol.c $(SRC)
$(B)/test_030: test_chapol.c ../../src/chapol.c | $(B)
	$(CROSS)gcc -m68030 $(CF) -static -o $@ test_chapol.c $(SRC)
$(B):
	mkdir -p $@
test: $(B)/test_host $(B)/test_030
	./$(B)/test_host
	qemu-m68k ./$(B)/test_030
clean:
	rm -rf $(B)
.PHONY: test clean
