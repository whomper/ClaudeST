| SPDX-License-Identifier: GPL-2.0-or-later
| Copyright (C) 2026 AERA | Erez Yaary
|
| mmu040.S - 0MMU040.PRG (run.py --machine ct60): a 68040's MMU on with page
| tables, as a CT60's TOS leaves its 68060's for FreeMiNT. On a 68040 or a
| 68060 FreeMiNT takes the MMU only where TOS has turned it on (TC's E bit)
| and copies its tree from URP (sys/arch/detect.S, mprot040.c); with RAM
| under transparent translation instead it says "Couldn't initialize memory
| protection" and stops. EmuTOS leaves the MMU off, so this runs in AUTO
| before MINT040.PRG (AUTO's order is its names').
|
| The first 80 MB mapped 1:1 in 4 KB pages: ST RAM, the ROM, the 24-bit
| hardware area $F00000-$FFFFFF (not cached), TT RAM from $01000000 (Hatari's
| --ttram 64). $FF000000 up, the hardware, by ITT1 and DTT1 (not cached).
| One root table, three pointer tables (each 128 entries of 256 KB: the
| 80 MB need 320) and 320 page tables (64 entries of 4 KB), all in the BSS,
| the root 512-aligned; Ptermres keeps them, as the MMU goes on reading them
| until FreeMiNT has copied them.
|
| Descriptors: a root or pointer entry is the next table's address | 2
| (resident); a page entry is its address | 1 (resident), | $40 for the
| hardware (CM 10: not cached, serialised).

	.text
	.globl	_start
_start:
	move.l	4(%sp),%a6		| the basepage
	clr.l	-(%sp)
	move.w	#0x20,-(%sp)		| Super(0)
	trap	#1
	addq.l	#6,%sp
	move.l	%d0,%a5			| (the user stack, for going back)
	move.l	0x18(%a6),%d0		| p_bbase: the BSS
	add.l	#511,%d0
	and.l	#0xfffffe00,%d0
	move.l	%d0,%a0			| the root table, 512-aligned
	move.l	%a0,%a1
	move.w	#511,%d1		| the root table and the three pointer tables, cleared
1:	clr.l	(%a1)+
	dbra	%d1,1b
	lea	512(%a0),%a2		| the pointer tables
	lea	1536(%a2),%a3		| the page tables, 256 bytes each, one after another
	moveq	#0,%d2			| the 256 KB block, 0 to 319
3:	move.l	%d2,%d3
	lsr.l	#7,%d3			| its root entry: block / 128
	move.l	%d3,%d4
	lsl.l	#8,%d4
	add.l	%d4,%d4			| x 512
	lea	(%a2,%d4.l),%a4		| its pointer table
	move.l	%a4,%d5
	or.l	#2,%d5
	move.l	%d5,(%a0,%d3.l*4)
	move.l	%d2,%d3
	and.l	#127,%d3		| its pointer entry: block % 128
	move.l	%a3,%d5
	or.l	#2,%d5
	move.l	%d5,(%a4,%d3.l*4)
	move.l	%d2,%d6
	moveq	#18,%d7
	lsl.l	%d7,%d6			| its first page's address
	moveq	#63,%d1
4:	move.l	%d6,%d5
	cmp.l	#0x00f00000,%d5
	blo.s	5f
	cmp.l	#0x01000000,%d5
	bhs.s	5f
	or.l	#0x40,%d5		| the 24-bit hardware area: not cached
5:	or.l	#1,%d5			| resident
	move.l	%d5,(%a3)+
	add.l	#4096,%d6
	dbra	%d1,4b
	addq.l	#1,%d2
	cmp.l	#320,%d2		| 80 MB
	blo.s	3b
	move.l	#0xff00c040,%d0		| $FFxxxxxx: enabled, either mode, not cached
	movec	%d0,%itt1
	movec	%d0,%dtt1
	moveq	#0,%d0
	movec	%d0,%itt0
	movec	%d0,%dtt0
	move.l	%a0,%d0
	movec	%d0,%urp
	movec	%d0,%srp
	move.l	#0x8000,%d0		| TC: enabled, 4 KB pages
	movec	%d0,%tc
	move.l	%a5,-(%sp)
	move.w	#0x20,-(%sp)		| back to user mode
	trap	#1
	addq.l	#6,%sp
	clr.w	-(%sp)
	move.l	%a3,%d0
	sub.l	%a6,%d0
	move.l	%d0,-(%sp)		| from the basepage to the tables' end
	move.w	#0x31,-(%sp)		| Ptermres(that, 0)
	trap	#1

	.bss
	.skip	511 + 2048 + 320 * 256	| the alignment, the root and pointer tables, the page tables
