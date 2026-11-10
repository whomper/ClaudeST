/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * tooltip.c - the family's tooltip (tooltip.h): EFTP's tip_show (ECal's, EMail's and EUpdate's copies
 * were the same) with BubbleGEM first, as EHypView does it, drawn once in a clip of its own.
 */
#include <string.h>
#include "tos.h"
#include "gemui.h"
#include "av.h"
#include "notify.h"
#include "tooltip.h"

#define TEXT 128			/* a tip's text: two lines of 60, the "|" and the end */

int (*tip_rtl)(const char *s, short n);

static void (*repaint)(const GRECT *r);
static char local[2 * TEXT];
static char *shared = local;		/* where BubbleGEM can read it: global under MiNT and MagiC; two in turn */
static short turn;
static short on;			/* our box is up, over tip_r */
static short tried = 1;			/* shown, or nothing here, or a key or click since: not again until a move */
static short hx = -1, hy = -1;		/* where the pointer rests */
static unsigned long since;		/* when it came there (tos_ms) */
static GRECT tip_r;			/* what the box touched, its frame included */

void tip_init(void (*repaint_fn)(const GRECT *r))
{
	repaint = repaint_fn;
	shared = av_shared(local, sizeof(local));
}

void tip_hide(void)
{
	if (!on)
		return;
	on = 0;
	if (repaint)
		repaint(&tip_r);
	else
		form_dial(FMD_FINISH, tip_r.x, tip_r.y, tip_r.w, tip_r.h);
}

int tip_area(GRECT *r)
{
	if (!on)
		return 0;
	if (r)
		*r = tip_r;
	return 1;
}

unsigned long tip_wait(unsigned long other)
{
	unsigned long left;
	long gone;
	if (tried || hx < 0)
		return other;
	gone = (long)(tos_ms() - since);
	left = gone >= TIP_DELAY ? 1 : (unsigned long)(TIP_DELAY - gone);
	return left < other ? left : other;
}

/* BubbleGEM's bubble at the pointer, when it runs (it takes the text from global memory at once and
   shows and takes it away itself; its lines end in '\n'): 1 when it was asked */
static int bubble(const char *text, short mx, short my)
{
	short id = appl_find("BUBBLE  "), msg[8];
	char *b;
	short i;
	if (id < 0)
		return 0;
	b = shared + turn * TEXT;
	turn ^= 1;
	for (i = 0; text[i] && i < TEXT - 1; i++)
		b[i] = text[i] == '|' ? '\n' : text[i];
	b[i] = 0;
	memset(msg, 0, sizeof(msg));
	msg[0] = (short)BUBBLEGEM_SHOW;
	msg[1] = appl_id();
	msg[3] = mx;
	msg[4] = my;
	msg[5] = (short)((unsigned long)b >> 16);
	msg[6] = (short)(unsigned long)b;
	appl_write(id, 16, msg);
	return 1;
}

static short cap(short n, short max)
{
	return n > max ? max : n;
}

/* our box: one or two lines, drawn once */
static void box(char *text)
{
	char *l2 = strchr(text, '|');
	short n1, n2, n, room;
	GRECT r;
	if (l2)
		*l2++ = 0;
	font_sys_push();
	room = (short)(desk_w / cw - 2);
	if (room > TIP_LINE)
		room = TIP_LINE;
	n1 = cap((short)strlen(text), room);
	n2 = l2 ? cap((short)strlen(l2), room) : 0;
	n = n1 > n2 ? n1 : n2;
	r.w = (short)((n + 2) * cw);
	r.h = (short)((l2 ? 2 : 1) * ch + 6);
	r.x = (short)(hx + cw);
	r.y = (short)(hy + ch + 2);
	if (r.x + r.w > desk_x + desk_w)		/* on the desktop: left of the pointer, above it */
		r.x = (short)(hx - r.w - cw / 2);
	if (r.y + r.h > desk_y + desk_h)
		r.y = (short)(hy - r.h - 2);
	if (r.x + r.w > desk_x + desk_w)
		r.x = (short)(desk_x + desk_w - r.w);
	r.x &= ~7;				/* its text on a byte (the guidelines' Speed) */
	if (r.x < desk_x)
		r.x = (short)((desk_x + 7) & ~7);
	if (r.y < desk_y)
		r.y = desk_y;
	tip_r = r;
	tip_r.w++;				/* one more each way: the frame and what a clip cuts */
	tip_r.h++;
	wind_update(BEG_UPDATE);
	graf_mouse(M_OFF, 0);
	clip_on(&tip_r);
	fill(&r, 0);
	frame(&r, 1);
	text_at((short)(r.x + cw), (short)(r.y + 3), text, n1, n1,
#ifdef TX_RIGHT
		(tip_rtl && tip_rtl(text, n1)) ? TX_RIGHT :
#endif
		TX_LTR);
	if (l2)
		text_at((short)(r.x + cw), (short)(r.y + 3 + ch), l2, n2, n2, TX_LTR | TX_DIM);
	clip_off();
	graf_mouse(M_ON, 0);
	wind_update(END_UPDATE);
	font_sys_pop();
	on = 1;
}

static void show(tip_text_fn text)
{
	char t[TEXT];
	tried = 1;
	t[0] = 0;
	if (!text || !text(hx, hy, t, (int)sizeof(t)) || !t[0])
		return;
	t[TEXT - 1] = 0;
	if (!bubble(t, hx, hy))
		box(t);
}

void tip_tick(short which, short x, short y, tip_text_fn text)
{
	unsigned long now = tos_ms();
	if (x < 0) {				/* no pointer to show a tip for */
		tip_hide();
		hx = -1;
		tried = 1;
		return;
	}
	if (x != hx || y != hy || (which & (MU_M1 | MU_KEYBD | MU_BUTTON | MU_MESAG))) {
		if (x != hx || y != hy || (which & MU_M1))
			tried = 0;		/* a move: a new place */
		if (which & (MU_KEYBD | MU_BUTTON))
			tried = 1;		/* a key or a click: none here until the pointer moves */
		tip_hide();
		hx = x;
		hy = y;
		since = now;
		return;
	}
	if (!tried && now - since >= TIP_DELAY)
		show(text);
}
