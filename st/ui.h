/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * ui.h - ClaudeST's drawing under the names the AERA programs' shared GEM
 * building blocks use (common/ui/gemui.h: the popup and the About box).
 * Only those include this file; claudest.c has the cst_ui_ functions.
 */
#ifndef CST_UI_H
#define CST_UI_H

#include "grect.h"

#define TX_BOLD    1
#define TX_INVERSE 2
#define TX_LTR     16
#define TX_DIM     128

#define KEY_SCAN(k)  (((unsigned short)(k)) >> 8)
#define KEY_ASCII(k) ((unsigned char)((k) & 0xff))

/* claudest.c's own fill(), line() and text() take other arguments */
#define fill           cst_ui_fill
#define frame          cst_ui_frame
#define hline          cst_ui_hline
#define line_col       cst_ui_line_col
#define text_at        cst_ui_text_at
#define clip_on        cst_ui_clip_on
#define clip_off       cst_ui_clip_off
#define st_ram         cst_ui_st_ram
#define font_sys_push  cst_ui_font_sys_push
#define font_sys_pop   cst_ui_font_sys_pop
#define vdi_h          vh
#define gl_wchar       sys_cw
#define gl_hchar       sys_ch

#endif
