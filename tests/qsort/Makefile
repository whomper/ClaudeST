# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
# atari/libc.c's qsort (merge sort of pointers, heap sort without memory)
# against the host's qsort and a stable reference: make test (swap_el
# reads longs on even addresses, as a 68000 may: no alignment check)
B := build
$(B)/qsort.c: ../../atari/libc.c | $(B)
	sed -n '/^\/\* ---- qsort/,/^\/\* ---- end of qsort/p' $< > $@
$(B)/test_qsort: test_qsort.c $(B)/qsort.c
	cc -O2 -Wall -Wextra -fsanitize=address,undefined -fno-sanitize=alignment -o $@ test_qsort.c
$(B):
	mkdir -p $@
test: $(B)/test_qsort
	./$(B)/test_qsort
clean:
	rm -rf $(B)
.PHONY: test clean
