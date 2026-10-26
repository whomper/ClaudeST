/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * crash.c - the crash catcher (see crash.h, and crash.S for the
 * exception entries), from EMail. The report gives the bombs, where they
 * went off as an offset into the program ("EMail+$1A2B4", which
 * common/tools/crashaddr.py turns into a function name with the build of
 * that version), the registers, likely callers from the stack and the
 * last status line.
 */
#include <stdarg.h>
#include <string.h>
#include <stdio.h>
#include "tos.h"
#include "crash.h"

/* the landing's stack and the report: taken when the catcher is
   installed (not in a desk accessory, which never installs it). The
   landing, the report and the app's crash_cleanup (alerts, closing its
   windows: the AES and VDI work on the system stack) used 424 bytes of
   it in EMail's check; test builds (CRASH_TEST) say how much on the
   emulator's console */
#ifndef CRASH_STACK
#define CRASH_STACK 4096
#endif
#define REP_SIZE 3000

/* shared with crash.S */
short crash_busy, crash_cpu, crash_fvec, crash_newsr;
long crash_regs[15], crash_usp, crash_old[12], crash_old_term;
long crash_stack_top;
void crash_e2(void), crash_e3(void), crash_e4(void), crash_e5(void),
     crash_e6(void), crash_e7(void), crash_e8(void), crash_e11(void), crash_term(void);
long crash_take(unsigned short *f, long vec);
void crash_landing(void);
void crash_restore(void);

void (*crash_cleanup)(const char *what);
void (*crash_report)(const char *text, long n);
extern short _is_acc;			/* crt0.S */

/* the vectors caught: 2 to 8, and 11 (line F) where TOS's own handler
   is on it (sup_install): with an FPU's, an emulator's or a 68040/68060
   package's, the program's FPU instructions are theirs */
static void (*const entry[12])(void) = {
	0, 0, crash_e2, crash_e3, crash_e4, crash_e5, crash_e6, crash_e7, crash_e8, 0, 0, crash_e11
};
static const char *const names[12] = {
	0, 0, "bus error, 2 bombs", "address error, 3 bombs", "illegal instruction, 4 bombs",
	"division by zero, 5 bombs", "CHK, 6 bombs", "TRAPV, 7 bombs", "privilege violation, 8 bombs", 0, 0,
	"line F (an FPU instruction or none), 11 bombs"
};
#define CAUGHT(i) ((i) >= 2 && (i) <= 11 && entry[i] != 0)

static int installed;
static const char *app = "the program", *app_version = "";
static char path[220];
static char note[80];
/* the program's code (crash.S looks at it too) and its end */
u32 crash_text_lo, crash_text_hi;
static u32 prog_hi;
#define text_lo crash_text_lo
#define text_hi crash_text_hi

/* what the exception left */
static long c_vec, c_pc, c_addr;
static short c_sr, c_has_addr;

/* TOS's own record of the last bombs, from $380 */
static long prev[27];
static const char *prev_name;

void crash_note(const char *what)
{
	strncpy(note, what, sizeof(note) - 1);
}

/* ---------------- in the exception (supervisor mode) ---------------- */

/* a frame's size (0: unknown) and the address it faulted on, if any */
static long frame(const unsigned short *f, long vec, long *addr, short *has)
{
	long size;
	*has = 1;
	if (!crash_cpu && vec <= 3) {	/* the 68000's long bus/address error frame */
		*addr = (long)f[1] << 16 | f[2];
		return 14;
	}
	if (!crash_cpu) {
		*has = 0;
		return 6;
	}
	switch (f[3] >> 12) {
	case 0: case 1: size = 8; *has = 0; break;
	case 2: case 3: size = 12; *addr = (long)f[4] << 16 | f[5]; break;
	case 4: size = 16; *addr = (long)f[4] << 16 | f[5]; break;
	case 7: size = 60; *addr = (long)f[10] << 16 | f[11]; break;
	case 8: size = 58; *addr = (long)f[5] << 16 | f[6]; break;
	case 9: size = 20; *has = 0; break;
	case 10: size = 32; *addr = (long)f[8] << 16 | f[9]; break;
	case 11: size = 92; *addr = (long)f[8] << 16 | f[9]; break;
	default: return 0;
	}
	if (vec > 3)
		*has = 0;
	return size;
}

