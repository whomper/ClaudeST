| SPDX-License-Identifier: GPL-2.0-or-later
| Copyright (C) 2026 AERA | Erez Yaary
| inflate_fast.S - inflate_piece's hot loop (src/inflate.c, INFLATE_ASM)
| on the 68000 and up: literals and matches into the piece while the
| input has the bits, as the C loop does, in fewer instructions and
| small enough for the 68030's 256-byte instruction cache (the C loop's
| literal and match paths took ~330 bytes on lines that evict each
| other: EUpdate, 1 Nov 2026, "saving takes forever"). 68000 code only.
|
| int inflate_fast(IFAST *f)
|   f: op 0, step 4, in 8, iend 12, bits 16, nbits 20, fast 24,
|      dfast 28, base 32, len 36 (out)
| Runs until op >= step, or stops where the C takes over: a code longer
| than the table's, the block's end, too few bits (nothing of that
| symbol taken). 0 then; 1: a length taken (len) whose distance it
| couldn't read; 2: a distance before the buffer's start (damaged).
| op, in, bits and nbits written back.

	.text
	.globl	inflate_fast
inflate_fast:
	movem.l	%d2-%d7/%a2-%a6,-(%sp)
	move.l	48(%sp),%a6
	move.l	%a6,-(%sp)		| f, at (sp)
	move.l	(%a6),%a2		| op
	move.l	4(%a6),%a3		| step
	move.l	8(%a6),%a0		| in
	move.l	12(%a6),%a1		| iend
	move.l	16(%a6),%d2		| bits
	move.l	20(%a6),%d3		| nbits
	move.l	24(%a6),%a4		| fast
	move.l	28(%a6),%a5		| dfast
	moveq	#0,%d6			| the answer
	cmp.l	%a3,%a2
	bcc	.Ldone

| a literal or a length's code. From .Lloop to the copy's end it is
| under 256 bytes, on a 16-byte line: the 68030's cache keeps all of it
	.p2align 4
.Lloop:
	cmp.w	#24,%d3
	bgt.s	.Lhave
.Lfill:
	cmp.l	%a1,%a0
	bcc.s	.Lfilled
	moveq	#0,%d0
	move.b	(%a0)+,%d0
	lsl.l	%d3,%d0
	or.l	%d0,%d2
	addq.w	#8,%d3
	cmp.w	#24,%d3
	ble.s	.Lfill
	bra.s	.Lhave
.Lfilled:
	cmp.w	#10,%d3
	blt	.Ldone
.Lhave:
	move.w	%d2,%d0
	and.w	#1023,%d0
	add.w	%d0,%d0
	move.w	(%a4,%d0.w),%d0		| (sym << 4) | len
	beq	.Ldone			| a longer code
	moveq	#15,%d1
	and.w	%d0,%d1			| its length
	lsr.w	#4,%d0
	cmp.w	#256,%d0
	bcc.s	.Lmatch
	lsr.l	%d1,%d2			| a literal
	sub.w	%d1,%d3
	move.b	%d0,(%a2)+
	cmp.l	%a3,%a2
	bcs.s	.Lloop
	bra	.Ldone

