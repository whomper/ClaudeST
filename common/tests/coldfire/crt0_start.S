/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
| test_crt0's start: the GEMDOS stand-in installed, then crt0.S's own
| start (crt0_start) entered as TOS enters it: an application with its
| basepage at 4(sp) and a0 = 0, an accessory (-DACC) with a0 its basepage
	.text
	.globl	_start
_start:
	jsr	gd_install
	lea	fake_bp,%a0
	clr.l	-(%sp)
	move.l	%a0,-(%sp)
	clr.l	-(%sp)			| (where TOS's return address would be)
#ifdef ACC
	lea	fake_bp,%a0
#else
	sub.l	%a0,%a0
#endif
	jmp	crt0_start

	.data
	.balign	4
	.globl	fake_bp
fake_bp:				| a basepage: p_tlen 0x0c, p_dlen 0x14, p_blen 0x1c
	.long	0, 0, 0, 0x1234, 0, 0x560, 0, 0x7a000
	.space	0x100 - 32
