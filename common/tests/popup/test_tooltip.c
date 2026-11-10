/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * tooltip.c on the host (tooltip.h): the delay, what is drawn and where, any event hiding it, BubbleGEM
 * first. The stand-in AES and drawing record the calls (stub.c).
 */
#include <stdio.h>
#include <string.h>
#include "stub.h"
#include "tos.h"
#include "tooltip.h"

static int failed, total;
static void check(int ok, const char *what)
{
	total++;
	if (!ok) {
		failed++;
		printf("FAIL %s\n", what);
	}
}

static const char *tip_is = "Stop (Esc)";
static int calls;
static GRECT repainted;
static int repaints;

static int text_for(short x, short y, char *out, int size)
{
	calls++;
	if (x < 100)			/* nothing under the left of the screen */
		return 0;
	(void)y;
	snprintf(out, (size_t)size, "%s", tip_is);
	return 1;
}
static void repaint(const GRECT *r)
{
	repainted = *r;
	repaints++;
}

static int inside(const GRECT *in, const GRECT *out)
{
	return in->x >= out->x && in->y >= out->y && in->x + in->w <= out->x + out->w && in->y + in->h <= out->y + out->h;
}

static void rest(short x, short y)		/* the pointer moves to (x, y): a move, then it rests */
{
	tip_tick(MU_M1, x, y, text_for);
}

