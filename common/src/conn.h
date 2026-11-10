/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * conn.h - a buffered, line-oriented TCP connection with timeouts and
 * an optional protocol log (NAME.LOG). IMAP, POP3 and SMTP (EMail), HTTP
 * (ECal, EContact) and FTP's control and data connections (EFTP) share it.
 */
#ifndef CONN_H
#define CONN_H

#include "util.h"

/* how a connection is protected */
#define SEC_PLAIN    0	/* none (through the Gateway, or a local server) */
#define SEC_TLS      1	/* TLS from the first byte (993, 995, 465, 443, 990) */
#define SEC_STARTTLS 2	/* plain first, then STARTTLS / STLS / AUTH TLS (143, 110, 587, 21) */

typedef struct {
	int h;
	void *tls;		/* TLS *, when encrypted */
	unsigned long tls_ms;	/* how long the handshake took, tries the server refused included */
	int tls_tries;		/* connections it took (another cipher, ECDSA) */
	char *buf;		/* right after the CONN: small for commands, more for data */
	long bufsize, pos, len;
	long timeout_ms;
	int dead;		/* known to be gone: close without a goodbye */
	char err[160];
	const char *name;	/* "IMAP", "HTTP", "FTP"... for the log */
} CONN;

/* bufsize: what is read from the network at a time, 0 for conn_bufsize
   (EFTP on a 1 MB ST: 2 KB for the commands, 8 KB for the files). sec:
   SEC_TLS starts TLS at once; otherwise plain (the protocol calls
   conn_starttls when it asks for SEC_STARTTLS) */
CONN *conn_open(const char *name, const char *host, unsigned short port, long bufsize, int sec,
		char *err, int errlen);
/* switch to TLS on an open connection (STARTTLS, FTP's AUTH TLS and its
   data connections); host names the server for the certificate and the
   session to resume. 0 with err on failure; conn_tls_retry is then set
   if a new connection should try again (with every cipher, or ECDSA) */
int   conn_starttls(CONN *c, const char *host, char *err, int errlen);
extern int conn_tls_retry;
/* the receive buffer when conn_open is given 0 (16 KB by default) */
extern long conn_bufsize;
/* the root certificates (CACERT.PEM), loaded at the first TLS connection */
extern char conn_cacert[200];
/* "TLS 1.2, ECDHE-RSA, ChaCha20-Poly1305, resumed", or "" */
const char *conn_security(CONN *c);
void  conn_close(CONN *c);
/* close after what was written has gone out (the end of an upload) */
void  conn_close_flush(CONN *c);
int   conn_write(CONN *c, const char *data, long n);
/* send one command line (CRLF added); secret: hide arguments in the log */
int   conn_cmd(CONN *c, int secret, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
/* read one line without CRLF into b (b is reset first); -1 on error */
long  conn_getline(CONN *c, SBUF *b);
/* read exactly n bytes and append them to b */
int   conn_getbytes(CONN *c, SBUF *b, long n);
/* raw bytes: > 0 read, 0 the other side closed, -1 timed out or stopped */
long  conn_read(CONN *c, char *buf, long max);
/* 1 if something has come in (or the line closed), without waiting */
int   conn_pending(CONN *c);

/* logging: set conn_logfile to a path to append the dialogue to it */
extern char conn_logfile[200];
void conn_log(const char *who, const char *dir, const char *text, long n);
void conn_log_flush(void);	/* write out what is still buffered */
/* 1: a job begins, its milestones are written with the buffer (when it
   is full, at an error, or at conn_log_hold(0), which writes it out) */
void conn_log_hold(int on);
/* the held log's buffer, taken from the heap for the job: 16384; 0 (or
   up to 4096) keeps the 4 KB static one */
extern long conn_log_hold_bytes;
/* the log buffer's size, bytes (256 to 4096, 4096 by default): an app short of heap lowers it before it logs (conn.c) */
extern long conn_log_small;
/* STinG's output buffer for each connection opened from now on: 4096;
   up to 32000 (back to 4096 when STinG's pool hasn't the room).
   MiNTnet's buffers are the kernel's */
extern long net_sendbuf;
/* add text (a crash report) at the end of the log, after what is still
   buffered: crash_report = conn_log_append (crash.h) */
void conn_log_append(const char *text, long n);
/* The log is capped (guidelines 6.5): a write that would take it over
   conn_log_max (256 KB; 0 = no cap) moves it to NAME.OLD (the extension
   replaced; an older NAME.OLD goes) and starts a new one. conn_log_size,
   if the app sets it (to its pf_handle_size), gives the size of the open
   file, so the check costs no look at the folder; without it the size is
   looked at once a run and counted. */
extern long conn_log_max;
extern long (*conn_log_size)(int h);
/* one line, "start -- APP VERSION, date time" (the date only where the clock
   has one), for an app to call once at its start; conn.c never calls it.
   app_version: "EMail 1.23" */
void conn_log_start(const char *app_version);
/* the shape of a login (lengths, '@', '-') without the login itself, so
   the log shows a cut-off or mistyped password without giving it away */
void conn_log_login(const char *who, const char *user, const char *pass);

/* bytes received and sent, ever (progress display) */
extern long conn_bytes, conn_bytes_out;
/* called whenever data comes in or goes out (progress display) */
extern void (*conn_tick)(void);
/* set by the UI (Esc, the Stop button): every wait ends at once */
extern int conn_stop;

/* The three phases of handing work to the Gateway (a PDF to convert, a
   letter to translate, a page to render; Erez, 7 Nov 2026), so the status
   line can say where the time goes. A gateway client calls conn_phase at
   each change when it is set (never when NULL):
     GATE_SEND     the request goes out (bytes: its size);
     GATE_WAIT     all of it is sent and the Gateway works (bytes 0);
     GATE_RECEIVE  the answer comes in (bytes: its size, -1 if unknown);
     GATE_DONE     finished, with an answer or a failure (bytes 0).
   An app sets the status line from its handler (guideline 7.3 has the
   words) and shows progress inside SEND and RECEIVE from conn_bytes_out
   and conn_bytes in its conn_tick. "Sent" means handed to the network:
   the last few KB may still be on the way. */
enum { GATE_SEND = 1, GATE_WAIT, GATE_RECEIVE, GATE_DONE };
extern void (*conn_phase)(int phase, long bytes);

#endif