| a match: its length
.Lmatch:
	sub.w	#257,%d0		| 256 (the block's end) is -1, 286 and 287
	add.w	%d0,%d0			| (damaged) 29 and 30: their entries ask for
	add.w	%d0,%d0			| more bits than there are, so the C's
	lea	.Lltab(%pc),%a6
	move.l	(%a6,%d0.w),%d4		| base << 16 | extra bits (63: the C's)
	move.w	%d4,%d7			| x (shifts take its low 6 bits)
	swap	%d4			| base, in the low word
	move.w	%d1,%d0
	add.w	%d7,%d0
	cmp.w	%d0,%d3
	blt	.Ldone			| too few bits: nothing taken
	lsr.l	%d1,%d2
	sub.w	%d0,%d3
	moveq	#1,%d5
	lsl.w	%d7,%d5
	subq.w	#1,%d5
	and.w	%d2,%d5
	lsr.l	%d7,%d2
	add.w	%d5,%d4			| the length

| its distance
	cmp.w	#24,%d3
	bgt.s	.Ldhave
.Ldfill:
	cmp.l	%a1,%a0
	bcc.s	.Ldfilled
	moveq	#0,%d0
	move.b	(%a0)+,%d0
	lsl.l	%d3,%d0
	or.l	%d0,%d2
	addq.w	#8,%d3
	cmp.w	#24,%d3
	ble.s	.Ldfill
	bra.s	.Ldhave
.Ldfilled:
	cmp.w	#10,%d3
	blt.s	.Lpend
.Ldhave:
	move.w	%d2,%d0
	and.w	#1023,%d0
	add.w	%d0,%d0
	move.w	(%a5,%d0.w),%d0
	beq.s	.Lpend
	moveq	#15,%d1
	and.w	%d0,%d1
	lsr.w	#4,%d0			| 30 and 31 (damaged): 63 bits, the C's
	add.w	%d0,%d0
	add.w	%d0,%d0
	lea	.Ldtab(%pc),%a6
	move.l	(%a6,%d0.w),%d5		| base << 16 | extra bits
	move.w	%d5,%d7			| x
	clr.w	%d5
	swap	%d5			| base
	move.w	%d1,%d0
	add.w	%d7,%d0
	cmp.w	%d0,%d3
	blt.s	.Lpend
	lsr.l	%d1,%d2
	sub.w	%d0,%d3
	moveq	#1,%d1
	lsl.l	%d7,%d1
	subq.l	#1,%d1
	and.l	%d2,%d1
	lsr.l	%d7,%d2
	add.l	%d1,%d5			| the distance
	move.l	(%sp),%a6
	move.l	%a2,%d0
	sub.l	32(%a6),%d0		| what there is behind op
	cmp.l	%d5,%d0
	bcs.s	.Lbad
	move.l	%a2,%a6
	sub.l	%d5,%a6
	subq.w	#1,%d4
.Lcp:
	move.b	(%a6)+,(%a2)+		| a byte at a time: it may overlap itself
	dbra	%d4,.Lcp
	cmp.l	%a3,%a2
	bcs	.Lloop
	bra.s	.Ldone

.Lpend:
	moveq	#1,%d6			| the length taken, the distance not
	move.l	(%sp),%a6
	moveq	#0,%d0
	move.w	%d4,%d0
	move.l	%d0,36(%a6)
	bra.s	.Ldone
.Lbad:
	moveq	#2,%d6
.Ldone:
	move.l	(%sp)+,%a6
	move.l	%a2,(%a6)
	move.l	%a0,8(%a6)
	move.l	%d2,16(%a6)
	ext.l	%d3
	move.l	%d3,20(%a6)
	move.l	%d6,%d0
	movem.l	(%sp)+,%d2-%d7/%a2-%a6
	rts

	.even
	.long	0x0000003f		| 256, the block's end: the C's
.Lltab:
	.long	0x00030000, 0x00040000, 0x00050000, 0x00060000, 0x00070000, 0x00080000
	.long	0x00090000, 0x000a0000, 0x000b0001, 0x000d0001, 0x000f0001, 0x00110001
	.long	0x00130002, 0x00170002, 0x001b0002, 0x001f0002, 0x00230003, 0x002b0003
	.long	0x00330003, 0x003b0003, 0x00430004, 0x00530004, 0x00630004, 0x00730004
	.long	0x00830005, 0x00a30005, 0x00c30005, 0x00e30005, 0x01020000
	.long	0x0000003f, 0x0000003f	| 286, 287: damaged
.Ldtab:
	.long	0x00010000, 0x00020000, 0x00030000, 0x00040000, 0x00050001, 0x00070001
	.long	0x00090002, 0x000d0002, 0x00110003, 0x00190003, 0x00210004, 0x00310004
	.long	0x00410005, 0x00610005, 0x00810006, 0x00c10006, 0x01010007, 0x01810007
	.long	0x02010008, 0x03010008, 0x04010009, 0x06010009, 0x0801000a, 0x0c01000a
	.long	0x1001000b, 0x1801000b, 0x2001000c, 0x3001000c, 0x4001000d, 0x6001000d
	.long	0x0000003f, 0x0000003f	| 30, 31: damaged
