/* title: "PAST" / "HARM" / "Joshua Reich Generative Works 1996-2026", bottom
 * left, in D-DIN, eaten by the picture.
 *
 * The lines are set once with CoreText into a coverage mask (D-DIN Bold, large,
 * for PAST and HARM; D-DIN for the credit), from the fonts
 * bundled in fonts/ (falling back to an installed D-DIN, then Helvetica).
 * Each text pixel has an integrity that only ever falls: wherever the
 * evolving picture differs from the plain background behind the text (cubes,
 * glow, glass, the lattice), the letters are nibbled away, raggedly, and the
 * damage creeps slowly from bitten edges into the strokes around them. The
 * text is stamped in before the feedback, so as it is eaten its fragments
 * are dragged into the blur and the echoes.
 */
#include <ApplicationServices/ApplicationServices.h>
#include <CoreText/CoreText.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "title.h"

static int				tw, th, tx0, ty0;		/* the text's box in the frame */
static int				fw, fh;					/* the frame size it was built for */
static unsigned char	*cov;					/* coverage, tw x th, rows bottom-up */
static float			*integ, *itmp;			/* integrity, 1 = whole, 0 = eaten */
static unsigned char	*bg;					/* the background behind the box, this frame */
static unsigned int		rng = 0x9e3779b9u;

static float frand (void)
{
	rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
	return (rng & 0xffffff) / 16777216.0f;
}

static CTFontRef load_font (const char *dir, const char *file, const char *name, CGFloat size)
{
	CTFontRef f = NULL;
	char path[1024];
	snprintf (path, sizeof path, "%s/fonts/%s", dir, file);
	{
		CFURLRef url = CFURLCreateFromFileSystemRepresentation (NULL, (const UInt8 *) path, (CFIndex) strlen (path), false);
		CGDataProviderRef dp = url ? CGDataProviderCreateWithURL (url) : NULL;
		CGFontRef cg = dp ? CGFontCreateWithDataProvider (dp) : NULL;
		if (cg) { f = CTFontCreateWithGraphicsFont (cg, size, NULL, NULL); CGFontRelease (cg); }
		if (getenv ("P10M1_DEBUG")) fprintf (stderr, "title font %s: %s\n", path, f ? "loaded" : "not found");
		if (dp) CGDataProviderRelease (dp);
		if (url) CFRelease (url);
	}
	if (!f)
	{
		CFStringRef n = CFStringCreateWithCString (NULL, name, kCFStringEncodingUTF8);
		f = CTFontCreateWithName (n, size, NULL);
		CFRelease (n);
	}
	return f;
}

