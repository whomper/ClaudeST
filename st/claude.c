/*
 * Claude ST - a Claude.ai client for the Atari ST, STE, TT and Falcon.
 *
 * A single GEM window: the sidebar on the left (New chat, Search, Chats,
 * Projects, Artifacts and the current list), the active conversation on
 * the right with an input line underneath. The machine talks a small
 * line protocol over the serial port (AUX:) to claude_bridge.py running
 * on any modern computer, which does the TLS/HTTP work against claude.ai.
 * See ../PROTOCOL.md.
 */
#include "tos.h"
#include "gem.h"
#include "sting.h"

#define VERSION "1.0"

/* ------------------------------------------------------------------ */
/* state                                                               */
/* ------------------------------------------------------------------ */

static short apid, phys, vh;
static short cw, ch;			/* character cell */
static short ncolors;
static short scr_w, scr_h;
static short win = -1;
static short wx, wy, ww, wh;		/* window work area */
static short quit;

/* layout */
static short sb_w;			/* sidebar width */
static short sb_user;			/* width set by dragging the divider, 0 = auto */
static short over_divider;		/* mouse is over the divider (MU_M1) */
static short px, pw;			/* chat pane x / width */
static short title_h, input_h, row_h;
static short tx, ty, tw, th;		/* text area */
static short rows, cols;
static short nav_y;			/* first nav row */
static short list_y, list_rows;		/* sidebar list */
static short status_y;

/* conversation text: paragraphs separated by '\n'.
 * "\x10R" starts a message header line (R = role), "\x11" a bold line. */
#define MK_HEADER 0x10
#define MK_BOLD   0x11

static char *tb;
static long tlen, tcap;

typedef struct {
	long off;
	short len;
	u8 kind;
	u8 role;
} LINE;

#define L_TEXT   0
#define L_HEADER 1
#define L_BOLD   2

static LINE *lines;
static short nlines, maxlines;
static short top_line;
static short last_slider_pos = -1, last_slider_size = -1;

/* sidebar list */
typedef struct {
	char id[40];
	char label[44];
	char pinned;
} ITEM;

#define MAXITEMS 300
static ITEM items[MAXITEMS];
static short nitems, list_top;
static short lcur = -1;			/* keyboard cursor in the list */
static char list_kind[12] = "CHATS";
static char list_title[48] = "Recents";
static char cur_id[40];
static char chat_title[80] = "New chat";
static char status[64] = "Waiting for bridge...";
static short busy;
static short online;

/* input line */
#define INMAX 1000
static char input[INMAX + 1];
static short inlen;
static short search_mode;
static short rename_mode;		/* input line edits a name */
static char rn_kind[10], rn_id[40];

/* "move to project" picker, filled by the bridge (Q/J/W) */
#define PICKMAX 20
static char pick_id[PICKMAX][40];
static char pick_label[PICKMAX][44];
static short npick;
static char pick_chat[40];
static short pick_x, pick_y;

/* dirty regions */
#define D_SIDEBAR 1
#define D_TITLE   2
#define D_TEXT    4
#define D_INPUT   8
#define D_STATUS  16
#define D_ALL     15
static short dirty;
static short text_dirty_row = 0x7fff;	/* first text row needing redraw */
static short pending_scroll;		/* lines auto-scrolled since last redraw */

/* serial */
static IOREC *iorec;
static char *old_ibuf;
static short old_ibufsiz, old_low, old_high;
#define RXBUF 16384
static char rxring[RXBUF];
static short baud = BAUD_19200;
static short serial_is_open;

/* link to the bridge: serial port, or TCP through STinG */
#define LINK_SERIAL 0
#define LINK_TCP    1
static short link = LINK_SERIAL;
static u32 tcp_ip;
static u16 tcp_port = 2323;
static short tcp_cn = -1;
static long next_connect;
static char txbuf[1200];
static short txlen;

static char rxline[1200];
static short rxlen;
static long ticks, last_hello;

static void tx_cmd(const char *a, const char *b, const char *c);
static void show_picker(void);
static void save_config(void);

/* ------------------------------------------------------------------ */
/* menu bar (built in code, so no .RSC file is needed)                 */
/* ------------------------------------------------------------------ */

static char t_desk[] = " Desk ";
static char t_file[] = " File ";
static char t_opts[] = " Options ";
static char m_about[] = "  About Claude ST... ";
static char m_sep1[]  = "---------------------";
static char m_acc[6][22] = {
	"  Desk Accessory 1   ", "  Desk Accessory 2   ", "  Desk Accessory 3   ",
	"  Desk Accessory 4   ", "  Desk Accessory 5   ", "  Desk Accessory 6   "
};
static char m_new[]   = "  New chat      ^N ";
static char m_find[]  = "  Search chats  ^F ";
static char m_ref[]   = "  Reconnect     ^R ";
static char m_sep2[]  = "-------------------";
static char m_quit[]  = "  Quit          ^Q ";
static char m_b48[]   = "  4800 baud       ";
static char m_b96[]   = "  9600 baud       ";
static char m_b192[]  = "  19200 baud      ";
static char m_sep3[]  = "------------------";
static char m_net[]   = "  Network (STinG) ";
static char m_ser[]   = "  Serial port     ";

enum {
	MN_ROOT, MN_BAR, MN_ACTIVE, MN_TDESK, MN_TFILE, MN_TOPTS, MN_SCREEN,
	MN_DDESK, MN_ABOUT, MN_SEP1, MN_ACC1, MN_ACC2, MN_ACC3, MN_ACC4, MN_ACC5, MN_ACC6,
	MN_DFILE, MN_NEW, MN_FIND, MN_REF, MN_SEP2, MN_QUIT,
	MN_DOPTS, MN_B48, MN_B96, MN_B192, MN_SEP3, MN_NET, MN_SER,
	MN_COUNT
};

#define S(p) ((long)(p))
static OBJECT menu[MN_COUNT] = {
	/* next  head tail type    flags state spec       x  y  w   h */
	{ -1, 1, 6,  G_IBOX, 0, 0, 0,          0, 0, 80, 25 },		/* ROOT */
	{ 6, 2, 2,   G_BOX,  0, 0, 0x1100L,    0, 0, 80, 0x201 },	/* BAR */
	{ 1, 3, 5,   G_IBOX, 0, 0, 0,          2, 0, 21, 0x301 },	/* ACTIVE */
	{ 4, -1, -1, G_TITLE, 0, 0, 0,         0, 0, 6, 0x301 },
	{ 5, -1, -1, G_TITLE, 0, 0, 0,         6, 0, 6, 0x301 },
	{ 2, -1, -1, G_TITLE, 0, 0, 0,         12, 0, 9, 0x301 },
	{ 0, 7, 22,  G_IBOX, 0, 0, 0,          0, 0x301, 80, 19 },	/* SCREEN */
	{ 16, 8, 15, G_BOX,  0, 0, 0xFF1100L, 2, 0, 21, 8 },		/* desk drop */
	{ 9, -1, -1, G_STRING, 0, 0, 0,        0, 0, 21, 1 },
	{ 10, -1, -1, G_STRING, 0, DISABLED, 0, 0, 1, 21, 1 },
	{ 11, -1, -1, G_STRING, 0, 0, 0,       0, 2, 21, 1 },
	{ 12, -1, -1, G_STRING, 0, 0, 0,       0, 3, 21, 1 },
	{ 13, -1, -1, G_STRING, 0, 0, 0,       0, 4, 21, 1 },
	{ 14, -1, -1, G_STRING, 0, 0, 0,       0, 5, 21, 1 },
	{ 15, -1, -1, G_STRING, 0, 0, 0,       0, 6, 21, 1 },
	{ 7, -1, -1, G_STRING, 0, 0, 0,        0, 7, 21, 1 },
	{ 22, 17, 21, G_BOX, 0, 0, 0xFF1100L,  8, 0, 19, 5 },		/* file drop */
	{ 18, -1, -1, G_STRING, 0, 0, 0,       0, 0, 19, 1 },
	{ 19, -1, -1, G_STRING, 0, 0, 0,       0, 1, 19, 1 },
	{ 20, -1, -1, G_STRING, 0, 0, 0,       0, 2, 19, 1 },
	{ 21, -1, -1, G_STRING, 0, DISABLED, 0, 0, 3, 19, 1 },
	{ 16, -1, -1, G_STRING, 0, 0, 0,       0, 4, 19, 1 },
	{ 6, 23, 28, G_BOX,  0, 0, 0xFF1100L,  14, 0, 18, 6 },		/* options drop */
	{ 24, -1, -1, G_STRING, 0, 0, 0,       0, 0, 18, 1 },
	{ 25, -1, -1, G_STRING, 0, 0, 0,       0, 1, 18, 1 },
	{ 26, -1, -1, G_STRING, 0, 0, 0,       0, 2, 18, 1 },
	{ 27, -1, -1, G_STRING, 0, DISABLED, 0, 0, 3, 18, 1 },
	{ 28, -1, -1, G_STRING, 0, 0, 0,       0, 4, 18, 1 },
	{ 22, -1, -1, G_STRING, LASTOB, 0, 0,  0, 5, 18, 1 },
};

