/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * FAKESTNG.PRG - a stand-in for the STinG TCP/IP stack, for testing the
 * AERA apps in an emulator that has no network card (Hatari). It installs
 * a "STiK" cookie and a TRANSPORT_TCPIP table whose connections are
 * tunnelled over the serial port to serial_bridge.py (next to this file),
 * which makes the real TCP connections. Never install it on a real Atari.
 *
 * Based on Claude ST's FAKESTNG (whomper/atari_claude), which carries one
 * connection; this one multiplexes up to four, in frames:
 *
 *   Atari -> bridge   'O' cn ip[4] port[2]   open
 *                     'D' cn len[2] data      send
 *                     'C' cn                  close
 *                     'A' cn len[2]           len bytes were read: the
 *                                             bridge may send that much more
 *                                             (a window, as TCP's, so a big
 *                                             download can't overflow the
 *                                             serial buffer or the rings)
 *   bridge -> Atari   'S' cn ok[1]            open result (1 = connected)
 *                     'D' cn len[2] data      received
 *                     'E' cn                  the server closed
 */
#include "tos.h"

#define NCONN  4
#define RXSIZE 16384

long get_dftab(void), TCP_open(void), TCP_close(void), TCP_send(void),
     TCP_wait_state(void), CNbyte_count(void), CNget_block(void),
     get_err_text(void), resolve(void), unsupported(void);

typedef struct {
	char magic[10];
	void *get_dftab, *etm_exec, *cfg, *basepage;
} DRV_LIST;

static DRV_LIST drivers = { "STiKmagic", get_dftab, unsupported, 0, 0 };

static struct {
	const char *module, *author, *version;
	void *fn[32];
} tpl = { "TRANSPORT_TCPIP", "fake", "01.00", {
	unsupported, unsupported, unsupported, unsupported, get_err_text, unsupported,
	unsupported, TCP_open, TCP_close, TCP_send, TCP_wait_state,
	unsupported, unsupported, unsupported, unsupported, unsupported,
	CNbyte_count, unsupported, unsupported, CNget_block, unsupported,
	resolve,
} };

static struct {
	short used, open, eof, refused;
	unsigned char buf[RXSIZE];
	long head, tail;	/* ring buffer of received bytes */
} conn[NCONN];

/* what common/atari/tos.c takes from a C library (there is none here) */
unsigned long strlen(const char *s)
{
	const char *p = s;
	while (*p)
		p++;
	return p - s;
}

char *strcpy(char *d, const char *s)
{
	char *r = d;
	while ((*d++ = *s++))
		;
	return r;
}

static u32 get_long(const short *a) { return ((u32)(u16)a[0] << 16) | (u16)a[1]; }

/* What goes to the bridge waits in a queue and leaves a little at each
   call, as real STinG sends from its own queue: writing a request out
   of the serial port before returning held the machine for it */
#define TXSIZE 8192
static unsigned char txq[TXSIZE];
static long tx_head, tx_tail;

static long tx_room(void)
{
	return TXSIZE - 1 - (tx_head - tx_tail + TXSIZE) % TXSIZE;
}

static void flush(long n)
{
	while (n-- > 0 && tx_tail != tx_head) {
		Bconout(1, txq[tx_tail]);
		tx_tail = (tx_tail + 1) % TXSIZE;
	}
}

static void out(int c)
{
	if (!tx_room())
		flush(TXSIZE);
	txq[tx_head] = (unsigned char)c;
	tx_head = (tx_head + 1) % TXSIZE;
}

/* The serial port's input buffer, read straight from memory as
   Bconin() would (the head moves on, then the byte there): two BIOS
   calls a byte held downloads to half the speed on a Falcon. Only the
   interrupt moves the tail, only this the head */
static IOREC *io;

static int in_ready(void) { return io->ibufhd != io->ibuftl; }

static int in_byte(void)
{
	short h = io->ibufhd + 1;
	if (h >= io->ibufsiz)
		h = 0;
	io->ibufhd = h;
	return (unsigned char)io->ibuf[h];
}

/* File what the bridge has sent, byte by byte as it has come, keeping
   its place in a frame between calls: waiting for the rest of one held
   the machine (a frame takes 100 ms over an ST's serial line), which
   real STinG doesn't, and spoilt ECal's tests/hatari/bgsync_test.py timings */
static int rx_type = -1, rx_pos, rx_cn;	/* the frame begun, and how far */
static long rx_left;				/* 'D': data bytes still to come */

