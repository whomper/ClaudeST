/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * dns.h - the answer to net_tos.c's DNS question (an A record for a
 * name), read from the nameserver's reply. Here, not in net_tos.c, so
 * that tests/dns can check it and fuzz it on the build machine (the AERA
 * guidelines, 7.1). Only net_tos.c includes it.
 *
 * r, n: the reply as it came; id: the two bytes the question went with.
 * 1 with *ip set (the first A record), 0 if the reply is no answer to
 * that question or holds no address. Every length in it is checked
 * against n: a reply can claim any number of records of any length.
 */
#ifndef AERA_DNS_H
#define AERA_DNS_H

static int dns_reply(const unsigned char *r, long n, const unsigned char *id, unsigned long *ip)
{
	long i = 12, qd, an;
	if (n < 12 || r[0] != id[0] || r[1] != id[1] || !(r[2] & 0x80) || (r[3] & 15))
		return 0;
	qd = ((long)r[4] << 8) | r[5];
	an = ((long)r[6] << 8) | r[7];
	/* a name: labels up to a 0, or up to a pointer (two bytes) */
#define DNS_SKIP_NAME()							\
	do {								\
		while (i < n && r[i] && !(r[i] & 0xc0))			\
			i += r[i] + 1;					\
		i += (i < n && (r[i] & 0xc0)) ? 2 : 1;			\
	} while (0)
	while (qd-- > 0 && i < n) {		/* the questions: skipped */
		DNS_SKIP_NAME();
		i += 4;
	}
	while (an-- > 0 && i < n) {
		long type, rdlen;
		DNS_SKIP_NAME();
		if (i + 10 > n)
			break;
		type = ((long)r[i] << 8) | r[i + 1];
		rdlen = ((long)r[i + 8] << 8) | r[i + 9];
		i += 10;
		if (type == 1 && rdlen == 4 && i + 4 <= n) {
			*ip = ((unsigned long)r[i] << 24) | ((unsigned long)r[i + 1] << 16) |
			      ((unsigned long)r[i + 2] << 8) | r[i + 3];
			return 1;
		}
		i += rdlen;			/* a CNAME, say: the next one */
	}
#undef DNS_SKIP_NAME
	return 0;
}

#endif
