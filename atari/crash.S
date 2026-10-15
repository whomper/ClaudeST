/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
| The crash catcher's exception entries (see crash.c). Vectors 2-8 (the
| 2 to 8 bombs) come here with the vector number pushed. A fault in
| program's own code (user mode) is turned into a jump to crash_landing,
| in user mode on a stack of its own, which writes CRASH.LOG and quits
| cleanly. A fault in supervisor mode (the system, a driver, an
| interrupt) goes on to TOS's handler as before: the bombs, and TOS's
| record of them at $380, which the program reads at its next start.
| Its stack (crash_stack_top) is taken by crash_install, so a desk
| accessory, which never installs the catcher, doesn't pay for it.

	.text
	.globl	crash_e2, crash_e3, crash_e4, crash_e5, crash_e6, crash_e7, crash_e8
crash_e2:	pea	2.w
	bra.s	common
crash_e3:	pea	3.w
	bra.s	common
crash_e4:	pea	4.w
	bra.s	common
crash_e5:	pea	5.w
	bra.s	common
crash_e6:	pea	6.w
	bra.s	common
crash_e7:	pea	7.w
	bra.s	common
crash_e8:	pea	8.w

| (sp) = vector number, 4(sp) = the exception frame
common:
	tst.w	crash_busy
	bne.s	chain_quick
	movem.l	%d0-%d7/%a0-%a6,crash_regs
	move.l	%usp,%a0
	move.l	%a0,crash_usp
	move.l	(%sp)+,%d0
	move.l	%sp,%a0
	move.l	%d0,-(%sp)		| crash_take(frame, vector)
	move.l	%a0,-(%sp)
	jsr	crash_take
	addq.l	#8,%sp
	tst.l	%d0
	beq.s	chain			| supervisor mode or a frame we don't know
	add.l	%d0,%sp			| drop the frame
	tst.w	crash_cpu
	beq.s	1f
	move.w	crash_fvec,-(%sp)	| 68010+: format 0
1:	move.l	#crash_landing,-(%sp)
	move.w	crash_newsr,-(%sp)
	move.l	crash_stack_top,%a0
	move.l	%a0,%usp
	rte

| on to TOS's handler with everything as it was
chain:
	movem.l	crash_regs,%d0-%d7/%a0-%a6
	move.l	crash_jump,-(%sp)
	rts
chain_quick:			| a fault while one is handled: the old way
	move.l	%d0,-(%sp)
	move.l	4(%sp),%d0
	lsl.l	#2,%d0
	move.l	%a0,-(%sp)
	lea	crash_old,%a0
	move.l	(%a0,%d0.l),8(%sp)	| the vector number's place: the old handler
	move.l	(%sp)+,%a0
	move.l	(%sp)+,%d0
	rts

| etv_term: the program is ending, however it ends; put the vectors back
	.globl	crash_term
crash_term:
	movem.l	%d0-%d2/%a0-%a2,-(%sp)
	jsr	crash_restore
	movem.l	(%sp)+,%d0-%d2/%a0-%a2
	move.l	crash_old_term,-(%sp)
	rts

