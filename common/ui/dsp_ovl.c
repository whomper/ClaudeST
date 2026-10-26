/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * dsp_ovl.c - the Falcon's DSP for a Direct connection when the TLS
 * code is TLS.OVL's (src/tlsovl.h): dsp.c's and dsp_ec.c's calls, the
 * work done in the overlay, where those files now are. It takes their
 * place in a program that links atari/tls_ovl.c.
 */
#include "ui.h"
#include "../src/tlsovl.h"

/* RSA and the curves both: falcon_dsp_ec_init() follows every
   falcon_dsp_init() (the family's rule), and a program that left it out
   had its curves on the 68030 on a Falcon (EFTP's and EMail's, 25 Oct
   2026); falcon_dsp_ec_init() again does no harm */
void falcon_dsp_init(void)
{
	tls_ovl_dsp(0, opt.falcon && opt.dsp);
	tls_ovl_dsp(1, opt.falcon && opt.dsp);
}

const char *falcon_dsp_state(void)
{
	return tls_ovl_dsp_state(0);
}

void falcon_dsp_ec_init(void)
{
	tls_ovl_dsp(1, opt.falcon && opt.dsp);
}

const char *falcon_dsp_ec_state(void)
{
	return tls_ovl_dsp_state(1);
}
