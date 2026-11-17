/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/* the C library tests/coldfire has: rt.h, and snprintf where a test
   gives one (test_tlsfb.c, for ovl.c's messages) */
#include "../rt.h"
int snprintf(char *buf, size_t n, const char *fmt, ...);
