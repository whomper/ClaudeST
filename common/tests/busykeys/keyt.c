/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * KEYT.PRG: what the AES gives for a key typed while the program is busy
 * (gem.h key_ctrl, key_shift). It starts, works for BUSY ms (a busy loop
 * with no AES call: the keys typed meanwhile wait), then reads every key
 * with evnt_multi_ and puts "KEYT kstate K key CODE" on the console
 * (NatFeats) with key_ctrl() and key_shift(). MODE.TXT: "lazy" leaves
 * evnt_multi_'s first call to start the shift state (else key_state_init()
 * at the start, so the keys typed while busy keep theirs); "bios" also
 * lists the BIOS keyboard buffer the keys wait in (Iorec, the 4-byte
 * entries as they are: the shift state in the top byte with conterm's
 * bit 3, which evnt_multi_ sets). Run by run.py.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tos.h"
#include "gem.h"

static void say(const char *fmt, long a, long b, long c)
{
	char line[96];
	snprintf(line, sizeof(line), fmt, a, b, c);
	nf_debug(line);
}

/* conterm, the system variable at 0x484 (GCC mustn't see a constant address) */
static volatile char *conterm_ptr(void)
{
	volatile char *p;
	__asm__("" : "=r"(p) : "0"(0x484L));
	return p;
}

static long rd_conterm;
static void sup_read(void)
{
	rd_conterm = *conterm_ptr();
}

int main(void)
{
	short msg[8];
	AEVENT ev;
	char mode[16] = { 0 }, b[16] = { 0 };
	long fh;
	short busy = 0;
	u32 s;
	appl_init();
	fh = Fopen("BUSY.TXT", 0);
	if (fh >= 0) {
		Fread((short)fh, 15, b);
		Fclose((short)fh);
		busy = (short)atoi(b);
	}
	fh = Fopen("MODE.TXT", 0);
	if (fh >= 0) {
		Fread((short)fh, 15, mode);
		Fclose((short)fh);
	}
	if (strncmp(mode, "lazy", 4))
		key_state_init();		/* "lazy": evnt_multi_'s own first call (after the busy time) */
	Supexec(sup_read);
	say("KEYT ready busy %ld mode %s conterm %lx\n", busy, (long)(mode[0] ? mode : "-"), rd_conterm);
	for (s = tos_ms(); tos_ms() - s < (u32)busy * 1000UL;)
		;
	if (!strncmp(mode, "bios", 4)) {		/* (the BIOS buffer as it is, for the notes) */
		IOREC *io = Iorec(1);
		short hd = io->ibufhd, tl = io->ibuftl, i;
		say("KEYT iorec head %ld tail %ld size %ld\n", hd, tl, io->ibufsiz);
		for (i = hd; i != tl;) {
			unsigned char *p;
			i = (short)(i + 4);
			if (i >= io->ibufsiz)
				i = 0;
			p = (unsigned char *)io->ibuf + i;
			say("KEYT entry %lx %lx %lx\n", p[0], p[1], p[2] << 8 | p[3]);
		}
	}
	for (;;) {
		short e = evnt_multi_(MU_KEYBD | MU_TIMER, 0, 0, 0, 3000, msg, &ev);
		if (e & MU_KEYBD) {
			{
				char l[96];
				snprintf(l, sizeof(l), "KEYT kstate %x key %04x ctrl %d shift %d\n", ev.kstate & 0xff,
					 (unsigned short)ev.kreturn, key_ctrl(ev.kstate, ev.kreturn),
					 key_shift(ev.kstate, ev.kreturn));
				nf_debug(l);
			}
			if ((ev.kreturn & 0xff) == 'q')
				break;
		}
	}
	appl_exit();
	return 0;
}
