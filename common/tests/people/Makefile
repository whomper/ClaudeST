# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
# Asking EContact about people (ui/people_ask.c) on the build machine: make test
CFLAGS := -O2 -Wall -Wextra -fsanitize=address,undefined -Istub -I../../ui
B      := build

$(B)/test_people: test_people.c ../../ui/people_ask.c ../../ui/people_ask.h | $(B)
	cc $(CFLAGS) -o $@ test_people.c ../../ui/people_ask.c
$(B):
	mkdir -p $@
test: $(B)/test_people
	./$(B)/test_people
clean:
	rm -rf build
.PHONY: test clean
