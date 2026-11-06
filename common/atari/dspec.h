/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * dspec.h - the Falcon's DSP56001 as src/ecdsp.c's port.
 */
#ifndef DSPEC_H
#define DSPEC_H

#include "../src/ecdsp.h"

int dspec_present(void);		/* this machine has a DSP56001 */
extern const ECDSP_PORT dspec_port;
/* the last job's turns for GEM and the ms spent in them (counted at the
   turn, not per word), for the log: on Erez's stock Falcon a job's real
   time is the DSP's, the 68030's and these */
extern unsigned short dspec_turns;
extern unsigned long dspec_turn_ms;

#endif