static volatile long *low(long a);

/* TOS's BIOS and XBIOS calls keep the caller's registers in a block below
   savptr ($4A2), one a call, and give it back on return. A Supexec that a
   fault taken in it abandons never returns: its block is given back here,
   or each crash caught in one boot took another (measured: 46 bytes a
   crash on TOS 1.04, TOS 4.04 and EmuTOS on an ST, 48 on EmuTOS on a
   Falcon). savptr_user: savptr in user mode, worked out by sup_install;
   0: not known, left alone. The supervisor stack needs nothing: at Pterm
   GEMDOS goes back to the parent with the supervisor stack the parent had
   (Super(0) read it steady over the ten on all four) */
static long savptr_user, savptr_nested;

static void sup_nested(void)
{
	savptr_nested = *low(0x4A2L);
}

/* the program is the process running now (GEMDOS's p_run, TOS 1.02 and
   later); supervisor mode */
static int ours_running(void)
{
	const unsigned char *h = (const unsigned char *)*low(0x4F2L);	/* _sysbase */
	long *run;
	if (h == 0 || *(const unsigned short *)(h + 2) < 0x0102)
		return 0;
	run = *(long *const *)(h + 0x28);
	return run != 0 && *run == (long)_BasePage;
}

/* the frame's size if the fault is the program's to handle, else 0 (on to
   TOS); notes what happened. Runs in the exception. */
long crash_take(unsigned short *f, long vec)
{
	const unsigned short *s = !crash_cpu && vec <= 3 ? f + 4 : f;
	long size;
	u32 pc = (u32)s[1] << 16 | s[2], old = (u32)crash_old[vec];
	/* a 68000 (and 68010) uses 24 bits of an address: TOS 1.x's own
	   handlers carry the vector's number in the top byte ($08FC0B0A) */
	u32 at = crash_cpu < 20 ? old & 0xFFFFFFUL : old;
	int rom = at >= 0xE00000UL && at < 0x1000000UL;
	if (s[0] & 0x2000) {
		/* supervisor mode: the program's own code there (a Supexec of its:
		   a DSP's host port, a hardware register) is ours; the system's,
		   a driver's or an interrupt's goes on to TOS, which leaves its
		   record of the bombs for the next start */
		if (pc < text_lo || pc >= text_hi)
			return 0;
	} else if (pc < text_lo || pc >= prog_hi) {
		/* in someone else's code, with a handler of theirs before ours:
		   theirs. STinG gets into supervisor mode by running "or.w #0,sr"
		   in user mode and catching the privilege violation (protect_exec,
		   in TCP_open and others); taking it for a crash bombed every
		   connection. Line F outside the program is never ours: TOS
		   1.0x's ROM calls its own routines with it, an FPU emulator
		   runs the code it is. With TOS's own handler (in ROM), or while
		   the program is the process running (a wild jump of its own
		   through a pointer gone bad: on a machine whose drivers hook the
		   vectors, its bombs went to TOS and nothing said where), ours */
		if (vec == 11 || (!rom && (vec == 8 || !ours_running())))
			return 0;
	}
	if (!(size = frame(f, vec, &c_addr, &c_has_addr)))
		return 0;
	if ((s[0] & 0x2000) && savptr_user)
		*low(0x4A2L) = savptr_user;	/* the abandoned call's block back */
	c_vec = vec;
	c_sr = s[0];
	c_pc = (long)s[1] << 16 | s[2];
	crash_fvec = (short)(vec * 4);
	crash_newsr = s[0] & 0x0700;		/* user mode, the same interrupt level */
	crash_busy = 1;
	return size;
}

/* a system variable or vector (GCC won't take a constant address) */
static volatile long *low(long a)
{
	volatile long *p;
	__asm__("" : "=a"(p) : "0"(a));
	return p;
}

