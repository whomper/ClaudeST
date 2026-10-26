| SPDX-License-Identifier: GPL-2.0-or-later
| Copyright (C) 2026 AERA | Erez Yaary
| BearSSL's br_i31_montymul loop on the 68020/030 (mulu.l 32x32->64):
| 31-bit words, d[] in BearSSL's i31 form (d[0] the bit length).
|
| uint32_t mont31_030(uint32_t *d, const uint32_t *x, const uint32_t *y,
|                     const uint32_t *m, uint32_t m0i, long len)
| d zeroed (br_i31_zero) by the caller; returns dh; d[0] is overwritten
| (the caller puts m[0] back, as BearSSL's C does).
	.text
	.globl	mont31_030
mont31_030:
	movem.l	%d2-%d7/%a2-%a6,-(%sp)
	move.l	48(%sp),%a5		| d
	move.l	52(%sp),%a6		| x
	addq.l	#4,%a6			| x + 1
	moveq	#0,%d7			| zero, for addx
	clr.l	-(%sp)			| dh
	move.l	72(%sp),-(%sp)		| u count (len); args now: d 56, x 60, y 64, m 68, m0i 72, len 76
.Lo:
	move.l	(%a6)+,%d5		| xu
	move.l	64(%sp),%a1
	addq.l	#4,%a1			| y + 1
	move.l	68(%sp),%a2
	addq.l	#4,%a2			| m + 1
	move.l	76(%sp),%d0
	lsl.l	#2,%d0
	lea	(%a1,%d0.l),%a3		| end of y
	move.l	(%a1),%d6		| f = ((d[1] + lo31(xu * y[1])) * m0i) & 0x7FFFFFFF
	mulu.l	%d5,%d6
	bclr	#31,%d6
	add.l	4(%a5),%d6
	mulu.l	72(%sp),%d6
	bclr	#31,%d6
	lea	4(%a5),%a0		| d[v + 1], read
	move.l	%a5,%a4			| d[v], written
	moveq	#0,%d4			| r
.Li:
	move.l	(%a1)+,%d1
	mulu.l	%d5,%d0:%d1		| xu * y[v + 1]
	move.l	(%a2)+,%d2
	mulu.l	%d6,%d3:%d2		| f * m[v + 1]
	add.l	%d2,%d1
	addx.l	%d3,%d0
	add.l	(%a0)+,%d1		| + d[v + 1]
	addx.l	%d7,%d0
	add.l	%d4,%d1			| + r
	addx.l	%d7,%d0
	move.l	%d1,%d2
	bclr	#31,%d2			| d[v] = z & 0x7FFFFFFF
	add.l	%d1,%d1
	addx.l	%d0,%d0			| r = z >> 31
	move.l	%d0,%d4
	move.l	%d2,(%a4)+
	cmp.l	%a3,%a1
	bne.s	.Li
	move.l	4(%sp),%d0		| zh = dh + r
	add.l	%d4,%d0
	moveq	#0,%d1
	addx.l	%d7,%d1
	move.l	%d0,%d2
	bclr	#31,%d2
	move.l	%d2,(%a4)		| d[len]
	add.l	%d0,%d0
	addx.l	%d1,%d1			| dh = zh >> 31
	move.l	%d1,4(%sp)
	subq.l	#1,(%sp)
	bne	.Lo
	move.l	4(%sp),%d0
	addq.l	#8,%sp
	movem.l	(%sp)+,%d2-%d7/%a2-%a6
	rts
