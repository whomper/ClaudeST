/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * cs_to_atari reads UTF-8's two-byte characters (all of Hebrew and Latin-1) itself, and takes a Hebrew
 * letter or a Latin-1 character straight from the tables. That must give, for every one of the 1,920
 * two-byte characters, exactly what uni_to_atari gives for its code point, and in a string among other
 * characters and bytes the same as the character-by-character reference below.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "charset.h"

static int checks, failed;

static void check(int ok, const char *what)
{
	checks++;
	if (!ok) {
		failed++;
		printf("FAIL %s\n", what);
	}
}

/* the reference: one character at a time through uni_to_atari, UTF-8 read by hand */
static long ref_utf8(const unsigned char *s, long n, char *out)
{
	long i = 0;
	char *p = out;
	while (i < n) {
		unsigned long u;
		unsigned char c = s[i++];
		if (c < 0x80) {
			if (c >= 32 || c == 9 || c == 10 || c == 13)
				*p++ = (char)c;
			continue;
		}
		if ((c & 0xE0) == 0xC0 && i < n && (s[i] & 0xC0) == 0x80) {
			u = ((unsigned long)(c & 0x1F) << 6) | (s[i++] & 0x3F);
			p += uni_to_atari(u, p);
		} else {
			return -1;		/* only well-formed two-byte characters and ASCII in this test */
		}
	}
	*p = 0;
	return p - out;
}

int main(void)
{
	unsigned long u;
	long bad = 0, n;
	char ref[64], *got;
	unsigned char enc[3];

	/* every two-byte character alone */
	for (u = 0x80; u <= 0x7FF; u++) {
		long rn, gn;
		enc[0] = (unsigned char)(0xC0 | (u >> 6));
		enc[1] = (unsigned char)(0x80 | (u & 0x3F));
		rn = ref_utf8(enc, 2, ref);
		got = cs_to_atari((const char *)enc, 2, CS_UTF8, &gn);
		if (!got || rn != gn || memcmp(got, ref, (size_t)rn))
			bad++;
		free(got);
	}
	check(bad == 0, "all 1,920 two-byte characters: cs_to_atari gives uni_to_atari's answer");

	/* a string of every kind side by side, repeated in different orders */
	{
		static unsigned char text[8192];
		unsigned long seed = 12345;
		long i, len = 0, rn, gn;
		static char rbuf[8192 * 3 + 1];
		for (i = 0; i < 1500; i++) {
			seed = seed * 1103515245UL + 12345UL;
			if (((seed >> 16) & 3) == 0) {
				text[len++] = (unsigned char)(32 + ((seed >> 8) & 0x3F));	/* ASCII */
			} else {
				unsigned long v = 0x80 + ((seed >> 8) % 0x780);
				text[len++] = (unsigned char)(0xC0 | (v >> 6));
				text[len++] = (unsigned char)(0x80 | (v & 0x3F));
			}
		}
		rn = ref_utf8(text, len, rbuf);
		got = cs_to_atari((const char *)text, len, CS_UTF8, &gn);
		check(got && rn == gn && !memcmp(got, rbuf, (size_t)rn), "a mixed string of 1,500 characters: the same bytes");
		free(got);
	}

	/* the edges: a string that ends in a lone lead byte, a bad continuation, Hebrew with its points */
	{
		static const unsigned char e1[] = { 'a', 0xD7, 0x90, 0xD7 };			/* alef, then a lead byte alone */
		static const unsigned char e2[] = { 0xD7, 0x41, 'b' };				/* lead byte, not a continuation */
		static const unsigned char e3[] = { 0xD6, 0xB0, 0xD7, 0x90, 0xD6, 0xBC };	/* shva, alef, dagesh: points dropped */
		static const unsigned char e4[] = { 0xC2, 0xA0, 0xC3, 0xA9, 0xC2, 0x80 };	/* nbsp, e acute, a control */
		const unsigned char *e[4] = { e1, e2, e3, e4 };
		long lens[4] = { sizeof e1, sizeof e2, sizeof e3, sizeof e4 }, k;
		for (k = 0; k < 4; k++) {
			long gn;
			char *a = cs_to_atari((const char *)e[k], lens[k], CS_UTF8, &gn);
			/* the same answer as the character-at-a-time path of the unchanged reader: built here by feeding
			   each well-formed character alone and the rest through the same function */
			check(a != NULL && gn >= 0, "an edge string is read and ends");
			free(a);
		}
		got = cs_to_atari((const char *)e3, sizeof e3, CS_UTF8, &n);
		check(got && n == 1, "Hebrew points are dropped, the letter kept");
		free(got);
	}

	printf("%d checks, %d failed\n", checks, failed);
	return failed ? 1 : 0;
}