/* the vectors as they were; supervisor mode */
void crash_restore(void)
{
	int i;
	if (!installed)
		return;
	for (i = 2; i <= 11; i++)
		if (CAUGHT(i) && *low(i * 4L) == (long)entry[i])
			*low(i * 4L) = crash_old[i];
	if (*low(0x408L) == (long)crash_term)
		*low(0x408L) = crash_old_term;
	installed = 0;
}

/* vector i holds TOS's own handler, the one that shows the bombs: the
   same as on vector j (TOS 2 to 4), the entry of a table of them 2 bytes
   apart (EmuTOS), or the vector's number in the top byte (TOS 1.0x) */
static int tos_default(int i, int j)
{
	u32 a = (u32)*low(i * 4L), b = (u32)*low(j * 4L);
	return a == b || a == b + 2UL * (u32)(i - j) ||
	       ((b >> 24) == (u32)j && a == ((b & 0xFFFFFFUL) | (u32)i << 24));
}

/* TOS 1.0x's own line F: at every start its AES copies its dispatcher
   into RAM (the copy rewrites itself) and puts it on vector 11. It runs
   line F from ROM only, so a line F in the program's code is a crash; and
   a stray one there must not reach the dispatcher, which jumps through a
   table or pops the supervisor stack. Known by its first words (TOS 1.04:
   move.w (sp)+,d2; movea.l (sp)+,a0; move.w (a0)+,d1; btst #0,d1); an
   emulator over it isn't */
static int tos1_aes_line_f(void)
{
	static const unsigned short first[4] = { 0x341F, 0x205F, 0x3218, 0x0801 };
	const unsigned char *h = (const unsigned char *)*low(0x4F2L);	/* _sysbase */
	const unsigned short *p = (const unsigned short *)(*low(0x2CL) & 0xFFFFFFL);
	int i;
	if (h == 0 || *(const unsigned short *)(h + 2) >= 0x0200 || ((long)p & 1))
		return 0;
	for (i = 0; i < 4; i++)
		if (p[i] != first[i])
			return 0;
	return 1;
}

static void sup_install(void)
{
	volatile long *rec = low(0x380L);
	long in = *low(0x4A2L), block;
	/* line F only while vector 11 has TOS's own handler on it: the one
	   that bombs, or TOS 1.0x's AES dispatcher. A software FPU emulator or
	   a 68040's or 68060's FPU package (CT60) keeps it, as the program's
	   FPU instructions are theirs to run. Looked at before vectors 3 and 7
	   are ours. Not on TOS 1.0x either (the guidelines' Speed): its ROM
	   calls its own routines through line F, from the AES and the VDI, so
	   the hook was on 4 to 7% of all an ST's time (EFTP's profiles, 26 Oct
	   2026); a line F in the program, which no AERA program has, then
	   bombs as before */
	int i, line_f = (tos_default(11, 7) || tos_default(11, 3)) && !tos1_aes_line_f();
	if (rec[0] == 0x12345678L) {		/* proc_lives: TOS saw bombs */
		for (i = 0; i < 27; i++)
			prev[i] = rec[i];
		rec[0] = 0;			/* told once */
	}
	/* savptr in user mode: this Supexec took one block, and the one
	   nested in it takes another the same size */
	Supexec(sup_nested);
	block = in - savptr_nested;
	savptr_user = block > 0 && block <= 128 ? in + block : 0;
	for (i = 2; i <= 11; i++) {
		if (i == 11 && !line_f)
			continue;
		if (CAUGHT(i)) {
			crash_old[i] = *low(i * 4L);
			*low(i * 4L) = (long)entry[i];
		}
	}
	crash_old_term = *low(0x408L);	/* etv_term */
	*low(0x408L) = (long)crash_term;
	installed = 1;
}

/* ---------------- the report (user mode) ---------------- */

static char *rep;
static int rl;

