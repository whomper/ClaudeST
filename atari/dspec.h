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

#endif
