/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * fuzz_net.h - the network for an app's fuzz targets (the AERA guidelines,
 * 7.1): every server the program talks to says the fuzz input, in pieces
 * of changing size; what the program sends goes nowhere. fuzz_net.c is
 * built in place of the net_* of the app's tests/host/plat_posix.c (which
 * leaves them out with -DFUZZ_NET), so the program's own protocol code
 * (HTTP, IMAP, POP3, SMTP, FTP) reads it, through common/src/conn.c.
 *
 * Programs that keep two connections open at once (FTP: the commands, and
 * a listing or a file) get one part of the input each: FUZZ_NET_NEXT
 * splits it, and the connections hear the parts in the order they are
 * opened, each closed at the end of its own. Without FUZZ_NET_NEXT in
 * the input, every connection takes up the input where the last one left
 * it (a program that reconnects hears the rest).
 *
 *   int LLVMFuzzerTestOneInput(const uint8_t *d, size_t n)
 *   {
 *       fuzz_net_set(d, n);
 *       ... imap_login(...), http_request(...) ...
 *   }
 */
#ifndef FUZZ_NET_H
#define FUZZ_NET_H

#define FUZZ_NET_NEXT "\n==next==\n"

void fuzz_net_set(const unsigned char *data, long size);
long fuzz_net_left(void);		/* bytes the server has still to say */

#endif
