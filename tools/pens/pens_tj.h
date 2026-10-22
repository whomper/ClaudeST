/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * pens_tj.h - pic.c's names for its EWEBST.PRG build (TJpgDec) in
 * aerapens, which carries both builds: forced into that pic.c with
 * -include, each pic_ name made tj_pic_ (the Makefile checks with nm that
 * none is left over, so that a new one can't be taken for the other's).
 */
#define pic_decode tj_pic_decode
#define pic_dither tj_pic_dither
#define pic_dither_state tj_pic_dither_state
#define pic_dsp tj_pic_dsp
#define pic_dsp_dither tj_pic_dsp_dither
#define pic_dsp_failed tj_pic_dsp_failed
#define pic_dsp_shrink tj_pic_dsp_shrink
#define pic_dsp_state tj_pic_dsp_state
#define pic_free tj_pic_free
#define pic_kind tj_pic_kind
#define pic_kind_name tj_pic_kind_name
#define pic_pal_free tj_pic_pal_free
#define pic_pal_init tj_pic_pal_init
#define pic_pal_nearest tj_pic_pal_nearest
#define pic_poll tj_pic_poll
#define pic_size tj_pic_size
#define pic_why_text tj_pic_why_text