static void rx_byte(int c)
{
	if (rx_type < 0) {
		if (c == 'S' || c == 'D' || c == 'E') {
			rx_type = c;
			rx_pos = 0;
		}
		return;
	}
	rx_pos++;
	if (rx_pos == 1) {
		rx_cn = c < NCONN ? c : NCONN;	/* NCONN: not ours, read past */
		if (rx_type == 'E') {
			if (rx_cn < NCONN)
				conn[rx_cn].eof = 1;
			rx_type = -1;
		}
	} else if (rx_type == 'S') {
		if (rx_cn < NCONN) {
			conn[rx_cn].open = c == 1;
			conn[rx_cn].refused = c != 1;
		}
		rx_type = -1;
	} else if (rx_pos == 2) {
		rx_left = (long)c << 8;
	} else if (rx_pos == 3) {
		rx_left |= c;
		if (!rx_left)
			rx_type = -1;
	} else {
		if (rx_cn < NCONN) {
			long next = (conn[rx_cn].head + 1) % RXSIZE;
			if (next != conn[rx_cn].tail) {
				conn[rx_cn].buf[conn[rx_cn].head] = (unsigned char)c;
				conn[rx_cn].head = next;
			}
		}
		if (--rx_left == 0)
			rx_type = -1;
	}
}

/* What has come, as real STinG's interrupts would have filed it, and a
   little of what goes. At most four ticks of the 200 Hz clock a call
   (15-20 ms): filing a byte costs about 0.2 ms on an 8 MHz ST, so 2 KB
   held a Mega ST for 450 ms (ECal's bgsync_test.py); a Falcon files
   far more in the same time */
static void pump(void)
{
	u32 start = tos_hz200();
	int n = 0;
	flush(16);
	while (n < 2048 && in_ready()) {
		rx_byte(in_byte());
		if ((++n & 63) == 0 && tos_hz200() - start >= 4)
			break;
	}
}

long c_unsupported(const short *a) { (void)a; return -32; }
long c_get_err_text(const short *a) { (void)a; return (long)"fake STinG error"; }
long c_resolve(const short *a) { (void)a; return -13; }	/* E_CANTRESOLVE: use IP addresses */

long c_get_dftab(const short *a)
{
	const char *name = (const char *)get_long(a);
	const char *t = "TRANSPORT_TCPIP";
	while (*name && *name == *t)
		name++, t++;
	return (*name || *t) ? 0 : (long)&tpl;
}

long c_TCP_open(const short *a)
{
	u32 ip = get_long(a);
	u16 port = (u16)a[2];
	short cn, i;
	for (cn = 0; cn < NCONN && conn[cn].used; cn++)
		;
	if (cn == NCONN)
		return -9;		/* E_NOCCB */
	conn[cn].used = 1;
	conn[cn].open = conn[cn].eof = conn[cn].refused = 0;
	conn[cn].head = conn[cn].tail = 0;
	out('O');
	out(cn);
	for (i = 3; i >= 0; i--)
		out((int)(ip >> (i * 8)));
	out(port >> 8);
	out(port);
	return cn;
}

long c_TCP_wait_state(const short *a)
{
	short cn = a[0];
	u32 start = tos_hz200();
	if (cn < 0 || cn >= NCONN || !conn[cn].used)
		return -9;
	while (!conn[cn].open && !conn[cn].refused) {
		pump();
		if (tos_hz200() - start > 200UL * a[2] + 400)
			return -16;	/* E_CNTIMEOUT */
	}
	return conn[cn].open ? 0 : -7;	/* E_REFUSE */
}

long c_TCP_send(const short *a)
{
	short cn = a[0], len = a[3], i;
	const unsigned char *buf = (const unsigned char *)get_long(a + 1);
	if (cn < 0 || cn >= NCONN || !conn[cn].open)
		return -9;
	if (len < 0)
		return -9;
	if (tx_room() < len + 4L)	/* a big upload: wait for room, as the */
		flush(len + 4L - tx_room());	/* serial line takes it */
	out('D');
	out(cn);
	out(len >> 8);
	out(len);
	for (i = 0; i < len; i++)
		out(buf[i]);
	return 0;
}

long c_CNbyte_count(const short *a)
{
	short cn = a[0];
	long n;
	if (cn < 0 || cn >= NCONN || !conn[cn].used)
		return -9;
	pump();
	n = (conn[cn].head - conn[cn].tail + RXSIZE) % RXSIZE;
	if (n > 32000)
		n = 32000;
	if (n)
		return n;
	return conn[cn].eof ? -3 : -2;	/* E_EOF : E_NODATA */
}

