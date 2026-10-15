/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * clipedit.c - ^C ^X ^V on a dialog's edit field (see clip.h), through
 * objc_edit, so the field's length and what it lets in (a port takes
 * digits) hold for a paste as for typing.
 */
#include <string.h>
#include "tos.h"
#include "gem.h"
#include "clip.h"

#define ESC 0x011b			/* objc_edit clears the field on Esc */

int clip_edit(OBJECT *tree, short obj, short *idx, short key)
{
	TEDINFO *t = (TEDINFO *)tree[obj].ob_spec;
	char buf[256], *p;
	switch (clip_key(key)) {
	case CLIP_COPY:
		clip_put(t->te_ptext, (long)strlen(t->te_ptext));
		return 1;
	case CLIP_CUT:
		if (clip_put(t->te_ptext, (long)strlen(t->te_ptext)))
			objc_edit(tree, obj, ESC, idx, ED_CHAR);
		return 1;
	case CLIP_PASTE:
		clip_get(buf, sizeof(buf));
		for (p = clip_line(buf); *p; p++)
			objc_edit(tree, obj, (unsigned char)*p, idx, ED_CHAR);
		return 1;
	}
	return 0;
}