static void menu_init(void)
{
	short i, cols_ = scr_w / cw;

	menu[MN_TDESK].ob_spec = S(t_desk);
	menu[MN_TFILE].ob_spec = S(t_file);
	menu[MN_TOPTS].ob_spec = S(t_opts);
	menu[MN_ABOUT].ob_spec = S(m_about);
	menu[MN_SEP1].ob_spec = S(m_sep1);
	for (i = 0; i < 6; i++)
		menu[MN_ACC1 + i].ob_spec = S(m_acc[i]);
	menu[MN_NEW].ob_spec = S(m_new);
	menu[MN_FIND].ob_spec = S(m_find);
	menu[MN_REF].ob_spec = S(m_ref);
	menu[MN_SEP2].ob_spec = S(m_sep2);
	menu[MN_QUIT].ob_spec = S(m_quit);
	menu[MN_B48].ob_spec = S(m_b48);
	menu[MN_B96].ob_spec = S(m_b96);
	menu[MN_B192].ob_spec = S(m_b192);
	menu[MN_SEP3].ob_spec = S(m_sep3);
	menu[MN_NET].ob_spec = S(m_net);
	menu[MN_SER].ob_spec = S(m_ser);

	menu[MN_ROOT].ob_width = cols_;
	menu[MN_ROOT].ob_height = scr_h / ch;
	menu[MN_BAR].ob_width = cols_;
	menu[MN_SCREEN].ob_width = cols_;
	for (i = 0; i < MN_COUNT; i++)
		rsrc_obfix(menu, i);
}

static void menu_check_baud(void)
{
	menu[MN_B48].ob_state = baud == BAUD_4800 ? CHECKED : 0;
	menu[MN_B96].ob_state = baud == BAUD_9600 ? CHECKED : 0;
	menu[MN_B192].ob_state = baud == BAUD_19200 ? CHECKED : 0;
	menu[MN_NET].ob_state = link == LINK_TCP ? CHECKED : 0;
	menu[MN_SER].ob_state = link == LINK_SERIAL ? CHECKED : 0;
}

/* ------------------------------------------------------------------ */
/* serial port                                                         */
/* ------------------------------------------------------------------ */

/* run in supervisor mode via Supexec, with interrupts masked */
static short ints_off(void)
{
	short sr;
	__asm__ volatile("move.w %%sr,%0\n\tor.w #0x0700,%%sr" : "=d"(sr) : : "memory", "cc");
	return sr;
}

static void ints_restore(short sr)
{
	__asm__ volatile("move.w %0,%%sr" : : "d"(sr) : "memory", "cc");
}

static void sup_set_iorec(void)
{
	short sr = ints_off();
	iorec->ibuf = rxring;
	iorec->ibufsiz = RXBUF;
	iorec->ibufhd = 0;
	iorec->ibuftl = 0;
	iorec->ibuflow = RXBUF / 4;
	iorec->ibufhi = RXBUF * 3 / 4;
	ints_restore(sr);
}

static void sup_restore_iorec(void)
{
	short sr = ints_off();
	iorec->ibuf = old_ibuf;
	iorec->ibufsiz = old_ibufsiz;
	iorec->ibufhd = 0;
	iorec->ibuftl = 0;
	iorec->ibuflow = old_low;
	iorec->ibufhi = old_high;
	ints_restore(sr);
}

static void serial_open(void)
{
	if (serial_is_open)
		return;
	serial_is_open = 1;
	Rsconf(baud, 0, -1, -1, -1, -1);
	iorec = Iorec(0);
	old_ibuf = iorec->ibuf;
	old_ibufsiz = iorec->ibufsiz;
	old_low = iorec->ibuflow;
	old_high = iorec->ibufhi;
	Supexec(sup_set_iorec);
}

static void serial_close(void)
{
	if (!serial_is_open)
		return;
	serial_is_open = 0;
	Supexec(sup_restore_iorec);
}

static void set_status(const char *s)
{
	strlcpy_(status, s, sizeof(status));
	dirty |= D_STATUS;
}

static void tcp_lost(short code)
{
	char msg[64];
	if (tcp_cn >= 0)
		sting_close(tcp_cn);
	tcp_cn = -1;
	online = 0;
	strcpy(msg, "Offline: ");
	strlcpy_(msg + 9, sting_error(code), sizeof(msg) - 9);
	set_status(msg);
	next_connect = ticks + 125;	/* retry in ~5 s */
}

static void link_flush(void)
{
	short i, tries = 0;

	if (link == LINK_SERIAL) {
		for (i = 0; i < txlen; i++)
			Bconout(DEV_AUX, (u8)txbuf[i]);
	} else if (tcp_cn >= 0) {
		for (;;) {
			short r = sting_send(tcp_cn, txbuf, txlen);
			if (r == E_NORMAL)
				break;
			if (r != E_OBUFFULL || ++tries > 250) {
				tcp_lost(r);
				break;
			}
			{
				short m[8];
				EVENT e;
				evnt_multi_(MU_TIMER, 0, 0, 0, 20, m, &e);
			}
		}
	}
	txlen = 0;
}

static void tx_byte(u8 c)
{
	if (txlen < (short)sizeof(txbuf))
		txbuf[txlen++] = c;
}

static void tx_str(const char *s)
{
	while (*s)
		tx_byte((u8)*s++);
}

/* send "A\tB\tC\n" (b and c may be NULL) */
static void tx_cmd(const char *a, const char *b, const char *c)
{
	tx_str(a);
	if (b) {
		tx_byte('\t');
		tx_str(b);
	}
	if (c) {
		tx_byte('\t');
		tx_str(c);
	}
	tx_byte('\n');
	link_flush();
}

/* send "A\tB\tC\tD\n" */
static void tx_cmd4(const char *a, const char *b, const char *c, const char *d)
{
	tx_str(a);
	tx_byte('\t');
	tx_str(b);
	tx_byte('\t');
	tx_str(c);
	tx_byte('\t');
	tx_str(d);
	tx_byte('\n');
	link_flush();
}

static void send_hello(void)
{
	tx_cmd("HELLO", "1", VERSION);
	last_hello = ticks;
}

/* ------------------------------------------------------------------ */
/* text buffer and word wrap                                           */
/* ------------------------------------------------------------------ */

static void add_line(long off, short len, u8 kind, u8 role)
{
	if (nlines >= maxlines)
		return;
	lines[nlines].off = off;
	lines[nlines].len = len;
	lines[nlines].kind = kind;
	lines[nlines].role = role;
	nlines++;
}

static short wrap_width(u8 role)
{
	short w = role == 'U' ? cols - 3 : cols;
	return w < 8 ? 8 : w;
}

static void wrap_from(long p, u8 role)
{
	long e, s;
	short w;
	u8 kind;

	while (p < tlen) {
		for (e = p; e < tlen && tb[e] != '\n'; e++)
			;
		if (tb[p] == MK_HEADER && e - p >= 2) {
			role = (u8)tb[p + 1];
			add_line(p, 0, L_HEADER, role);
		} else if (e == p) {
			add_line(p, 0, L_TEXT, role);
		} else {
			kind = L_TEXT;
			s = p;
			if (tb[s] == MK_BOLD) {
				kind = L_BOLD;
				s++;
			}
			w = wrap_width(role);
			while (s < e) {
				long b;
				if (e - s <= w) {
					add_line(s, (short)(e - s), kind, role);
					break;
				}
				for (b = s + w; b > s && tb[b] != ' '; b--)
					;
				if (b == s) {
					add_line(s, w, kind, role);
					s += w;
				} else {
					add_line(s, (short)(b - s), kind, role);
					s = b + 1;
				}
			}
		}
		if (e >= tlen)
			break;
		p = e + 1;
	}
}

static short is_para_start(long off)
{
	return off == 0 || tb[off - 1] == '\n' || tb[off - 1] == MK_BOLD;
}

/* rewrap the last paragraph after text was appended; returns first changed line */
static short wrap_tail(void)
{
	short li = nlines;
	long p = 0;
	u8 role = 0;

	while (li > 0) {
		li--;
		if (is_para_start(lines[li].off))
			break;
	}
	if (nlines) {
		p = lines[li].off;
		if (li < nlines && lines[li].kind == L_BOLD && p > 0 && tb[p - 1] == MK_BOLD)
			p--;
		role = li > 0 ? lines[li - 1].role : 0;
	}
	nlines = li;
	wrap_from(p, role);
	return li;
}

static void rewrap_all(void)
{
	nlines = 0;
	wrap_from(0, 0);
}

static short at_bottom(void)
{
	return top_line + rows >= nlines;
}

static void clamp_top(void)
{
	if (top_line > nlines - rows)
		top_line = nlines - rows;
	if (top_line < 0)
		top_line = 0;
}

static void mark_text_from_line(short li)
{
	short r = li - top_line;
	if (r < 0)
		r = 0;
	if (r < text_dirty_row)
		text_dirty_row = r;
	dirty |= D_TEXT;
}

/* drop the oldest half of the conversation when the buffer fills up */
static void trim_buffer(void)
{
	long cut = tlen / 2;
	while (cut < tlen && tb[cut] != '\n')
		cut++;
	if (cut >= tlen)
		cut = tlen;
	else
		cut++;
	memmove(tb, tb + cut, tlen - cut);
	tlen -= cut;
	rewrap_all();
	top_line = nlines;
	clamp_top();
	text_dirty_row = 0;
	dirty |= D_TEXT;
}

