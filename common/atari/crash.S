/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
| The crash catcher's exception entries (see crash.c). Vectors 2-8 and 11
| (the 2 to 8 and 11 bombs) come here with the vector number pushed. A
| fault that is the program's (crash_take) is turned into a jump to crash_landing,
| in user mode on a stack of its own, which writes CRASH.LOG and quits
| cleanly. A fault in supervisor mode outside the program (the system, a
| driver, an interrupt) goes on to TOS's handler as before: the bombs, and TOS's
| record of them at $380, which the program reads at its next start.
| Its stack (crash_stack_top) is taken by crash_install, so a desk
| accessory, which never installs the catcher, doesn't pay for it.

	.text
	.globl	crash_e2, crash_e3, crash_e4, crash_e5, crash_e6, crash_e7, crash_e8, crash_e11
| line F: in the program's code a crash (it has no FPU instruction);
| anywhere else on at once to the handler before ours (TOS's, an FPU
| emulator's, a 68040/68060 package's), touching nothing
| Every entry has an XBRA header ('XBRA', 'AERA', the handler before it),
| the standard one: a program that hooks the vector after this one finds
| who it chains to, and crash_restore can take this one out of the chain
| when it isn't on top any more (otherwise the vector would lead into the
| program's memory after it has gone: bombs, now and then, after it quits)
	.globl	crash_slot
	.data
	.even
crash_slot:			| where each vector's handler before ours is kept
	.long	0, 0, crash_old_2, crash_old_3, crash_old_4, crash_old_5
	.long	crash_old_6, crash_old_7, crash_old_8, 0, 0, crash_old_11
	.text
	.even
	.long	0x58425241, 0x41455241	| 'XBRA', 'AERA'
crash_old_11:
	.long	0
crash_e11:
	move.l	%d0,-(%sp)
	move.l	6(%sp),%d0		| the PC (2(sp) of the frame)
	cmp.l	crash_text_lo,%d0
	blo.s	1f
	cmp.l	crash_text_hi,%d0
	bhs.s	1f
	move.l	(%sp)+,%d0
	pea	11.w
	bra	common
1:	move.l	(%sp)+,%d0
	move.l	crash_old_11,-(%sp)
	rts

	.macro	ENTRY n
	.long	0x58425241, 0x41455241	| 'XBRA', 'AERA'
crash_old_\n:
	.long	0
crash_e\n:
	pea	\n\().w
	bra	common
	.endm

	ENTRY	2
	ENTRY	3
	ENTRY	4
	ENTRY	5
	ENTRY	6
	ENTRY	7
	ENTRY	8

| (sp) = vector number, 4(sp) = the exception frame. The registers are
| kept on this stack while crash_take looks, not in crash_regs: a fault
| that comes in the meantime (an interrupt's) finds its own, and goes on
| with them
common:
	tst.w	crash_busy
	bne.s	chain_quick
	movem.l	%d0-%d7/%a0-%a6,-(%sp)
	move.l	60(%sp),-(%sp)		| crash_take(frame, vector)
	pea	68(%sp)
	jsr	crash_take
	addq.l	#8,%sp
	tst.l	%d0
	beq.s	chain			| supervisor mode or a frame we don't know
	lea	crash_regs,%a1		| the program's: its registers for the report
	moveq	#14,%d1
2:	move.l	(%sp)+,(%a1)+
	dbra	%d1,2b
	move.l	%usp,%a0
	move.l	%a0,crash_usp
	addq.l	#4,%sp			| the vector number
	add.l	%d0,%sp			| drop the frame
	tst.w	crash_cpu
	beq.s	1f
	move.w	crash_fvec,-(%sp)	| 68010+: format 0
1:	move.l	#crash_landing,-(%sp)
	move.w	crash_newsr,-(%sp)
	move.l	crash_stack_top,%a0
	move.l	%a0,%usp
	rte

| on to the handler before ours with everything as it was
chain:
	movem.l	(%sp)+,%d0-%d7/%a0-%a6
chain_quick:			| a fault while one is handled: the old way
	move.l	%d0,-(%sp)
	move.l	4(%sp),%d0
	lsl.l	#2,%d0
	move.l	%a0,-(%sp)
	lea	crash_slot,%a0
	move.l	(%a0,%d0.l),%a0
	move.l	(%a0),8(%sp)		| the vector number's place: the old handler
	move.l	(%sp)+,%a0
	move.l	(%sp)+,%d0
	rts

| etv_term: the program is ending, however it ends; put the vectors back
	.globl	crash_term, crash_old_term
	.long	0x58425241, 0x41455241	| 'XBRA', 'AERA'
crash_old_term:
	.long	0
crash_term:
	movem.l	%d0-%d2/%a0-%a2,-(%sp)
	jsr	crash_restore
	movem.l	(%sp)+,%d0-%d2/%a0-%a2
	tst.l	crash_old_term		| MiNT: the process may have had none
	beq.s	1f
	move.l	crash_old_term,-(%sp)
1:	rts

