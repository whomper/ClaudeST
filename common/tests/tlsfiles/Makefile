# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
# The files src/tls.c opens and looks up (the guidelines, 6.5), counted
# on the build machine: make test
BSSL   := ../../third_party/bearssl
CFLAGS := -O1 -Wall -Wextra -Wno-unused-parameter -Istub -I../../src -I$(BSSL)/inc -I$(BSSL)/src
BDEF   := -DBR_USE_URANDOM=0 -DBR_USE_GETENTROPY=0 -DBR_USE_UNIX_TIME=0
B      := build
BSRC   := $(wildcard $(BSSL)/src/*/*.c)
BOBJ   := $(patsubst $(BSSL)/src/%.c,$(B)/bssl/%.o,$(BSRC))

$(B)/test_tlsfiles: test_tlsfiles.c ../../src/tls.c ../../src/util.c stub/plat.h $(B)/libbearssl.a
	cc $(CFLAGS) -fsanitize=address,undefined -o $@ test_tlsfiles.c ../../src/tls.c ../../src/util.c $(B)/libbearssl.a
$(B)/libbearssl.a: $(BOBJ)
	ar rcs $@ $^
$(B)/bssl/%.o: $(BSSL)/src/%.c
	@mkdir -p $(dir $@)
	@cc -O1 -I$(BSSL)/inc -I$(BSSL)/src $(BDEF) -c -o $@ $<
test: $(B)/test_tlsfiles
	@rm -rf $(B)/w && mkdir -p $(B)/w
	./$(B)/test_tlsfiles roots $(B)/w
	./$(B)/test_tlsfiles seed $(B)/w 1
	./$(B)/test_tlsfiles seed $(B)/w 2
clean:
	rm -rf $(B)
.PHONY: test clean
