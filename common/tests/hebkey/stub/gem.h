/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/* the bits of atari/gem.h that hebkey.c uses, for the build machine */
#ifndef STUB_GEM_H
#define STUB_GEM_H
#define K_RSHIFT 0x01
#define K_LSHIFT 0x02
#define K_CTRL   0x04
static inline int key_ctrl(short kstate, short key)
{
	short scan = (key >> 8) & 0xff;
	short ascii = key & 0xff;
	return (kstate & K_CTRL) || (ascii >= 1 && ascii <= 26 && scan != 0x0e && scan != 0x0f &&
				     scan != 0x1c && scan != 0x72);
}
typedef struct {
	void *fd_addr;
	short fd_w, fd_h, fd_wdwidth, fd_stand, fd_nplanes, fd_r1, fd_r2, fd_r3;
} MFDB;
void vqt_attributes(short h, short *a);
void vrt_cpyfm(short h, short mode, const short *pxy, MFDB *src, MFDB *dst, const short *colors);
#endif
