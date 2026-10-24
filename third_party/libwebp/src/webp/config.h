/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * config.h - the AERA family's settings for libwebp's decoder (its
 * build's config.h, which autoconf or CMake would make; HAVE_CONFIG_H
 * reads it): plain C on every machine (no SIMD; a 68030 has none), no
 * threads, and only the output kinds and paths pic.c takes (no rescaler:
 * pic.c's scaler shrinks; RGBA and the like only).
 */
#ifndef WEBP_WEBP_CONFIG_H_
#define WEBP_WEBP_CONFIG_H_

#define WEBP_REDUCE_SIZE
#define WEBP_REDUCE_CSP

#endif
