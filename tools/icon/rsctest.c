/*
 * RSCTEST.PRG - loads CLAUDE.RSC with the AES and draws its icon tree,
 * to check the resource file on a real (or emulated) Atari. Any key quits.
 */
#include "../../st/tos.h"
#include "../../st/gem.h"

int main(void)
{
	OBJECT *tree;
	short m[8], d1, d2, d3, d4;
	EVENT e;

	appl_init();
	graf_handle(&d1, &d2, &d3, &d4);
	graf_mouse(ARROW, 0);
	if (!rsrc_load("CLAUDE.RSC")) {
		form_alert(1, "[3][CLAUDE.RSC didn't load.][ Quit ]");
		appl_exit();
		return 1;
	}
	tree = rsrc_tree(0);
	tree[0].ob_x = 100;
	tree[0].ob_y = 100;
	wind_update(BEG_UPDATE);
	graf_mouse(M_OFF, 0);
	objc_draw(tree, 0, 8, 0, 0, 2000, 2000);
	graf_mouse(M_ON, 0);
	wind_update(END_UPDATE);
	evnt_multi_(MU_KEYBD, 0, 0, 0, 0, m, &e);
	appl_exit();
	return 0;
}
