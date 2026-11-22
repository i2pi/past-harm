/* web: the title (title.h) set by the browser in D-DIN -- the same layout as
 * title.c's CoreText version (sizes from the frame height, PAST and HARM
 * stacked by cap height, the credit beneath), drawn by the page into a
 * canvas and handed back as a coverage mask, rows bottom-up. */
#include <emscripten.h>
#include <stdlib.h>
#include "../title.h"

static int				tw, th, tx0, ty0, fw, fh;
static unsigned char	*cov;

EM_JS (int, js_title, (int W, int H, int *out), {
	return Module.titleMask ? Module.titleMask (W, H, out) : 0;
});

void title_build (int W, int H, const char *dir)
{
	int box[5];
	(void) dir;
	free (cov); cov = NULL;
	if (!js_title (W, H, box)) return;
	tx0 = box[0]; ty0 = box[1]; tw = box[2]; th = box[3];
	cov = (unsigned char *) (size_t) box[4];			/* malloc'd by the page */
	fw = W; fh = H;
}

int title_built_for (int W, int H) { return cov && fw == W && fh == H; }
void title_save_background (const unsigned char *img, int W) { (void) img; (void) W; }
void title_draw (unsigned char *img, int W, float dt, double now) { (void) img; (void) W; (void) dt; (void) now; }

int title_info (int *x0, int *y0, int *w, int *h, const unsigned char **coverage)
{
	if (!cov) return 0;
	*x0 = tx0; *y0 = ty0; *w = tw; *h = th; *coverage = cov;
	return 1;
}
