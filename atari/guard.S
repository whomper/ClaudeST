| SPDX-License-Identifier: GPL-2.0-or-later
| Copyright (C) 2026 AERA | Erez Yaary
|
| guard_run: one read the machine may refuse, in supervisor mode (call it
| through Supexec), with the bus error, address error, illegal instruction,
| privilege violation and line A/F vectors (2, 3, 4, 8, 10, 11) pointed
| here while it runs: a fault puts the stack back and says "no answer"
| (guard_ok 0) instead of bombs. For the FireBee tester's logs (the hub,
| 10 Nov 2026): a ColdFire may refuse what a 68030 answers (the CACR can't
| be read there). Written in instructions a 68000, a 68030 and a ColdFire
| all have (no movem with -(sp) or (sp)+, no memory-to-memory move), as
| the vectors cf68klib would use are ours while it runs. The vectors are
| taken at address 0 (VBR 0: TOS, EmuTOS and FireTOS's 68k mode).
| Interrupts are off (IPL 7) while they are ours: on a FireBee, FireTOS's
| 68k emulation (cf68klib) runs through the illegal instruction and line
| A/F vectors, and an interrupt's 68k code in that time came to fault
| here, which put back a stack not its own: 1.0's tools hung at the first
| read, under FireTOS and FreeMiNT alike (a tester, 11 Nov 2026).
|
|   guard_what 0: guard_val = *(long *) guard_addr
|              1: guard_val = the CACR (movec cacr,d0)
|              2: guard_val = *(unsigned short *) guard_addr
	.text
	.globl	guard_run
	.globl	guard_addr, guard_what, guard_val, guard_ok

guard_run:
	move.w	%sr,%d0
	move.w	%d0,saved_sr
	move.w	#0x2700,%sr		| (no interrupt while the vectors are ours)
	lea	old_vec,%a1
	move.l	0x08.w,%d0
	move.l	%d0,(%a1)+
	move.l	0x0c.w,%d0
	move.l	%d0,(%a1)+
	move.l	0x10.w,%d0
	move.l	%d0,(%a1)+
	move.l	0x20.w,%d0
	move.l	%d0,(%a1)+
	move.l	0x28.w,%d0
	move.l	%d0,(%a1)+
	move.l	0x2c.w,%d0
	move.l	%d0,(%a1)+
	lea	fault,%a0
	move.l	%a0,%d0
	move.l	%d0,0x08.w
	move.l	%d0,0x0c.w
	move.l	%d0,0x10.w
	move.l	%d0,0x20.w
	move.l	%d0,0x28.w
	move.l	%d0,0x2c.w
	move.l	%sp,saved_sp
	moveq	#0,%d0
	move.l	%d0,guard_ok
	move.l	%d0,guard_val
	move.l	guard_addr,%a0
	move.l	guard_what,%d1
	beq.s	1f
	subq.l	#1,%d1
	beq.s	2f
	moveq	#0,%d0
	move.w	(%a0),%d0
	bra.s	3f
1:	move.l	(%a0),%d0
	bra.s	3f
2:	.dc.w	0x4e7a,0x0002		| movec cacr,d0 (a 68000's assembler has no movec)
3:	move.l	%d0,guard_val
	moveq	#1,%d0
	move.l	%d0,guard_ok
back:
	lea	old_vec,%a1
	move.l	(%a1)+,%d0
	move.l	%d0,0x08.w
	move.l	(%a1)+,%d0
	move.l	%d0,0x0c.w
	move.l	(%a1)+,%d0
	move.l	%d0,0x10.w
	move.l	(%a1)+,%d0
	move.l	%d0,0x20.w
	move.l	(%a1)+,%d0
	move.l	%d0,0x28.w
	move.l	(%a1)+,%d0
	move.l	%d0,0x2c.w
	move.w	saved_sr,%d0
	move.w	%d0,%sr
	move.l	guard_ok,%d0
	rts
fault:
	move.l	saved_sp,%sp
	bra.s	back

	.bss
	.even
guard_addr:	.space	4
guard_what:	.space	4
guard_val:	.space	4
guard_ok:	.space	4
saved_sp:	.space	4
saved_sr:	.space	2
	.even
old_vec:	.space	24