static void append(const char *s, short n)
{
	short follow = at_bottom(), li;

	if (tlen + n + 4 >= tcap || nlines >= maxlines - 50)
		trim_buffer();
	memcpy(tb + tlen, s, n);
	tlen += n;
	li = wrap_tail();
	if (follow) {
		short old = top_line;
		top_line = nlines - rows;
		clamp_top();
		if (top_line > old) {
			/* rows already marked dirty moved up with the scroll */
			short d = top_line - old;
			pending_scroll += d;
			if (text_dirty_row != 0x7fff)
				text_dirty_row = text_dirty_row > d ? text_dirty_row - d : 0;
		}
	}
	mark_text_from_line(li);
}

static void clear_text(void)
{
	tlen = 0;
	nlines = 0;
	top_line = 0;
	text_dirty_row = 0;
	dirty |= D_TEXT;
}

static void begin_message(char role)
{
	char hdr[4];
	if (tlen > 0) {
		if (tb[tlen - 1] != '\n')
			append("\n", 1);
		append("\n", 1);
	}
	hdr[0] = MK_HEADER;
	hdr[1] = role;
	hdr[2] = '\n';
	append(hdr, 3);
}

/* ------------------------------------------------------------------ */
/* drawing                                                             */
/* ------------------------------------------------------------------ */

static short clip[4];			/* current clip rect x1,y1,x2,y2 */

static short row_visible(short y1, short y2)
{
	return y2 >= clip[1] && y1 <= clip[3];
}

static void fill(short x1, short y1, short x2, short y2, short color)
{
	short p[4];
	p[0] = x1;
	p[1] = y1;
	p[2] = x2;
	p[3] = y2;
	vsf_interior(vh, 1);
	vsf_color(vh, color);
	vr_recfl(vh, p);
}

static void line(short x1, short y1, short x2, short y2, short color)
{
	short p[4];
	p[0] = x1;
	p[1] = y1;
	p[2] = x2;
	p[3] = y2;
	vsl_color(vh, color);
	v_pline(vh, 2, p);
}

static void text(short x, short y, const char *s, short n, short fx, short color)
{
	vst_effects(vh, fx);
	vst_color(vh, color);
	v_gtext_n(vh, x, y, s, n);
}

static short accent(void)
{
	return ncolors >= 4 ? 2 : 1;	/* red where we have colour, else black */
}

/* the Claude "spark": eight rays around a centre */
static void spark(short x, short y, short r)
{
	short d = r * 7 / 10;
	short c = accent();
	line(x - r, y, x + r, y, c);
	line(x, y - r, x, y + r, c);
	line(x - d, y - d, x + d, y + d, c);
	line(x - d, y + d, x + d, y - d, c);
	line(x - r, y + 1, x + r, y + 1, c);
}

static void layout(void)
{
	short min_sb = 18 * cw, max_sb = 30 * cw;

	if (sb_user) {
		sb_w = sb_user;
		if (sb_w > ww - 34 * cw)
			sb_w = ww - 34 * cw;
		if (sb_w < 14 * cw)
			sb_w = 14 * cw;
	} else {
		sb_w = ww * 3 / 10;
		if (sb_w < min_sb)
			sb_w = min_sb;
		if (sb_w > max_sb)
			sb_w = max_sb;
		if (ww < 56 * cw)
			sb_w = 16 * cw;
		sb_w -= sb_w % cw;
	}

	row_h = ch + (ch >= 16 ? 4 : 2);
	title_h = ch + (ch >= 16 ? 8 : 4);
	input_h = ch + (ch >= 16 ? 10 : 6);

	px = wx + sb_w + 1;
	pw = ww - sb_w - 1;

	tx = px + cw;
	tw = pw - 2 * cw;
	ty = wy + title_h + 2;
	th = wh - title_h - input_h - 4;
	cols = tw / cw;
	rows = th / ch;
	if (cols < 10)
		cols = 10;
	if (rows < 1)
		rows = 1;

	nav_y = wy + title_h + 2;
	list_y = nav_y + 6 * row_h + 4;
	status_y = wy + wh - row_h;
	list_rows = (status_y - 2 - (list_y + row_h)) / row_h;
	if (list_rows < 1)
		list_rows = 1;
}

static const char *nav_labels[5] = { "+ New chat", "  Search", "  Chats", "  Projects", "  Artifacts" };

static short nav_active(short i)
{
	if (i == 2)
		return !strcmp(list_kind, "CHATS");
	if (i == 3)
		return !strcmp(list_kind, "PROJECTS") || !strcmp(list_kind, "PROJECT");
	if (i == 4)
		return !strcmp(list_kind, "ARTIFACTS");
	if (i == 1)
		return !strcmp(list_kind, "SEARCH");
	return 0;
}

static void draw_sidebar(void)
{
	short i, y, maxc = sb_w / cw - 2;
	short x = wx + cw;

	fill(wx, wy, wx + sb_w - 1, wy + wh - 1, 0);
	line(wx + sb_w, wy, wx + sb_w, wy + wh - 1, 1);

	/* brand */
	if (row_visible(wy, wy + title_h)) {
		spark(x + cw / 2, wy + title_h / 2, ch / 2 - 1);
		text(x + 2 * cw, wy + (title_h - ch) / 2, "Claude", 6, 1, 1);
		text(x + 9 * cw, wy + (title_h - ch) / 2, "ST", 2, 0, 1);
		line(wx, wy + title_h, wx + sb_w - 1, wy + title_h, 1);
	}

	/* navigation */
	for (i = 0; i < 5; i++) {
		y = nav_y + i * row_h;
		if (!row_visible(y, y + row_h))
			continue;
		if (nav_active(i)) {
			fill(wx + 4, y, wx + sb_w - 5, y + row_h - 2, 1);
			text(x, y + (row_h - ch) / 2 - 1, nav_labels[i], strlen(nav_labels[i]), 1, 0);
		} else {
			text(x, y + (row_h - ch) / 2 - 1, nav_labels[i], strlen(nav_labels[i]), i == 0 ? 1 : 0, 1);
		}
	}
	line(wx + 4, list_y - 3, wx + sb_w - 5, list_y - 3, 1);

	/* list header with paging arrows */
	if (row_visible(list_y, list_y + row_h)) {
		short n = strlen(list_title);
		if (n > maxc - 4)
			n = maxc - 4;
		text(x, list_y, list_title, n, 1, 1);
		if (list_top > 0)
			text(wx + sb_w - 4 * cw, list_y, "\001", 1, 0, 1);
		if (list_top + list_rows < nitems)
			text(wx + sb_w - 2 * cw, list_y, "\002", 1, 0, 1);
	}

	/* items */
	for (i = 0; i < list_rows && list_top + i < nitems; i++) {
		ITEM *it = &items[list_top + i];
		short n = strlen(it->label);
		y = list_y + (i + 1) * row_h;
		if (!row_visible(y, y + row_h))
			continue;
		if (n > maxc - (it->pinned ? 1 : 0))
			n = maxc - (it->pinned ? 1 : 0);
		{
			short sel = cur_id[0] && !strcmp(it->id, cur_id);
			if (sel)
				fill(wx + 4, y - 1, wx + sb_w - 5, y + row_h - 3, 1);
			text(x, y + (row_h - ch) / 2 - 1, it->label, n, 0, sel ? 0 : 1);
			if (it->pinned) {
				/* a small filled diamond: pinned / starred */
				short cx = wx + sb_w - cw - 2, cy = y + row_h / 2 - 1, k;
				for (k = 0; k <= 3; k++) {
					line(cx - 3 + k, cy - k, cx + 3 - k, cy - k, sel ? 0 : accent());
					line(cx - 3 + k, cy + k, cx + 3 - k, cy + k, sel ? 0 : accent());
				}
			}
		}
		if (list_top + i == lcur) {
			short x1 = wx + 3, x2 = wx + sb_w - 4, y1 = y - 2, y2 = y + row_h - 2;
			line(x1, y1, x2, y1, 1);
			line(x2, y1, x2, y2, 1);
			line(x2, y2, x1, y2, 1);
			line(x1, y2, x1, y1, 1);
		}
	}
	if (nitems == 0) {
		y = list_y + row_h;
		text(x, y, "(empty)", 7, 2, 1);
	}

	/* status line */
	line(wx, status_y - 2, wx + sb_w - 1, status_y - 2, 1);
	{
		short n = strlen(status);
		if (n > maxc - 1)
			n = maxc - 1;
		fill(x, status_y + row_h / 2 - 2, x + 3, status_y + row_h / 2 + 1, online ? accent() : 1);
		if (!online)
			fill(x + 1, status_y + row_h / 2 - 1, x + 2, status_y + row_h / 2, 0);
		text(x + cw, status_y + (row_h - ch) / 2, status, n, 0, 1);
	}
}

static void draw_title(void)
{
	short n = strlen(chat_title), maxc = pw / cw - 2;
	fill(px, wy, px + pw - 1, wy + title_h, 0);
	if (n > maxc)
		n = maxc;
	text(px + cw, wy + (title_h - ch) / 2, chat_title, n, 1, 1);
	if (busy && maxc > n + 12)
		text(px + pw - 12 * cw, wy + (title_h - ch) / 2, "thinking...", 11, 2, 1);
	line(px, wy + title_h, px + pw - 1, wy + title_h, 1);
}

