/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/* the VDI calls progbar.c makes, for the host test (test_progbar.c) */

void vq_color(short h, short index, short *rgb);
void vsf_interior(short h, short style);
void vsf_perimeter(short h, short on);
void vswr_mode(short h, short mode);
void vsf_color(short h, short color);
void v_bar(short h, const short *pxy);
