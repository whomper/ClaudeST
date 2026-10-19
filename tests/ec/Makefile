# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
# src/ecdsp.c against BearSSL on the build machine: make test (the model
# of dsp/ec.a56); make sim (ec.a56 itself, on Hatari's DSP core: run
# ./fetch-hatari.sh first)
BSSL   := ../../third_party/bearssl
CFLAGS := -O2 -Wall -Wextra -fsanitize=address,undefined -Istub -I../../src -I$(BSSL)/inc
B      := build
SRC    := test_ec.c model.c ../../src/ecdsp.c

$(B)/libbearssl.a: | $(B)
	cd $(B) && for f in ../$(BSSL)/src/*/*.c; do cc -O2 -w -I../$(BSSL)/inc -I../$(BSSL)/src -c $$f; done && ar rcs libbearssl.a *.o
$(B)/test_ec: $(SRC) model.h ../../src/ecdsp.h stub/plat.h $(B)/libbearssl.a
	cc $(CFLAGS) -o $@ $(SRC) $(B)/libbearssl.a
$(B)/test_ec_sim: $(SRC) sim.c model.h ../../src/ecdsp.h ../../dsp/ec_prog.h $(B)/libbearssl.a $(B)/hatari
	cc -O2 -DWITH_SIM -Istub -I../../src -I$(BSSL)/inc -I$(B)/hatari -I$(B)/hatari/stub -o $@ \
		$(SRC) sim.c $(B)/hatari/dsp_core.c $(B)/hatari/dsp_cpu.c $(B)/libbearssl.a -w
$(B):
	mkdir -p $@
test: $(B)/test_ec
	./$(B)/test_ec 20
sim: $(B)/test_ec_sim
	./$(B)/test_ec_sim -s 2
clean:
	rm -rf build
.PHONY: test sim clean
