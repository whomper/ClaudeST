/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Erez Yaary */
/*
 * net_tos.c - TCP for the AERA programs on the Atari, like the GFA Troll does it:
 *   - STinG / STiK (TOS, MagiC, or MiNT with GlueSTiK) through its TPL,
 *   - MiNTnet sockets (FreeMiNT, MagiC-Net) through GEMDOS calls.
 * Names are resolved with STinG when it is there, otherwise with a
 * small DNS client over a MiNTnet UDP socket (nameserver taken from
 * resolv.conf). Dotted IP addresses always work, which is all a
 * Raspberry Pi gateway on the LAN needs.
 */
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "tos.h"
#include "sting.h"
#include "../src/plat.h"

#define MAXCONN 4		/* e.g. FTP: the control connection, one for data */

static short have_sting, have_mintnet;

static struct {
	short used, mintnet, h;
} conns[MAXCONN];

struct sockaddr_in_ {
	short family;
	u16 port;
	u32 addr;
	char zero[8];
};

int net_init(void)
{
	long fd;
	have_sting = sting_init();
	fd = Fsocket(2, 1, 0);		/* AF_INET, SOCK_STREAM */
	if (fd >= 0) {
		Fclose((short)fd);
		have_mintnet = 1;
	}
	return have_sting || have_mintnet;
}

const char *net_stack(void)
{
	if (have_mintnet && have_sting)
		return "MiNTnet + STiK";
	if (have_mintnet)
		return "MiNTnet";
	if (have_sting)
		return "STinG";
	return "none";
}

static int parse_ip(const char *s, u32 *ip)
{
	u32 v = 0;
	int i;
	for (i = 0; i < 4; i++) {
		char *e;
		unsigned long b = strtoul(s, &e, 10);
		if (e == s || b > 255)
			return 0;
		v = (v << 8) | b;
		s = e;
		if (i < 3) {
			if (*s != '.')
				return 0;
			s++;
		}
	}
	while (*s == ' ')
		s++;
	if (*s)
		return 0;
	*ip = v;
	return 1;
}

/* ---- DNS over a MiNTnet UDP socket ---- */

static int nameserver(u32 *ip)
{
	static const char *files[] = { "U:\\etc\\resolv.conf", "C:\\etc\\resolv.conf",
				       "C:\\ETC\\RESOLV.CON", 0 };
	short i;
	for (i = 0; files[i]; i++) {
		char *buf = pf_load(files[i], 0), *p;
		if (!buf)
			continue;
		for (p = buf; p && *p; p = strchr(p, '\n') ? strchr(p, '\n') + 1 : 0) {
			if (!strncmp(p, "nameserver", 10)) {
				char tmp[20], *q = p + 10;
				short n = 0;
				while (*q == ' ' || *q == '\t')
					q++;
				while (n < 19 && ((*q >= '0' && *q <= '9') || *q == '.'))
					tmp[n++] = *q++;
				tmp[n] = 0;
				if (parse_ip(tmp, ip)) {
					free(buf);
					return 1;
				}
			}
		}
		free(buf);
	}
	return 0;
}

static int dns_query(const char *name, u32 *ip)
{
	u8 q[300], r[512], id[4];	/* pf_entropy fills at least 4 */
	struct sockaddr_in_ sa;
	u32 ns, mask;
	long fd, n;
	short len = 12, qd, an, i;
	const char *p = name;

	if (!nameserver(&ns))
		return 0;
	memset(q, 0, 12);
	pf_entropy(id, 4);	/* a fresh, unguessable id: a fixed one lets */
	q[0] = id[0];		/* anyone on the network answer for the server */
	q[1] = id[1];
	q[2] = 0x01;		/* recursion desired */
	q[5] = 1;		/* one question */
	while (*p && len < 280) {
		const char *dot = strchr(p, '.');
		short l = dot ? dot - p : (short)strlen(p);
		if (l == 0 || l > 63)
			return 0;
		q[len++] = (u8)l;
		memcpy(q + len, p, l);
		len += l;
		p += l;
		if (*p == '.')
			p++;
	}
	q[len++] = 0;
	q[len++] = 0;
	q[len++] = 1;		/* A */
	q[len++] = 0;
	q[len++] = 1;		/* IN */

	fd = Fsocket(2, 2, 0);	/* AF_INET, SOCK_DGRAM */
	if (fd < 0)
		return 0;
	memset(&sa, 0, sizeof(sa));
	sa.family = 2;
	sa.port = 53;
	sa.addr = ns;
	if (Fconnect((short)fd, &sa, sizeof(sa)) < 0 || Fwrite((short)fd, len, q) != len) {
		Fclose((short)fd);
		return 0;
	}
	mask = 1UL << fd;
	if (Fselect(4000, &mask, 0, 0) <= 0) {
		Fclose((short)fd);
		return 0;
	}
	n = Fread((short)fd, sizeof(r), r);
	Fclose((short)fd);
	if (n < 12 || r[0] != id[0] || r[1] != id[1] || !(r[2] & 0x80) || (r[3] & 15))
		return 0;
	qd = (r[4] << 8) | r[5];
	an = (r[6] << 8) | r[7];
	i = 12;
	while (qd-- > 0) {			/* skip the questions */
		while (i < n && r[i] && !(r[i] & 0xc0))
			i += r[i] + 1;
		i += (i < n && (r[i] & 0xc0)) ? 2 : 1;
		i += 4;
	}
	while (an-- > 0 && i < n) {
		short type, rdlen;
		if (r[i] & 0xc0)
			i += 2;
		else {
			while (i < n && r[i])
				i += r[i] + 1;
			i++;
		}
		if (i + 10 > n)
			break;
		type = (r[i] << 8) | r[i + 1];
		rdlen = (r[i + 8] << 8) | r[i + 9];
		i += 10;
		if (type == 1 && rdlen == 4 && i + 4 <= n) {
			*ip = ((u32)r[i] << 24) | ((u32)r[i + 1] << 16) | ((u32)r[i + 2] << 8) | r[i + 3];
			return 1;
		}
		i += rdlen;
	}
	return 0;
}

