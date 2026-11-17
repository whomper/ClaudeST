# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
# oddaccess.py must find both: f, and g after a switch table whose last
# entry (0x0072) put objdump out of step (EContact, 13 Nov 2026).
	.text
	.globl f
	.type f,@function
f:	movew %sp@(3),%d0
	rts
	.size f,.-f
	.globl g
	.type g,@function
g:	addl %d0,%d0
	movew %pc@(2f,%d0:l),%d0
	jmp %pc@(2f,%d0:w)
2:	.short 3f-2b, 4f-2b, 0x0072
3:	movel %sp@(5),%d1
4:	rts
	.size g,.-g