static void draw_text(short from_row)
{
	short r, y, x;

	if (from_row < 0)
		from_row = 0;
	if (from_row == 0)
		fill(px, wy + title_h + 1, px + pw - 1, ty - 1, 0);
	/* clear and draw row by row so streaming text doesn't flicker */
	for (r = from_row; r < rows; r++) {
		short li = top_line + r;
		LINE *l;
		y = ty + r * ch;
		if (!row_visible(y, y + ch))
			continue;
		fill(px, y, px + pw - 1, y + ch - 1, 0);
		if (li >= nlines)
			continue;
		l = &lines[li];
		x = tx;
		if (l->kind == L_HEADER) {
			const char *name = "Claude";
			short n = 6;
			switch (l->role) {
			case 'U': name = "You"; n = 3; break;
			case 'K': name = "Artifact"; n = 8; break;
			case 'E': name = "Error"; n = 5; break;
			case 'I': name = "Info"; n = 4; break;
			}
			if (l->role == 'A') {
				spark(x + cw / 2, y + ch / 2, ch / 2 - 1);
				x += 2 * cw;
			}
			text(x, y, name, n, 1, 1);
			continue;
		}
		if (l->role == 'U') {
			line(x + cw / 2, y, x + cw / 2, y + ch - 1, accent());
			line(x + cw / 2 + 1, y, x + cw / 2 + 1, y + ch - 1, accent());
			x += 3 * cw;
		} else if (l->role == 'K') {
			line(x + cw / 2, y, x + cw / 2, y + ch - 1, 1);
		}
		text(x, y, tb + l->off, l->len, l->kind == L_BOLD ? 1 : 0, 1);
	}
	y = ty + rows * ch;
	if (row_visible(y, wy + wh - input_h - 1))
		fill(px, y, px + pw - 1, wy + wh - input_h - 1, 0);
}

/* move the text area up by n lines on screen, if the window is fully visible */
static short blit_scroll(short n)
{
	short r[4], pxy[8];
	MFDB scr;

	if (n <= 0 || n >= rows)
		return 0;
	wind_get(win, WF_FIRSTXYWH, &r[0], &r[1], &r[2], &r[3]);
	if (r[0] != wx || r[1] != wy || r[2] != ww || r[3] != wh ||
	    wx < 0 || wy < 0 || wx + ww > scr_w || wy + wh > scr_h)
		return 0;
	memset(&scr, 0, sizeof(scr));
	pxy[0] = px;
	pxy[1] = ty + n * ch;
	pxy[2] = px + pw - 1;
	pxy[3] = ty + rows * ch - 1;
	pxy[4] = px;
	pxy[5] = ty;
	pxy[6] = px + pw - 1;
	pxy[7] = ty + (rows - n) * ch - 1;
	wind_update(BEG_UPDATE);
	graf_mouse(M_OFF, 0);
	vs_clip(vh, 0, pxy);
	vro_cpyfm(vh, 3, pxy, &scr, &scr);
	graf_mouse(M_ON, 0);
	wind_update(END_UPDATE);
	return 1;
}

static void draw_input(void)
{
	short y = wy + wh - input_h;
	short x1 = px + cw / 2, x2 = px + pw - cw / 2 - 1;
	short y1 = y + 2, y2 = wy + wh - 3;
	const char *prompt = rename_mode ? "Rename: " : search_mode ? "Find: " : "> ";
	short pl = strlen(prompt);
	short avail = (x2 - x1) / cw - pl - 2;
	short start = inlen > avail ? inlen - avail : 0;
	short tyy = y1 + (y2 - y1 - ch) / 2 + 1;
	short tx0 = x1 + cw / 2;

	fill(px, y, px + pw - 1, wy + wh - 1, 0);
	line(px, y, px + pw - 1, y, 1);
	line(x1, y1, x2, y1, 1);
	line(x2, y1, x2, y2, 1);
	line(x2, y2, x1, y2, 1);
	line(x1, y2, x1, y1, 1);
	text(tx0, tyy, prompt, pl, 1, 1);
	if (inlen == 0 && !search_mode && !rename_mode) {
		const char *ph = busy ? "Claude is replying..." : "Reply to Claude...";
		short n = strlen(ph);
		if (n > avail)
			n = avail;
		text(tx0 + pl * cw, tyy, ph, n, 2, 1);
		fill(tx0 + pl * cw, tyy, tx0 + pl * cw + 1, tyy + ch - 1, 1);
	} else {
		text(tx0 + pl * cw, tyy, input + start, inlen - start, 0, 1);
		fill(tx0 + (pl + inlen - start) * cw, tyy, tx0 + (pl + inlen - start) * cw + 1, tyy + ch - 1, 1);
	}
}

static short rc_intersect(const short *a, short *b)
{
	/* a, b: x,y,w,h. result in b */
	short x1 = a[0] > b[0] ? a[0] : b[0];
	short y1 = a[1] > b[1] ? a[1] : b[1];
	short x2 = (a[0] + a[2] < b[0] + b[2]) ? a[0] + a[2] : b[0] + b[2];
	short y2 = (a[1] + a[3] < b[1] + b[3]) ? a[1] + a[3] : b[1] + b[3];
	b[0] = x1;
	b[1] = y1;
	b[2] = x2 - x1;
	b[3] = y2 - y1;
	return x2 > x1 && y2 > y1;
}

/* redraw the parts in mask that intersect area, honouring the rect list */
static void redraw(short mask, short ax, short ay, short aw, short ah)
{
	short r[4], area[4], scr[4];
	short text_from = mask & D_TEXT ? text_dirty_row : 0;

	if (win < 0)
		return;
	area[0] = ax;
	area[1] = ay;
	area[2] = aw;
	area[3] = ah;
	scr[0] = 0;
	scr[1] = 0;
	scr[2] = scr_w;
	scr[3] = scr_h;
	if (!rc_intersect(scr, area))
		return;

	wind_update(BEG_UPDATE);
	graf_mouse(M_OFF, 0);
	vswr_mode(vh, 2);	/* transparent text over our own fills */
	vsf_perimeter(vh, 0);
	vst_alignment(vh, 0, 5);
	wind_get(win, WF_FIRSTXYWH, &r[0], &r[1], &r[2], &r[3]);
	while (r[2] && r[3]) {
		if (rc_intersect(area, r)) {
			clip[0] = r[0];
			clip[1] = r[1];
			clip[2] = r[0] + r[2] - 1;
			clip[3] = r[1] + r[3] - 1;
			vs_clip(vh, 1, clip);
			if (mask & D_SIDEBAR)
				draw_sidebar();
			if (mask & D_TITLE)
				draw_title();
			if (mask & D_TEXT)
				draw_text(text_from);
			if (mask & D_INPUT)
				draw_input();
		}
		wind_get(win, WF_NEXTXYWH, &r[0], &r[1], &r[2], &r[3]);
	}
	vs_clip(vh, 0, clip);
	graf_mouse(M_ON, 0);
	wind_update(END_UPDATE);
}

static void update_slider(void)
{
	short size = 1000, pos = 0;
	if (nlines > rows) {
		size = (short)((long)rows * 1000 / nlines);
		pos = (short)((long)top_line * 1000 / (nlines - rows));
	}
	if (size < 30)
		size = 30;
	if (size != last_slider_size) {
		wind_set(win, WF_VSLSIZE, size, 0, 0, 0);
		last_slider_size = size;
	}
	if (pos != last_slider_pos) {
		wind_set(win, WF_VSLIDE, pos, 0, 0, 0);
		last_slider_pos = pos;
	}
}

static void flush_dirty(void)
{
	if (pending_scroll) {
		if (text_dirty_row > 0 && blit_scroll(pending_scroll)) {
			short keep = rows - pending_scroll - 1;
			if (keep < text_dirty_row)
				text_dirty_row = keep < 0 ? 0 : keep;
		} else {
			text_dirty_row = 0;
		}
		pending_scroll = 0;
		dirty |= D_TEXT;
	}
	if (!dirty)
		return;
	if ((dirty & D_ALL) == D_ALL) {
		text_dirty_row = 0;
		redraw(D_ALL, wx, wy, ww, wh);
	} else {
		if (dirty & D_SIDEBAR)
			redraw(D_SIDEBAR, wx, wy, sb_w + 1, wh);
		else if (dirty & D_STATUS)
			redraw(D_SIDEBAR, wx, status_y - 1, sb_w, wy + wh - status_y + 1);
		if (dirty & D_TITLE)
			redraw(D_TITLE, px, wy, pw, title_h + 1);
		if (dirty & D_TEXT) {
			short r0 = text_dirty_row < rows ? text_dirty_row : rows;
			short y0 = ty + r0 * ch;
			redraw(D_TEXT, px, y0, pw, wy + wh - input_h - y0);
		}
		if (dirty & D_INPUT)
			redraw(D_INPUT, px, wy + wh - input_h, pw, input_h);
	}
	if (dirty & D_TEXT)
		update_slider();
	dirty = 0;
	text_dirty_row = 0x7fff;
}

/* ------------------------------------------------------------------ */
/* protocol: lines from the bridge                                     */
/* ------------------------------------------------------------------ */

static short split(char *s, char **f, short max)
{
	short n = 0;
	f[n++] = s;
	while (*s && n < max) {
		if (*s == '\t') {
			*s = 0;
			f[n++] = s + 1;
		}
		s++;
	}
	return n;
}

