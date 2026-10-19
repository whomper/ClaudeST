/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * pic_lj8.c - libjpeg-turbo for 8-bit JPEGs only (pic.c, PIC_LIBJPEG):
 * the 12-bit, 16-bit and lossless decoders its jdmaster.c can call are
 * left out of the build (twice the code, for JPEGs no web page has), and
 * these stand in for them: such a JPEG fails as "can't be shown".
 */
#define JPEG_INTERNALS		/* jpeglib.h brings jpegint.h */
#include "jinclude.h"
#include "jpeglib.h"
#include "jerror.h"

static void no(j_decompress_ptr cinfo)
{
	ERREXIT1(cinfo, JERR_BAD_PRECISION, cinfo->data_precision);
}

#define NO1(f) void f(j_decompress_ptr cinfo) { no(cinfo); }
#define NO2(f) void f(j_decompress_ptr cinfo, boolean b) { (void) b; no(cinfo); }

NO1(j12init_1pass_quantizer)
NO1(j12init_2pass_quantizer)
NO1(j12init_color_deconverter)
NO2(j12init_d_coef_controller)
NO2(j12init_d_diff_controller)
NO2(j12init_d_main_controller)
NO2(j12init_d_post_controller)
NO1(j12init_inverse_dct)
NO1(j12init_lossless_decompressor)
NO1(j12init_merged_upsampler)
NO1(j12init_upsampler)
NO1(j16init_color_deconverter)
NO2(j16init_d_diff_controller)
NO2(j16init_d_main_controller)
NO2(j16init_d_post_controller)
NO1(j16init_lossless_decompressor)
NO1(j16init_upsampler)
NO2(jinit_d_diff_controller)
NO1(jinit_lhuff_decoder)
NO1(jinit_lossless_decompressor)
