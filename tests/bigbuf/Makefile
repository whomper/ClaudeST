# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
# atari/bigbuf.c on the build machine: make test
CFLAGS := -O2 -Wall -Wextra -Istub -I../../atari
B      := build

$(B)/test_bigbuf: test_bigbuf.c ../../atari/bigbuf.c stub/tos.h ../../atari/bigbuf.h | $(B)
	cc $(CFLAGS) -fsanitize=address,undefined -fno-sanitize-recover=undefined -o $@ test_bigbuf.c ../../atari/bigbuf.c
$(B):
	mkdir -p $@
test: $(B)/test_bigbuf
	./$(B)/test_bigbuf
clean:
	rm -rf $(B)
.PHONY: test clean
