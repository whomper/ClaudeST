/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/* enlarge.c - the family's enlarge mark (see enlarge.h) */
#include "gemui.h"
#include "enlarge.h"

static void line(short h, short x1, short y1, short x2, short y2)
{
	short p[4];

	p[0] = x1;
	p[1] = y1;
	p[2] = x2;
	p[3] = y2;
	v_pline(h, 2, p);
}

void enlarge_mark(short h, short x, short y)
{
	short p[4], e = ENLARGE_SIZE - 1;

	p[0] = x;
	p[1] = y;
	p[2] = (short)(x + e);
	p[3] = (short)(y + e);
	vswr_mode(h, MD_REPLACE);
	vsf_interior(h, FIS_SOLID);
	vsf_perimeter(h, 0);
	vsf_color(h, 0);
	vr_recfl(h, p);
	vsl_color(h, 1);
	vsl_type(h, 1);
	line(h, x, y, (short)(x + e), y);			/* the edge */
	line(h, (short)(x + e), y, (short)(x + e), (short)(y + e));
	line(h, (short)(x + e), (short)(y + e), x, (short)(y + e));
	line(h, x, (short)(y + e), x, y);
	line(h, (short)(x + 3), (short)(y + 7), (short)(x + 7), (short)(y + 3));	/* the arrow */
	line(h, (short)(x + 5), (short)(y + 3), (short)(x + 7), (short)(y + 3));
	line(h, (short)(x + 7), (short)(y + 3), (short)(x + 7), (short)(y + 5));
	line(h, (short)(x + 3), (short)(y + 5), (short)(x + 3), (short)(y + 7));
	line(h, (short)(x + 3), (short)(y + 7), (short)(x + 5), (short)(y + 7));
}
