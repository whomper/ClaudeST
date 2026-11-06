# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
# src/inflate.c's streaming decoder against inflate_raw and zlib: make test
# inflate_piece's 68000 assembly (atari/inflate_fast.S) against its C, byte
# for byte, the assembly under qemu-m68k: make asmcheck
S      := ../../src
B      := build
CFLAGS := -O2 -g -Wall -Wextra -Wno-sign-compare -Wno-unused-parameter -fsanitize=address,undefined -fno-sanitize-recover=undefined -I$(S) -Istub
$(B)/test_inflate: test_inflate.c $(S)/inflate.c $(S)/inflate.h $(S)/util.c | $(B)
	cc $(CFLAGS) -o $@ test_inflate.c $(S)/inflate.c $(S)/util.c -lz
$(B):
	mkdir -p $@
test: $(B)/test_inflate
	./$(B)/test_inflate
$(B)/asm_check_c: asm_check.c $(S)/inflate.c $(S)/inflate.h $(S)/util.c | $(B)
	cc -O2 -Wall -Wextra -Wno-sign-compare -DGEN -I$(S) -Istub -o $@ asm_check.c $(S)/inflate.c $(S)/util.c -lz
$(B)/asm_check_m68k: asm_check.c $(S)/inflate.c $(S)/inflate.h $(S)/util.c ../../atari/inflate_fast.S | $(B)
	m68k-linux-gnu-gcc -O2 -static -DINFLATE_ASM -Wl,-z,noexecstack -I$(S) -Istub -o $@ asm_check.c $(S)/inflate.c \
		$(S)/util.c ../../atari/inflate_fast.S
# FILES: raw DEFLATE streams to take as well (a release's programs)
asmcheck: $(B)/asm_check_c $(B)/asm_check_m68k
	./$(B)/asm_check_c gen $(B)/corpus.bin $(FILES)
	./$(B)/asm_check_c run $(B)/corpus.bin $(B)/out_c.bin
	qemu-m68k $(B)/asm_check_m68k run $(B)/corpus.bin $(B)/out_asm.bin
	cmp $(B)/out_c.bin $(B)/out_asm.bin && echo "asmcheck: the assembly gives the C's every byte and answer"
clean:
	rm -rf $(B)
.PHONY: test asmcheck clean
