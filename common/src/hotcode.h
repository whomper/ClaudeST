/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * hotcode.h - a hot function's place fixed in the program (the
 * guidelines, Speed: "The hot loop is pinned by link order"). Where the
 * hot path is a few functions inside a big file, listing the file first
 * in the link isn't enough: any edit to it moves them. Mark each function
 * the hot path runs with HOT_CODE("NNname") on the line above its
 * definition; the program's LDFLAGS add -Wl,--sort-section=name, and the
 * linker puts every .text.0hot.* section first, after the startup
 * assembly, in NN order (common's atari/link.ld keeps input order
 * otherwise; with the sort, the rest is in name order, a one-time move a
 * paired run measures). From EPlay 1.34's tickcode.h (13 Nov 2026: its
 * ticks no longer move with other code; 572 bytes smaller). In src/ so
 * every target's include path reaches it; empty on host builds.
 */
#ifndef HOTCODE_H
#define HOTCODE_H

#if defined(__m68k__)
#define HOT_CODE(n) __attribute__((section(".text.0hot." n)))
#else
#define HOT_CODE(n)
#endif

#endif
