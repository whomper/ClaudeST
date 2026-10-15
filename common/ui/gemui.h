/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * gemui.h - what the shared building blocks that draw (popup.c,
 * aboutbox.c, iconify.c) take from the app's own drawing code. Each
 * app's ui.h (its draw.c, font.c) provides them; the declarations here
 * must match, so the compiler checks the app keeps to them.
 *
 *   types:       GRECT { x, y, w, h }; KEY_SCAN, KEY_ASCII
 *   the screen:  vdi_h, cw, ch (the current font's cell), scr_w, scr_h,
 *                desk_x/y/w/h, planes, gl_wchar, gl_hchar (the AES's)
 *   drawing:     fill, frame, hline, line_col, text_at with TX_BOLD,
 *                TX_INVERSE, TX_LTR and TX_DIM, clip_on, clip_off
 *   fonts:       font_sys_push/pop: the system font for a popup or the
 *                About box, then the window's font again (cw, ch follow)
 *   memory:      st_ram(n): ST RAM (1 KB, one bitmap at a time) to blit
 *                from, since the screen can't be fed from TT RAM
 */
#ifndef AERA_GEMUI_H
#define AERA_GEMUI_H

#include "gem.h"
#include "ui.h"

#if !defined(TX_BOLD) || !defined(TX_INVERSE) || !defined(TX_LTR) || !defined(TX_DIM)
#error "the app's ui.h must define TX_BOLD, TX_INVERSE, TX_LTR and TX_DIM"
#endif

extern short vdi_h, cw, ch, scr_w, scr_h, desk_x, desk_y, desk_w, desk_h, planes;
extern short gl_wchar, gl_hchar;

void fill(GRECT *r, short color);
void frame(GRECT *r, short color);
void hline(short x1, short x2, short y);
void line_col(short x1, short y1, short x2, short y2, short color);
void text_at(short x, short y, const char *s, long n, short cols, short flags);
void clip_on(GRECT *r);
void clip_off(void);
void font_sys_push(void);
void font_sys_pop(void);
void *st_ram(long n);

#endif
