/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/* tos.h for tests/bgwork: the clock and the cookie are the test's own */
#ifndef TOS_H
#define TOS_H
typedef unsigned long u32;
u32 tos_ms(void);
long get_cookie(long id);
#endif
