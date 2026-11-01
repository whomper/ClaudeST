/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/* the host test's stand-in for an app's ui.h: what gemui.h asks of it */
typedef struct {
	short x, y, w, h;
} GRECT;
#define TX_BOLD 1
#define TX_INVERSE 2
#define TX_LTR 4
#define TX_DIM 8
