/* web/mktitle: the title set by CoreText (title.c) at a reference size, for
 * the browser, which scales it to the frame (web/shell.html, titleMask):
 *   mktitle out.gray   -> header line "W H tx0 ty0 wm a2 d2 cap" then W*H bytes,
 *                         coverage, rows bottom-up */
#include <stdio.h>
#include "../title.h"

#define HREF 2160

int main (int argc, char **argv)
{
	int x0, y0, w, h;
	const unsigned char *cov;
	double m[4];
	FILE *f;
	if (argc < 2) return 1;
	title_build (HREF * 16 / 9, HREF, PDIR);
	if (!title_info (&x0, &y0, &w, &h, &cov)) { fprintf (stderr, "no title\n"); return 1; }
	title_metrics (m);
	f = fopen (argv[1], "wb");
	if (!f) return 1;
	fprintf (f, "%d %d %d %d %.9f %.9f %.9f %.9f\n", w, h, HREF, x0, m[0], m[1], m[2], m[3]);
	fwrite (cov, 1, (size_t) w * h, f);
	fclose (f);
	return 0;
}
