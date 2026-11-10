/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * net_tos.c - TCP for the AERA programs on the Atari:
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
#include "dns.h"
#include "conn.h"		/* conn_stop: Esc while connecting */

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

/* the question for name's A record into q (300 bytes), its id the first
   two bytes of id: its length, 0 for a name that can't be asked */
static short dns_question(const char *name, u8 *q, const u8 *id)
{
	short len = 12;
	const char *p = name;
	memset(q, 0, 12);
	q[0] = id[0];		/* a fresh, unguessable id (the caller's): a fixed */
	q[1] = id[1];		/* one lets anyone on the network answer for the server */
	q[2] = 0x01;		/* recursion desired */
	q[5] = 1;		/* one question */
	while (*p) {
		const char *dot = strchr(p, '.');
		long l = dot ? dot - p : (long)strlen(p);
		/* a label of 1 to 63, and room for it, the end and the type */
		if (l == 0 || l > 63 || len + 1 + l + 5 > 300)
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
	return len;
}

static int dns_query(const char *name, u32 *ip)
{
	u8 q[300], r[512], id[4];	/* pf_entropy fills at least 4 */
	struct sockaddr_in_ sa;
	u32 ns, mask;
	long fd, n;
	short len;

	if (!nameserver(&ns))
		return 0;
	pf_entropy(id, 4);
	if ((len = dns_question(name, q, id)) == 0)
		return 0;

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
	return dns_reply(r, n, id, ip);
}

/* ---- DNS over STinG's UDP, the program going on meanwhile ----
   STinG's own resolve() is one call that returns when it has an answer
   or gives up: EWeb's window stood still for 11 s on Erez's Falcon while
   it looked up cdnjs.cloudflare.com (7 Nov 2026). Asked here instead,
   over a UDP connection to STinG's NAMESERVER, polled with pf_idle (GEM
   goes on) and conn_stop (Esc); STinG's resolve() wherever this can't
   ask or gets no answer (no NAMESERVER, no UDP: GlueSTiK; a stack whose
   UDP reads differ: it answers as it always did) */

#define DNS_TRIES 2		/* a question to each nameserver, then again */
#define DNS_WAIT 3000		/* ms for the answer of the second round */
#define DNS_WAIT1 1500		/* ms for the first round's: a nameserver that is up answers in 0.1 to 0.3 s */

/* the nameservers in STinG's NAMESERVER ("a.b.c.d, e.f.g.h" or spaces
   between): up to max of them */
static short sting_nameservers(u32 *ns, short max)
{
	const char *v = sting_getvstr("NAMESERVER"), *p;
	short n = 0;
	if (!v)
		return 0;
	for (p = v; *p && n < max;) {
		char tmp[20];
		short k = 0;
		while (*p == ' ' || *p == ',' || *p == '\t' || *p == ';')
			p++;
		while (k < 19 && ((*p >= '0' && *p <= '9') || *p == '.'))
			tmp[k++] = *p++;
		tmp[k] = 0;
		if (k && parse_ip(tmp, &ns[n]))
			n++;
		while (*p && *p != ' ' && *p != ',' && *p != '\t' && *p != ';')
			p++;
	}
	return n;
}

/* 1 found, 0 no such name (NXDOMAIN, or one that can't be asked), -1 no
   answer or can't ask (STinG's resolve() then), -2 stopped */
static int sting_dns(const char *name, u32 *ip)
{
	u32 ns[3];
	u8 q[300], r[512], id[4];
	short nns = sting_nameservers(ns, 3), len, tries;
	if (nns == 0)
		return -1;
	for (tries = 0; tries < DNS_TRIES * nns; tries++) {
		short cn;
		unsigned long t0;
		pf_entropy(id, 4);
		if ((len = dns_question(name, q, id)) == 0)
			return 0;
		cn = sting_udp_open(ns[tries % nns], 53);
		if (cn < 0)
			return -1;	/* (no UDP here) */
		if (sting_udp_send(cn, (const char *)q, len) < 0) {
			sting_udp_close(cn);
			return -1;
		}
		t0 = pf_ms();
		for (;;) {
			short got = sting_count(cn);
			if (got > 0) {
				long n = sting_read(cn, (char *)r, got > (short)sizeof(r) ? (short)sizeof(r) : got);
				if (n > 0 && dns_reply(r, n, id, ip)) {
					sting_udp_close(cn);
					return 1;
				}
				if (n >= 4 && r[0] == id[0] && r[1] == id[1] && (r[2] & 0x80) && (r[3] & 15) == 3) {
					sting_udp_close(cn);
					return 0;	/* NXDOMAIN: there is no such name */
				}
			}
			if (conn_stop) {
				sting_udp_close(cn);
				return -2;
			}
			if (pf_ms() - t0 > (tries < nns ? DNS_WAIT1 : DNS_WAIT)) {
				char m[80];
				snprintf(m, sizeof(m), "%s: no answer from nameserver %d in %lu ms", name, tries % nns + 1,
					 pf_ms() - t0);
				conn_log("DNS", " -- ", m, strlen(m));
				break;
			}
			if (pf_idle)
				pf_idle();
		}
		sting_udp_close(cn);
	}
	return -1;
}

/* names found this run, the newest first: a page's pictures and sheets
   come from the same few hosts, and each connection would ask again */
#define NAMES 16
#define NAME_KEEP 600000UL	/* 10 minutes */
static struct {
	char host[64];
	u32 ip;
	unsigned long at;
} names[NAMES];

static int name_kept(const char *host, u32 *ip)
{
	short i;
	for (i = 0; i < NAMES; i++)
		if (names[i].host[0] && !strcmp(names[i].host, host) && pf_ms() - names[i].at < NAME_KEEP) {
			*ip = names[i].ip;
			return 1;
		}
	return 0;
}

static void name_keep(const char *host, u32 ip)
{
	if (strlen(host) >= sizeof(names[0].host))
		return;
	memmove(&names[1], &names[0], sizeof(names[0]) * (NAMES - 1));
	strcpy(names[0].host, host);
	names[0].ip = ip;
	names[0].at = pf_ms();
}

static int resolve(const char *host, u32 *ip)
{
	int r;
	unsigned long t0;
	char msg[120];
	if (parse_ip(host, ip))
		return 1;
	if (name_kept(host, ip))
		return 1;
	t0 = pf_ms();
	if (have_sting) {
		r = sting_dns(host, ip);
		if (r == -2)
			return 0;		/* (stopped: net_open says so) */
		if (r == 0)
			return have_mintnet && dns_query(host, ip);	/* no such name */
		if (r == -1) {
			r = sting_resolve(host, ip) > 0;
			if (r == 1) {
				snprintf(msg, sizeof(msg), "%s found by STinG's resolve() in %lu ms", host, pf_ms() - t0);
				conn_log("DNS", " -- ", msg, strlen(msg));
				name_keep(host, *ip);
				return 1;
			}
		}
		if (r == 1) {
			snprintf(msg, sizeof(msg), "%s found in %lu ms", host, pf_ms() - t0);
			conn_log("DNS", " -- ", msg, strlen(msg));
			name_keep(host, *ip);
			return 1;
		}
	}
	if (have_mintnet && dns_query(host, ip)) {
		name_keep(host, *ip);
		return 1;
	}
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
		if (conn_stop)
			snprintf(err, errlen, "stopped");
		else
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
		/* the output buffer the app asked for; when STinG's pool (its
		   ALLOCMEM) hasn't that much, the 4 KB it always had */
		short ob = net_sendbuf > 4096 && net_sendbuf <= 32000 ? (short)net_sendbuf : 4096;
		short cn = sting_open_buf(ip, port, ob), r, s;
		if (cn == E_NOMEM && ob > 4096)
			cn = sting_open_buf(ip, port, 4096);
		if (cn < 0) {
			snprintf(err, errlen, "%s", sting_error(cn));
			return -1;
		}
		/* up to 20 s, a second at a time, so that it can be stopped */
		for (s = 0;; s++) {
			r = sting_wait_established(cn, 1);
			if (r >= 0 || (r != E_CNTIMEOUT && r != -15) || s >= 19)
				break;
			if (pf_idle)
				pf_idle();
			if (conn_stop) {
				sting_close(cn);
				snprintf(err, errlen, "stopped");
				return -1;
			}
		}
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
