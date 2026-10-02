/*
 * sting.c - TCP over the STinG (or STiK) network stack.
 *
 * STinG publishes a "STiK" cookie pointing at its driver list; from that
 * we fetch the TRANSPORT_TCPIP function table (TPL). TPL functions use
 * Pure C cdecl: 16-bit arguments on the stack, result in d0, so every
 * call goes through sting_call() in sting.S.
 */
#include "tos.h"
#include "sting.h"

long sting_call(void *fn, const short *args, long nwords);

typedef struct {
	char magic[10];			/* "STiKmagic" */
	void *get_dftab;
	void *etm_exec;
	void *cfg;
	void *basepage;
} DRV_LIST;

/* TPL: header strings, then function pointers in this order */
enum {
	T_KRMALLOC, T_KRFREE, T_KRGETFREE, T_KRREALLOC, T_GET_ERR_TEXT, T_GETVSTR,
	T_CARRIER_DETECT, T_TCP_OPEN, T_TCP_CLOSE, T_TCP_SEND, T_TCP_WAIT_STATE,
	T_TCP_ACK_WAIT, T_UDP_OPEN, T_UDP_CLOSE, T_UDP_SEND, T_CNKICK,
	T_CNBYTE_COUNT, T_CNGET_CHAR, T_CNGET_NDB, T_CNGET_BLOCK, T_HOUSEKEEP
};

typedef struct {
	const char *module, *author, *version;
	void *fn[32];
} TPL;

#define TESTABLISH 4

static DRV_LIST *drivers;
static TPL *tpl;

static void put_long(short *a, u32 v)
{
	a[0] = (short)(v >> 16);
	a[1] = (short)(v & 0xffff);
}

static long find_cookie;

static void sup_find_cookie(void)
{
	long *jar;
	__asm__ volatile("move.l 0x5a0.w,%0" : "=a"(jar));	/* _p_cookies */
	find_cookie = 0;
	if (!jar)
		return;
	for (; jar[0]; jar += 2) {
		if (jar[0] == 0x5354694bL) {	/* 'STiK' */
			find_cookie = jar[1];
			return;
		}
	}
}

short sting_init(void)
{
	short a[2];
	if (tpl)
		return 1;
	Supexec(sup_find_cookie);
	drivers = (DRV_LIST *)find_cookie;
	if (!drivers || memcmp(drivers->magic, "STiKmagic", 10) != 0)
		return 0;
	put_long(a, (u32)"TRANSPORT_TCPIP");
	tpl = (TPL *)sting_call(drivers->get_dftab, a, 2);
	return tpl != 0;
}

short sting_open(u32 ip, u16 port)
{
	short a[5];
	put_long(a, ip);
	a[2] = port;
	a[3] = 0;		/* type of service */
	a[4] = 4096;		/* output buffer */
	return (short)sting_call(tpl->fn[T_TCP_OPEN], a, 5);
}

short sting_wait_established(short cn, short seconds)
{
	short a[3];
	a[0] = cn;
	a[1] = TESTABLISH;
	a[2] = seconds;
	return (short)sting_call(tpl->fn[T_TCP_WAIT_STATE], a, 3);
}

short sting_send(short cn, const char *buf, short len)
{
	short a[4];
	a[0] = cn;
	put_long(a + 1, (u32)buf);
	a[3] = len;
	return (short)sting_call(tpl->fn[T_TCP_SEND], a, 4);
}

short sting_count(short cn)
{
	short a[1];
	a[0] = cn;
	return (short)sting_call(tpl->fn[T_CNBYTE_COUNT], a, 1);
}

short sting_read(short cn, char *buf, short len)
{
	short a[4];
	a[0] = cn;
	put_long(a + 1, (u32)buf);
	a[3] = len;
	return (short)sting_call(tpl->fn[T_CNGET_BLOCK], a, 4);
}

void sting_close(short cn)
{
	short a[4];
	a[0] = cn;
	a[1] = 0;		/* don't wait */
	put_long(a + 2, 0);
	sting_call(tpl->fn[T_TCP_CLOSE], a, 4);
}

const char *sting_error(short code)
{
	short a[1];
	switch (code) {
	case E_REFUSE: return "refused - is the gateway running?";
	case E_CNTIMEOUT: case -15: return "timed out - check the IP address";
	case E_EOF: return "the gateway closed the connection";
	case E_RRESET: return "connection reset";
	case -24: return "gateway unreachable";
	}
	if (tpl) {
		a[0] = code;
		return (const char *)sting_call(tpl->fn[T_GET_ERR_TEXT], a, 1);
	}
	return "STinG is not loaded";
}
