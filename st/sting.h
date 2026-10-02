/*
 * sting.h - a small STinG TCP client (STinG / STiK transport layer).
 */
#ifndef STING_H
#define STING_H

#define E_NORMAL    0
#define E_OBUFFULL  -1
#define E_NODATA    -2
#define E_EOF       -3
#define E_RRESET    -4
#define E_REFUSE    -7
#define E_CNTIMEOUT -16

short sting_init(void);			/* 1 if STinG is loaded */
short sting_open(u32 ip, u16 port);	/* handle >= 0 or error */
short sting_wait_established(short cn, short seconds);
short sting_send(short cn, const char *buf, short len);
short sting_count(short cn);
short sting_read(short cn, char *buf, short len);
void sting_close(short cn);
const char *sting_error(short code);

#endif
