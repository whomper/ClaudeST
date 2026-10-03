/*
 * tos.h - the handful of GEMDOS/BIOS/XBIOS calls Claude ST needs, as
 * inline traps. Built freestanding (no MiNTLib), so the program runs
 * on plain TOS 1.0 through TOS 4.x (Falcon), EmuTOS and MiNT.
 *
 * TOS may trash d0-d2/a0-a2 across a trap; all inputs are forced into
 * registers so an sp-relative operand can't move under our pushes.
 */
#ifndef TOS_H
#define TOS_H

typedef unsigned char  u8;
typedef unsigned short u16;
typedef unsigned long  u32;

#define TRAP_CLOBBER "d1", "d2", "a0", "a1", "a2", "memory", "cc"

static inline long trap1_w(short fn)
{
	register long r __asm__("d0");
	__asm__ volatile("move.w %1,-(%%sp)\n\ttrap #1\n\taddq.l #2,%%sp"
		: "=r"(r) : "r"(fn) : TRAP_CLOBBER);
	return r;
}

static inline long trap1_wl(short fn, long a)
{
	register long r __asm__("d0");
	__asm__ volatile("move.l %2,-(%%sp)\n\tmove.w %1,-(%%sp)\n\ttrap #1\n\taddq.l #6,%%sp"
		: "=r"(r) : "r"(fn), "r"(a) : TRAP_CLOBBER);
	return r;
}

static inline long trap1_ww(short fn, short a)
{
	register long r __asm__("d0");
	__asm__ volatile("move.w %2,-(%%sp)\n\tmove.w %1,-(%%sp)\n\ttrap #1\n\taddq.l #4,%%sp"
		: "=r"(r) : "r"(fn), "r"(a) : TRAP_CLOBBER);
	return r;
}

static inline long trap1_wlw(short fn, long a, short b)
{
	register long r __asm__("d0");
	__asm__ volatile("move.w %3,-(%%sp)\n\tmove.l %2,-(%%sp)\n\tmove.w %1,-(%%sp)\n\ttrap #1\n\taddq.l #8,%%sp"
		: "=r"(r) : "r"(fn), "r"(a), "r"(b) : TRAP_CLOBBER);
	return r;
}

static inline long trap1_wwll(short fn, short a, long b, long c)
{
	register long r __asm__("d0");
	__asm__ volatile("move.l %4,-(%%sp)\n\tmove.l %3,-(%%sp)\n\tmove.w %2,-(%%sp)\n\tmove.w %1,-(%%sp)\n\ttrap #1\n\tlea 12(%%sp),%%sp"
		: "=r"(r) : "r"(fn), "r"(a), "r"(b), "r"(c) : TRAP_CLOBBER);
	return r;
}

static inline long trap13_ww(short fn, short a)
{
	register long r __asm__("d0");
	__asm__ volatile("move.w %2,-(%%sp)\n\tmove.w %1,-(%%sp)\n\ttrap #13\n\taddq.l #4,%%sp"
		: "=r"(r) : "r"(fn), "r"(a) : TRAP_CLOBBER);
	return r;
}

static inline long trap13_www(short fn, short a, short b)
{
	register long r __asm__("d0");
	__asm__ volatile("move.w %3,-(%%sp)\n\tmove.w %2,-(%%sp)\n\tmove.w %1,-(%%sp)\n\ttrap #13\n\taddq.l #6,%%sp"
		: "=r"(r) : "r"(fn), "r"(a), "r"(b) : TRAP_CLOBBER);
	return r;
}

static inline long trap14_ww(short fn, short a)
{
	register long r __asm__("d0");
	__asm__ volatile("move.w %2,-(%%sp)\n\tmove.w %1,-(%%sp)\n\ttrap #14\n\taddq.l #4,%%sp"
		: "=r"(r) : "r"(fn), "r"(a) : TRAP_CLOBBER);
	return r;
}

static inline long trap14_wl(short fn, long a)
{
	register long r __asm__("d0");
	__asm__ volatile("move.l %2,-(%%sp)\n\tmove.w %1,-(%%sp)\n\ttrap #14\n\taddq.l #6,%%sp"
		: "=r"(r) : "r"(fn), "r"(a) : TRAP_CLOBBER);
	return r;
}

/* Rsconf(speed, flow, ucr, rsr, tsr, scr) */
static inline long trap14_w6(short fn, short a, short b, short c, short d, short e, short f)
{
	register long r __asm__("d0");
	__asm__ volatile(
		"move.w %7,-(%%sp)\n\tmove.w %6,-(%%sp)\n\tmove.w %5,-(%%sp)\n\t"
		"move.w %4,-(%%sp)\n\tmove.w %3,-(%%sp)\n\tmove.w %2,-(%%sp)\n\t"
		"move.w %1,-(%%sp)\n\ttrap #14\n\tlea 14(%%sp),%%sp"
		: "=r"(r) : "r"(fn), "r"(a), "r"(b), "r"(c), "r"(d), "r"(e), "r"(f) : TRAP_CLOBBER);
	return r;
}

/* GEMDOS */
#define Cconws(s)          trap1_wl(0x09, (long)(s))
#define Malloc(n)          ((void *)trap1_wl(0x48, (long)(n)))
#define Mfree(p)           trap1_wl(0x49, (long)(p))
#define Fopen(n, m)        trap1_wlw(0x3d, (long)(n), (m))
#define Fcreate(n, a)      trap1_wlw(0x3c, (long)(n), (a))
#define Fclose(h)          trap1_ww(0x3e, (h))
#define Fread(h, n, b)     trap1_wwll(0x3f, (h), (long)(n), (long)(b))
#define Fwrite(h, n, b)    trap1_wwll(0x40, (h), (long)(n), (long)(b))
#define Fdelete(n)         trap1_wl(0x41, (long)(n))
#define Dgetdrv()          trap1_w(0x19)
#define Dgetpath(b, d)     trap1_wlw(0x47, (long)(b), (d))

/* BIOS - device 1 is AUX: (ST RS-232 / Falcon modem port via Bconmap) */
#define DEV_AUX 1
#define Bconstat(d)        trap13_ww(1, (d))
#define Bconin(d)          trap13_ww(2, (d))
#define Bconout(d, c)      trap13_www(3, (d), (c))
#define Bcostat(d)         trap13_ww(8, (d))

/* XBIOS */
#define Iorec(d)           ((void *)trap14_ww(14, (d)))
#define Rsconf(s,f,u,r,t,c) trap14_w6(15, (s), (f), (u), (r), (t), (c))
#define Supexec(f)         trap14_wl(38, (long)(f))

/* Rsconf speed codes */
#define BAUD_19200 0
#define BAUD_9600  1
#define BAUD_4800  2
#define BAUD_2400  4

typedef struct {
	char *ibuf;
	short ibufsiz;
	volatile short ibufhd;
	volatile short ibuftl;
	short ibuflow;
	short ibufhi;
} IOREC;

/* tiny libc (tos.c) */
void *memcpy(void *d, const void *s, unsigned long n);
void *memmove(void *d, const void *s, unsigned long n);
void *memset(void *d, int c, unsigned long n);
int memcmp(const void *a, const void *b, unsigned long n);
unsigned long strlen(const char *s);
char *strcpy(char *d, const char *s);
void strlcpy_(char *d, const char *s, int n);
int strcmp(const char *a, const char *b);

#endif
