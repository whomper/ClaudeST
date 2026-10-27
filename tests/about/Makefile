# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
# ui/aboutline.c (the About box's status lines) on the host: make test
B := build
$(B)/test_aboutline: test_aboutline.c ../../ui/aboutline.c ../../ui/aboutline.h | $(B)
	cc -O2 -Wall -Wextra -fsanitize=address,undefined -DABOUTLINE_HOST -I../../ui -o $@ test_aboutline.c ../../ui/aboutline.c
test: $(B)/test_aboutline
	./$(B)/test_aboutline
$(B):
	mkdir -p $@
clean:
	rm -rf $(B)
.PHONY: test clean
