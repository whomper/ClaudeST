/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * tos.c - cookie jar, timer, debug output and the libgcc helpers a
 * plain 68000 needs (the cross compiler's libgcc is built for 68020+).
 * The multiply/divide helpers come from Claude ST (whomper/atari_claude).
 */
#include <string.h>
#include "tos.h"

/* noipa: GCC must take this as any call, which may call back each
   function whose address it was given (tos.h, Supexec) */
__attribute__((noipa)) long tos_supexec(void *f)
{
	return trap14_wl(38, (long)f);
}

static long cookie_id, cookie_val;

static void sup_cookie(void)
{
	long *jar;
	__asm__ volatile("move.l 0x5a0.w,%0" : "=a"(jar));	/* _p_cookies */
	cookie_val = 0;
	if (!jar)
		return;
	for (; jar[0]; jar += 2) {
		if (jar[0] == cookie_id) {
			cookie_val = jar[1];
			return;
		}
	}
}

long get_cookie(long id)
{
	cookie_id = id;
	Supexec(sup_cookie);
	return cookie_val;
}

static short bootdev;

static void sup_bootdev(void)
{
	__asm__ volatile("move.w 0x446.w,%0" : "=d"(bootdev));	/* _bootdev */
}

/* the command line: the basepage's, or, where the parent passed it the
   ARGV way (MiNT and MagiC launching a GEM program: the length byte 127
   and the words in the environment after "ARGV="), the words after the
   program's name, joined by spaces, a word with a space in quotes */
/* (no C library here, nor _BasePage: FAKESTNG links tos.c alone, with a
   start of its own) */
extern void *_BasePage __attribute__((weak));

static long slen(const char *s)
{
	long n = 0;
	while (s[n])
		n++;
	return n;
}

const char *cmd_line(void)
{
	static char arg[256];
	const unsigned char *bp = &_BasePage ? (const unsigned char *)_BasePage : 0;
	const unsigned char *cmd = bp ? bp + 0x80 : 0;
	long n = 0, k, i;
	if (!cmd)
		return "";
	if (cmd[0] == 127) {
		const char *env = *(const char *const *)(bp + 0x2c);
		while (env && *env && !(env[0] == 'A' && env[1] == 'R' && env[2] == 'G' && env[3] == 'V' &&
					env[4] == '='))
			env += slen(env) + 1;
		if (env && *env) {
			env += slen(env) + 1;			/* "ARGV=..." */
			if (*env)
				env += slen(env) + 1;		/* the program's name */
			while (*env && n < (long)sizeof(arg) - 4) {
				int q = 0;
				k = slen(env);
				for (i = 0; i < k; i++)
					if (env[i] == ' ')
						q = 1;
				if (n)
					arg[n++] = ' ';
				if (q)
					arg[n++] = '"';
				if (k > (long)sizeof(arg) - 3 - n)
					k = (long)sizeof(arg) - 3 - n;
				for (i = 0; i < k; i++)
					arg[n++] = env[i];
				if (q)
					arg[n++] = '"';
				env += slen(env) + 1;
			}
			arg[n] = 0;
			return arg;
		}
	}
	n = cmd[0] < 127 ? cmd[0] : 126;
	for (i = 0; i < n; i++)
		arg[i] = (char)cmd[1 + i];
	arg[n] = 0;
	while (n > 0 && (arg[n - 1] == ' ' || arg[n - 1] == '\r' || arg[n - 1] == '\n'))
		arg[--n] = 0;
	return arg;
}

short boot_drive(void)
{
	Supexec(sup_bootdev);
	return bootdev;
}

static u32 hz;

static void sup_hz200(void)
{
	__asm__ volatile("move.l 0x4ba.w,%0" : "=d"(hz));	/* _hz_200 */
}

u32 tos_hz200(void)
{
	Supexec(sup_hz200);
	return hz;
}

/* ---------------- milliseconds ----------------
 * _hz_200 goes up once per interrupt of the MFP's timer C. A driver that
 * sets timer C faster and still calls TOS's handler each time makes it
 * run fast: on Erez's Falcon at 312 Hz, so a transfer of 16 real seconds
 * showed as 25 (EFTP, whomper/EFTP a1e3b44). So the first time tos_ms()
 * is asked, the counter is measured against timer C's own count-down,
 * which runs off the MFP's 2.4576 MHz crystal however the timer is set,
 * for CLOCK_STEPS of the counter's steps (30 ms when all is well; it
 * busy-waits that long once, so keep it short); more than 3%
 * off, tos_ms() is scaled. The count-down's reload value is read with
 * timer C's interrupt masked in the MFP for a round: with it taken, TOS's
 * handler runs while the first counts go by, the largest one seen fell
 * short (185 of 192 on an ST's accessory at boot) and every wrap with it,
 * which read a 200 Hz clock as 206 to 222 Hz (EContact's rigs, 21 Oct
 * 2026). The pending interrupt is taken when it is unmasked: no step is
 * lost. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Warray-bounds"	/* fixed hardware addresses */
#define CLOCK_STEPS 6			/* 0.1% resolution at 200 Hz: plenty for a 3% test */
static long clock_hz10;			/* the counter's rate in tenths of Hz; 0 not measured, -1 can't */
static u32 ms256 = 5 * 256;		/* milliseconds per step, times 256 */

static void sup_clock(void)
{
	static const unsigned short pre[8] = { 0, 4, 10, 16, 50, 64, 100, 200 };
	volatile unsigned char *tcdcr = (volatile unsigned char *)0xfffffa1dL;
	volatile unsigned char *tcdr = (volatile unsigned char *)0xfffffa23L;
	volatile unsigned char *imrb = (volatile unsigned char *)0xfffffa15L;
	volatile u32 *hz = (volatile u32 *)0x4baL;
	u32 p = pre[(*tcdcr >> 4) & 7], acc = 0, h, n = 0, top = 0, r, limit;
	unsigned char prev, cur, mask;
	int i;
	clock_hz10 = -1;
	if (!p)
		return;				/* timer C stopped */
	limit = 2457600UL / p;			/* a second of count-down: give up after it */
	/* the value the timer starts each round from: read just after it
	   reloads, with timer C's interrupt masked (bit 5 of IMRB) so that
	   nothing runs between; a round at most */
	mask = *imrb;
	*imrb = mask & ~0x20;
	prev = *tcdr;
	while ((cur = *tcdr) <= prev && acc < limit && n < 200000UL) {
		acc += prev - cur;
		prev = cur;
		n++;
	}
	top = cur;
	for (i = 0; i < 4; i++)
		if ((cur = *tcdr) > top)
			top = cur;
	*imrb = mask;
	if (acc >= limit || n >= 200000UL)
		return;				/* the counter doesn't move */
	r = top ? top : 256;
	n = 0;
	/* CLOCK_STEPS steps of the counter, in count-down ticks */
	h = *hz;
	while (*hz == h && n < 1000000UL)
		n++;
	h = *hz;
	acc = 0;
	prev = *tcdr;
	while (*hz < h + CLOCK_STEPS && acc < limit) {
		cur = *tcdr;
		acc += cur <= prev ? prev - cur : prev + r - cur;
		prev = cur;
	}
	if (acc >= limit || !acc)
		return;
	clock_hz10 = (long)(CLOCK_STEPS * 10UL * 2457600UL / (acc * p));
}
#pragma GCC diagnostic pop

u32 tos_ms(void)
{
	u32 t;
	if (!clock_hz10) {
		Supexec(sup_clock);
		if (clock_hz10 > 0 && (clock_hz10 < 1940 || clock_hz10 > 2060))
			ms256 = 2560000UL / (u32)clock_hz10;
	}
	t = tos_hz200();
	if (ms256 == 5 * 256)
		return t * 5;
	return (t >> 8) * ms256 + ((t & 255) * ms256 >> 8);
}

static char *put_num(char *d, u32 v)
{
	char b[12];
	int n = 0;
	do
		b[n++] = (char)('0' + v % 10);
	while (v /= 10);
	while (n)
		*d++ = b[--n];
	return d;
}

const char *tos_clock(void)
{
	static char s[64];
	char *d;
	tos_ms();
	if (clock_hz10 < 0)
		return "the 200 Hz clock couldn't be measured";
	if (ms256 == 5 * 256)
		return "";
	d = s + strlen(strcpy(s, "the 200 Hz clock runs at "));
	d = put_num(d, (u32)clock_hz10 / 10);
	*d++ = '.';
	d = put_num(d, (u32)clock_hz10 % 10);
	strcpy(d, " Hz: times corrected");
	return s;
}

long nf_probe(void);
long nf_call2(long id, long arg);
static long nf_id = -1;

static void sup_nf_probe(void)
{
	nf_id = nf_probe();
}

void nf_debug(const char *s)
{
	if (nf_id < 0)
		Supexec(sup_nf_probe);
	if (nf_id > 0)
		nf_call2(nf_id, (long)s);
}

/* ---- libgcc replacements for -m68000 ---- */

static inline unsigned long mulu16(unsigned long a, unsigned long b)
{
	/* one mulu.w: 16 x 16 -> 32 bits (written in asm, since C would
	   promote the operands and call __mulsi3 again) */
	__asm__("mulu.w %1,%0" : "+d"(a) : "d"(b));
	return a;
}

unsigned long __mulsi3(unsigned long a, unsigned long b)
{
	unsigned long al = a & 0xffff, bl = b & 0xffff, ah = a >> 16, bh = b >> 16;
	/* the low 32 bits of the product: al*bl + ((ah*bl + al*bh) << 16) */
	unsigned long r = mulu16(al, bl);
	if (ah)
		r += mulu16(ah, bl) << 16;
	if (bh)
		r += mulu16(al, bh) << 16;
	return r;
}

/* __builtin_clz/__builtin_ctz on a 68000: libgcc's are 68020 code
   (bfffo), an illegal instruction on an ST (EPDF, 27 Oct 2026). Halving
   steps, not a loop GCC could turn back into a call of itself */
int __clzsi2(unsigned int x)
{
	int n = 0;
	if (!x)
		return 32;
	if (!(x & 0xffff0000UL)) { n += 16; x <<= 16; }
	if (!(x & 0xff000000UL)) { n += 8; x <<= 8; }
	if (!(x & 0xf0000000UL)) { n += 4; x <<= 4; }
	if (!(x & 0xc0000000UL)) { n += 2; x <<= 2; }
	if (!(x & 0x80000000UL))
		n += 1;
	return n;
}

int __ctzsi2(unsigned int x)
{
	int n = 0;
	if (!x)
		return 32;
	if (!(x & 0xffff)) { n += 16; x >>= 16; }
	if (!(x & 0xff)) { n += 8; x >>= 8; }
	if (!(x & 0xf)) { n += 4; x >>= 4; }
	if (!(x & 0x3)) { n += 2; x >>= 2; }
	if (!(x & 0x1))
		n += 1;
	return n;
}

static unsigned long udivmod(unsigned long n, unsigned long d, unsigned long *rem)
{
	unsigned long q = 0, bit = 1;
	/* a divisor of 16 bits (nearly all of ours: 7, 60, 1440...): two
	   divu.w, high word then low word; neither quotient can overflow */
	if (d && d < 0x10000UL) {
		unsigned long t = n >> 16, q1, q0;
		__asm__("divu.w %1,%0" : "+d"(t) : "d"(d));
		q1 = t & 0xffff;
		t = (t & 0xffff0000UL) | (n & 0xffff);
		__asm__("divu.w %1,%0" : "+d"(t) : "d"(d));
		q0 = t & 0xffff;
		*rem = t >> 16;
		return (q1 << 16) | q0;
	}
	if (d == 0) {
		*rem = n;
		return 0xffffffffUL;
	}
	while (d < n && !(d & 0x80000000UL)) {
		d <<= 1;
		bit <<= 1;
	}
	while (bit) {
		if (n >= d) {
			n -= d;
			q |= bit;
		}
		d >>= 1;
		bit >>= 1;
	}
	*rem = n;
	return q;
}

unsigned long __udivsi3(unsigned long a, unsigned long b)
{
	unsigned long r;
	return udivmod(a, b, &r);
}

unsigned long __umodsi3(unsigned long a, unsigned long b)
{
	unsigned long r;
	udivmod(a, b, &r);
	return r;
}

long __divsi3(long a, long b)
{
	unsigned long r;
	int neg = (a < 0) ^ (b < 0);
	unsigned long q = udivmod(a < 0 ? -(unsigned long)a : (unsigned long)a,
				  b < 0 ? -(unsigned long)b : (unsigned long)b, &r);
	return neg ? -(long)q : (long)q;
}

long __modsi3(long a, long b)
{
	unsigned long r;
	udivmod(a < 0 ? -(unsigned long)a : (unsigned long)a,
		b < 0 ? -(unsigned long)b : (unsigned long)b, &r);
	return a < 0 ? -(long)r : (long)r;
}