static void handle_line(char *s)
{
	char *f[4];
	short n = split(s, f, 4);
	char c = f[0][0];

	if (!online) {
		online = 1;
		dirty |= D_STATUS;
	}
	if (f[0][1] != 0)
		return;		/* all bridge commands are one letter */

	switch (c) {
	case 'S':			/* S <status> */
		if (n > 1 && strcmp(status, f[1])) {
			strlcpy_(status, f[1], sizeof(status));
			dirty |= D_STATUS;
		}
		break;
	case 'L':			/* L <kind> <title> : start a list */
		if (n > 1)
			strlcpy_(list_kind, f[1], sizeof(list_kind));
		strlcpy_(list_title, n > 2 ? f[2] : "", sizeof(list_title));
		nitems = 0;
		list_top = 0;
		lcur = -1;
		dirty |= D_SIDEBAR;
		break;
	case 'I':			/* I <id> <label> */
		if (nitems < MAXITEMS && n > 2) {
			strlcpy_(items[nitems].id, f[1], sizeof(items[0].id));
			strlcpy_(items[nitems].label, f[2], sizeof(items[0].label));
			items[nitems].pinned = n > 3 && f[3][0] == 'P';
			nitems++;
		}
		break;
	case 'Q':			/* start a picker list */
		npick = 0;
		break;
	case 'J':			/* J <id> <label> : picker entry */
		if (npick < PICKMAX && n > 2) {
			strlcpy_(pick_id[npick], f[1], sizeof(pick_id[0]));
			strlcpy_(pick_label[npick], f[2], sizeof(pick_label[0]));
			npick++;
		}
		break;
	case 'W':			/* show the picker */
		show_picker();
		break;
	case 'E':			/* end of list */
		dirty |= D_SIDEBAR;
		break;
	case 'C':			/* C <id> : current chat (for highlight) */
		if (strcmp(cur_id, n > 1 ? f[1] : "")) {
			strlcpy_(cur_id, n > 1 ? f[1] : "", sizeof(cur_id));
			dirty |= D_SIDEBAR;
		}
		break;
	case 'T':			/* T <title> */
		if (strcmp(chat_title, n > 1 ? f[1] : "")) {
			strlcpy_(chat_title, n > 1 ? f[1] : "", sizeof(chat_title));
			dirty |= D_TITLE;
		}
		break;
	case 'R':			/* reset conversation pane */
		clear_text();
		break;
	case 'M':			/* M <role> : begin message */
		begin_message(n > 1 && f[1][0] ? f[1][0] : 'A');
		break;
	case 'P':			/* P <text> : append text */
		if (n > 1)
			append(f[1], strlen(f[1]));
		break;
	case 'H': {			/* H <text> : bold heading paragraph */
		char m = MK_BOLD;
		if (tlen > 0 && tb[tlen - 1] != '\n')
			append("\n", 1);
		append(&m, 1);
		if (n > 1)
			append(f[1], strlen(f[1]));
		append("\n", 1);
		break;
	}
	case 'B':			/* line break */
		append("\n", 1);
		break;
	case 'Z':			/* end of message */
		if (tlen > 0 && tb[tlen - 1] != '\n')
			append("\n", 1);
		break;
	case 'Y':			/* Y 0|1 : busy */
		busy = n > 1 && f[1][0] == '1';
		dirty |= D_TITLE | D_INPUT;
		break;
	case 'A': {			/* A <text> : alert box */
		static char buf[200];
		char *p = buf;
		const char *q = n > 1 ? f[1] : "";
		const char *pre = "[1][", *post = "][ OK ]";
		while (*pre)
			*p++ = *pre++;
		while (*q && p < buf + 180)
			*p++ = (*q == '[' || *q == ']') ? '|' : *q, q++;
		while (*post)
			*p++ = *post++;
		*p = 0;
		flush_dirty();
		form_alert(1, buf);
		break;
	}
	}
}

static void rx_byte(u8 c)
{
	if (c == '\n') {
		rxline[rxlen] = 0;
		handle_line(rxline);
		rxlen = 0;
	} else if (c != '\r') {
		if (rxlen < (short)sizeof(rxline) - 1)
			rxline[rxlen++] = c;
	}
}

static void ip_to_str(u32 ip, char *p)
{
	short i;
	for (i = 3; i >= 0; i--) {
		short v = (ip >> (i * 8)) & 0xff, d = 100;
		short started = 0;
		for (; d; d /= 10) {
			if (v / d || started || d == 1) {
				*p++ = '0' + v / d;
				started = 1;
			}
			v %= d;
		}
		if (i)
			*p++ = '.';
	}
	*p = 0;
}

static void tcp_connect(void)
{
	char msg[64];
	short cn, r;

	next_connect = ticks + 375;	/* if this fails, try again in ~15 s */
	if (!sting_init()) {
		set_status("Offline: STinG not loaded");
		return;
	}
	if (!tcp_ip) {
		set_status("Type /connect <gateway IP>");
		return;
	}
	strcpy(msg, "Connecting to ");
	ip_to_str(tcp_ip, msg + 14);
	set_status(msg);
	flush_dirty();
	graf_mouse(BUSYBEE, 0);
	cn = sting_open(tcp_ip, tcp_port);
	if (cn >= 0) {
		r = sting_wait_established(cn, 8);
		if (r < 0) {
			sting_close(cn);
			cn = r;
		}
	}
	graf_mouse(ARROW, 0);
	if (cn < 0) {
		strcpy(msg, "Offline: ");
		strlcpy_(msg + 9, sting_error(cn), sizeof(msg) - 9);
		set_status(msg);
		return;
	}
	tcp_cn = cn;
	rxlen = 0;
	set_status("Connected, loading...");
	send_hello();
}

static void poll_link(void)
{
	static char buf[512];
	short budget;

	if (link == LINK_SERIAL) {
		budget = 3000;
		while (budget-- > 0 && Bconstat(DEV_AUX))
			rx_byte((u8)Bconin(DEV_AUX));
		return;
	}
	if (tcp_cn < 0) {
		if (ticks >= next_connect)
			tcp_connect();
		return;
	}
	for (budget = 8; budget > 0 && tcp_cn >= 0; budget--) {
		short i, n = sting_count(tcp_cn);
		if (n == E_NODATA || n == 0)
			break;
		if (n < 0) {
			tcp_lost(n);
			break;
		}
		if (n > (short)sizeof(buf))
			n = sizeof(buf);
		n = sting_read(tcp_cn, buf, n);
		if (n < 0) {
			tcp_lost(n);
			break;
		}
		for (i = 0; i < n; i++)
			rx_byte((u8)buf[i]);
	}
}

/* ---- settings: CLAUDE.INF next to the program ---- */

static short parse_ip(const char *s, u32 *ip, u16 *port)
{
	u32 v = 0;
	short parts = 0, n = -1;
	for (;; s++) {
		if (*s >= '0' && *s <= '9') {
			n = (n < 0 ? 0 : n * 10) + (*s - '0');
			if (n > 255)
				return 0;
		} else if (*s == '.' || *s == ':' || *s == ' ' || *s == 0 || *s == '\r' || *s == '\n') {
			if (n < 0)
				return 0;
			v = (v << 8) | n;
			n = -1;
			if (++parts == 4)
				break;
			if (*s != '.')
				return 0;
		} else {
			return 0;
		}
	}
	*ip = v;
	if ((*s == ':' || *s == ' ') && port) {
		u16 p = 0;
		for (s++; *s >= '0' && *s <= '9'; s++)
			p = p * 10 + (*s - '0');
		if (p)
			*port = p;
	}
	return 1;
}

static void parse_config_line(const char *l)
{
	if (!memcmp(l, "tcp ", 4) && parse_ip(l + 4, &tcp_ip, &tcp_port))
		link = LINK_TCP;
	else if (!memcmp(l, "serial", 6))
		link = LINK_SERIAL;
	else if (!memcmp(l, "sidebar ", 8)) {
		short v = 0;
		for (l += 8; *l >= '0' && *l <= '9'; l++)
			v = v * 10 + (*l - '0');
		sb_user = v;
	}
}

static void load_config(void)
{
	char buf[256], *p, *l;
	long fd = Fopen("CLAUDE.INF", 0);
	long n;
	if (fd < 0)
		return;
	n = Fread((short)fd, sizeof(buf) - 1, buf);
	Fclose((short)fd);
	if (n <= 0)
		return;
	buf[n] = 0;
	for (l = p = buf; ; p++) {
		if (*p == '\r' || *p == '\n' || *p == 0) {
			char end = *p;
			*p = 0;
			if (p > l)
				parse_config_line(l);
			if (!end)
				break;
			l = p + 1;
		}
	}
}

static char *put_num(char *p, u16 v)
{
	short d = 10000, started = 0;
	for (; d; d /= 10) {
		if (v / d || started || d == 1) {
			*p++ = '0' + v / d;
			started = 1;
		}
		v %= d;
	}
	return p;
}

static void save_config(void)
{
	char text[96], *p = text;
	long fd = Fcreate("CLAUDE.INF", 0);
	if (fd < 0)
		return;
	if (link == LINK_TCP && tcp_ip) {
		strcpy(p, "tcp ");
		ip_to_str(tcp_ip, p + 4);
		p += strlen(p);
		*p++ = ' ';
		p = put_num(p, tcp_port);
	} else {
		strcpy(p, "serial");
		p += 6;
	}
	*p++ = '\r';
	*p++ = '\n';
	if (sb_user) {
		strcpy(p, "sidebar ");
		p = put_num(p + 8, sb_user);
		*p++ = '\r';
		*p++ = '\n';
	}
	*p = 0;
	Fwrite((short)fd, strlen(text), text);
	Fclose((short)fd);
}

