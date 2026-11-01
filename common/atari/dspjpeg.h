/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * dspjpeg.h - the Falcon's DSP56001 as pic.c's JPEG port (pic_dsp).
 */
#ifndef DSPJPEG_H
#define DSPJPEG_H

#include "../src/pic.h"

int dspjpeg_present(void);		/* this machine has a DSP56001 */
extern const PIC_DSP dspjpeg_port;
/* JPEGs only (the dither stays the CPU's): a program that doesn't dither
   on the DSP (a TJpgDec build's: pic_dither's DSP way is libjpeg's builds')
   links no dither.a56 (2.4 KB) with --gc-sections */
extern const PIC_DSP dspjpeg_port_jpeg;

#endif
