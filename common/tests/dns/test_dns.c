/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * The DNS reply reader of atari/net_tos.c (atari/dns.h) on the build
 * machine, with AddressSanitizer: answers as nameservers give them, and
 * replies that claim more than they hold (the AERA guidelines, 7.1). Each
 * reply is a block of its own size, so reading past it is caught.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../atari/dns.h"

static int checks, failed;

static void check(int ok, const char *what)
{
	checks++;
	if (!ok) {
		failed++;
		printf("FAIL %s\n", what);
	}
}

static const unsigned char ID[2] = { 0x4a, 0x17 };

/* a reply to "ftp.example.org" A IN; flags, counts, then the rest */
static long reply(unsigned char *r, int flags, int qd, int an, const unsigned char *rest, long rn)
{
	static const unsigned char q[] = { 3, 'f', 't', 'p', 7, 'e', 'x', 'a', 'm', 'p', 'l', 'e', 3, 'o', 'r', 'g', 0,
					   0, 1, 0, 1 };
	r[0] = ID[0];
	r[1] = ID[1];
	r[2] = (unsigned char)(flags >> 8);
	r[3] = (unsigned char)flags;
	r[4] = (unsigned char)(qd >> 8);
	r[5] = (unsigned char)qd;
	r[6] = (unsigned char)(an >> 8);
	r[7] = (unsigned char)an;
	memset(r + 8, 0, 4);
	memcpy(r + 12, q, sizeof(q));
	memcpy(r + 12 + sizeof(q), rest, rn);
	return 12 + (long)sizeof(q) + rn;
}

/* dns_reply on a copy of exactly n bytes */
static int ask(const unsigned char *r, long n, unsigned long *ip)
{
	unsigned char *c = malloc(n ? n : 1);
	int ok;
	memcpy(c, r, n);
	*ip = 0;
	ok = dns_reply(c, n, ID, ip);
	free(c);
	return ok;
}

#define A_93 0xc0, 0x0c, 0, 1, 0, 1, 0, 0, 0x0e, 0x10, 0, 4, 93, 184, 216, 34

int main(void)
{
	unsigned char r[1024];
	unsigned long ip;
	long n;
	static const unsigned char a[] = { A_93 };
	/* a CNAME (to "www.example.org", the rest pointed to), then the A */
	static const unsigned char cname[] = { 0xc0, 0x0c, 0, 5, 0, 1, 0, 0, 0, 60, 0, 6, 3, 'w', 'w', 'w', 0xc0, 0x10,
					       0xc0, 0x2d, 0, 1, 0, 1, 0, 0, 0, 60, 0, 4, 10, 1, 2, 3 };
	/* the name written out, not pointed to */
	static const unsigned char plain[] = { 3, 'f', 't', 'p', 7, 'e', 'x', 'a', 'm', 'p', 'l', 'e', 3, 'o', 'r', 'g',
					       0, 0, 1, 0, 1, 0, 0, 0, 1, 0, 4, 192, 0, 2, 1 };
	/* an A record sixteen bytes long, then a good one */
	static const unsigned char odd[] = { 0xc0, 0x0c, 0, 1, 0, 1, 0, 0, 0, 1, 0, 16, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10,
					     11, 12, 13, 14, 15, 16, A_93 };
	/* a CNAME that claims 65535 bytes of data */
	static const unsigned char huge[] = { 0xc0, 0x0c, 0, 5, 0, 1, 0, 0, 0, 60, 0xff, 0xff, 3, 'w', 'w', 'w', 0 };
	/* a label that claims to run past the end */
	static const unsigned char past[] = { 60, 'a', 'b' };

	n = reply(r, 0x8180, 1, 1, a, sizeof(a));
	check(ask(r, n, &ip) && ip == 0x5db8d822UL, "an A record");
	check(!ask(r, n - 1, &ip), "an A record cut short");
	check(!ask(r, 11, &ip), "less than a header");
	n = reply(r, 0x8180, 1, 2, cname, sizeof(cname));
	check(ask(r, n, &ip) && ip == 0x0a010203UL, "a CNAME, then the A record");
	n = reply(r, 0x8180, 1, 1, plain, sizeof(plain));
	check(ask(r, n, &ip) && ip == 0xc0000201UL, "the name written out");
	n = reply(r, 0x8180, 1, 2, odd, sizeof(odd));
	check(ask(r, n, &ip) && ip == 0x5db8d822UL, "an A record of 16 bytes passed over");
	n = reply(r, 0x8180, 1, 1, a, sizeof(a));
	r[1] ^= 1;
	check(!ask(r, n, &ip), "another question's id");
	n = reply(r, 0x0100, 1, 1, a, sizeof(a));
	check(!ask(r, n, &ip), "a question, not a reply");
	n = reply(r, 0x8183, 1, 0, a, 0);
	check(!ask(r, n, &ip), "no such name");
	n = reply(r, 0x8180, 1, 0, a, sizeof(a));
	check(!ask(r, n, &ip), "no answers");
	/* what a short position made of on the Atari: 32767 questions, or
	   65535 answers, read past (or before) the reply */
	n = reply(r, 0x8180, 0x7fff, 1, a, sizeof(a));
	check(!ask(r, n, &ip), "32767 questions claimed");
	n = reply(r, 0x8180, 0xffff, 0xffff, a, 0);
	check(!ask(r, n, &ip), "65535 questions and answers claimed");
	n = reply(r, 0x8180, 1, 3, huge, sizeof(huge));
	memcpy(r + n, a, sizeof(a));
	n += sizeof(a);
	check(!ask(r, n, &ip), "a record of 65535 bytes claimed");
	n = reply(r, 0x8180, 1, 1, past, sizeof(past));
	check(!ask(r, n, &ip), "a label running past the end");
	r[12] = 0xc0;
	n = 13;
	check(!ask(r, n, &ip), "a pointer cut in half");
	printf("dns: %d checks, %d failed\n", checks, failed);
	return failed != 0;
}