static void use_link(short l)
{
	if (link == LINK_TCP && tcp_cn >= 0)
		sting_close(tcp_cn);
	tcp_cn = -1;
	link = l;
	online = 0;
	rxlen = 0;
	if (link == LINK_SERIAL) {
		serial_open();
		set_status("Waiting for bridge...");
		send_hello();
	} else {
		serial_close();
		next_connect = 0;	/* connect on the next tick */
	}
	menu_check_baud();
	save_config();
}

/* ------------------------------------------------------------------ */
/* user actions                                                        */
/* ------------------------------------------------------------------ */

static void set_window_name(void)
{
	static char name[] = " Claude ";
	wind_set_str(win, WF_NAME, name);
}

static void do_new_chat(void)
{
	search_mode = 0; rename_mode = 0;
	cur_id[0] = 0;
	strcpy(chat_title, "New chat");
	clear_text();
	begin_message('I');
	append("How can I help you today?", 25);
	tx_cmd("NEW", 0, 0);
	dirty |= D_ALL;
}

static void do_search(void)
{
	search_mode = 1; rename_mode = 0;
	inlen = 0;
	input[0] = 0;
	dirty |= D_INPUT;
}

static void do_list(const char *kind)
{
	search_mode = 0; rename_mode = 0;
	tx_cmd("LIST", kind, 0);
	dirty |= D_INPUT;
}

static void submit(void)
{
	input[inlen] = 0;
	if (rename_mode) {
		rename_mode = 0;
		if (inlen > 0)
			tx_cmd4("RENAME", rn_kind, rn_id, input);
	} else if (!search_mode && !memcmp(input, "/connect ", 9)) {
		u32 ip;
		u16 port = tcp_port;
		if (parse_ip(input + 9, &ip, &port)) {
			tcp_ip = ip;
			tcp_port = port;
			use_link(LINK_TCP);
		} else {
			form_alert(1, "[1][Use: /connect 192.168.1.10|or /connect 192.168.1.10:2323][ OK ]");
			return;
		}
	} else if (!search_mode && !strcmp(input, "/serial")) {
		use_link(LINK_SERIAL);
	} else if (search_mode) {
		tx_cmd("FIND", input, 0);
		search_mode = 0; rename_mode = 0;
	} else if (inlen > 0) {
		tx_cmd("SEND", input, 0);
	} else {
		return;
	}
	inlen = 0;
	input[0] = 0;
	dirty |= D_INPUT;
}

static void open_item(short i)
{
	ITEM *it = &items[i];
	const char *kind = "CHAT";
	if (!strcmp(list_kind, "PROJECTS"))
		kind = "PROJECT";
	else if (!strcmp(list_kind, "ARTIFACTS"))
		kind = "ARTIFACT";
	if (!strcmp(it->id, "..")) {
		do_list("PROJECTS");
		return;
	}
	tx_cmd("OPEN", kind, it->id);
	if (!strcmp(kind, "CHAT")) {
		strlcpy_(cur_id, it->id, sizeof(cur_id));
		strlcpy_(chat_title, it->label, sizeof(chat_title));
		clear_text();
		begin_message('I');
		append("Loading...", 10);
		dirty |= D_SIDEBAR | D_TITLE;
	}
}

static void scroll_text(short delta)
{
	short old = top_line;
	top_line += delta;
	clamp_top();
	if (top_line != old) {
		text_dirty_row = 0;
		dirty |= D_TEXT;
	}
}

static void scroll_list(short delta)
{
	short old = list_top;
	list_top += delta;
	if (list_top > nitems - list_rows)
		list_top = nitems - list_rows;
	if (list_top < 0)
		list_top = 0;
	if (list_top != old)
		dirty |= D_SIDEBAR;
}

static void about(void)
{
	form_alert(1, "[1][Claude ST " VERSION "|Claude.ai for Atari ST/TT/Falcon.|"
		"Talks to claude_bridge.py|over STinG or serial.][  OK  ]");
}

static void set_baud(short b)
{
	baud = b;
	if (link == LINK_SERIAL) {
		Rsconf(baud, 0, -1, -1, -1, -1);
		send_hello();
	}
	menu_check_baud();
}

static void reconnect(void)
{
	if (link == LINK_TCP) {
		if (tcp_cn >= 0)
			sting_close(tcp_cn);
		tcp_cn = -1;
		online = 0;
		next_connect = 0;
	} else {
		send_hello();
	}
}

/* ------------------------------------------------------------------ */
/* popup menus                                                         */
/* ------------------------------------------------------------------ */

static void wait_release(void)
{
	short mx, my, mb, ks, m[8];
	EVENT e;
	for (;;) {
		graf_mkstate(&mx, &my, &mb, &ks);
		if (!(mb & 3))
			return;
		evnt_multi_(MU_TIMER, 0, 0, 0, 10, m, &e);
	}
}

static short pop_x, pop_y, pop_w, pop_ih;

static void pop_item(const char *const *lab, short i, short on)
{
	short y = pop_y + 2 + i * pop_ih;
	short x1 = pop_x + 1, x2 = pop_x + pop_w - 2;
	if (lab[i][0] == '-') {
		fill(x1, y, x2, y + pop_ih - 1, 0);
		line(x1 + 2, y + pop_ih / 2, x2 - 2, y + pop_ih / 2, 1);
		return;
	}
	fill(x1, y, x2, y + pop_ih - 1, on ? 1 : 0);
	text(pop_x + 2 * cw, y + (pop_ih - ch) / 2, lab[i], strlen(lab[i]), 0, on ? 0 : 1);
}

static short pop_next(const char *const *lab, short n, short from, short d)
{
	short i = from;
	do {
		i += d;
		if (i < 0)
			i = n - 1;
		if (i >= n)
			i = 0;
	} while (lab[i][0] == '-' && i != from);
	return i;
}

/* a GEM-style popup at (x, y); returns the chosen item or -1.
 * Mouse: click an item (or press-drag-release). Keys: arrows, Return, Esc. */
static short popup(short x, short y, const char *const *lab, short n)
{
	short i, w = 0, sel = -1, res = -1, held, moved = 0;
	short mx, my, mb, ks, ox, oy, m[8], clipr[4];
	EVENT e;

	for (i = 0; i < n; i++)
		if ((short)strlen(lab[i]) > w)
			w = strlen(lab[i]);
	pop_ih = row_h;
	pop_w = (w + 4) * cw;
	{
		short bh = n * pop_ih + 4;
		if (x + pop_w + 2 > wx + ww)
			x = wx + ww - pop_w - 2;
		if (x < wx)
			x = wx;
		if (y + bh + 2 > wy + wh)
			y = wy + wh - bh - 2;
		if (y < wy)
			y = wy;
		pop_x = x;
		pop_y = y;

		wind_update(BEG_UPDATE);
		wind_update(3);		/* BEG_MCTRL: we own the mouse */
		form_dial(FMD_START, x, y, pop_w + 3, bh + 3);
		graf_mouse(M_OFF, 0);
		vswr_mode(vh, 2);
		vsf_perimeter(vh, 0);
		vst_alignment(vh, 0, 5);
		clipr[0] = 0;
		clipr[1] = 0;
		clipr[2] = scr_w - 1;
		clipr[3] = scr_h - 1;
		vs_clip(vh, 1, clipr);
		fill(x + 2, y + 2, x + pop_w + 2, y + bh + 2, 1);	/* shadow */
		fill(x, y, x + pop_w - 1, y + bh - 1, 0);
		line(x, y, x + pop_w - 1, y, 1);
		line(x + pop_w - 1, y, x + pop_w - 1, y + bh - 1, 1);
		line(x + pop_w - 1, y + bh - 1, x, y + bh - 1, 1);
		line(x, y + bh - 1, x, y, 1);
		for (i = 0; i < n; i++)
			pop_item(lab, i, 0);
		graf_mouse(M_ON, 0);
	}

	graf_mkstate(&ox, &oy, &held, &ks);
	held &= 3;
	for (;;) {
		short hit = -1, nsel, ev_x, ev_y;
		/* button events come from the AES queue, so even a very quick
		 * click is never missed; while the opening button is still held
		 * we wait for its release instead */
		evnt_multi_(MU_KEYBD | MU_BUTTON | MU_TIMER,
			    held ? 1 : 0x101, 3, 0, 20, m, &e);
		graf_mkstate(&mx, &my, &mb, &ks);
		if (e.which & MU_BUTTON) {
			ev_x = e.mx;
			ev_y = e.my;
		} else {
			ev_x = mx;
			ev_y = my;
		}
		if (ev_x - ox > 3 || ox - ev_x > 3 || ev_y - oy > 3 || oy - ev_y > 3)
			moved = 1;
		if (ev_x >= pop_x && ev_x < pop_x + pop_w && ev_y >= pop_y + 2 &&
		    ev_y < pop_y + 2 + n * pop_ih) {
			hit = (ev_y - pop_y - 2) / pop_ih;
			if (lab[hit][0] == '-')
				hit = -1;
		}
		nsel = sel;
		if (e.which & MU_KEYBD) {
			u8 sc = e.kreturn >> 8, as = e.kreturn & 0xff;
			if (as == 0x1b || sc == 0x61)
				break;
			if (sc == 0x48)
				nsel = pop_next(lab, n, sel < 0 ? 0 : sel, -1);
			else if (sc == 0x50)
				nsel = pop_next(lab, n, sel < 0 ? n - 1 : sel, 1);
			else if ((as == 0x0d || as == ' ') && sel >= 0) {
				res = sel;
				break;
			}
		} else if (moved) {
			nsel = hit;
		}
		if (nsel != sel) {
			graf_mouse(M_OFF, 0);
			if (sel >= 0)
				pop_item(lab, sel, 0);
			if (nsel >= 0)
				pop_item(lab, nsel, 1);
			graf_mouse(M_ON, 0);
			sel = nsel;
		}
		if (!(e.which & MU_BUTTON))
			continue;
		if (held) {				/* the opening button came up */
			held = 0;
			if (moved && hit >= 0) {	/* press, drag, release */
				res = hit;
				break;
			}
		} else {				/* click: an item, or outside to cancel */
			res = hit;
			break;
		}
	}
	vs_clip(vh, 0, clipr);
	wind_update(2);		/* END_MCTRL */
	wind_update(END_UPDATE);
	wait_release();
	/* repaint our part now, and let the AES repaint anything else covered */
	{
		short saved = text_dirty_row;
		text_dirty_row = 0;
		redraw(D_ALL, pop_x, pop_y, pop_w + 3, n * pop_ih + 4 + 3);
		text_dirty_row = saved;
	}
	form_dial(FMD_FINISH, pop_x, pop_y, pop_w + 3, n * pop_ih + 4 + 3);
	return res;
}

