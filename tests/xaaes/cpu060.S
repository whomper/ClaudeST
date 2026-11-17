| SPDX-License-Identifier: GPL-2.0-or-later
| Copyright (C) 2026 AERA | Erez Yaary
|
| cpu060.S - ZCPU060.PRG (run.py --machine ct60): the cookie jar's _CPU set
| to 60, as a CT60's TOS says, so that a program takes a 68060's ways on the
| 68040 that stands in for it. In AUTO after MINT040.PRG (AUTO's order is
| its names'): FreeMiNT builds its own jar with the CPU it finds, and runs
| the AUTO programs after itself. Code built for the 68060 alone may still
| not run on the 68040 (fintrz is an unimplemented instruction there).

	.text
	.globl	_start
_start:
	clr.l	-(%sp)
	move.w	#0x20,-(%sp)		| Super(0)
	trap	#1
	addq.l	#6,%sp
	move.l	%d0,%a5
	move.l	0x5a0.w,%a0		| _p_cookies
	move.l	%a0,%d0
	beq.s	9f
1:	move.l	(%a0),%d0
	beq.s	9f
	cmp.l	#0x5F435055,%d0		| '_CPU'
	bne.s	2f
	moveq	#60,%d0
	move.l	%d0,4(%a0)
2:	addq.l	#8,%a0
	bra.s	1b
9:	move.l	%a5,-(%sp)
	move.w	#0x20,-(%sp)		| back to user mode
	trap	#1
	addq.l	#6,%sp
	clr.w	-(%sp)			| Pterm0
	trap	#1