int main(void)
{
	GRECT box;
	int i;

	stub_reset();
	tip_init(repaint);
	check(tip_wait(1000) == 1000, "nothing pending: the app's own wait");
	check(tip_area(0) == 0, "no tip up: tip_area says 0");

	now_ms = 10000;
	rest(300, 100);
	check(tip_wait(1000) == TIP_DELAY, "a pointer just arrived: the tip is due in 700 ms");
	now_ms += 400;
	tip_tick(MU_TIMER, 300, 100, text_for);
	check(ntexts == 0 && calls == 0, "400 ms: nothing yet, the app's function not asked");
	check(tip_wait(1000) == 300 && tip_wait(100) == 100, "tip_wait counts down and never exceeds the app's");
	now_ms += 300;
	tip_tick(MU_TIMER, 300, 100, text_for);
	check(calls == 1 && ntexts == 1 && nfills == 1 && nframes == 1, "700 ms: asked once, drawn once (one text, one fill, one frame)");
	check(!strcmp(texts[0].s, "Stop (Esc)"), "the sentence with its key in brackets");
	box = frames[0];
	check(box.w == (10 + 2) * 8 && box.h == 16 + 6, "(longest line + 2) cells wide, lines * ch + 6 high");
	check(box.x % 8 == 0 && texts[0].x == box.x + 8, "the box and its text on a byte");
	check(box.x >= 300 && box.y == 100 + 16 + 2, "right of the pointer and a line and 2 pixels below it");
	check(nclip_on == 1 && nclip_off == 1 && inside(&box, &clip) && inside(&fills[0], &clip), "all in one clip that holds the box");
	check(clip.w <= box.w + 1 && clip.h <= box.h + 1, "and the clip is the box (+1), not the screen");
	now_ms += 5000;
	tip_tick(MU_TIMER, 300, 100, text_for);
	check(ntexts == 1 && calls == 1, "a timer alone does not draw it again, nor hide it");
	check(tip_wait(1000) == 1000, "shown: no more wake-ups for it");
	{
		GRECT a = {0, 0, 0, 0};
		check(tip_area(&a) == 1 && a.x == box.x && a.y == box.y && a.w >= box.w && a.h >= box.h, "tip_area gives the box's rectangle, frame included, while it is up");
		check(tip_area(0) == 1, "and takes a null pointer");
	}

	tip_tick(MU_KEYBD, 300, 100, text_for);
	check(tip_area(0) == 0, "a key took it away: tip_area says 0");
	check(repaints == 1 && repainted.x == box.x && inside(&box, &repainted) && repainted.w >= box.w, "a key takes it away: the app repaints the box's area");
	now_ms += 2000;
	tip_tick(MU_TIMER, 300, 100, text_for);
	check(ntexts == 1, "after a key it does not come back until the pointer moves");
	check(tip_wait(1000) == 1000, "and nothing is pending");
	rest(310, 100);
	now_ms += TIP_DELAY;
	tip_tick(MU_TIMER, 310, 100, text_for);
	check(ntexts == 2, "a move starts it again");
	tip_tick(MU_BUTTON, 310, 100, text_for);
	check(repaints == 2, "a click takes it away");
	rest(320, 100);
	tip_tick(MU_MESAG, 320, 100, text_for);
	now_ms += TIP_DELAY;
	tip_tick(MU_TIMER, 320, 100, text_for);
	check(ntexts == 3, "a message restarts the wait, and the tip comes after it");
	rest(330, 100);
	check(repaints == 3, "a move takes it away");

	/* no tip here: asked once */
	stub_reset();
	calls = 0;
	rest(10, 10);
	now_ms += TIP_DELAY;
	tip_tick(MU_TIMER, 10, 10, text_for);
	now_ms += TIP_DELAY;
	tip_tick(MU_TIMER, 10, 10, text_for);
	check(calls == 1 && ntexts == 0 && tip_wait(1000) == 1000, "no tip there: the function is asked once, nothing drawn");

	/* the polling app moves the pointer without MU_M1 */
	stub_reset();
	calls = 0;
	tip_tick(MU_TIMER, 400, 100, text_for);
	now_ms += 100;
	tip_tick(MU_TIMER, 410, 100, text_for);
	now_ms += TIP_DELAY - 100;
	tip_tick(MU_TIMER, 410, 100, text_for);
	check(ntexts == 0, "a position that changed between timer ticks is a move: the wait started again");
	now_ms += 100;
	tip_tick(MU_TIMER, 410, 100, text_for);
	check(ntexts == 1, "and the tip comes 700 ms after the move");

	/* iconified: x < 0 */
	stub_reset();
	tip_tick(MU_TIMER, -1, 0, text_for);
	now_ms += 2000;
	tip_tick(MU_TIMER, -1, 0, text_for);
	check(ntexts == 0 && tip_wait(1000) == 1000, "x < 0: no tip, no wake-ups");

	/* the corners: on the desktop */
	stub_reset();
	tip_is = "Connect to one of your sites, or close the connection (^D)";
	rest(630, 395);
	now_ms += TIP_DELAY;
	tip_tick(MU_TIMER, 630, 395, text_for);
	box = frames[0];
	check(box.x >= 0 && box.x + box.w <= 640 && box.y >= 19 && box.y + box.h <= 400, "at the bottom right: left of and above the pointer, on the desktop");
	check(box.x % 8 == 0, "still on a byte");

	/* two lines, the second dim; more than 60 characters cut */
	stub_reset();
	tip_is = "Sync|2 contacts changed, 0 photos fetched in 12 s, and then something that makes this line much much longer than sixty characters";
	rest(200, 100);
	now_ms += TIP_DELAY;
	tip_tick(MU_TIMER, 200, 100, text_for);
	box = frames[0];
	check(ntexts == 2 && !strcmp(texts[0].s, "Sync") && (texts[1].flags & TX_DIM) && !(texts[0].flags & TX_DIM), "two lines: the second dim");
	check(texts[1].n == TIP_LINE && box.w == (TIP_LINE + 2) * 8 && box.h == 2 * 16 + 6, "a line is cut at 60 characters; the box has two lines");
	check(texts[1].y == texts[0].y + 16, "the second line a text line below the first");

	/* BubbleGEM first */
	stub_reset();
	tip_is = "Stop (Esc)";
	bubble_id = 5;
	rest(250, 120);
	now_ms += TIP_DELAY;
	tip_tick(MU_TIMER, 250, 120, text_for);
	check(nsent == 1 && ntexts == 0 && nfills == 0, "BubbleGEM running: it is asked, nothing drawn");
	check((unsigned short)sent[0] == 0xBABB && sent[1] == 7 && sent[3] == 250 && sent[4] == 120, "BUBBLEGEM_SHOW from us, the pointer's place in it");
	check(!strcmp(shared_buf, "Stop (Esc)") || !strcmp(shared_buf + 128, "Stop (Esc)"), "the text is in the shared memory");
	{
		unsigned long a = ((unsigned long)(unsigned short)sent[5] << 16) | (unsigned short)sent[6];
		check(a == ((unsigned long)shared_buf & 0xffffffffUL) || a == (((unsigned long)shared_buf + 128) & 0xffffffffUL),
		      "its address is in msg[5] and msg[6]");
	}
	tip_is = "Two|lines";
	rest(260, 120);
	now_ms += TIP_DELAY;
	tip_tick(MU_TIMER, 260, 120, text_for);
	check(nsent == 2 && (!strcmp(shared_buf, "Two\nlines") || !strcmp(shared_buf + 128, "Two\nlines")), "a '|' goes to BubbleGEM as a line break, in the other of two buffers");
	i = repaints;
	rest(270, 120);
	check(repaints == i, "nothing of ours to take away");
	bubble_id = -1;

	/* tip_hide with no repaint function: the AES is asked */
	stub_reset();
	tip_init(0);
	rest(200, 100);
	now_ms += TIP_DELAY;
	tip_tick(MU_TIMER, 200, 100, text_for);
	i = nforms;
	tip_hide();
	check(nforms == i + 1, "without a repaint function, form_dial(FMD_FINISH) asks the AES");
	tip_hide();
	check(nforms == i + 1, "hiding twice does nothing");

	printf("%d checks, %d failed\n", total, failed);
	return failed != 0;
}