long c_CNget_block(const short *a)
{
	short cn = a[0], len = a[3], n = 0;
	unsigned char *buf = (unsigned char *)get_long(a + 1);
	if (cn < 0 || cn >= NCONN || !conn[cn].used)
		return -9;
	pump();
	while (n < len && conn[cn].tail != conn[cn].head) {
		buf[n++] = conn[cn].buf[conn[cn].tail];
		conn[cn].tail = (conn[cn].tail + 1) % RXSIZE;
	}
	if (n) {
		out('A');
		out(cn);
		out(n >> 8);
		out(n);
	}
	return n ? n : (conn[cn].eof ? -3 : -2);
}

long c_TCP_close(const short *a)
{
	short cn = a[0];
	if (cn < 0 || cn >= NCONN || !conn[cn].used)
		return -9;
	out('C');
	out(cn);
	flush(TXSIZE);
	conn[cn].used = 0;
	return 0;
}

/* --- install --- */

static char rxbuf[16384];

/* a cookie jar of our own: TOS 1.04 and older have none (as STinG does,
   FAKESTNG makes one, and takes it away again at a reset: those TOSes
   don't clear _p_cookies, jar_reset in fstart.S does), and a full jar is
   moved into it */
#define OWNJAR 32
static long ownjar[OWNJAR * 2];
long old_resvalid, old_resvector;
void jar_reset(void);

#pragma GCC diagnostic ignored "-Warray-bounds"	/* fixed system variables */
static void sup_install(void)
{
	long *jar, i = 0;
	short sr;
	__asm__ volatile("move.l 0x5a0.w,%0" : "=a"(jar));
	if (!jar) {
		ownjar[1] = OWNJAR;		/* empty: the terminator holds the size */
		old_resvalid = *(volatile long *)0x426L;
		old_resvector = *(volatile long *)0x42aL;
		*(volatile long *)0x42aL = (long)jar_reset;
		*(volatile long *)0x426L = 0x31415926L;
		jar = ownjar;
	} else {
		while (jar[i * 2])
			i++;
		if (i + 1 >= jar[i * 2 + 1] && i + 2 <= OWNJAR) {	/* full: move it */
			long n;
			for (n = 0; n < i * 2; n++)
				ownjar[n] = jar[n];
			ownjar[i * 2] = 0;
			ownjar[i * 2 + 1] = OWNJAR;
			jar = ownjar;
		}
	}
	if (i + 1 < jar[i * 2 + 1]) {
		/* the terminator's value is the jar size; append before it */
		jar[i * 2 + 2] = 0;
		jar[i * 2 + 3] = jar[i * 2 + 1];
		jar[i * 2] = 0x5354694bL;	/* 'STiK' */
		jar[i * 2 + 1] = (long)&drivers;
	}
	*(volatile long *)0x5a0L = (long)jar;
	__asm__ volatile("move.w %%sr,%0\n\tor.w #0x0700,%%sr" : "=d"(sr) :: "memory");
	io->ibuf = rxbuf;
	io->ibufsiz = sizeof(rxbuf);
	io->ibufhd = io->ibuftl = 0;
	io->ibuflow = 4096;
	io->ibufhi = 12288;
	__asm__ volatile("move.w %0,%%sr" :: "d"(sr) : "memory");
}

/* tests only: with a file C:\FASTCLK, MFP timer C interrupts at 312 Hz
   instead of 200 (2457600 / 64 / 123), TOS's handler still counting
   each one in _hz_200: a 200 Hz clock that runs 1.56 times too fast, as
   on Erez's Falcon, for EFTP's tests/hatari/clock_test.py */
static void sup_fastclk(void)
{
	*(volatile unsigned char *)0xfffffa23L = 123;	/* timer C's data register */
}

long fake_install(long *basepage)
{
	long h = Fopen("C:\\FASTCLK", 0);
	if (h >= 0) {
		Fclose((short)h);
		Supexec(sup_fastclk);
		Cconws("FASTCLK: timer C at 312 Hz\r\n");
	}
	/* a Falcon's AUX port is its SCC; Hatari wires the MFP port (6) */
	if ((get_cookie(0x5f4d4348L) >> 16) == 3)	/* '_MCH' */
		trap14_ww(44, 6);			/* Bconmap(6) */
	/* no flow control: the frames are binary, and TOS 1.04 starts with
	   XON/XOFF, which takes 0x11 and 0x13 out of the input (a length of
	   19 is 0x13) and stops sending at an XOFF */
	Rsconf(-1, 0, -1, -1, -1, -1);
	io = Iorec(0);
	Supexec(sup_install);
	Cconws("Fake STinG installed (TCP over serial)\r\n");
	return 0x100 + basepage[3] + basepage[5] + basepage[7];
}
