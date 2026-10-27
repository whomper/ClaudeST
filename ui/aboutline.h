/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * aboutline.h - the About box's status lines in the family's words (the
 * guidelines, 1.3; Erez, 27 Oct 2026). At most three, in this order, a
 * line left out where it has nothing to say:
 *
 *   STinG  •  Direct  •  online              1. the connection (about_net_line)
 *   654 contacts in 2 address books          2. the app's own: what it holds
 *   TT RAM  •  Free: 98.2 MB TT, 3.1 MB ST   3. the memory (about_mem_line)
 *
 * Lines 1 and 3 are built here, so every app says them the same way; the
 * app writes line 2. What only matters when something goes wrong (TLS.OVL's
 * version, the DSP) is in the app's log, not here.
 */
#ifndef AERA_ABOUTLINE_H
#define AERA_ABOUTLINE_H

/* how the app reaches its servers, line 1's middle */
#define ABOUT_DIRECT     "Direct"
#define ABOUT_GATEWAY    "Pi gateway"
#define ABOUT_NO_GATEWAY "No gateway"
/* its state, line 1's end */
#define ABOUT_ONLINE     "online"		/* a connection open now */
#define ABOUT_READY      "ready"
#define ABOUT_OFFLINE    "offline"		/* Work offline is ticked */

/* line 1: stack is net_stack()'s ("none": No network), route one of the
   above (ClaudeST: "Serial port, 19200 baud"), state one of the above or
   0 for none */
void about_net_line(char *out, int n, const char *stack, const char *route, const char *state);

/* line 3, from where the program and its memory are and what is free
   (the host tests): prog_tt, heap_tt and heap_st as in libc (bytes the
   heap took), free_tt -1 where there is no TT RAM */
void about_mem_format(char *out, int n, int prog_tt, long heap_tt, long heap_st, long free_tt,
		      long free_st);

#ifndef ABOUTLINE_HOST
/* line 3, as it is now */
void about_mem_line(char *out, int n);
#endif

#endif
