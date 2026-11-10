/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/* a stand-in AES and drawing for popup.c and tooltip.c on the host: they record what is drawn */
#include <string.h>
#include "gem.h"
#include "ui.h"
#include "dialog.h"
#include "stub.h"

short vdi_h = 1, cw = 8, ch = 16, scr_w = 640, scr_h = 400, desk_x = 0, desk_y = 19, desk_w = 640, desk_h = 381, planes = 1;
short gl_wchar = 8, gl_hchar = 16;
TEXT_AT texts[MAXT];
int ntexts, nfills, nframes, nclip_on, nclip_off, nforms;
GRECT fills[MAXT], frames[MAXT], clip;
unsigned long now_ms;
short key_next = 0x011b;
short bubble_id = -1;
short sent[8];
int nsent;
char shared_buf[512];

void stub_reset(void)
{
	ntexts = nfills = nframes = nclip_on = nclip_off = nforms = nsent = 0;
	memset(texts, 0, sizeof(texts));
	cw = 8;
	ch = 16;
}

void fill(GRECT *r, short c)
{
	(void)c;
	if (nfills < MAXT)
		fills[nfills] = *r;
	nfills++;
}
void frame(GRECT *r, short c)
{
	(void)c;
	if (nframes < MAXT)
		frames[nframes] = *r;
	nframes++;
}
void hline(short a, short b, short y) { (void)a, (void)b, (void)y; }
void line_col(short a, short b, short c, short d, short e) { (void)a, (void)b, (void)c, (void)d, (void)e; }
void text_at(short x, short y, const char *s, long n, short cols, short flags)
{
	(void)cols;
	if (ntexts < MAXT) {
		TEXT_AT *t = &texts[ntexts];
		t->x = x;
		t->y = y;
		t->n = (short)n;
		t->flags = flags;
		strncpy(t->s, s, (size_t)n < 79 ? (size_t)n : 79);
	}
	ntexts++;
}
void clip_on(GRECT *r)
{
	clip = *r;
	nclip_on++;
}
void clip_off(void) { nclip_off++; }
void font_sys_push(void) {}
void font_sys_pop(void) {}
void *st_ram(long n) { (void)n; return 0; }

short mouse_x, mouse_y;
int mk_calls;
short graf_mkstate(short *mx, short *my, short *mb, short *ks) { mk_calls++; *mx = mouse_x; *my = mouse_y; *mb = *ks = 0; return 1; }
short graf_mouse(short n, void *f) { (void)n, (void)f; return 1; }
short wind_update(short m) { (void)m; return 1; }
short form_dial(short f, short x, short y, short w, short h) { (void)f, (void)x, (void)y, (void)w, (void)h; nforms++; return 1; }
short evnt_multi_(short flags, short clicks, short mask, short state, unsigned long timer, short *msg, AEVENT *ev)
{
	(void)flags, (void)clicks, (void)mask, (void)state, (void)timer, (void)msg;
	memset(ev, 0, sizeof(*ev));
	ev->which = MU_KEYBD;
	ev->kreturn = key_next;
	return ev->which;
}
short evnt_timer_(unsigned long ms) { (void)ms; return 1; }
void vs_clip(short h, short on, const short *pxy) { (void)h, (void)on, (void)pxy; }
void *dlg_under(short x, short y, short w, short h) { (void)x, (void)y, (void)w, (void)h; return 0; }
void dlg_under_back(void *p) { (void)p; }

unsigned long tos_ms(void) { return now_ms; }
short appl_find(const char *name) { return !strcmp(name, "BUBBLE  ") ? bubble_id : -1; }
short appl_id(void) { return 7; }
short appl_write(short id, short len, const void *buf)
{
	(void)id, (void)len;
	memcpy(sent, buf, sizeof(sent));
	nsent++;
	return 1;
}
char *av_shared(char *local, long size) { (void)local, (void)size; return shared_buf; }
