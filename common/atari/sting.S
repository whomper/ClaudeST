/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
| long sting_call(void *fn, const short *args, long nwords)
|
| Calls a STinG (Pure C "cdecl") function: the arguments are pushed as
| packed 16-bit words, exactly as laid out in args[], and the result comes
| back in d0. GCC can't express that directly because it widens shorts.
| Built for the ColdFire (__mcoldfire__), ColdFire instructions only.

	.text
	.globl	sting_call
sting_call:
#ifdef __mcoldfire__
| (the ColdFire's: movem only to and from (An), no dbra)
	lea	-44(%sp),%sp
	movem.l	%d2-%d7/%a2-%a6,(%sp)	| 44 bytes
	move.l	48(%sp),%a1		| fn
	move.l	52(%sp),%a0		| args
	move.l	56(%sp),%d0		| nwords
	move.l	%sp,%a5			| a5 survives Pure C calls
	move.l	%d0,%d1
	add.l	%d1,%d1
	sub.l	%d1,%sp
	move.l	%sp,%a2
	bra.s	2f
1:	move.w	(%a0)+,(%a2)+
2:	subq.l	#1,%d0
	bpl.s	1b
	jsr	(%a1)
	move.l	%a5,%sp
	movem.l	(%sp),%d2-%d7/%a2-%a6
	lea	44(%sp),%sp
	rts
#else
	movem.l	%d2-%d7/%a2-%a6,-(%sp)	| 44 bytes
	move.l	48(%sp),%a1		| fn
	move.l	52(%sp),%a0		| args
	move.l	56(%sp),%d0		| nwords
	move.l	%sp,%a5			| a5 survives Pure C calls
	move.l	%d0,%d1
	add.l	%d1,%d1
	sub.l	%d1,%sp
	move.l	%sp,%a2
	bra.s	2f
1:	move.w	(%a0)+,(%a2)+
2:	dbra	%d0,1b
	jsr	(%a1)
	move.l	%a5,%sp
	movem.l	(%sp)+,%d2-%d7/%a2-%a6
	rts
#endif