static void add(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void add(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	if (rl < REP_SIZE - 1)
		rl += vsnprintf(rep + rl, REP_SIZE - rl, fmt, ap);
	va_end(ap);
	if (rl > REP_SIZE - 1)
		rl = REP_SIZE - 1;
}

/* "$00E1A2B4 (EMail+$01A2B4)" */
static void where(u32 a)
{
	add("$%08lX", (unsigned long)a);
	if (a >= text_lo && a < text_hi)
		add(" (%s+$%06lX)", app, (unsigned long)(a - text_lo));
}

static void header(const char *what)
{
	unsigned short d = (unsigned short)Tgetdate(), t = (unsigned short)Tgettime();
	rl = 0;
	add("---- %02u.%02u.%04u %02u:%02u:%02u  %s %s: %s\r\n", d & 31, (d >> 5) & 15,
	    1980 + (d >> 9), t >> 11, (t >> 5) & 63, (t & 31) * 2, app, app_version, what);
	add("CPU 680%02d, machine %ld, %s at $%08lX-$%08lX\r\n", crash_cpu,
	    get_cookie(0x5F4D4348L) >> 16, app, (unsigned long)text_lo, (unsigned long)text_hi);
}

static void regs(const long *d, const long *a, long usp)
{
	int i;
	add("D0-D7");
	for (i = 0; i < 8; i++)
		add(" %08lX", (unsigned long)d[i]);
	add("\r\nA0-A6");
	for (i = 0; i < 7; i++)
		add(" %08lX", (unsigned long)a[i]);
	add("\r\nUSP %08lX\r\n", (unsigned long)usp);
}

static void save(void)
{
	long h = Fopen(path, 1);
	if (h >= 0)
		Fseek(0, (short)h, 2);
	else
		h = Fcreate(path, 0);
	if (h >= 0) {
		Fwrite((short)h, rl, rep);
		Fclose((short)h);
	}
	if (crash_report)
		crash_report(rep, rl);
}

void crash_landing(void)
{
	const char *name = names[c_vec];
	u32 sp = (u32)crash_usp;
	int i, n;
	header(name);
	add("at ");
	where((u32)c_pc);
	add(", SR %04X", (unsigned short)c_sr);
	if (c_has_addr) {
		add(", accessing ");
		where((u32)c_addr);
	}
	add("\r\n");
	if (c_pc >= (long)text_lo + 4 && c_pc < (long)text_hi - 6) {
		const unsigned short *p = (const unsigned short *)((c_pc - 4) & ~1L);
		add("code around it:");
		for (i = 0; i < 6; i++)
			add(" %04X", p[i]);
		add("\r\n");
	}
	regs(crash_regs, crash_regs + 8, crash_usp);
	/* return addresses into the program on the stack: likely callers */
	if (sp >= text_lo && sp < prog_hi && !(sp & 1)) {
		const u32 *s = (const u32 *)sp;
		add("callers?");
		for (i = n = 0; i < 256 && n < 12 && (u32)(s + i) + 4 <= prog_hi; i++)
			if (s[i] > text_lo && s[i] < text_hi && !(s[i] & 1)) {
				add(" +$%06lX", (unsigned long)(s[i] - text_lo));
				n++;
			}
		add("\r\n");
	}
	if (note[0])
		add("last status: %s\r\n", note);
	save();
	Supexec(crash_restore);
	if (crash_cleanup)
		crash_cleanup(name);
#ifdef CRASH_TEST
	{
		const unsigned char *p = (const unsigned char *)(crash_stack_top - CRASH_STACK + 4);
		char s[60];
		while (*p == 0xA5)
			p++;
		snprintf(s, sizeof(s), "crash: %ld bytes of the stack used\n", crash_stack_top - (long)p);
		nf_debug(s);
	}
#endif
	Pterm(1);
}

/* ---------------- setting up ---------------- */

void crash_install(const char *workdir, const char *name, const char *version)
{
	long *bp = (long *)_BasePage;
	int n;
	char *mem;
	if (installed || _is_acc || get_cookie(0x4D694E54L) || get_cookie(0x4D616758L))	/* MiNT, MagX */
		return;
	if (!rep) {
		mem = (char *)Malloc(CRASH_STACK + REP_SIZE);
		if ((long)mem <= 0)
			return;
		crash_stack_top = ((long)mem + CRASH_STACK) & ~3L;
		rep = mem + CRASH_STACK;
#ifdef CRASH_TEST
		memset(mem, 0xA5, CRASH_STACK);	/* to see how much is used */
#endif
	}
	app = name;
	app_version = version;
	n = snprintf(path, sizeof(path), "%s", workdir);
	if (n > 0 && path[n - 1] != '\\' && n < (int)sizeof(path) - 1)
		path[n++] = '\\', path[n] = 0;
	strncat(path, "CRASH.LOG", sizeof(path) - strlen(path) - 1);
	crash_cpu = (short)get_cookie(0x5F435055L);	/* _CPU: 0 on a plain ST */
	text_lo = (u32)bp[2];
	text_hi = text_lo + (u32)bp[3];
	prog_hi = (u32)bp[6] + (u32)bp[7];		/* the end of the BSS */
	Supexec(sup_install);
	if (prev[0] == 0x12345678L) {
		/* proc_dregs $384, proc_aregs $3A4 (A7 = SSP), proc_enum $3C4,
		   proc_usp $3C8, proc_stk $3CC: the exception's frame */
		long vec = (prev[17] >> 24) & 0xff, addr;
		const unsigned short *f = (const unsigned short *)&prev[19];
		const unsigned short *st;
		short has;
		u32 pc;
		const char *name;
		if (!CAUGHT(vec))
			vec = prev[17];		/* the number as a long */
		name = CAUGHT(vec) ? names[vec] : "an exception";
		st = !crash_cpu && vec <= 3 ? f + 4 : f;
		pc = (u32)st[1] << 16 | st[2];
		header("found TOS's record of a crash before this run");
		add("%s at ", name);
		where(pc);
		add(", SR %04X", st[0]);
		if (frame(f, vec, &addr, &has) && has) {
			add(", accessing ");
			where((u32)addr);
		}
		add("\r\n(in %s if the offset is given and %s was loaded at the same address)\r\n", app, app);
		regs(&prev[1], &prev[9], prev[18]);
		add("SSP %08lX, stack", (unsigned long)prev[16]);
		for (n = 0; n < 16; n++)
			add(" %04X", f[n]);
		add("\r\n");
		save();
		if (pc >= text_lo && pc < text_hi)
			prev_name = name;
	}
}

void crash_remove(void)
{
	Supexec(crash_restore);
}

const char *crash_log(void)
{
	return path;
}

const char *crash_previous(void)
{
	return prev_name;
}

#ifdef CRASH_TEST
static void crash_test_sup(void)
{
	volatile long *p = (volatile long *)0xFFFFFA40L;	/* no TT MFP here */
	(void)*p;
}

int crash_test(short scan)
{
	static volatile long *volatile bad;
	static void (*volatile ill)(void);
	static const unsigned short illegal[] = { 0x4afc };
	if (scan == 0x3b) {		/* F1: bus error in the program */
		bad = (long *)0x10L;
		*bad = 0;
	} else if (scan == 0x3c) {	/* F2: bus error in its code, supervisor mode */
		Supexec(crash_test_sup);
	} else if (scan == 0x3f) {	/* F5: the same outside the program: the bombs */
		/* tst.l $FFFFFA40; rts, in memory of its own */
		static const unsigned short sup_out[] = { 0x4AB9, 0xFFFF, 0xFA40, 0x4E75 };
		unsigned short *m = Malloc(sizeof sup_out);
		if ((long)m <= 0)
			return 1;
		memcpy(m, sup_out, sizeof sup_out);
		Supexec(m);
	} else if (scan == 0x3d) {	/* F3: illegal instruction */
		ill = (void (*)(void))illegal;
		ill();
	} else if (scan == 0x3e) {	/* F4: address error (68000) */
		bad = (long *)0x20001L;
		*bad = 0;
	} else {
		return 0;
	}
	return 1;
}
#else
int crash_test(short scan)
{
	(void)scan;
	return 0;
}
#endif
