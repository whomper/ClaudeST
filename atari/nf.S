/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
| NatFeats (emulator native features) for debug output under Hatari/ARAnyM.
| long nf_probe(void)  - supervisor only: NF id of "NF_STDERR", 0 if absent
| long nf_call2(long id, long arg)
| long nf_find(const char *name) - supervisor only: NF id of name, 0 if absent
|
| NatFeats read their argument at 4(sp), as in a C call, so the caller
| leaves room for a return address (EHypView found that the earlier copy
| missed it, so nf_debug() never printed).

#ifdef __mcoldfire__
| The ColdFire (the FireBee): no emulator, so no NatFeats; and NatFeats'
| opcodes are no illegal instructions there (0x7300 is mvs.b %d0,%d1 on a
| V4e), so nothing is tried and no vector touched: none found, and a call
| does nothing
	.text
	.globl	nf_probe, nf_call2, nf_find, nf_vec
nf_probe:
nf_find:
nf_call2:
	moveq	#0,%d0
	rts
	.globl	nf_debug
nf_debug:
	rts
	.data
	.weak	nf_on
nf_vec:
	.long	0x10
nf_on:
	.long	0
	.text
#else
	.text
| nf_vec from the CPU: VBR + 0x10 on a 68010 or later (the _CPU cookie),
| 0x10 on a 68000. In nf.S itself, so every caller of nf_probe and nf_find
| is safe, also those that call them through their own Supexec (EWeb's
| log_event). A Vampire V4SA's Atari core keeps VBR away from 0: the
| handler put on 0x10 wasn't used, 0x7300 went to the system's own
| handler, and EMail and EHypView were ended, EWeb, EPDF and EPlay hung
| (15 Nov 2026). Uses d0, a0
nf_none:
	moveq	#0,%d0
	rts

nf_setvec:
	move.l	0x5a0.w,%a0		| the cookie jar
	move.l	%a0,%d0
	beq.s	9f
1:	move.l	(%a0)+,%d0
	beq.s	9f
	cmp.l	#0x5F435055,%d0		| '_CPU'
	beq.s	2f
	addq.l	#4,%a0
	bra.s	1b
2:	moveq	#10,%d0
	cmp.l	(%a0),%d0
	bgt.s	9f			| a 68000: no VBR
	.dc.w	0x4e7a,0x0801		| movec %vbr,%d0 (as words: no .chip)
	add.l	#0x10,%d0
	move.l	%d0,nf_vec
9:	rts

	.globl	nf_probe
nf_probe:
	tst.l	nf_on			| only where the app's INF asked for them
	beq.s	nf_none
	bsr.s	nf_setvec
	move.l	%a2,-(%sp)
	movea.l	nf_vec,%a2		| the illegal instruction vector (VBR + 0x10)
	move.l	(%a2),%a1
	move.l	%sp,%a0
	move.l	#1f,(%a2)
	pea	nf_name
	subq.l	#4,%sp			| NatFeats read the argument at 4(sp),
	moveq	#0,%d0			| as in a C call: leave room for the
	.dc.w	0x7300			| return address (nf_id)
	addq.l	#8,%sp
	move.l	%a1,(%a2)
	movea.l	(%sp)+,%a2
	rts
1:	move.l	%a0,%sp			| no NatFeats: illegal instruction
	moveq	#0,%d0
	move.l	%a1,(%a2)
	movea.l	(%sp)+,%a2
	rts

	.globl	nf_call2
nf_call2:
	.dc.w	0x7301			| nf_call(id, arg)
	rts

| void nf_debug(const char *s) - from user mode: s on Hatari's or ARAnyM's
| stderr; probes once (through Supexec), nothing where there are no
| NatFeats or the INF didn't ask (nf_on). Here rather than in tos.c: only a
| timed build (AES_PROFILE) calls it, and GCC put tos.c's Supexec targets
| first in tos.o whatever their place, so the timed build kept code in the
| middle of tos.o that profpad.py couldn't put back (EContact, 17 Nov 2026).
| A whole object only the timed build keeps would be fine; nf.S is linked
| by every program that links tos.c. Keeps d2-d7/a2-a6 (TOS's trap may
| change d2 and a2: saved here)
	.globl	nf_debug
nf_debug:
	move.l	nf_id,%d0
	bpl.s	1f			| probed already
	movem.l	%d2/%a2,-(%sp)
	pea	nf_sup
	move.w	#38,-(%sp)		| Supexec
	trap	#14
	addq.l	#6,%sp
	movem.l	(%sp)+,%d2/%a2
	move.l	nf_id,%d0
1:	ble.s	2f			| 0: none
	move.l	4(%sp),-(%sp)		| nf_call2(id, s)
	move.l	%d0,-(%sp)
	bsr	nf_call2
	addq.l	#8,%sp
2:	rts

nf_sup:
	bsr	nf_probe
	move.l	%d0,nf_id
	rts

	.data
nf_id:
	.long	-1
	.text

	.data
nf_name:
	.asciz	"NF_STDERR"
	.even

| long nf_find(const char *name) - supervisor only: NF id of `name`,
| 0 if absent (EHypView's test program finds NF_EXIT with it)
	.text
	.globl	nf_find
nf_find:
	tst.l	nf_on
	beq	nf_none
	bsr	nf_setvec
	move.l	4(%sp),%d1
	move.l	%a2,-(%sp)
	movea.l	nf_vec,%a2
	move.l	(%a2),%a1
	move.l	%sp,%a0
	move.l	#2f,(%a2)
	move.l	%d1,-(%sp)
	subq.l	#4,%sp			| argument at 4(sp), as in a C call
	moveq	#0,%d0
	.dc.w	0x7300			| nf_id
	addq.l	#8,%sp
	move.l	%a1,(%a2)
	movea.l	(%sp)+,%a2
	rts
2:	move.l	%a0,%sp
	moveq	#0,%d0
	move.l	%a1,(%a2)
	movea.l	(%sp)+,%a2
	rts

| where the illegal-instruction vector is: 0x10, or VBR + 0x10 as
| nf_setvec finds it
	.data
	.globl	nf_vec
	.weak	nf_on			| a test program defines its own, set to 1
nf_vec:
	.long	0x10
| NatFeats are looked for only when the app sets nf_on, from its INF's
| NATFEATS=1, which only the test rigs write (decided 15 Nov 2026: an INF
| setting the tests can add, with no bearing outside): a real machine never runs the probe's illegal instruction
nf_on:
	.long	0
	.text
#endif