static CTLineRef make_line (CTFontRef font, const char *text, float kern)
{
	CFStringRef s = CFStringCreateWithCString (NULL, text, kCFStringEncodingUTF8);
	CGColorSpaceRef gs = CGColorSpaceCreateDeviceGray ();
	CGFloat white[2] = { 1, 1 };
	CGColorRef col = CGColorCreate (gs, white);
	CFNumberRef k = CFNumberCreate (NULL, kCFNumberFloatType, &kern);
	CFStringRef keys[3] = { kCTFontAttributeName, kCTForegroundColorAttributeName, kCTKernAttributeName };
	CFTypeRef vals[3] = { font, col, k };
	CFDictionaryRef attr = CFDictionaryCreate (NULL, (const void **) keys, (const void **) vals, 3,
		&kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
	CFAttributedStringRef as = CFAttributedStringCreate (NULL, s, attr);
	CTLineRef line = CTLineCreateWithAttributedString (as);
	CFRelease (as); CFRelease (attr); CFRelease (k); CGColorRelease (col); CGColorSpaceRelease (gs); CFRelease (s);
	return line;
}

void title_build (int W, int H, const char *dir)
{
	CGFloat big = H * 0.14, small = H * 0.024, margin = H * 0.045, gap = H * 0.018, lead = big * 0.02;
	CTFontRef fb = load_font (dir, "D-DIN-Bold.otf", "D-DIN-Bold", big);
	CTFontRef fr = load_font (dir, "D-DIN.otf", "D-DIN", small);
	CTLineRef l0, l1, l2;
	CGContextRef cx;
	CGColorSpaceRef gs;
	CGFloat a0, d0, a1, d1, a2, d2, w0, w1, w2, wm, cap;
	int i;

	if (!fb) fb = CTFontCreateWithName (CFSTR ("Helvetica-Bold"), big, NULL);
	if (!fr) fr = CTFontCreateWithName (CFSTR ("Helvetica"), small, NULL);
	l0 = make_line (fb, "PAST", (float) (big * 0.03));
	l1 = make_line (fb, "HARM", (float) (big * 0.03));
	l2 = make_line (fr, "Joshua Reich Generative Works 1996-2026", (float) (small * 0.04));
	w0 = CTLineGetTypographicBounds (l0, &a0, &d0, NULL);
	w1 = CTLineGetTypographicBounds (l1, &a1, &d1, NULL);
	w2 = CTLineGetTypographicBounds (l2, &a2, &d2, NULL);
	cap = CTFontGetCapHeight (fb);					/* capitals only: stack by cap height */
	wm = w0 > w1 ? w0 : w1; wm = wm > w2 ? wm : w2;

	fw = W; fh = H;
	tx0 = (int) margin; ty0 = (int) margin;
	tw = (int) ceil (wm + 4);
	th = (int) ceil (d2 + a2 + gap + cap + lead + cap + 4);
	if (tx0 + tw > W) tw = W - tx0;
	if (ty0 + th > H) th = H - ty0;
	free (cov); free (integ); free (itmp); free (bg);
	cov = (unsigned char *) calloc ((size_t) tw * th, 1);
	integ = (float *) malloc ((size_t) tw * th * sizeof *integ);
	itmp = (float *) malloc ((size_t) tw * th * sizeof *itmp);
	bg = (unsigned char *) malloc ((size_t) tw * th * 3);
	for (i = 0; i < tw * th; i++) integ[i] = 1.0f;

	/* CoreGraphics' origin is bottom-left, like the frame's rows */
	gs = CGColorSpaceCreateDeviceGray ();
	cx = CGBitmapContextCreate (NULL, (size_t) tw, (size_t) th, 8, (size_t) tw, gs, kCGImageAlphaNone);
	CGContextSetShouldAntialias (cx, true);
	CGContextSetShouldSmoothFonts (cx, false);
	CGContextSetGrayFillColor (cx, 1, 1);
	CGContextSetTextPosition (cx, 2, 2 + d2);
	CTLineDraw (l2, cx);
	CGContextSetTextPosition (cx, 2, 2 + d2 + a2 + gap);				/* HARM's baseline */
	CTLineDraw (l1, cx);
	CGContextSetTextPosition (cx, 2, 2 + d2 + a2 + gap + cap + lead);	/* PAST's */
	CTLineDraw (l0, cx);
	{
		/* the bitmap context's memory is top row first */
		const unsigned char *p = (const unsigned char *) CGBitmapContextGetData (cx);
		size_t bpr = CGBitmapContextGetBytesPerRow (cx);
		int y;
		for (y = 0; y < th; y++) memcpy (cov + (size_t) y * tw, p + (size_t) (th - 1 - y) * bpr, (size_t) tw);
	}
	CGContextRelease (cx); CGColorSpaceRelease (gs);
	CFRelease (l0); CFRelease (l1); CFRelease (l2); CFRelease (fb); CFRelease (fr);
}

void title_save_background (const unsigned char *img, int W)
{
	int y;
	if (!cov) return;
	for (y = 0; y < th; y++)
		memcpy (bg + (size_t) y * tw * 3, img + ((size_t) (ty0 + y) * W + tx0) * 3, (size_t) tw * 3);
}

void title_draw (unsigned char *img, int W, float dt, double now)
{
	/* eating starts gently over the first ~10 s */
	float rate = (float) (now < 10.0 ? now / 10.0 : 1.0) * 0.065f;
	int x, y, c;
	if (!cov) return;
	/* eaten where the picture has moved in over the background */
	for (y = 0; y < th; y++)
		for (x = 0; x < tw; x++)
		{
			size_t i = (size_t) y * tw + x;
			const unsigned char *p = img + ((size_t) (ty0 + y) * W + tx0 + x) * 3, *b = bg + i * 3;
			float d = (abs (p[0] - b[0]) + abs (p[1] - b[1]) + abs (p[2] - b[2])) / 3.0f, act;
			if (integ[i] <= 0) continue;
			act = (d - 18.0f) / 70.0f;
			act = act < 0 ? 0 : act > 1 ? 1 : act;
			if (act > 0) integ[i] -= dt * rate * act * (0.4f + 1.6f * frand ());	/* ragged bites */
		}
	/* the damage creeps from bitten edges into the strokes around them */
	memcpy (itmp, integ, (size_t) tw * th * sizeof *integ);
	for (y = 1; y < th - 1; y++)
		for (x = 1; x < tw - 1; x++)
		{
			size_t i = (size_t) y * tw + x;
			float m = (itmp[i - 1] + itmp[i + 1] + itmp[i - tw] + itmp[i + tw]) * 0.25f;
			if (m < itmp[i]) integ[i] -= dt * 0.5f * (itmp[i] - m) * (0.5f + frand ());
			if (integ[i] < 0) integ[i] = 0;
		}
	/* stamp what is left */
	for (y = 0; y < th; y++)
		for (x = 0; x < tw; x++)
		{
			size_t i = (size_t) y * tw + x;
			int a = (int) (cov[i] * (integ[i] > 0 ? integ[i] : 0) * 0.94f);
			unsigned char *p;
			static const int col[3] = { 242, 239, 231 };
			if (a <= 0) continue;
			p = img + ((size_t) (ty0 + y) * W + tx0 + x) * 3;
			for (c = 0; c < 3; c++) p[c] = (unsigned char) (p[c] + ((col[c] - p[c]) * a) / 255);
		}
}

int title_built_for (int W, int H) { return cov && fw == W && fh == H; }

/* for the GPU pipeline: the text's box and coverage (tw x th, rows bottom-up) */
int title_info (int *x0, int *y0, int *w, int *h, const unsigned char **coverage)
{
	if (!cov) return 0;
	*x0 = tx0; *y0 = ty0; *w = tw; *h = th; *coverage = cov;
	return 1;
}
