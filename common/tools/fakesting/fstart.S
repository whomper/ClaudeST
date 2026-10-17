/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
| FAKESTNG.PRG startup: install the fake STinG and stay resident.
| From Claude ST (whomper/atari_claude).
	.text
	.globl	_start
_start:
	move.l	4(%sp),%a0
	move.l	%a0,-(%sp)
	jsr	fake_install		| returns bytes to keep in d0
	addq.l	#4,%sp
	move.l	%d0,-(%sp)
	bsr	install_privvio
	move.l	(%sp)+,%d0
	clr.w	-(%sp)
	move.l	%d0,-(%sp)
	move.w	#0x31,-(%sp)		| Ptermres
	trap	#1

| ---- STinG 1.26's way into supervisor mode (sting/thread.s) ----
| STinG executes "or.w #0,sr" in user mode on purpose and catches the
| privilege violation with its own XBRA 'STNG' handler on vector 8:
| TCP_open, TCP_wait_state and others do it through protect_exec. A
| program that hooks vector 8 after STinG (a crash catcher) must pass
| such faults on, or every connect bombs; FAKESTNG does the same so the
| Hatari rigs see it.
install_privvio:
	move.l	#-1,-(%sp)
	move.w	#8,-(%sp)		| the privilege violation vector
	move.w	#5,-(%sp)		| Setexc: read it
	trap	#13
	addq.l	#8,%sp
	move.l	%d0,old_privvio
	pea	my_privvio
	move.w	#8,-(%sp)
	move.w	#5,-(%sp)		| Setexc: ours
	trap	#13
	addq.l	#8,%sp
	pea	calc_frame
	move.w	#38,-(%sp)		| Supexec
	trap	#14
	addq.l	#6,%sp
	rts

calc_frame:
	tst.w	0x59e.w			| _longframe: a 68010 or better?
	beq.s	1f
	move.w	#8,framesize
1:	rts

| protect_exec(param, code): code(param) with interrupts off, in
| supervisor mode, from either mode
	.globl	protect_exec
protect_exec:
	move.l	4(%sp),%a0
	move.l	8(%sp),%a1
prot_exec:
	or.w	#0x0000,%sr		| a privilege violation in user mode
	move.w	%sr,-(%sp)
	or.w	#0x0700,%sr
	move.l	%a0,-(%sp)
	jsr	(%a1)
	addq.l	#4,%sp
	move.w	(%sp)+,%sr
	rts

	.dc.l	0x58425241		| XBRA
	.dc.l	0x53544e47		| 'STNG', as STinG's
old_privvio:
	.dc.l	0
my_privvio:
	cmp.l	#prot_exec,2(%sp)	| ours?
	beq.s	prot_code
	move.l	old_privvio,-(%sp)	| no: on to the one before
	rts
prot_code:
	or.w	#0x0700,%sr
	move.l	%a0,-(%sp)
	jsr	(%a1)
	addq.l	#4,%sp
	move.w	(%sp),%d1		| the caller's status
	adda.w	framesize,%sp		| drop the frame
	move.w	%d1,%sr			| back to user mode, on the user stack
	rts				| to protect_exec's caller
framesize:
	.dc.w	6

| what TCP_open and TCP_wait_state run protected (STinG: next_port, get_sr)
get_sr:
	moveq	#0,%d0
	move.w	%sr,%d0
	rts

| TCP_open and TCP_wait_state go through protect_exec first, as STinG's
	.macro	PSTUB name
	.globl	\name
\name:
	pea	get_sr
	clr.l	-(%sp)
	bsr	protect_exec
	addq.l	#8,%sp
	pea	4(%sp)
	jsr	c_\name
	addq.l	#4,%sp
	rts
	.endm

| Pure C cdecl entry points: hand C a pointer to the packed word args
	.macro	STUB name
	.globl	\name
\name:
	pea	4(%sp)
	jsr	c_\name
	addq.l	#4,%sp
	rts
	.endm

	STUB	get_dftab
	PSTUB	TCP_open
	STUB	TCP_close
	STUB	TCP_send
	PSTUB	TCP_wait_state
	STUB	CNbyte_count
	STUB	CNget_block
	STUB	get_err_text
	STUB	unsupported
	STUB	resolve
