/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * test_istr.c - src/util.c's str_istr (its first letter looked for first)
 * against the plain way, strncasecmp at every place: random haystacks and
 * needles of letters, digits, punctuation and 8-bit characters, and the
 * empty cases.
 *   make test
 */
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include "build/istr.c"

static const char *plain(const char *h, const char *n)
{
	long l = strlen(n);
	for (; *h; h++)
		if (!strncasecmp(h, n, l))
			return h;
	return 0;
}

static unsigned long seed = 1;
static int rnd(int n)
{
	seed = seed * 1103515245UL + 12345UL;
	return (int)((seed >> 16) % (unsigned long)n);
}

int main(void)
{
	static const char abc[] = "aAbBcCzZ09 .-@\xE9\xC9\xD7";
	char h[64], n[8];
	long i, fails = 0, checks = 0;
	for (i = 0; i < 200000; i++) {
		int hl = rnd(40), nl = rnd(5), k;
		for (k = 0; k < hl; k++)
			h[k] = abc[rnd(sizeof(abc) - 1)];
		h[hl] = 0;
		for (k = 0; k < nl; k++)
			n[k] = abc[rnd(sizeof(abc) - 1)];
		n[nl] = 0;
		if (rnd(3) == 0 && hl > nl) {	/* a needle that is in it */
			int at = rnd(hl - nl + 1);
			memcpy(n, h + at, nl);
			for (k = 0; k < nl; k++)
				if (rnd(2))
					n[k] = (char)(islower((unsigned char)n[k]) ? toupper((unsigned char)n[k]) : tolower((unsigned char)n[k]));
		}
		checks++;
		if (str_istr(h, n) != plain(h, n) && fails++ < 5)
			printf("FAIL \"%s\" in \"%s\"\n", n, h);
	}
	checks += 2;
	if (str_istr("", "") != plain("", "") || str_istr("x", "") != plain("x", ""))
		fails++;
	printf("istr: %ld checks, %s\n", checks, fails ? "FAILED" : "all passed");
	return fails != 0;
}