static int resolve(const char *host, u32 *ip)
{
	if (parse_ip(host, ip))
		return 1;
	if (have_sting && sting_resolve(host, ip) > 0)
		return 1;
	if (have_mintnet && dns_query(host, ip))
		return 1;
	return 0;
}

int net_open(const char *host, unsigned short port, char *err, int errlen)
{
	u32 ip;
	short slot;

	for (slot = 0; slot < MAXCONN && conns[slot].used; slot++)
		;
	if (slot == MAXCONN) {
		snprintf(err, errlen, "too many connections");
		return -1;
	}
	if (!have_sting && !have_mintnet) {
		snprintf(err, errlen, "no TCP/IP stack (load STinG or run MiNT)");
		return -1;
	}
	if (!resolve(host, &ip)) {
		snprintf(err, errlen, "can't find %s", host);
		return -1;
	}

	if (have_mintnet) {
		struct sockaddr_in_ sa;
		long fd = Fsocket(2, 1, 0), r;
		if (fd < 0) {
			snprintf(err, errlen, "socket error %ld", fd);
			return -1;
		}
		memset(&sa, 0, sizeof(sa));
		sa.family = 2;
		sa.port = port;
		sa.addr = ip;
		r = Fconnect((short)fd, &sa, sizeof(sa));
		if (r < 0) {
			Fclose((short)fd);
			snprintf(err, errlen, "can't connect to %s:%u (error %ld)", host, port, r);
			return -1;
		}
		conns[slot].mintnet = 1;
		conns[slot].h = (short)fd;
	} else {
		short cn = sting_open(ip, port), r;
		if (cn < 0) {
			snprintf(err, errlen, "%s", sting_error(cn));
			return -1;
		}
		r = sting_wait_established(cn, 20);
		if (r < 0) {
			sting_close(cn);
			snprintf(err, errlen, "%s:%u: %s", host, port, sting_error(r));
			return -1;
		}
		conns[slot].mintnet = 0;
		conns[slot].h = cn;
	}
	conns[slot].used = 1;
	return slot;
}

long net_write(int slot, const void *buf, long n)
{
	const char *p = buf;
	long done = 0;
	u32 start = pf_ms();

	if (slot < 0 || slot >= MAXCONN || !conns[slot].used)
		return -1;
	if (conns[slot].mintnet)
		return Fwrite(conns[slot].h, n, buf);
	while (done < n) {
		short chunk = n - done > 2048 ? 2048 : (short)(n - done);
		short r = sting_send(conns[slot].h, p + done, chunk);
		if (r == E_OBUFFULL || r == E_NODATA) {
			if (pf_ms() - start > 60000)
				return -1;
			if (pf_idle)
				pf_idle();
			continue;
		}
		if (r < 0)
			return r;
		done += chunk;
		start = pf_ms();
	}
	return done;
}

long net_read(int slot, void *buf, long max)
{
	short h;
	if (slot < 0 || slot >= MAXCONN || !conns[slot].used)
		return -1;
	h = conns[slot].h;
	if (conns[slot].mintnet) {
		long avail = 0;
		u32 mask;
		if (Fcntl(h, &avail, FIONREAD) < 0)
			return -1;
		if (avail == 0x7fffffffL)
			return -1;
		if (avail > 0)
			return Fread(h, avail < max ? avail : max, buf);
		/* nothing buffered: readable now means the peer closed */
		mask = 1UL << h;
		if (Fselect(1, &mask, 0, 0) > 0 && mask) {
			long r = Fread(h, max, buf);
			return r > 0 ? r : -1;
		}
		return 0;
	} else {
		short n = sting_count(h), r;
		if (n == E_NODATA || n == 0)
			return 0;
		if (n < 0)
			return -1;
		if (n > max)
			n = (short)(max > 30000 ? 30000 : max);
		r = sting_read(h, buf, n);
		if (r == E_NODATA)
			return 0;
		return r < 0 ? -1 : r;
	}
}

void net_close(int slot)
{
	if (slot < 0 || slot >= MAXCONN || !conns[slot].used)
		return;
	if (conns[slot].mintnet)
		Fclose(conns[slot].h);
	else
		sting_close(conns[slot].h);
	conns[slot].used = 0;
}

/* close once what was sent has gone out (STinG closes in the
   background otherwise, and an upload's last bytes could be lost if the
   server answers first); at most seconds */
void net_close_wait(int slot, int seconds)
{
	if (slot < 0 || slot >= MAXCONN || !conns[slot].used)
		return;
	if (conns[slot].mintnet)
		Fclose(conns[slot].h);
	else
		sting_close_wait(conns[slot].h, (short)seconds);
	conns[slot].used = 0;
}
