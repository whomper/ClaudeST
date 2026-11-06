/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/* The Hebrew keyboard (hebkey.h). Weak definitions: see there. */
#include "gem.h"
#include "hebkey.h"
#include <string.h>
void *st_ram(long n);	/* gemui.h: ST RAM to blit from */

#ifndef K_ALT
#define K_ALT 0x08
#endif

short hebrew_kbd __attribute__((weak));
void (*hebrew_kbd_hook)(void) __attribute__((weak));

/* Israeli SI-1452 layout by key position (scancode 0x10-0x35), giving
   Atari character codes; 0 = not remapped. The table ECal, EMail, EFTP
   and Claude ST each had a copy of. */
static const unsigned char hebrew_keys[0x36 - 0x10] = {
	/* 10 q..p */ '/', '\'', 0xD4, 0xD5, 0xC2, 0xCA, 0xC7, 0xD8, 0xDA, 0xD2,
	/* 1a [ ] ret ctrl */ 0, 0, 0, 0,
	/* 1e a..' */ 0xD6, 0xC5, 0xC4, 0xCC, 0xD1, 0xCB, 0xC9, 0xCD, 0xD9, 0xDB, ',',
	/* 29 ` lshift \ */ 0, 0, 0,
	/* 2c z../ */ 0xC8, 0xD0, 0xC3, 0xC6, 0xCF, 0xCE, 0xD3, 0xD7, 0xDC, '.'
};

unsigned char key_char(short kstate, short key) __attribute__((weak));
unsigned char key_char(short kstate, short key)
{
	short scan = (key >> 8) & 0xff;
	unsigned char ascii = (unsigned char)(key & 0xff);
	if (hebrew_kbd && !(kstate & (K_LSHIFT | K_RSHIFT | K_ALT)) && !key_ctrl(kstate, key) &&
	    scan >= 0x10 && scan < 0x36 && hebrew_keys[scan - 0x10])
		ascii = hebrew_keys[scan - 0x10];
	return ascii;
}

void set_hebrew_kbd(short on) __attribute__((weak));
void set_hebrew_kbd(short on)
{
	hebrew_kbd = on;
	if (hebrew_kbd_hook)
		hebrew_kbd_hook();
}

/* The badge is a bitmap of the two letters ayin and bet, "ayin bet" read
   right to left (ayin at the right), so it shows in any app and any VDI
   font, with or without a Hebrew one. Two sizes: 16 rows for a text cell
   13 rows high or more, else 8. Letters are 8 pixels wide, bet at the
   left. */
static const unsigned char bet_tall[9] = {0x7C, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x7E};
static const unsigned char ayin_tall[9] = {0x86, 0x82, 0x84, 0x88, 0x50, 0x30, 0x20, 0x40, 0xC0};
static const unsigned char bet_low[6] = {0x7C, 0x06, 0x04, 0x04, 0x04, 0x7E};
static const unsigned char ayin_low[6] = {0x84, 0x84, 0x48, 0x30, 0x20, 0x60};

void hebrew_badge_size(short vdi, short *w, short *h)
{
	short a[10];
	vqt_attributes(vdi, a);
	*w = 16;
	*h = a[9] >= 13 ? 16 : 8;
}

void hebrew_badge(short vdi, short x, short y, short colour)
{
	short w, h, r, n, top, pxy[8], colors[2];
	unsigned short *bits;
	const unsigned char *b, *a;
	MFDB src, dst;
	if (!hebrew_kbd)
		return;
	hebrew_badge_size(vdi, &w, &h);
	bits = st_ram(2L * h);
	if (!bits)
		return;
	if (h == 16) {
		n = 9;
		top = 3;
		b = bet_tall;
		a = ayin_tall;
	} else {
		n = 6;
		top = 1;
		b = bet_low;
		a = ayin_low;
	}
	for (r = 0; r < h; r++)
		bits[r] = r >= top && r < top + n ? (unsigned short)((b[r - top] << 8) | a[r - top]) : 0;
	memset(&src, 0, sizeof(src));
	memset(&dst, 0, sizeof(dst));
	src.fd_addr = bits;
	src.fd_w = w;
	src.fd_h = h;
	src.fd_wdwidth = 1;
	src.fd_nplanes = 1;
	pxy[0] = 0;
	pxy[1] = 0;
	pxy[2] = w - 1;
	pxy[3] = h - 1;
	pxy[4] = x;
	pxy[5] = y;
	pxy[6] = x + w - 1;
	pxy[7] = y + h - 1;
	colors[0] = colour;
	colors[1] = 0;
	vrt_cpyfm(vdi, 2, pxy, &src, &dst, colors);	/* transparent */
}

void hebrew_badge_right(short vdi, short x, short y, short w, short h, short colour)
{
	short bw, bh, by;
	if (!hebrew_kbd)
		return;
	hebrew_badge_size(vdi, &bw, &bh);
	by = h > bh ? (short)(y + (h - bh) / 2) : y;
	hebrew_badge(vdi, (short)(x + w - bw - 3), by, colour);
}
