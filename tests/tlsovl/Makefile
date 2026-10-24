# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
# TLS.OVL's loader (src/ovl.c) on the build machine: the real
# tlsovl/build/TLS.OVL relocated, and fixup tables made by hand, good and
# damaged. make test (builds TLS.OVL first)
B      := build
CFLAGS := -O1 -g -Wall -Wextra -Istub -I../../src -fsanitize=address,undefined

$(B)/test_ovl: test_ovl.c ../../src/ovl.c ../../src/ovl.h stub/plat.h | $(B)
	cc $(CFLAGS) -o $@ test_ovl.c ../../src/ovl.c
$(B):
	mkdir -p $@
test: $(B)/test_ovl
	$(MAKE) -C ../../tlsovl
	./$(B)/test_ovl ../../tlsovl/build/TLS.OVL
clean:
	rm -rf $(B)
.PHONY: test clean
