# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
# the shared alert() on the build machine, with AddressSanitizer: make test
CFLAGS := -O1 -g -Wall -Wextra -fsanitize=address,undefined -fno-sanitize-recover=all \
          -Istub -I../../ui -I../../src -I../../atari
SRC := test_alert.c ../../ui/alert.c ../../src/bidi.c
build/test_alert: $(SRC) ../../ui/alert.h ../../ui/dialog.h stub/tos.h | build
	cc $(CFLAGS) -o $@ $(SRC)
# the FireBee under MyAES (_CF_ and its ALERT program): ours even there
build/test_alert_fb: $(SRC) ../../ui/alert.h ../../ui/dialog.h stub/tos.h | build
	cc $(CFLAGS) -DFIREBEE=1 -o $@ $(SRC)
build:
	mkdir -p $@
test: build/test_alert build/test_alert_fb
	./build/test_alert
	./build/test_alert_fb
clean:
	rm -rf build
.PHONY: test clean
