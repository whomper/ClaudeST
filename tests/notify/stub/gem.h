/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/* host stand-in for common's gem.h: the calls notify.c makes (test_notify.c) */
short appl_find(const char *name);
short appl_write(short id, short len, void *msg);
short appl_id(void);
