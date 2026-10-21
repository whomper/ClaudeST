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

#endif
