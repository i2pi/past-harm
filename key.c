#include <math.h>
#include "key.h"

static const float kk_maj[12] = { 6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88 };
static const float kk_min[12] = { 6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17 };

static float corr (const float *a, const float *b)
{
	float ma = 0, mb = 0, sab = 0, saa = 0, sbb = 0;
	int i;
	for (i = 0; i < 12; i++) { ma += a[i]; mb += b[i]; }
	ma /= 12; mb /= 12;
	for (i = 0; i < 12; i++) { sab += (a[i] - ma) * (b[i] - mb); saa += (a[i] - ma) * (a[i] - ma); sbb += (b[i] - mb) * (b[i] - mb); }
	return (saa > 0 && sbb > 0) ? sab / sqrtf (saa * sbb) : 0;
}

void key_init (key_tracker *k) { k->pc = 0; k->minor = 0; k->score = 0; k->hold = 0; }

void key_update (key_tracker *k, const past_state *s, const float *env)
{
	float h[12] = { 0 }, best = -2, prof[12];
	int i, kk, m, pc = 0, minor = 0;
	/* scale[] drives melody and bass, scale2[] the tick chords */
	for (i = 0; i < PAST_SCALE_LEN; i++)
	{
		h[((s->scale[i] % 12) + 12) % 12] += 2.0f;
		h[((s->scale2[i] % 12) + 12) % 12] += 1.0f;
		h[((s->scale1[i] % 12) + 12) % 12] += 0.25f;
	}
	for (i = 0; i < PAST_OSCS; i++)
		if (s->osc[i].is_on) h[((s->osc[i].note % 12) + 12) % 12] += 1.5f * env[i];
	for (m = 0; m < 2; m++)
		for (kk = 0; kk < 12; kk++)
		{
			float c;
			for (i = 0; i < 12; i++) prof[(i + kk) % 12] = (m ? kk_min : kk_maj)[i];
			c = corr (h, prof);
			if (c > best) { best = c; pc = kk; minor = m; }
		}
	if (pc != k->pc || minor != k->minor)
	{
		/* called every 512 samples: ~4 s of persistence, or a clear win */
		if (best > k->score + 0.03f || ++k->hold > 350) { k->pc = pc; k->minor = minor; k->hold = 0; }
	}
	else k->hold = 0;
	k->score = best;
}
