/* Host-side test for bidi.c: cc -o bidi_test bidi_test.c bidi.c && ./bidi_test */
#include <stdio.h>
#include <string.h>
#include "tos.h"
#include "bidi.h"

/* Atari codes for a few Hebrew letters */
#define ALEF "\xc2"
#define BET  "\xc3"
#define GIMEL "\xc4"
#define DALET "\xc5"
#define SHIN "\xd6"
#define LAMED "\xcd"
#define VAV  "\xc7"
#define FMEM "\xda"	/* final mem */

static int fails;

static void check(const char *name, const char *in, int rtl, const char *want)
{
	char out[BIDI_MAX + 1];
	short n = (short)strlen(in);
	bidi_visual(in, n, rtl, out);
	out[n] = 0;
	if (strcmp(out, want)) {
		printf("FAIL %s\n  got  ", name);
		for (int i = 0; i < n; i++) printf("%02x ", (unsigned char)out[i]);
		printf("\n  want ");
		for (int i = 0; i < n; i++) printf("%02x ", (unsigned char)want[i]);
		printf("\n");
		fails++;
	} else {
		printf("ok   %s\n", name);
	}
}

int main(void)
{
	/* shalom = shin lamed vav final-mem, shown reversed */
	check("pure Hebrew", SHIN LAMED VAV FMEM, 1, FMEM VAV LAMED SHIN);
	check("Hebrew words keep word order RTL",
	      ALEF BET " " GIMEL DALET, 1, DALET GIMEL " " BET ALEF);
	check("number stays LTR in Hebrew", ALEF " 1990 " BET, 1, BET " 1990 " ALEF);
	check("English stays LTR in Hebrew",
	      ALEF " EClaude " BET, 1, BET " EClaude " ALEF);
	check("trailing punctuation goes left", ALEF BET ".", 1, "." BET ALEF);
	check("brackets mirror", "(" ALEF ")", 1, "(" ALEF ")");
	check("Hebrew run inside English",
	      "say " SHIN LAMED VAV FMEM " now", 0, "say " FMEM VAV LAMED SHIN " now");
	check("pure English untouched", "Hello, world!", 0, "Hello, world!");
	check("is_rtl", bidi_is_rtl("  " ALEF "abc", 5) ? "y" : "n", 0, "y");
	check("is_rtl digits are weak", bidi_is_rtl("1 " ALEF, 3) ? "y" : "n", 0, "y");
	check("is_rtl latin first", bidi_is_rtl("a " ALEF, 3) ? "y" : "n", 0, "n");
	{
		/* "AB 12" in RTL: visual "12 BA"; logical 0 (A) is at visual 4 */
		char out[8];
		short pos[8];
		unsigned char odd[8];
		bidi_visual_map(ALEF BET " 12", 5, 1, out, pos, odd);
		if (pos[0] != 4 || pos[1] != 3 || pos[3] != 0 || pos[4] != 1 || !odd[0] || odd[3]) {
			printf("FAIL map %d %d %d %d\n", pos[0], pos[1], pos[3], pos[4]);
			fails++;
		} else {
			printf("ok   logical->visual map\n");
		}
	}
	return fails != 0;
}
