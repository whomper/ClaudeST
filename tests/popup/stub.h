/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/* what the host test's stand-in AES and drawing (stub.c) record */
#ifndef POPUP_STUB_H
#define POPUP_STUB_H
#include "gem.h"
#include "ui.h"

#define MAXT 16
typedef struct {
	short x, y, n, flags;
	char s[80];
} TEXT_AT;
extern TEXT_AT texts[MAXT];
extern int ntexts, nfills, nframes, nclip_on, nclip_off, nforms;
extern GRECT fills[MAXT], frames[MAXT], clip;
extern unsigned long now_ms;
extern short mouse_x, mouse_y;	/* what graf_mkstate says */
extern int mk_calls;
extern short key_next;			/* evnt_multi_'s answer: this key (0x011b Esc) */
extern short bubble_id;			/* appl_find("BUBBLE  "): its id, -1 none */
extern short sent[8];
extern int nsent;
extern char shared_buf[512];
void stub_reset(void);
#endif
