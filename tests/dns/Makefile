# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
# The DNS reply reader (atari/dns.h) on the build machine: make test; and
# make fuzz (clang with libFuzzer: Debian/Ubuntu clang, libclang-rt-dev)
CFLAGS   := -O1 -g -Wall -Wextra -fsanitize=address,undefined -fno-sanitize-recover=undefined
FUZZTIME ?= 60
B        := build

$(B)/test_dns: test_dns.c ../../atari/dns.h | $(B)
	cc $(CFLAGS) -o $@ test_dns.c
$(B)/fuzz_dns: fuzz_dns.c ../../atari/dns.h | $(B)
	clang -g -O1 -fsanitize=fuzzer,address,undefined -fno-sanitize-recover=undefined -o $@ fuzz_dns.c
$(B):
	mkdir -p $@
test: $(B)/test_dns
	./$(B)/test_dns
fuzz: $(B)/fuzz_dns
	mkdir -p $(B)/corpus
	./$(B)/fuzz_dns -max_total_time=$(FUZZTIME) -timeout=10 $(B)/corpus
clean:
	rm -rf build
.PHONY: test fuzz clean
