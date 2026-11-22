/* musiclog: run the music (as tools/render -mix does: engine + key tracker)
 * and log what it plays, for the musicians' page (tools/mkmusicians.py).
 *
 *   tools/musiclog [-noharmony] from_s to_s > events.json
 *
 * One JSON object per line:
 *   {"n": t, j, note, dur, amp, r, choir, solo}     a note starts (t in samples)
 *   {"d": t, k, f, lvl}                              a polyrhythm drum hit (f in Hz)
 *   {"bar": b, key, minor, mood, arc, voices, genes}  the state at each bar
 *   {"scale": t, s0, s1, s2}                         past's scale tables changed
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../past_engine.h"
#include "../key.h"

static past_state s;
static key_tracker kt;

static void arr (const int *a, int n)
{
	int i;
	putchar ('[');
	for (i = 0; i < n; i++) printf ("%s%d", i ? "," : "", a[i]);
	putchar (']');
}

int main (int argc, char **argv)
{
	int harmony = 1, a = 1, j, since = 0, was_on[PAST_OSCS] = { 0 }, last_bar = -1;
	float env[PAST_OSCS] = { 0 }, penv[PAST_POLY] = { 0 };
	int sc[3][PAST_SCALE_LEN];
	double from, to;
	short l, r;
	if (argc > 1 && !strcmp (argv[1], "-noharmony")) { harmony = 0; a = 2; }
	if (argc < a + 2) { fprintf (stderr, "usage: musiclog [-noharmony] from_s to_s\n"); return 1; }
	from = atof (argv[a]); to = atof (argv[a + 1]);
	past_init (&s);
	key_init (&kt);
	s.arc = s.choir = s.poly = s.endless = 1;
	s.harmony = harmony;
	past_prepare_choir ();
	memset (sc, 0, sizeof sc);
	while ((unsigned long long) s.i < (unsigned long) (to * PAST_RATE) && past_next (&s, &l, &r))
	{
		int in = (unsigned long long) s.i >= (unsigned long) (from * PAST_RATE);
		/* the key tracker, as in tools/render.c */
		for (j = 0; j < PAST_OSCS; j++) if (s.osc[j].env > env[j] || since == 0) env[j] = s.osc[j].env;
		if (++since >= 512) { key_update (&kt, &s, env); s.key_pc = kt.pc; s.key_minor = kt.minor; since = 0; }
		for (j = 0; j < PAST_OSCS; j++)
		{
			if (s.osc[j].is_on && !was_on[j] && in)
				printf ("{\"n\":%llu,\"j\":%d,\"note\":%d,\"dur\":%.3f,\"amp\":%.3f,\"r\":%.3f,\"choir\":%d,\"solo\":%d}\n",
					(unsigned long long) s.i, j, s.osc[j].note, s.osc[j].dur, s.osc[j].amp, s.osc[j].r, s.osc[j].choir, s.osc[j].solo);
			was_on[j] = s.osc[j].is_on;
		}
		for (j = 0; j < PAST_POLY; j++)
		{
			if (s.poly_env[j] > penv[j] + 0.05f && in)
				printf ("{\"d\":%llu,\"k\":%d,\"f\":%.2f,\"lvl\":%.3f}\n", (unsigned long long) s.i, j, s.poly_f[j], s.poly_env[j] * s.poly_g[j] * s.poly_guide[j]);
			penv[j] = s.poly_env[j];
		}
		if (s.bar != last_bar && in)
		{
			last_bar = s.bar;
			printf ("{\"bar\":%d,\"t\":%llu,\"key\":%d,\"minor\":%d,\"mood\":%.3f,\"arc\":%.3f,\"voices\":%d,\"genes\":", s.bar, (unsigned long long) s.i, s.key_pc, s.key_minor, s.mood, s.arc_w, s.voices);
			arr (s.gene, 5);
			printf ("}\n");
		}
		if (in && (memcmp (sc[0], s.scale, sizeof sc[0]) || memcmp (sc[1], s.scale1, sizeof sc[1]) || memcmp (sc[2], s.scale2, sizeof sc[2])))
		{
			memcpy (sc[0], s.scale, sizeof sc[0]); memcpy (sc[1], s.scale1, sizeof sc[1]); memcpy (sc[2], s.scale2, sizeof sc[2]);
			printf ("{\"scale\":%llu,\"s0\":", (unsigned long long) s.i); arr (sc[0], PAST_SCALE_LEN);
			printf (",\"s1\":"); arr (sc[1], PAST_SCALE_LEN);
			printf (",\"s2\":"); arr (sc[2], PAST_SCALE_LEN);
			printf ("}\n");
		}
	}
	return 0;
}
