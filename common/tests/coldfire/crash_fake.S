/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
| test_crash's stand-ins for an exception: fake_exc(entry, sr, pc, vec)
| sets every register to a pattern, builds a ColdFire frame (the format
| and vector word, SR, PC) and enters crash.S's entry as the CPU would;
| old_handler, the handler before ours, notes what it was given (the
| registers in seen_regs, sp in seen_sp) and goes back to fake_exc's
| caller with 1
	.text
	.globl	fake_exc, old_handler, seen_regs, seen_sp, frame_at, saved_sp
fake_exc:
	lea	-44(%sp),%sp
	movem.l	%d2-%d7/%a2-%a6,(%sp)
	move.l	%sp,saved_sp
	move.l	48(%sp),%a0		| entry
	move.l	52(%sp),%d0		| sr
	move.l	56(%sp),%d1		| pc
	move.l	60(%sp),%d2		| vector
	move.l	%d1,-(%sp)		| PC
	move.w	%d0,-(%sp)		| SR
	lsl.l	#2,%d2
	add.l	#0x4000,%d2
	move.w	%d2,-(%sp)		| format 4, the vector
	move.l	%sp,frame_at
	move.l	%a0,entry
	move.l	#0xd0d0d0d0,%d0
	move.l	#0xd1d1d1d1,%d1
	move.l	#0xd2d2d2d2,%d2
	move.l	#0xd3d3d3d3,%d3
	move.l	#0xd4d4d4d4,%d4
	move.l	#0xd5d5d5d5,%d5
	move.l	#0xd6d6d6d6,%d6
	move.l	#0xd7d7d7d7,%d7
	move.l	#0xa0a0a0a0,%a0
	move.l	#0xa1a1a1a1,%a1
	move.l	#0xa2a2a2a2,%a2
	move.l	#0xa3a3a3a3,%a3
	move.l	#0xa4a4a4a4,%a4
	move.l	#0xa5a5a5a5,%a5
	move.l	#0xa6a6a6a6,%a6
	move.l	entry,-(%sp)
	rts				| (into the entry, every register set)

old_handler:
	move.l	%sp,seen_sp
	move.l	%a0,-(%sp)
	lea	seen_regs,%a0
	move.l	%d0,(%a0)
	move.l	%d1,4(%a0)
	move.l	%d2,8(%a0)
	move.l	%d3,12(%a0)
	move.l	%d4,16(%a0)
	move.l	%d5,20(%a0)
	move.l	%d6,24(%a0)
	move.l	%d7,28(%a0)
	move.l	(%sp)+,32(%a0)
	move.l	%a1,36(%a0)
	move.l	%a2,40(%a0)
	move.l	%a3,44(%a0)
	move.l	%a4,48(%a0)
	move.l	%a5,52(%a0)
	move.l	%a6,56(%a0)
	move.l	saved_sp,%sp
	movem.l	(%sp),%d2-%d7/%a2-%a6
	lea	44(%sp),%sp
	moveq	#1,%d0
	rts

	.bss
	.balign	4
seen_regs:	.space	60
seen_sp:	.space	4
frame_at:	.space	4
saved_sp:	.space	4
entry:		.space	4
