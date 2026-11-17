/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * trail.h - where a stretch between event calls goes (TLS.OVL's and the
 * connection's): built with -DTLS_TRAIL, TRAIL("what") writes a line "TRAIL
 * -- what" to the program's log (conn_log, 10 ms stamps), so that the
 * log lines between two event calls (the program's own TRAIL "E" lines) say
 * what a long stretch was doing. Without TLS_TRAIL it is nothing at all.
 * Never in supervisor mode: conn_log is the program's.
 */
#ifndef TRAIL_H
#define TRAIL_H

#ifdef TLS_TRAIL
void conn_log(const char *who, const char *dir, const char *text, long n);
#define TRAIL(w) conn_log("TRAIL", " -- ", w, (long)sizeof(w) - 1)
#else
#define TRAIL(w) ((void)0)
#endif

#endif
