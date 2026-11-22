/* reverb: an 8-line feedback delay network (Hadamard mixing, damped lines,
 * no modulation, so nothing wobbles in pitch) behind four allpass diffusers
 * per side. Its send level and tail length follow past's sparseness. */
#include <math.h>
#include <string.h>
#include "reverb.h"

#define FS	44100.0f

static const int line_len[RV_LINES] = { 2011, 2381, 2713, 3089, 3371, 3691, 4001, 4507 };
static const int ap_len[2][RV_AP] = { { 142, 379, 607, 1031 }, { 151, 397, 631, 1063 } };

/* how the reverb follows past */
#define RISE_S		0.8f	/* density rising (voices entering): the reverb recedes */
#define FALL_S		2.5f	/* density falling (voices thinning): the reverb swells */
#define WET_MIN		0.06f
#define WET_MAX		0.70f
#define RT_MIN		2.2f
#define RT_MAX		7.5f
#define DAMP_HZ		4500.0f	/* soft: the tail darkens as it decays */

void reverb_init (reverb_state *r)
{
	int i, s;
	memset (r, 0, sizeof *r);
	for (i = 0; i < RV_LINES; i++) r->len[i] = line_len[i];
	for (s = 0; s < 2; s++) for (i = 0; i < RV_AP; i++) r->ap_len[s][i] = ap_len[s][i];
	r->density = 0;
	r->sparse = 1;
	r->wet = WET_MAX;
	r->rt60 = RT_MAX;
	r->mood = 1;
}

static float allpass (reverb_state *r, int s, int i, float x)
{
	const float g = 0.6f;
	float *b = r->ap[s][i], d = b[r->ap_pos[s][i]], y = d - g * x;
	b[r->ap_pos[s][i]] = x + g * y;
	if (++r->ap_pos[s][i] >= r->ap_len[s][i]) r->ap_pos[s][i] = 0;
	return y;
}

static float smoothstep (float a, float b, float x)
{
	float t = (x - a) / (b - a);
	t = t < 0 ? 0 : t > 1 ? 1 : t;
	return t * t * (3 - 2 * t);
}

static short soft (float x) { return (short) lrintf (32767.0f * tanhf (x / 32767.0f)); }

void reverb_step (reverb_state *r, float env_sum, short *l, short *rr)
{
	const float hpk = 1.0f - expf (-2.0f * (float) M_PI * 120.0f / FS);
	const float lpk = 1.0f - expf (-2.0f * (float) M_PI * DAMP_HZ / FS);
	float in_l = *l, in_r = *rr, x[RV_LINES], y[RV_LINES], o_l = 0, o_r = 0, a, sp;
	int i, h;

	/* how full past is: follow the envelope sum, slowly when it thins */
	a = env_sum > r->density ? 1.0f / (RISE_S * FS) : 1.0f / (FALL_S * FS);
	r->density += (env_sum - r->density) * a;
	sp = 1.0f - smoothstep (0.4f, 6.0f, r->density);
	r->sparse = sp;
	r->wet = WET_MIN + (WET_MAX - WET_MIN) * sp;
	r->rt60 = RT_MIN + (RT_MAX - RT_MIN) * sp;

	/* input: high-passed (keeps the low end clear), diffused */
	r->hp_l += (in_l - r->hp_l) * hpk;
	r->hp_r += (in_r - r->hp_r) * hpk;
	in_l = (in_l - r->hp_l) * 0.25f;
	in_r = (in_r - r->hp_r) * 0.25f;
	for (i = 0; i < RV_AP; i++) { in_l = allpass (r, 0, i, in_l); in_r = allpass (r, 1, i, in_r); }

	/* the network */
	for (i = 0; i < RV_LINES; i++)
	{
		float d = r->line[i][r->pos[i]];
		float g = powf (10.0f, -3.0f * r->len[i] / (r->rt60 * FS));
		r->lp[i] += (d - r->lp[i]) * lpk;
		y[i] = r->lp[i] * g;
		if (i % 2) o_r += (i & 2 ? -1.0f : 1.0f) * d;
		else       o_l += (i & 4 ? -1.0f : 1.0f) * d;
	}
	/* Hadamard mixing (in place, normalised) */
	for (i = 0; i < RV_LINES; i++) x[i] = y[i];
	for (h = 1; h < RV_LINES; h <<= 1)
		for (i = 0; i < RV_LINES; i += 2 * h)
		{
			int j;
			for (j = i; j < i + h; j++) { float p = x[j], q = x[j + h]; x[j] = p + q; x[j + h] = p - q; }
		}
	for (i = 0; i < RV_LINES; i++)
	{
		r->line[i][r->pos[i]] = x[i] * 0.35355339f + ((i % 2) ? in_r : in_l);
		if (++r->pos[i] >= r->len[i]) r->pos[i] = 0;
	}

	o_l *= 1.5f; o_r *= 1.5f;
	r->level += ((fabsf (o_l) + fabsf (o_r)) / 65536.0f - r->level) * (1.0f / (0.1f * FS));
	{
		/* the mood darkens the whole mix: a low-pass from ~900 Hz at the
		   opening, opening fully as the mood lifts */
		float yl = *l + r->wet * o_l, yr = *rr + r->wet * o_r;
		if (r->mood < 0.999f)
		{
			float fc = 900.0f * powf (2.0f, 4.5f * r->mood), k = 1.0f - expf (-2.0f * (float) M_PI * fc / FS);
			r->tone_l += (yl - r->tone_l) * k; r->tone_r += (yr - r->tone_r) * k;
			yl = r->tone_l + (yl - r->tone_l) * r->mood;
			yr = r->tone_r + (yr - r->tone_r) * r->mood;
		}
		*l = soft (yl);
		*rr = soft (yr);
	}
}
