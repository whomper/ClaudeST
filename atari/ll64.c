/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * ll64.c - 64-bit multiply, divide and remainder for the builds the cross
 * compiler's libgcc can't serve: it has them only as 68020 code (divu.l,
 * bfffo, mulu.l 32x32->64), an illegal instruction on an ST's 68000 and on
 * a ColdFire (the FireBee). GCC calls these for long long arithmetic (a
 * layout's fixed point, dates, BearSSL's 31-bit products). The 32-bit ones
 * are tos.c's on a 68000 (__mulsi3, __divsi3...) and the ColdFire's own
 * instructions; these use only those, 16-bit mulu, and 64-bit adds, shifts
 * and compares, which GCC writes inline. From EWeb's atari/ll68000.c
 * (EWEBST.PRG's).
 */

typedef unsigned long long u64;
typedef long long s64;
typedef unsigned int u32;		/* 32 bits on the 68000, the ColdFire and a test host */

u64 __udivdi3(u64 n, u64 d);
u64 __umoddi3(u64 n, u64 d);
s64 __divdi3(s64 n, s64 d);
s64 __moddi3(s64 n, s64 d);
s64 __muldi3(s64 a, s64 b);

/* a 32 x 32 -> 64 multiply from four 16 x 16 ones (mulu.w) */
static u64 mul32(u32 a, u32 b)
{
	u32 al = a & 0xFFFF, ah = a >> 16, bl = b & 0xFFFF, bh = b >> 16;
	u32 ll = (unsigned short) al * (u32) (unsigned short) bl;
	u32 lh = (unsigned short) al * (u32) (unsigned short) bh;
	u32 hl = (unsigned short) ah * (u32) (unsigned short) bl;
	u32 hh = (unsigned short) ah * (u32) (unsigned short) bh;
	u64 r = ((u64) hh << 32) + ll;
	r += (u64) lh << 16;
	r += (u64) hl << 16;
	return r;
}

s64 __muldi3(s64 a, s64 b)
{
	u64 ua = (u64) a, ub = (u64) b;
	u32 al = (u32) ua, ah = (u32) (ua >> 32), bl = (u32) ub, bh = (u32) (ub >> 32);
	u64 r = mul32(al, bl);
	/* the high halves' products: only their low 32 bits stay */
	r += (u64) ((u32) mul32(al, bh) + (u32) mul32(ah, bl)) << 32;
	return (s64) r;
}

/* n / d and n % d, shifting and subtracting; d 0 gives 0 and n (no trap) */
static u64 udivmod(u64 n, u64 d, u64 *rem)
{
	u64 q = 0, r = 0;
	int i;
	if (d == 0) {
		*rem = n;
		return 0;
	}
	if ((n >> 32) == 0 && (d >> 32) == 0) {	/* the 32-bit routines */
		u32 a = (u32) n, b = (u32) d;
		*rem = a % b;
		return a / b;
	}
	if (n < d) {
		*rem = n;
		return 0;
	}
	for (i = 63; i >= 0; i--) {
		r = (r << 1) | ((n >> i) & 1);
		q <<= 1;
		if (r >= d) {
			r -= d;
			q |= 1;
		}
	}
	*rem = r;
	return q;
}

u64 __udivdi3(u64 n, u64 d)
{
	u64 r;
	return udivmod(n, d, &r);
}

u64 __umoddi3(u64 n, u64 d)
{
	u64 r;
	udivmod(n, d, &r);
	return r;
}

s64 __divdi3(s64 n, s64 d)
{
	u64 r, q = udivmod(n < 0 ? -(u64) n : (u64) n, d < 0 ? -(u64) d : (u64) d, &r);
	return (n < 0) != (d < 0) ? -(s64) q : (s64) q;
}

s64 __moddi3(s64 n, s64 d)
{
	u64 r;
	udivmod(n < 0 ? -(u64) n : (u64) n, d < 0 ? -(u64) d : (u64) d, &r);
	return n < 0 ? -(s64) r : (s64) r;
}

/* 64-bit shifts by n (0 to 63): libgcc's are 68020 code too */
u64 __ashldi3(u64 a, int n);
u64 __lshrdi3(u64 a, int n);
s64 __ashrdi3(s64 a, int n);

u64 __ashldi3(u64 a, int n)
{
	u32 lo = (u32) a, hi = (u32) (a >> 32);
	if (n == 0)
		return a;
	if (n >= 32)
		return (u64) (lo << (n - 32)) << 32;
	return (u64) (hi << n | lo >> (32 - n)) << 32 | (u32) (lo << n);
}

u64 __lshrdi3(u64 a, int n)
{
	u32 lo = (u32) a, hi = (u32) (a >> 32);
	if (n == 0)
		return a;
	if (n >= 32)
		return hi >> (n - 32);
	return (u64) (hi >> n) << 32 | (u32) (lo >> n | hi << (32 - n));
}

s64 __ashrdi3(s64 a, int n)
{
	u32 lo = (u32) a;
	long hi = (long) (a >> 32);
	if (n == 0)
		return a;
	if (n >= 32)
		return (s64) (hi >> (n - 32));
	return (s64) ((u64) (u32) (hi >> n) << 32 | (u32) (lo >> n | (u32) hi << (32 - n)));
}
