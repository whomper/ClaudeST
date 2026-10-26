| SPDX-License-Identifier: GPL-2.0-or-later
| Copyright (C) 2026 AERA | Erez Yaary
| Montgomery multiplication on the 68020/030 (mulu.l 32x32->64), the
| inner loop of an RSA public operation, after BearSSL's br_i32_montymul.
|
| uint32_t mont030(uint32_t *buf, const uint32_t *x, const uint32_t *y,
|                  const uint32_t *m, uint32_t m0i, long len)
| buf: len+1 words, zero: buf[0] scratch, buf[1..len] the result d
| (little-endian 32-bit words, as x, y, m). Returns d's high word (0 or 1).
	.text
	.globl	mont030
mont030:
	movem.l	%d2-%d7/%a2-%a6,-(%sp)
	move.l	48(%sp),%a5		| buf
	move.l	52(%sp),%a6		| x
	moveq	#0,%d7			| zero, for addx
	clr.l	-(%sp)			| dh
	move.l	72(%sp),-(%sp)		| u count (len); args now: buf 56, x 60, y 64, m 68, m0i 72, len 76
.Louter:
	move.l	(%a6)+,%d5		| xu
	move.l	64(%sp),%a1		| y
	move.l	68(%sp),%a2		| m
	move.l	76(%sp),%d0
	lsl.l	#2,%d0
	lea	(%a1,%d0.l),%a3		| end of y
	move.l	(%a1),%d6		| f = (d[0] + xu * y[0]) * m0i
	mulu.l	%d5,%d6
	add.l	4(%a5),%d6
	mulu.l	72(%sp),%d6
	lea	4(%a5),%a0		| d, read
	move.l	%a5,%a4			| d - 1, written
	moveq	#0,%d3			| r1
	moveq	#0,%d4			| r2
.Linner:
	move.l	(%a1)+,%d1
	mulu.l	%d5,%d0:%d1		| xu * y[v]
	add.l	(%a0)+,%d1
	addx.l	%d7,%d0
	add.l	%d3,%d1
	addx.l	%d7,%d0
	move.l	%d0,%d3
	move.l	(%a2)+,%d2
	mulu.l	%d6,%d0:%d2		| f * m[v]
	add.l	%d1,%d2
	addx.l	%d7,%d0
	add.l	%d4,%d2
	addx.l	%d7,%d0
	move.l	%d0,%d4
	move.l	%d2,(%a4)+		| d[v - 1]
	cmp.l	%a3,%a1
	bne.s	.Linner
	move.l	4(%sp),%d0		| zh = dh + r1 + r2
	moveq	#0,%d1
	add.l	%d3,%d0
	addx.l	%d7,%d1
	add.l	%d4,%d0
	addx.l	%d7,%d1
	move.l	%d0,(%a4)		| d[len - 1]
	move.l	%d1,4(%sp)
	subq.l	#1,(%sp)
	bne	.Louter
	move.l	4(%sp),%d0
	addq.l	#8,%sp
	movem.l	(%sp)+,%d2-%d7/%a2-%a6
	rts

| void dbl030(uint32_t *r, const uint32_t *n, long k): r = 2r mod n (r < n)
	.globl	dbl030
dbl030:
	movem.l	%d2-%d3/%a2-%a3,-(%sp)
	move.l	20(%sp),%a0		| r
	move.l	24(%sp),%a1		| n
	move.l	28(%sp),%d1		| k
	move.l	%a0,%a2
	move.w	%d1,%d2
	subq.w	#2,%d2
	move.l	(%a2),%d0
	add.l	%d0,%d0
	move.l	%d0,(%a2)+
.Ls:	move.l	(%a2),%d0
	addx.l	%d0,%d0
	move.l	%d0,(%a2)+
	dbra	%d2,.Ls
	moveq	#0,%d3
	addx.l	%d3,%d3			| the bit shifted out
	bne.s	.Lsub
	move.l	%d1,%d0
	lsl.l	#2,%d0
	lea	(%a0,%d0.l),%a2
	lea	(%a1,%d0.l),%a3
	move.w	%d1,%d2
	subq.w	#1,%d2
.Lc:	move.l	-(%a2),%d0
	cmp.l	-(%a3),%d0
	dbne	%d2,.Lc
	bcs.s	.Ldone			| r < n
.Lsub:	move.l	%a0,%a2
	move.l	%a1,%a3
	move.w	%d1,%d2
	subq.w	#1,%d2
	move.w	#0,%ccr
.Lb:	move.l	(%a3)+,%d0
	move.l	(%a2),%d3
	subx.l	%d0,%d3
	move.l	%d3,(%a2)+
	dbra	%d2,.Lb
.Ldone:	movem.l	(%sp)+,%d2-%d3/%a2-%a3
	rts
