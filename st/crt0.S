| Minimal TOS startup for Claude ST.
| TOS enters with 4(sp) = basepage. We give memory we don't need back
| to GEMDOS (Mshrink), move the stack into our own BSS, run main() and
| terminate with its return value.

	.text
	.globl	_start
_start:
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
	jsr	main
	move.w	%d0,-(%sp)
	move.w	#0x4c,-(%sp)		| Pterm
	trap	#1

	.bss
	.even
	.globl	_BasePage
_BasePage:
	.space	4
	.space	24576
_stack_top:
