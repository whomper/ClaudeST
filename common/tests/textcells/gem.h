/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/* test_textcells's stand-in for the VDI: each call recorded */
short vqt_extent_n(short h, const char *s, short n);
short vqt_width(short h, short c);
void v_justified_n(short h, short x, short y, const char *s, short n, short len, short ws, short cs);
void v_gtext_n(short h, short x, short y, const char *s, short n);
void v_gtext_w(short h, short x, short y, const unsigned short *s, short n);
short vst_map_mode(short h, short mode);
short vst_font(short h, short id);
void vst_height(short h, short height, short *cw, short *ch);
short vst_point(short h, short point, short *cw, short *ch);
