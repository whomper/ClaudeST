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

void falcon_dsp_init(void)
{
	tls_ovl_dsp(0, opt.falcon && opt.dsp);
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