static void start_rename(const char *kind, ITEM *it)
{
	strlcpy_(rn_kind, kind, sizeof(rn_kind));
	strlcpy_(rn_id, it->id, sizeof(rn_id));
	strlcpy_(input, it->label, sizeof(input));
	inlen = strlen(input);
	rename_mode = 1;
	search_mode = 0;
	dirty |= D_INPUT;
}

static void open_item(short i);

/* right-click menu for a sidebar item, like the one on claude.ai */
static void context_menu(short i, short x, short y)
{
	static const char *chat_menu[6];
	static const char *proj_menu[6];
	static const char *art_menu[1] = { "Open" };
	ITEM *it = &items[i];
	short r;

	if (!strcmp(it->id, ".."))
		return;
	if (!strcmp(list_kind, "ARTIFACTS")) {
		if (popup(x, y, art_menu, 1) == 0)
			open_item(i);
		return;
	}
	if (!strcmp(list_kind, "PROJECTS")) {
		proj_menu[0] = "Open";
		proj_menu[1] = it->pinned ? "Unpin" : "Pin";
		proj_menu[2] = "Rename...";
		proj_menu[3] = "Archive";
		proj_menu[4] = "-";
		proj_menu[5] = "Delete...";
		r = popup(x, y, proj_menu, 6);
		switch (r) {
		case 0: open_item(i); break;
		case 1: tx_cmd4("PIN", "PROJECT", it->id, it->pinned ? "0" : "1"); break;
		case 2: start_rename("PROJECT", it); break;
		case 3: tx_cmd("ARCHIVE", "PROJECT", it->id); break;
		case 5:
			if (form_alert(1, "[3][Delete this project?|This can't be undone.][Cancel|Delete]") == 2)
				tx_cmd("DELETE", "PROJECT", it->id);
			break;
		}
		return;
	}
	chat_menu[0] = "Open";
	chat_menu[1] = it->pinned ? "Unpin" : "Pin";
	chat_menu[2] = "Rename...";
	chat_menu[3] = "Move to project...";
	chat_menu[4] = "-";
	chat_menu[5] = "Delete...";
	r = popup(x, y, chat_menu, 6);
	switch (r) {
	case 0: open_item(i); break;
	case 1: tx_cmd4("PIN", "CHAT", it->id, it->pinned ? "0" : "1"); break;
	case 2: start_rename("CHAT", it); break;
	case 3:
		strlcpy_(pick_chat, it->id, sizeof(pick_chat));
		pick_x = x;
		pick_y = y;
		tx_cmd("PICKPROJ", it->id, 0);
		break;
	case 5:
		if (form_alert(1, "[3][Delete this chat?|This can't be undone.][Cancel|Delete]") == 2)
			tx_cmd("DELETE", "CHAT", it->id);
		break;
	}
}

static void show_picker(void)
{
	const char *lab[PICKMAX + 1];
	short i, r;
	if (!pick_chat[0])
		return;
	if (npick == 0) {
		form_alert(1, "[1][You have no projects yet.][ OK ]");
		return;
	}
	for (i = 0; i < npick; i++)
		lab[i] = pick_label[i];
	flush_dirty();
	r = popup(pick_x, pick_y, lab, npick);
	if (r >= 0)
		tx_cmd("MOVE", pick_chat, pick_id[r]);
	pick_chat[0] = 0;
}

static short list_hit(short mx, short my)
{
	short r;
	if (mx < wx || mx >= wx + sb_w || my < list_y + row_h || my >= status_y - 2)
		return -1;
	r = (my - list_y - row_h) / row_h;
	if (r >= list_rows || list_top + r >= nitems)
		return -1;
	return list_top + r;
}

static void handle_rclick(short mx, short my)
{
	short i = list_hit(mx, my);
	if (i >= 0)
		context_menu(i, mx, my);
}

/* ------------------------------------------------------------------ */
/* the draggable divider between sidebar and conversation              */
/* ------------------------------------------------------------------ */

static short divider_x(void)
{
	return wx + sb_w;
}

static void set_divider_watch(void)
{
	evnt_set_m1(over_divider, divider_x() - 2, wy, 5, wh);
}

static void xor_divider(short x)
{
	short p[4];
	p[0] = x;
	p[1] = wy;
	p[2] = x;
	p[3] = wy + wh - 1;
	v_pline(vh, 2, p);
	p[0] = p[2] = x + 1;
	v_pline(vh, 2, p);
}

static void on_resize(void);

static void drag_divider(void)
{
	short mx, my, mb, ks, x, last = -1, m[8], clipr[4];
	short minx = wx + 14 * cw, maxx = wx + ww - 34 * cw;
	EVENT e;

	wind_update(BEG_UPDATE);
	wind_update(3);
	graf_mouse(FLAT_HAND, 0);
	clipr[0] = wx;
	clipr[1] = wy;
	clipr[2] = wx + ww - 1;
	clipr[3] = wy + wh - 1;
	vs_clip(vh, 1, clipr);
	vswr_mode(vh, 3);		/* XOR */
	vsl_color(vh, 1);
	do {
		graf_mkstate(&mx, &my, &mb, &ks);
		x = mx < minx ? minx : mx > maxx ? maxx : mx;
		if (x != last) {
			graf_mouse(M_OFF, 0);
			if (last >= 0)
				xor_divider(last);
			xor_divider(x);
			graf_mouse(M_ON, 0);
			last = x;
		}
		evnt_multi_(MU_TIMER, 0, 0, 0, 10, m, &e);
	} while (mb & 1);
	graf_mouse(M_OFF, 0);
	xor_divider(last);
	graf_mouse(M_ON, 0);
	vswr_mode(vh, 2);
	vs_clip(vh, 0, clipr);
	wind_update(2);
	wind_update(END_UPDATE);
	if (last - wx != sb_w) {
		sb_user = last - wx;
		on_resize();
		save_config();
	}
	over_divider = 0;
	graf_mouse(ARROW, 0);
	set_divider_watch();
}

static void handle_click(short mx, short my)
{
	short i;
	if (mx < wx || my < wy || mx >= wx + ww || my >= wy + wh)
		return;
	if (mx >= divider_x() - 2 && mx <= divider_x() + 2) {
		drag_divider();
		return;
	}
	if (mx < wx + sb_w) {
		for (i = 0; i < 5; i++) {
			short y = nav_y + i * row_h;
			if (my >= y && my < y + row_h) {
				switch (i) {
				case 0: do_new_chat(); break;
				case 1: do_search(); break;
				case 2: do_list("CHATS"); break;
				case 3: do_list("PROJECTS"); break;
				case 4: do_list("ARTIFACTS"); break;
				}
				return;
			}
		}
		if (my >= list_y && my < list_y + row_h) {
			if (mx >= wx + sb_w - 4 * cw && mx < wx + sb_w - 2 * cw)
				scroll_list(-(list_rows - 1));
			else if (mx >= wx + sb_w - 2 * cw)
				scroll_list(list_rows - 1);
			return;
		}
		if (my >= list_y + row_h && my < status_y - 2) {
			i = list_top + (my - list_y - row_h) / row_h;
			if (i < nitems && (my - list_y - row_h) / row_h < list_rows)
				open_item(i);
		}
		return;
	}
	if (my >= ty && my < ty + th) {
		/* click in upper/lower half of the text pages it */
		scroll_text(my < ty + th / 2 ? -(rows - 1) : rows - 1);
	}
}

static void move_cursor(short d)
{
	if (nitems == 0)
		return;
	lcur = lcur < 0 ? (d > 0 ? list_top : list_top + list_rows - 1) : lcur + d;
	if (lcur >= nitems)
		lcur = nitems - 1;
	if (lcur < 0)
		lcur = 0;
	if (lcur < list_top)
		list_top = lcur;
	if (lcur >= list_top + list_rows)
		list_top = lcur - list_rows + 1;
	dirty |= D_SIDEBAR;
}

