/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
| test_sting's stand-ins: purec_fn(short a, long b, short c), packed as
| Pure C passes them (4, 6 and 10 off sp): a * 100000 + b + c, d0-d2 and
| a0-a1 clobbered as Pure C may; and call_kept(fn, args, n), sting_call
| with d2-d7 and a2-a6 set to patterns, its answer the number of them
| not kept (0) or, with keep_ret, sting_call's result
	.text
	.globl	purec_fn, purec_none, call_kept, keep_ret
purec_fn:
	move.w	4(%sp),%d0
	ext.l	%d0
	move.l	#100000,%d1
	muls.l	%d1,%d0
	add.l	6(%sp),%d0
	move.w	10(%sp),%d1
	ext.l	%d1
	add.l	%d1,%d0
	moveq	#-1,%d1
	move.l	%d1,%d2
	move.l	%d1,%a0
	move.l	%d1,%a1
	rts
purec_none:				| no arguments: 77
	moveq	#77,%d0
	rts

call_kept:
	lea	-44(%sp),%sp
	movem.l	%d2-%d7/%a2-%a6,(%sp)
	move.l	#0x22222222,%d2
	move.l	#0x33333333,%d3
	move.l	#0x44444444,%d4
	move.l	#0x55555555,%d5
	move.l	#0x66666666,%d6
	move.l	#0x77777777,%d7
	move.l	#0xa2a2a2a2,%a2
	move.l	#0xa3a3a3a3,%a3
	move.l	#0xa4a4a4a4,%a4
	move.l	#0xa5a5a5a5,%a5
	move.l	#0xa6a6a6a6,%a6
	move.l	56(%sp),-(%sp)
	move.l	56(%sp),-(%sp)
	move.l	56(%sp),-(%sp)
	jsr	sting_call
	lea	12(%sp),%sp
	move.l	%d0,keep_ret
	moveq	#0,%d0
	cmp.l	#0x22222222,%d2
	beq.s	1f
	addq.l	#1,%d0
1:	cmp.l	#0x33333333,%d3
	beq.s	1f
	addq.l	#1,%d0
1:	cmp.l	#0x44444444,%d4
	beq.s	1f
	addq.l	#1,%d0
1:	cmp.l	#0x55555555,%d5
	beq.s	1f
	addq.l	#1,%d0
1:	cmp.l	#0x66666666,%d6
	beq.s	1f
	addq.l	#1,%d0
1:	cmp.l	#0x77777777,%d7
	beq.s	1f
	addq.l	#1,%d0
1:	cmp.l	#0xa2a2a2a2,%a2
	beq.s	1f
	addq.l	#1,%d0
1:	cmp.l	#0xa3a3a3a3,%a3
	beq.s	1f
	addq.l	#1,%d0
1:	cmp.l	#0xa4a4a4a4,%a4
	beq.s	1f
	addq.l	#1,%d0
1:	cmp.l	#0xa5a5a5a5,%a5
	beq.s	1f
	addq.l	#1,%d0
1:	cmp.l	#0xa6a6a6a6,%a6
	beq.s	1f
	addq.l	#1,%d0
1:	movem.l	(%sp),%d2-%d7/%a2-%a6
	lea	44(%sp),%sp
	rts

	.bss
	.balign	4
keep_ret:
	.space	4
