/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Erez Yaary */
| TOS start-up for the AERA programs, for a desk accessory and an
| application alike (from ECal, EMail and Claude ST).
|
| TOS starts an accessory with a0 = its basepage, and has already given
| it exactly text + data + bss; an application finds its basepage at
| 4(sp) and owns all free memory until it gives back what it doesn't
| need (Mshrink). Either way the stack moves into our own BSS.
|
| The stack is 48 KB; an app that needs less memory sets STACK_SIZE
| (EFTP: -DSTACK_SIZE=24576 for a 1 MB ST).

#ifndef STACK_SIZE
#define STACK_SIZE 49152
#endif

	.text
	.globl	_start
_start:
	move.l	%a0,%d0
	bne.s	accessory
	move.l	4(%sp),%a0		| basepage
	move.l	%a0,_BasePage
	lea	_stack_top,%sp
	move.l	0x0c(%a0),%d0		| p_tlen
	add.l	0x14(%a0),%d0		| p_dlen
	add.l	0x1c(%a0),%d0		| p_blen
	add.l	#0x100,%d0		| basepage itself
	move.l	%d0,-(%sp)
	move.l	%a0,-(%sp)
	clr.w	-(%sp)
	move.w	#0x4a,-(%sp)		| Mshrink
	trap	#1
	lea	12(%sp),%sp
	clr.w	_is_acc
	jsr	main
	move.w	%d0,-(%sp)
	move.w	#0x4c,-(%sp)		| Pterm
	trap	#1

accessory:
	move.l	%a0,_BasePage
	lea	_stack_top,%sp
	move.w	#1,_is_acc
	jsr	main
hang:					| an accessory never ends
	bra.s	hang

	.bss
	.even
	.globl	_BasePage
	.globl	_is_acc
_BasePage:
	.space	4
_is_acc:
	.space	2
	.even
	.space	STACK_SIZE
_stack_top:
