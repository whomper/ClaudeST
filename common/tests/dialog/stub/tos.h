/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/* the one TOS call dialog.c makes (the bell), for the host test */
void fake_bconout(short dev, short c);
#define Bconout(d, c) fake_bconout((d), (c))
void nf_debug(const char *s);
long get_cookie(long id);
