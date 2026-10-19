#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
# Hatari's DSP56001 core (GPL-2.0-or-later), for make sim: the core alone,
# with the few headers of the emulator around it stubbed out.
set -e
cd "$(dirname "$0")"
REV=6c80daaf18ebd54f5940a86fac1b4124df29798d
rm -rf build/hatari build/hatari-src
mkdir -p build/hatari/stub
git init -q build/hatari-src
git -C build/hatari-src fetch -q --depth 1 https://github.com/hatari/hatari "$REV"
git -C build/hatari-src checkout -q FETCH_HEAD
for f in dsp_core.c dsp_cpu.c dsp_core.h dsp_cpu.h dsp_disasm.h; do
	cp build/hatari-src/src/falcon/$f build/hatari/
done
cat > build/hatari/stub/log.h <<'H'
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#define LOG_TRACE(l, ...) do {} while (0)
#define LOG_TRACE_LEVEL(l) 0
#define LOG_TRACE_PRINT(...) do {} while (0)
#define Log_Printf(l, ...) fprintf(stderr, __VA_ARGS__)
#define LOG_WARN 0
#define LOG_ERROR 0
#define LOG_DEBUG 0
#define TraceFile stderr
#define ExceptionDebugMask 0
#define EXCEPT_DSP 1
#define REASON_DSP_EXCEPTION 0
#define DebugUI(x) do {} while (0)
#define SDL_GetTicks() 0
#define Uint32 uint32_t
H
for f in config.h ioMem.h dsp.h main.h debugui.h SDL_timer.h; do
	echo '#include "log.h"' > build/hatari/stub/$f
done
rm -rf build/hatari-src
echo "Hatari's DSP core is in build/hatari"
