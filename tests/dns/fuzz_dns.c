/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * Nameservers' replies, from the fuzz input, to atari/dns.h (the AERA
 * guidelines, 7.1); the id is the reply's own, so every input gets past
 * it. make fuzz (clang with libFuzzer).
 */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "../../atari/dns.h"

int LLVMFuzzerTestOneInput(const uint8_t *d, size_t n)
{
	unsigned long ip;
	if (n >= 2)
		dns_reply(d, (long)n, d, &ip);
	return 0;
}
