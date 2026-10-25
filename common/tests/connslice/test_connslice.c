/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * test_connslice.c - conn.c gives the machine a turn (pf_idle) while a
 * reply keeps coming, not only while it waits for one: a server that
 * trickles a few bytes a read with no pause (a serial link, FAKESTNG) took
 * EMail's accessory 130-215 ms between event calls on an 8 MHz ST. The
 * clock here moves 3 ms a read; no stretch between turns may pass 50 ms.
 */
#include <stdio.h>
#include <string.h>
#include "plat.h"
#include "conn.h"

static unsigned long now;
static unsigned long last_turn, longest;
static int turns, fails;
static const char *feed;
static long fed, feed_len;

int pf_open(const char *path, int mode) { return -1; }
long pf_write(int h, const void *buf, long n) { return n; }
void pf_close(int h) { }
void pf_now(PFTIME *t) { memset(t, 0, sizeof(*t)); }
unsigned long pf_ms(void) { return now; }

static void idle(void)
{
	if (now - last_turn > longest)
		longest = now - last_turn;
	last_turn = now;
	turns++;
}
void (*pf_idle)(void) = idle;

int net_open(const char *host, unsigned short port, char *err, int errlen) { return 5; }
long net_write(int h, const void *buf, long n) { return n; }
/* 7 bytes a read, 3 ms each, never a read that finds nothing; or (burst)
   as much as fits */
static int burst;
long net_read(int h, void *buf, long max)
{
	long n = feed_len - fed;
	now += 3;
	if (n <= 0)
		return 0;
	if (n > 7 && !burst)
		n = 7;
	if (n > max)
		n = max;
	memcpy(buf, feed + fed, n);
	fed += n;
	return n;
}
void net_close(int h) { }
void net_close_wait(int h, int seconds) { }

static void check(int ok, const char *what)
{
	printf("%s %s\n", ok ? "ok  " : "FAIL", what);
	fails += !ok;
}

static char text[20000];

int main(void)
{
	char err[80], msg[160];
	SBUF b;
	CONN *c;
	long i, lines = 0;
	for (i = 0; i < (long)sizeof(text) - 1; i++)
		text[i] = i % 80 == 79 ? '\n' : 'a' + i % 26;
	feed = text;
	feed_len = sizeof(text) - 1;
	c = conn_open("TEST", "server", 143, 0, 0, err, sizeof(err));
	check(c != 0, "a connection to the stand-in server");
	if (!c)
		return 1;
	memset(&b, 0, sizeof(b));
	last_turn = now;
	while (fed < feed_len || conn_pending(c))
		if (conn_getline(c, &b) >= 0)
			lines++;
		else
			break;
	if (now - last_turn > longest)
		longest = now - last_turn;
	snprintf(msg, sizeof(msg), "a reply of %ld lines trickling in: %d turns over %lu ms, the longest stretch %lu ms (<= 50)",
		 lines, turns, now, longest);
	check(lines == 249 && longest <= 50, msg);

	{
		char buf[64];
		long got = 0, r;
		fed = 0;
		turns = 0;
		longest = 0;
		last_turn = now;
		while (got < feed_len && (r = conn_read(c, buf, sizeof(buf))) > 0)
			got += r;
		if (now - last_turn > longest)
			longest = now - last_turn;
		snprintf(msg, sizeof(msg), "conn_read of %ld bytes the same: the longest stretch %lu ms (<= 50)", got, longest);
		check(got == feed_len && longest <= 50, msg);
	}
	{
		/* the whole reply in one read, 10 ms of the caller's work a line
		   (EMail's untagged() and conn_log on an ST): a turn still every
		   40 ms or so, not one per read */
		long n = 0;
		burst = 1;
		fed = 0;
		feed_len = 80 * 60;	/* 60 lines in one 4 KB-ish read and a bit */
		turns = 0;
		longest = 0;
		last_turn = now;
		while (n < 60 && conn_getline(c, &b) >= 0) {
			n++;
			now += 10;
		}
		if (now - last_turn > longest)
			longest = now - last_turn;
		snprintf(msg, sizeof(msg), "%ld lines from one read, 10 ms each: the longest stretch %lu ms (<= 50)", n, longest);
		check(n == 60 && longest <= 50, msg);
	}
	sb_free(&b);
	conn_close(c);
	printf("%s\n", fails ? "FAILED" : "all passed");
	return fails != 0;
}
