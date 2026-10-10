/*
 * tos.c - the bits of libc and libgcc EClaude needs.
 *
 * The cross compiler's libgcc is built for 68020+, so the 32-bit
 * multiply/divide helpers a plain 68000 needs are provided here,
 * written with shifts and adds only so they can't call themselves.
 */
#include "tos.h"

void *memcpy(void *d, const void *s, unsigned long n)
{
	u8 *dp = d;
	const u8 *sp = s;
	while (n--)
		*dp++ = *sp++;
	return d;
}

void *memmove(void *d, const void *s, unsigned long n)
{
	u8 *dp = d;
	const u8 *sp = s;
	if (dp < sp) {
		while (n--)
			*dp++ = *sp++;
	} else {
		dp += n;
		sp += n;
		while (n--)
			*--dp = *--sp;
	}
	return d;
}

void *memset(void *d, int c, unsigned long n)
{
	u8 *dp = d;
	while (n--)
		*dp++ = (u8)c;
	return d;
}

int memcmp(const void *a, const void *b, unsigned long n)
{
	const u8 *ap = a, *bp = b;
	for (; n; n--, ap++, bp++)
		if (*ap != *bp)
			return *ap - *bp;
	return 0;
}

unsigned long strlen(const char *s)
{
	const char *p = s;
	while (*p)
		p++;
	return p - s;
}

char *strcat(char *d, const char *s)
{
	strcpy(d + strlen(d), s);
	return d;
}

char *strcpy(char *d, const char *s)
{
	char *r = d;
	while ((*d++ = *s++))
		;
	return r;
}

/* copy at most n-1 chars and always terminate */
void strlcpy_(char *d, const char *s, int n)
{
	while (n > 1 && *s) {
		*d++ = *s++;
		n--;
	}
	*d = 0;
}

int strcmp(const char *a, const char *b)
{
	while (*a && *a == *b)
		a++, b++;
	return (u8)*a - (u8)*b;
}

/* ---- libgcc replacements for -m68000 ---- */

unsigned long __mulsi3(unsigned long a, unsigned long b)
{
	unsigned long r = 0;
	while (b) {
		if (b & 1)
			r += a;
		a <<= 1;
		b >>= 1;
	}
	return r;
}

static unsigned long udivmod(unsigned long n, unsigned long d, unsigned long *rem)
{
	unsigned long q = 0, bit = 1;
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
