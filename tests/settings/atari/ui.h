/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * ui.h - SETDEMO.PRG's stand-in for an app's ui.h: what common's drawing
 * blocks (gemui.h) take from it, as EPlay's has it.
 */
#ifndef UI_H
#define UI_H

#include "tos.h"
#include "gem.h"

typedef struct { short x, y, w, h; } GRECT;

#define TX_BOLD    1
#define TX_INVERSE 2
#define TX_LTR     4
#define TX_DIM     8

#define KEY_SCAN(k)  (((k) >> 8) & 0xff)
#define KEY_ASCII(k) ((k) & 0xff)

extern short vdi_h, cw, ch, scr_w, scr_h, desk_x, desk_y, desk_w, desk_h, planes;
extern short gl_wchar, gl_hchar, ap_id;

void fill(GRECT *r, short color);
void frame(GRECT *r, short color);
void hline(short x1, short x2, short y);
void line_col(short x1, short y1, short x2, short y2, short color);
void text_at(short x, short y, const char *s, long n, short cols, short flags);
void clip_on(GRECT *r);
void clip_off(void);
void draw_forget(void);	/* the VDI's attributes not as draw.c left them (the About box) */
void font_sys_push(void);
void font_sys_pop(void);
void *st_ram(long n);
int rc_intersect(const GRECT *a, GRECT *b);

#endif
