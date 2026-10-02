| FAKESTNG.PRG startup: install the fake STinG and stay resident.
	.text
	.globl	_start
_start:
	move.l	4(%sp),%a0
	move.l	%a0,-(%sp)
	jsr	fake_install		| returns bytes to keep in d0
	addq.l	#4,%sp
	clr.w	-(%sp)
	move.l	%d0,-(%sp)
	move.w	#0x31,-(%sp)		| Ptermres
	trap	#1

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
	STUB	TCP_open
	STUB	TCP_close
	STUB	TCP_send
	STUB	TCP_wait_state
	STUB	CNbyte_count
	STUB	CNget_block
	STUB	get_err_text
	STUB	unsupported
