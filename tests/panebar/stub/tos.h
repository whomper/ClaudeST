/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/* the TOS calls panebar.c makes (the clock, a cookie), for the host test */
unsigned long tos_ms(void);
long get_cookie(long id);	/* 0 when missing */