static void handle_key(short kstate, short kr)
{
	u8 ascii = kr & 0xff;
	u8 scan = (kr >> 8) & 0xff;
	short shift = kstate & (K_LSHIFT | K_RSHIFT);

	switch (scan) {
	case 0x48: scroll_text(shift ? -(rows - 1) : -1); return;	/* up */
	case 0x50: scroll_text(shift ? rows - 1 : 1); return;		/* down */
	case 0x47: scroll_text(shift ? 32000 : -32000); return;		/* Clr/Home */
	case 0x62: about(); return;					/* Help */
	case 0x61: inlen = 0; search_mode = 0; rename_mode = 0; dirty |= D_INPUT; return; /* Undo */
	case 0x52:						/* Insert: item menu */
		{
			short i = lcur;
			if (i < 0)
				for (i = 0; i < nitems && strcmp(items[i].id, cur_id); i++)
					;
			if (i >= 0 && i < nitems && i >= list_top && i < list_top + list_rows)
				context_menu(i, wx + sb_w / 2, list_y + (i - list_top + 1) * row_h + row_h / 2);
		}
		return;
	case 0x0f: move_cursor(shift ? -1 : 1); return;			/* Tab */
	case 0x3b: do_new_chat(); return;				/* F1 */
	case 0x3c: do_list("CHATS"); return;				/* F2 */
	case 0x3d: do_list("PROJECTS"); return;				/* F3 */
	case 0x3e: do_list("ARTIFACTS"); return;			/* F4 */
	case 0x3f: do_search(); return;					/* F5 */
	}
	switch (ascii) {
	case 0x0e: do_new_chat(); return;	/* ^N */
	case 0x06: do_search(); return;		/* ^F */
	case 0x12: reconnect(); return;		/* ^R */
	case 0x11: quit = 1; return;		/* ^Q */
	case 0x0d:				/* Return / Enter */
		if (inlen == 0 && !search_mode && !rename_mode && lcur >= 0 && lcur < nitems) {
			short i = lcur;
			lcur = -1;
			dirty |= D_SIDEBAR;
			open_item(i);
		} else {
			submit();
		}
		return;
	case 0x08:				/* Backspace */
		if (inlen > 0) {
			inlen--;
			dirty |= D_INPUT;
		}
		return;
	case 0x1b:				/* Esc */
		if (lcur >= 0) {
			lcur = -1;
			dirty |= D_SIDEBAR;
		}
		inlen = 0;
		search_mode = 0; rename_mode = 0;
		dirty |= D_INPUT;
		return;
	}
	if (ascii >= 32 && ascii != 127 && inlen < INMAX) {
		input[inlen++] = ascii;
		dirty |= D_INPUT;
	}
}

/* ------------------------------------------------------------------ */
/* window management                                                   */
/* ------------------------------------------------------------------ */

#define WKIND (NAME | CLOSER | FULLER | MOVER | SIZER | UPARROW | DNARROW | VSLIDE)

static void on_resize(void)
{
	short at_end;
	wind_get(win, WF_WORKXYWH, &wx, &wy, &ww, &wh);
	at_end = at_bottom();
	layout();
	rewrap_all();
	if (at_end)
		top_line = nlines - rows;
	clamp_top();
	last_slider_pos = last_slider_size = -1;
	update_slider();
	set_divider_watch();
	dirty |= D_ALL;
}

static void set_curr(short x, short y, short w, short h)
{
	short minw = 48 * cw, minh = 14 * ch;
	if (w < minw)
		w = minw;
	if (h < minh)
		h = minh;
	wind_set(win, WF_CURRXYWH, x, y, w, h);
	on_resize();
}

static void handle_msg(short *msg)
{
	switch (msg[0]) {
	case MN_SELECTED:
		switch (msg[4]) {
		case MN_ABOUT: about(); break;
		case MN_NEW: do_new_chat(); break;
		case MN_FIND: do_search(); break;
		case MN_REF: reconnect(); break;
		case MN_NET:
			if (tcp_ip)
				use_link(LINK_TCP);
			else
				form_alert(1, "[1][Type /connect and the IP|address of your gateway|in the reply line, e.g.|/connect 192.168.1.10][ OK ]");
			break;
		case MN_SER: use_link(LINK_SERIAL); break;
		case MN_QUIT: quit = 1; break;
		case MN_B48: set_baud(BAUD_4800); break;
		case MN_B96: set_baud(BAUD_9600); break;
		case MN_B192: set_baud(BAUD_19200); break;
		}
		menu_tnormal(menu, msg[3], 1);
		break;
	case WM_REDRAW:
		if (msg[3] == win) {
			short saved = text_dirty_row;
			text_dirty_row = 0;
			redraw(D_ALL, msg[4], msg[5], msg[6], msg[7]);
			text_dirty_row = saved;
			if (pending_scroll) {
				/* the screen no longer matches the pre-scroll state */
				pending_scroll = 0;
				text_dirty_row = 0;
				dirty |= D_TEXT;
			}
		}
		break;
	case WM_TOPPED:
		wind_set(win, WF_TOP, 0, 0, 0, 0);
		break;
	case WM_CLOSED:
	case AP_TERM:
		quit = 1;
		break;
	case WM_FULLED: {
		short cx, cy, cw_, ch_, fx, fy, fw, fh;
		wind_get(win, WF_CURRXYWH, &cx, &cy, &cw_, &ch_);
		wind_get(win, WF_FULLXYWH, &fx, &fy, &fw, &fh);
		if (cx == fx && cy == fy && cw_ == fw && ch_ == fh)
			wind_get(win, WF_PREVXYWH, &fx, &fy, &fw, &fh);
		set_curr(fx, fy, fw, fh);
		break;
	}
	case WM_SIZED:
	case WM_MOVED:
		set_curr(msg[4], msg[5], msg[6], msg[7]);
		break;
	case WM_ARROWED:
		switch (msg[4]) {
		case 0: scroll_text(-(rows - 1)); break;
		case 1: scroll_text(rows - 1); break;
		case 2: scroll_text(-1); break;
		case 3: scroll_text(1); break;
		}
		break;
	case WM_VSLID:
		if (nlines > rows) {
			short want = (short)((long)msg[4] * (nlines - rows) / 1000);
			scroll_text(want - top_line);
		}
		break;
	}
}

/* ------------------------------------------------------------------ */
/* main                                                                */
/* ------------------------------------------------------------------ */

static short alloc_buffers(void)
{
	long sizes[3] = { 98304L, 49152L, 24576L };
	short i;
	for (i = 0; i < 3; i++) {
		tcap = sizes[i];
		maxlines = (short)(tcap / 16);
		tb = Malloc(tcap + (long)maxlines * sizeof(LINE));
		if ((long)tb > 0) {
			lines = (LINE *)(tb + tcap);
			return 1;
		}
	}
	return 0;
}

int main(void)
{
	short work_out[57], dx, dy, dw, dh, wbox, hbox;
	short msg[8];
	EVENT ev;

	apid = appl_init();
	if (apid < 0)
		return 1;
	phys = graf_handle(&cw, &ch, &wbox, &hbox);
	vh = v_opnvwk_(phys, work_out);
	scr_w = work_out[0] + 1;
	scr_h = work_out[1] + 1;
	ncolors = work_out[13];

	if (!alloc_buffers()) {
		form_alert(1, "[3][Claude ST: not enough memory.][ Quit ]");
		v_clsvwk(vh);
		appl_exit();
		return 1;
	}

	menu_init();
	menu_check_baud();
	menu_bar(menu, 1);
	graf_mouse(ARROW, 0);

	wind_get(0, WF_WORKXYWH, &dx, &dy, &dw, &dh);
	win = wind_create(WKIND, dx, dy, dw, dh);
	if (win < 0) {
		form_alert(1, "[3][Claude ST: no window available.][ Quit ]");
		menu_bar(menu, 0);
		v_clsvwk(vh);
		appl_exit();
		return 1;
	}
	set_window_name();
	wind_open(win, dx, dy, dw, dh);
	wind_get(win, WF_WORKXYWH, &wx, &wy, &ww, &wh);
	load_config();
	layout();
	set_divider_watch();

	begin_message('I');
	{
		static const char hi[] = "Welcome to Claude ST. Connecting to your Claude "
			"gateway (claude_bridge.py) over the network or the serial port...";
		append(hi, sizeof(hi) - 1);
	}

	menu_check_baud();
	if (link == LINK_SERIAL) {
		serial_open();
		send_hello();
	}
	dirty = D_ALL;

	while (!quit) {
		/* 0x101/3/0: wake on any button press, left or right */
		evnt_multi_(MU_KEYBD | MU_BUTTON | MU_MESAG | MU_TIMER | MU_M1,
			    0x101, 3, 0, 40, msg, &ev);
		ticks++;
		if (ev.which & MU_MESAG)
			handle_msg(msg);
		if (ev.which & MU_KEYBD)
			handle_key(ev.kstate, ev.kreturn);
		if (ev.which & MU_M1) {
			/* hand cursor over the divider */
			over_divider = !over_divider;
			graf_mouse(over_divider ? FLAT_HAND : ARROW, 0);
			set_divider_watch();
		}
		if (ev.which & MU_BUTTON) {
			if (ev.mbutton & 2)
				handle_rclick(ev.mx, ev.my);
			else
				handle_click(ev.mx, ev.my);
			wait_release();
		}
		poll_link();
		/* keep saying hello until the bridge answers (~every 5s) */
		if (!online && ticks - last_hello > 125 && (link == LINK_SERIAL || tcp_cn >= 0))
			send_hello();
		flush_dirty();
	}

	tx_cmd("BYE", 0, 0);
	if (tcp_cn >= 0)
		sting_close(tcp_cn);
	serial_close();
	wind_close(win);
	wind_delete(win);
	menu_bar(menu, 0);
	v_clsvwk(vh);
	Mfree(tb);
	appl_exit();
	return 0;
}
