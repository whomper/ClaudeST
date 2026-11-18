/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/* Room linked after an object, never run or read: it puts what follows
   it back at the places it should have modulo 256, as the 68030's caches
   see them. EWeb uses it to keep code where the release before had it,
   and common/tools/profpad.py to put a timed build (AES_PROFILE) where
   the shipped one is. Made once for each place with -DPAD_NAME=name and
   the sizes -DPAD_TEXT, -DPAD_RO (read-only data), -DPAD_STR (into the
   merged strings), -DPAD_DATA and -DPAD_BSS (0 or absent: none). Its
   symbols are PAD_NAME, then the same name with _ro, _str, _data and
   _bss; the link keeps each one with -u, since
   --gc-sections would drop them. */
	.section .text.PAD_NAME,"ax"
	.globl	PAD_NAME
PAD_NAME:
#if PAD_TEXT > 0
	.space	PAD_TEXT
#endif
#if PAD_RO > 0
#define RO_(n) n ## _ro
#define RO(n) RO_(n)
	.section .rodata.PAD_NAME,"a"
	.globl	RO(PAD_NAME)
RO(PAD_NAME):
	.space	PAD_RO
#endif
#if defined(PAD_STR) && PAD_STR > 0
#define STR_(n) n ## _str
#define STR(n) STR_(n)
	/* (strings, into the block the linker merges every string into: the
	   read-only data after that block back at its places, when a timed
	   build's own strings made the block longer) */
	.section .rodata.str1.1,"aMS",@progbits,1
	.globl	STR(PAD_NAME)
STR(PAD_NAME):
	.rept	PAD_STR - 1
	.byte	0x7e	/* (not 0x7f: a string another pad's tail could hold) */
	.endr
	.byte	0
#endif
#if defined(PAD_DATA) && PAD_DATA > 0
#define DATA_(n) n ## _data
#define DATA(n) DATA_(n)
	.section .data.PAD_NAME,"aw"
	.globl	DATA(PAD_NAME)
DATA(PAD_NAME):
	.space	PAD_DATA
#endif
#if PAD_BSS > 0
#define BSS_(n) n ## _bss
#define BSS(n) BSS_(n)
	.section .bss.PAD_NAME,"aw",@nobits
	.globl	BSS(PAD_NAME)
BSS(PAD_NAME):
	.space	PAD_BSS
#endif
