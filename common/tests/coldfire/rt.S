/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
| The ColdFire tests' start and Linux calls (qemu-m68k -cpu cfv4e): no C
| library, as the m68k one here is 68020 code. ColdFire instructions only.
	.text
	.globl	sys_write, sys_exit, sys_open, sys_read, sys_close, sys_call4
#ifndef NO_START
	.globl	_start
_start:
	move.l	(%sp),%d0		| argc
	lea	4(%sp),%a0		| argv
	move.l	%a0,-(%sp)
	move.l	%d0,-(%sp)
	jsr	main
	move.l	%d0,%d1
	moveq	#1,%d0			| exit
	trap	#0
#endif

| long sys_call3(number, a, b, c): d0 the number, d1-d3 the arguments
call3:
	lea	-8(%sp),%sp
	move.l	%d2,(%sp)
	move.l	%d3,4(%sp)
	move.l	12(%sp),%d1
	move.l	16(%sp),%d2
	move.l	20(%sp),%d3
	trap	#0
	move.l	(%sp),%d2
	move.l	4(%sp),%d3
	lea	8(%sp),%sp
	rts
sys_exit:
	move.l	4(%sp),%d1
	moveq	#1,%d0
	trap	#0
sys_read:
	moveq	#3,%d0
	bra.s	call3
sys_write:
	moveq	#4,%d0
	bra.s	call3
sys_open:
	moveq	#5,%d0
	bra.s	call3
sys_close:
	moveq	#6,%d0
	bra.s	call3

| long sys_call4(number, a, b, c, d)
sys_call4:
	lea	-12(%sp),%sp
	move.l	%d2,(%sp)
	move.l	%d3,4(%sp)
	move.l	%d4,8(%sp)
	move.l	16(%sp),%d0
	move.l	20(%sp),%d1
	move.l	24(%sp),%d2
	move.l	28(%sp),%d3
	move.l	32(%sp),%d4
	trap	#0
	move.l	(%sp),%d2
	move.l	4(%sp),%d3
	move.l	8(%sp),%d4
	lea	12(%sp),%sp
	rts

| sig_entry: a signal's way into handler(sig, info, uc) in sig_handler, and
| back out with rt_sigreturn in ColdFire code: QEMU's own trampoline has a
| not.b, which a ColdFire doesn't have, and it ignores sa_restorer
	.globl	sig_entry, sig_handler
sig_entry:
	move.l	12(%sp),-(%sp)		| uc
	move.l	12(%sp),-(%sp)		| info
	move.l	12(%sp),-(%sp)		| sig
	move.l	sig_handler,%a0
	jsr	(%a0)
	lea	16(%sp),%sp		| (and the return address: sp at sig, as
	move.l	#173,%d0		| the trampoline would have it)
	trap	#0

	.bss
	.balign	4
sig_handler:
	.space	4
