/* tools/render: render past (Dec 2003) offline.
 *   render out.wav [seconds]   -> 16-bit stereo WAV (default: the whole 8 min)
 *   render -raw out.raw secs   -> the exact /dev/dsp byte stream (R,L per frame),
 *                                 for comparing with the original program
 *   render -mix out.wav [secs] -> past through harm's effects and the reverb, as p10m1-past plays it
 *   -swing F (anywhere) swings past's step grid (0..0.33) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include "../past_engine.h"
#include "../harm_fx.h"
#include "../key.h"
#include "../reverb.h"

static void put32 (unsigned char *p, uint32_t v) { p[0]=v; p[1]=v>>8; p[2]=v>>16; p[3]=v>>24; }

int main (int argc, char **argv)
{
	static past_state s;
	float swing = 0;
	int raw, mix, a;
	/* -swing F may appear anywhere: take it out first */
	for (a = 1; a + 1 < argc; a++)
		if (!strcmp (argv[a], "-swing"))
		{
			swing = (float) atof (argv[a + 1]);
			for (; a + 2 < argc; a++) argv[a] = argv[a + 2];
			argc -= 2;
			break;
		}
	raw = argc > 1 && !strcmp (argv[1], "-raw");
	mix = argc > 1 && !strcmp (argv[1], "-mix");
	static harm_state hs;
	static key_tracker kt;
	static reverb_state rv;
	float env[PAST_OSCS];
	long since = 0;
	if (mix) { argv++; argc--; harm_init (&hs); key_init (&kt); reverb_init (&rv); }
	const char *out = argv[raw ? 2 : 1];
	double secs = argc > (raw ? 3 : 2) ? atof (argv[raw ? 3 : 2]) : 1e9;
	long want = (long) (secs * PAST_RATE), n = 0;
	FILE *f;
	short l, r;
	unsigned char h[44] = { 'R','I','F','F',0,0,0,0,'W','A','V','E','f','m','t',' ',16,0,0,0,1,0,2,0,
		0x44,0xAC,0,0, 0x10,0xB1,2,0, 4,0,16,0, 'd','a','t','a',0,0,0,0 };
	if (!out) { fprintf (stderr, "usage: render out.wav [seconds] | render -raw out.raw seconds\n"); return 1; }
	f = fopen (out, "wb");
	if (!f) { perror (out); return 1; }
	if (!raw) fwrite (h, 1, 44, f);
	past_init (&s);
	s.swing = (int) lroundf (swing * 8192.0f);
	s.arc = s.harmony = s.choir = s.poly = s.endless = mix;
	if (mix && secs >= 1e9) { secs = 480; want = (long) (secs * PAST_RATE); }	/* endless: default to the first 8 min */
	if (mix) past_prepare_choir ();		/* the app's arc of voices; plain renders stay past's original */
	while (n < want && past_next (&s, &l, &r))
	{
		int copies = 1, c;
		if (mix)
		{
			int j;
			for (j = 0; j < PAST_OSCS; j++) if (s.osc[j].env > env[j] || since == 0) env[j] = s.osc[j].env;
			if (++since >= 512) { key_update (&kt, &s, env); harm_set_key (&hs, kt.pc); s.key_pc = kt.pc; s.key_minor = kt.minor; since = 0; }
			float es = 0;
			for (j = 0; j < PAST_OSCS; j++) if (s.osc[j].is_on) es += s.osc[j].env;
			hs.t = s.i; hs.arc = s.arc_w; hs.ldiv = s.gene[2]; hs.rdiv = s.gene[3]; hs.tdiv = s.gene[4];
			harm_step (&hs, l, r, &l, &r, &copies);
			l = (short) lrintf (32767.0f * tanhf ((l + s.poly_l) / 32767.0f));
			r = (short) lrintf (32767.0f * tanhf ((r + s.poly_r) / 32767.0f));
			rv.mood = s.mood;
			reverb_step (&rv, es, &l, &r);
		}
		for (c = 0; c < copies && n < want; c++)
		{
			if (raw) { fwrite (&r, 2, 1, f); fwrite (&l, 2, 1, f); }	/* past.c's to_buffer order */
			else     { fwrite (&l, 2, 1, f); fwrite (&r, 2, 1, f); }
			n++;
		}
	}
	if (!raw) { put32 (h + 4, 36 + n * 4); put32 (h + 40, n * 4); fseek (f, 0, SEEK_SET); fwrite (h, 1, 44, f); }
	fclose (f);
	fprintf (stderr, "%ld frames (%.1f s)\n", n, n / (double) PAST_RATE);
	return 0;
}
