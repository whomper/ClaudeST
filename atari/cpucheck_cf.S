/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
| cpucheck_cf.S - the first code of a program built for the ColdFire (the
| FireBee: EWeb's third build), as cpucheck.c is for one built for the
| 68020: main() here, in instructions a 68000, a 68030 and a ColdFire all
| run alike (it assembles to the same bytes for -m68000 and -mcpu=5475:
| tests/coldfire's make cpucheck), finds the _CF_ cookie (FireTOS's and
| EmuTOS's for the ColdFire) and calls the program's app_main(); without
| it (an ST, a TT, a Falcon) it shows the program's alert and goes. No
| C: GCC's code for a 68000 has instructions a ColdFire hasn't (cmpa.w,
| move.l #,abs.l), and its code for a ColdFire ones a 68000 hasn't. The
| AES's arrays are filled in beforehand, as a ColdFire can't move a
| constant to an absolute address. The program defines, in its own
| sources:
|
|   const char cpu_alert[] = "[3][EWeb for the FireBee needs a|ColdFire: on this machine run|EWEB.PRG or EWEBST.PRG.][ Quit ]";
|   int app_main(void);
	.text
	.globl	main
main:
	pea	cf_cookie		| Supexec(cf_cookie)
	moveq	#38,%d0
	move.w	%d0,-(%sp)
	trap	#14
	addq.l	#6,%sp
	tst.l	%d0
	beq.w	refuse
	jmp	app_main		| a ColdFire: the program's own main

refuse:
	lea	pb_init,%a0		| appl_init
	bsr.w	aes
	move.l	#_is_acc,%d0		| an accessory (crt0's flag; weak: none outside an app)
	beq.w	4f
	move.l	%d0,%a0
	move.w	(%a0),%d0
	ext.l	%d0
	tst.l	%d0
	beq.w	4f
| an accessory never ends (crt0's loop would stop the machine) and says nothing at the boot: its Desk
| entry (cpu_acc_name, weak), and the alert when the user opens it (decided 22 Nov 2026; ECal found the hang)
	move.l	#cpu_acc_name,%d0
	beq.s	5f
	move.l	%d0,addrin
	move.w	global+4,%d0		| ap_id
	move.w	%d0,intin
	lea	pb_reg,%a0		| menu_register(ap_id, cpu_acc_name)
	bsr.w	aes
5:	move.l	#msgbuf,%d0
	move.l	%d0,addrin
	lea	pb_mesag,%a0		| evnt_mesag(msgbuf)
	bsr.w	aes
	move.w	msgbuf,%d0
	ext.l	%d0
	cmp.l	#40,%d0			| AC_OPEN
	bne.s	5b
	move.l	#cpu_acc_name,%d0
	beq.s	5b
	moveq	#1,%d0
	move.w	%d0,intin
	move.l	#cpu_acc_alert,%d0	| the accessory's own words where it has them (weak)
	bne.s	6f
	move.l	#cpu_alert,%d0
6:	move.l	%d0,addrin
	lea	pb_alert,%a0		| form_alert(1, cpu_alert)
	bsr.w	aes
	bra.s	5b
4:	move.w	intout,%d0
	ext.l	%d0
	tst.l	%d0
	blt.s	1f
	moveq	#1,%d0
	move.w	%d0,intin
	move.l	#cpu_alert,%d0
	move.l	%d0,addrin
	lea	pb_alert,%a0		| form_alert(1, cpu_alert)
	bsr.w	aes
	lea	pb_exit,%a0		| appl_exit
	bsr.w	aes
1:	moveq	#1,%d0
	rts

aes:
	move.l	%a0,%d1
	move.l	#200,%d0
	trap	#2
	rts

| supervisor mode (Supexec): 1 if the cookie jar has _CF_, else 0 (TOS 1.x
| has no jar, and no ColdFire)
cf_cookie:
	moveq	#0,%d0
	move.l	0x5a0.w,%a0		| _p_cookies
	move.l	%a0,%d1
	beq.s	2f
1:	move.l	(%a0),%d1
	beq.s	2f
	cmp.l	#0x5f43465f,%d1		| '_CF_'
	beq.s	3f
	addq.l	#8,%a0
	bra.s	1b
3:	moveq	#1,%d0
2:	rts

	.data
	.balign	2
| three AES parameter blocks, each its own control array filled in
pb_init:	.long	ctl_init, global, intin, intout, addrin, addrout
pb_alert:	.long	ctl_alert, global, intin, intout, addrin, addrout
pb_exit:	.long	ctl_exit, global, intin, intout, addrin, addrout
ctl_init:	.word	10, 0, 1, 0, 0
ctl_alert:	.word	52, 1, 1, 1, 0
ctl_exit:	.word	19, 0, 1, 0, 0
pb_reg:		.long	ctl_reg, global, intin, intout, addrin, addrout
pb_mesag:	.long	ctl_mesag, global, intin, intout, addrin, addrout
ctl_reg:	.word	35, 1, 1, 1, 0
ctl_mesag:	.word	23, 0, 1, 1, 0
intin:		.word	1, 0
addrin:		.long	cpu_alert, 0
	.bss
	.balign	2
global:		.space	30
intout:		.space	14
addrout:	.space	4
msgbuf:		.space	16
	.weak	_is_acc
	.weak	cpu_acc_name
	.weak	cpu_acc_alert
