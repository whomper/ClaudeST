/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/* hebkey.c on the build machine: the table, the modifiers, the hook. */
#include <stdio.h>
#include "gem.h"
#include "hebkey.h"

static int fails, checks;
#define CK(c) do { checks++; if (!(c)) { fails++; printf("FAIL line %d: %s\n", __LINE__, #c); } } while (0)

static int hooked, cp_calls, ram_calls;
static short cp_pxy[8], cp_col[2], cp_h, cp_mode, cp_w, cp_planes;
static unsigned short cp_bits[16];
static short cell_h = 16;
void vqt_attributes(short h, short *a) { (void)h; a[9] = cell_h; }
void *st_ram(long n) { static unsigned short buf[64]; (void)n; ram_calls++; return buf; }
void vrt_cpyfm(short h, short mode, const short *pxy, MFDB *src, MFDB *dst, const short *colors)
{
	int i;
	(void)h; (void)dst;
	cp_calls++;
	cp_mode = mode;
	for (i = 0; i < 8; i++)
		cp_pxy[i] = pxy[i];
	cp_col[0] = colors[0];
	cp_col[1] = colors[1];
	cp_h = src->fd_h;
	cp_w = src->fd_w;
	cp_planes = src->fd_nplanes;
	for (i = 0; i < src->fd_h; i++)
		cp_bits[i] = ((unsigned short *)src->fd_addr)[i];
}

static void hook(void) { hooked++; }

/* the AES's key word: scancode << 8 | ascii */
#define KEY(scan, ascii) ((short)(((scan) << 8) | (ascii)))

int main(void)
{
	short s;
	/* off: every key is itself */
	CK(key_char(0, KEY(0x10, 'q')) == 'q');
	CK(key_char(0, KEY(0x1e, 'a')) == 'a');
	set_hebrew_kbd(1);
	CK(hebrew_kbd == 1);
	/* the SI-1452 rows: q w e r t y u i o p */
	{
		static const unsigned char row1[10] = { '/', '\'', 0xD4, 0xD5, 0xC2, 0xCA, 0xC7, 0xD8, 0xDA, 0xD2 };
		static const unsigned char row2[11] = { 0xD6, 0xC5, 0xC4, 0xCC, 0xD1, 0xCB, 0xC9, 0xCD, 0xD9, 0xDB, ',' };
		static const unsigned char row3[10] = { 0xC8, 0xD0, 0xC3, 0xC6, 0xCF, 0xCE, 0xD3, 0xD7, 0xDC, '.' };
		for (s = 0; s < 10; s++)
			CK(key_char(0, KEY(0x10 + s, 'x')) == row1[s]);
		for (s = 0; s < 11; s++)
			CK(key_char(0, KEY(0x1e + s, 'x')) == row2[s]);
		for (s = 0; s < 10; s++)
			CK(key_char(0, KEY(0x2c + s, 'x')) == row3[s]);
	}
	/* Shift, Control and Alt keep the Latin key; so do the keys outside the table */
	CK(key_char(K_LSHIFT, KEY(0x10, 'Q')) == 'Q');
	CK(key_char(K_RSHIFT, KEY(0x10, 'Q')) == 'Q');
	CK(key_char(0x08, KEY(0x10, 'q')) == 'q');
	CK(key_char(K_CTRL, KEY(0x10, 'q')) == 'q');
	CK(key_char(0, KEY(0x10, 0x11)) == 0x11);	/* ^Q typed while the program was busy: kstate 0 */
	CK(key_char(0, KEY(0x1c, 13)) == 13);		/* Return */
	CK(key_char(0, KEY(0x39, ' ')) == ' ');		/* Space */
	CK(key_char(0, KEY(0x0b, '0')) == '0');		/* digits */
	CK(key_char(0, KEY(0x1a, '[')) == '[');		/* unmapped keys inside the range */
	/* the hook: called when the keyboard is switched */
	hebrew_kbd_hook = hook;
	set_hebrew_kbd(0);
	CK(hooked == 1 && hebrew_kbd == 0);
	CK(key_char(0, KEY(0x10, 'q')) == 'q');
	set_hebrew_kbd(1);
	CK(hooked == 2 && hebrew_kbd == 1);
	/* the badge: a bitmap of ayin and bet, only while the keyboard is on */
	set_hebrew_kbd(0);
	hebrew_badge(1, 100, 20, 1);
	CK(cp_calls == 0 && ram_calls == 0);
	set_hebrew_kbd(1);
	hebrew_badge(1, 100, 20, 3);
	CK(cp_calls == 1 && cp_mode == 2 && cp_col[0] == 3 && cp_col[1] == 0);
	CK(cp_w == 16 && cp_h == 16 && cp_planes == 1);
	CK(cp_pxy[0] == 0 && cp_pxy[2] == 15 && cp_pxy[3] == 15 && cp_pxy[4] == 100 && cp_pxy[5] == 20 &&
	   cp_pxy[6] == 115 && cp_pxy[7] == 35);
	CK(cp_bits[0] == 0 && cp_bits[2] == 0 && cp_bits[3] == 0x7C86 && cp_bits[11] == 0x7EC0 && cp_bits[12] == 0);
	{
		short w, h;
		int r, ink = 0;
		cell_h = 8;
		hebrew_badge_size(1, &w, &h);
		CK(w == 16 && h == 8);
		hebrew_badge(1, 5, 6, 2);
		CK(cp_calls == 2 && cp_h == 8 && cp_pxy[7] == 13 && cp_pxy[3] == 7);
		CK(cp_bits[0] == 0 && cp_bits[7] == 0 && cp_bits[1] == 0x7C84 && cp_bits[6] == 0x7E60);
		for (r = 0; r < 8; r++)
			if (cp_bits[r])
				ink++;
		CK(ink == 6);
		cell_h = 13;
		hebrew_badge_size(1, &w, &h);
		CK(h == 16);
		cell_h = 12;
		hebrew_badge_size(1, &w, &h);
		CK(h == 8);
	}
	/* hebrew_badge_right: the right end of a band, centred, off when the keyboard is off */
	{
		int c0;
		cell_h = 16;
		set_hebrew_kbd(0);
		c0 = cp_calls;
		hebrew_badge_right(1, 10, 100, 300, 20, 1);
		CK(cp_calls == c0);
		set_hebrew_kbd(1);
		hebrew_badge_right(1, 10, 100, 300, 20, 1);
		CK(cp_calls == c0 + 1 && cp_pxy[4] == 10 + 300 - 16 - 3 && cp_pxy[5] == 102);
		CK(cp_pxy[6] == cp_pxy[4] + 15 && cp_pxy[7] == cp_pxy[5] + 15);
		hebrew_badge_right(1, 0, 50, 200, 12, 1);	/* a band lower than the badge: at the top */
		CK(cp_pxy[5] == 50 && cp_pxy[4] == 200 - 16 - 3);
		cell_h = 8;
		hebrew_badge_right(1, 0, 0, 100, 16, 2);
		CK(cp_pxy[5] == 4 && cp_pxy[7] == 11 && cp_pxy[4] == 81);
	}
	printf("hebkey: %d checks, %d failed\n", checks, fails);
	return fails != 0;
}
