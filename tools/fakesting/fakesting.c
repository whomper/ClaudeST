/*
 * FAKESTNG.PRG - a stand-in for the STinG TCP/IP stack, for testing
 * Claude ST in an emulator without networking. It installs a "STiK"
 * cookie and a TRANSPORT_TCPIP table whose single "connection" is
 * tunnelled over the serial port, so claude_bridge.py --pipe can serve
 * it. TCP_open announces "DBG OPEN <ip> <port>" so the bridge log shows
 * the address Claude ST asked for.
 */
#include "../../st/tos.h"

long get_dftab(void), TCP_open(void), TCP_close(void), TCP_send(void),
     TCP_wait_state(void), CNbyte_count(void), CNget_block(void),
     get_err_text(void), unsupported(void);

typedef struct {
	char magic[10];
	void *get_dftab, *etm_exec, *cfg, *basepage;
} DRV_LIST;

static DRV_LIST drivers = { "STiKmagic", get_dftab, unsupported, 0, 0 };

static struct {
	const char *module, *author, *version;
	void *fn[32];
} tpl = { "TRANSPORT_TCPIP", "fake", "01.00", {
	unsupported, unsupported, unsupported, unsupported, get_err_text, unsupported,
	unsupported, TCP_open, TCP_close, TCP_send, TCP_wait_state,
	unsupported, unsupported, unsupported, unsupported, unsupported,
	CNbyte_count, unsupported, unsupported, CNget_block, unsupported,
} };

static u32 get_long(const short *a) { return ((u32)(u16)a[0] << 16) | (u16)a[1]; }

static void out(const char *s) { while (*s) Bconout(1, (u8)*s++); }

static void out_num(u32 v)
{
	char b[12], *p = b + 11;
	*p = 0;
	do { *--p = '0' + v % 10; v /= 10; } while (v);
	out(p);
}

long c_unsupported(const short *a) { (void)a; return -32; }
long c_get_err_text(const short *a) { (void)a; return (long)"fake STinG error"; }
long c_TCP_close(const short *a) { (void)a; return 0; }
long c_TCP_wait_state(const short *a) { return a[1] == 4 ? 0 : -15; }

long c_get_dftab(const short *a)
{
	const char *name = (const char *)get_long(a);
	return strcmp(name, "TRANSPORT_TCPIP") ? 0 : (long)&tpl;
}

long c_TCP_open(const short *a)
{
	u32 ip = get_long(a);
	out("DBG\tOPEN\t");
	out_num(ip >> 24); out("."); out_num((ip >> 16) & 255); out(".");
	out_num((ip >> 8) & 255); out("."); out_num(ip & 255);
	out("\t");
	out_num((u16)a[2]);
	out("\n");
	return 3;		/* connection handle */
}

long c_TCP_send(const short *a)
{
	const char *buf = (const char *)get_long(a + 1);
	short i, len = a[3];
	if (a[0] != 3)
		return -9;
	for (i = 0; i < len; i++)
		Bconout(1, (u8)buf[i]);
	return 0;
}

long c_CNbyte_count(const short *a)
{
	if (a[0] != 3)
		return -9;
	return Bconstat(1) ? 1 : -2;
}

long c_CNget_block(const short *a)
{
	char *buf = (char *)get_long(a + 1);
	short n = 0, len = a[3];
	while (n < len && Bconstat(1))
		buf[n++] = (char)Bconin(1);
	return n ? n : -2;
}

/* --- install --- */

static char rxbuf[16384];
static IOREC *io;

static void sup_install(void)
{
	long *jar;
	short sr;
	__asm__ volatile("move.l 0x5a0.w,%0" : "=a"(jar));
	if (jar) {
		long i = 0;
		while (jar[i * 2])
			i++;
		/* the terminator's value is the jar size; append before it */
		jar[i * 2 + 2] = 0;
		jar[i * 2 + 3] = jar[i * 2 + 1];
		jar[i * 2] = 0x5354694bL;	/* 'STiK' */
		jar[i * 2 + 1] = (long)&drivers;
	}
	__asm__ volatile("move.w %%sr,%0\n\tor.w #0x0700,%%sr" : "=d"(sr) :: "memory");
	io->ibuf = rxbuf;
	io->ibufsiz = sizeof(rxbuf);
	io->ibufhd = io->ibuftl = 0;
	io->ibuflow = 4096;
	io->ibufhi = 12288;
	__asm__ volatile("move.w %0,%%sr" :: "d"(sr) : "memory");
}

long fake_install(long *basepage)
{
	io = Iorec(0);
	Supexec(sup_install);
	Cconws("Fake STinG installed (TCP over serial)\r\n");
	return 0x100 + basepage[3] + basepage[5] + basepage[7];
}
