/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * FAKEVIEW.ACC - a stand-in hypertext viewer for the Hatari checks of
 * help.c (from ECal's fakeguide). Installed as EHYPVIEW.ACC, ST-GUIDE.ACC
 * or HYPVIEW.ACC, the AES knows it by that name; what VA_START hands it
 * goes into C:\V_EHYPVI.TXT (the first six letters of the name), so the
 * check sees which viewer got which request.
 */
#include "tos.h"
#include "gem.h"

int main(void)
{
	static const char *const names[] = { "EHYPVIEW", "ST-GUIDE", "HYPVIEW ", "HYP_VIEW" };
	static char file[20] = "C:\\V_XXXXXX.TXT";
	short msg[8], id = appl_init(), i, k;
	AEVENT ev;
	for (i = 0; i < 4; i++)
		if (appl_find(names[i]) == id)
			for (k = 0; k < 6; k++)
				file[5 + k] = names[i][k] == ' ' ? '_' : names[i][k];
	menu_register(id, "  Viewer (test)");
	for (;;) {
		evnt_multi_(MU_MESAG, 0, 0, 0, 0, msg, &ev);
		if (msg[0] == 0x4711) {
			const char *s = (const char *)(((long)msg[3] << 16) | (unsigned short)msg[4]);
			short n = 0, h = (short)Fcreate(file, 0);
			while (s && s[n])
				n++;
			if (h >= 0) {
				Fwrite(h, n, s);
				Fclose(h);
			}
		}
	}
	return 0;
}
