/* harm's convolution and rhythm, from dsp/harm3.c (before 2003, file copied Apr 2003; the
 * port in old_i2pi/dsp, which reproduces the 2006 harm.raw render).
 *
 * Copied verbatim from harm3.c: convolve(), clear_cbuf(), click1/2/3(), the
 * chanon arrangement (sections of 1.8M samples, counter c), the drifting tap
 * modulation, and the chaotic q/z counter with its clear / nofx / nofxfeed
 * events. harm's time shift (samples written two or three times) is not used.
 *
 * How it is wired here instead of harm's own voices:
 *   slot 2 (harm: jiggle; taps on fifths of the bar)  <- past, left  (x 0.5)
 *   slot 3 (harm: chords; taps on sevenths of the bar) <- past, right (x 0.5)
 *   slots 4, 5, 6 (harm's click1/2/3)          -- silent (clicks removed)
 *   slots 0, 1 (harm: swell, notes) are silent but their counters run as in harm.
 * The past slots are always on (harm switched jiggle/chords by section).
 *
 * De-clicking (added): harm made its nofx / nofxfeed / clear events and the
 * jump of its gains at the 400000-sample wrap within one sample, which clicks
 * on past's sustained tones. On past's networks these now glide: the echo
 * level and the network's input fade over 30-50 ms, a clear fades the echoes
 * out, empties the network, and lets it fill again, and the feedback gains
 * follow harm's over ~10 ms. (nofxfeed, which froze the network in harm,
 * mutes its echoes and lets it decay instead.)
 *
 * The other change to harm's maths: past is far louder than harm's voices, and
 * several tap sets have feedback gains summing above 1 (slot 2 reaches 1.77),
 * which would run away. Each network's feedback gains are scaled so their
 * absolute sum is at most 0.95. The unscaled gains are kept for the visuals.
 */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "i386conv.h"
#include "harm_fx.h"

#define CBUF_SIZE		HARM_CBUF_SIZE
#define MAX_CBUF_TAPS	HARM_TAPS
#define CHANS			HARM_CHANS
#define FB_MAX			0.95f
#define HB				131072		/* past's bar: 16 steps of 8192 samples */

typedef harm_cbuf circBufT;

/* ---- verbatim from harm3.c ---- */

static short	convolve (short x, circBufT *buf)
{
	int	n;
	float	fsum, bsum;

	fsum = buf->tap[0].gforward * x;
	bsum = 0.0;

	for (n = 1; n < MAX_CBUF_TAPS; n++)
	{
	    fsum += buf->data[(buf->idx - buf->tap[n].tapnum) % CBUF_SIZE] * buf->tap[n].gforward;
	    bsum += buf->data[(buf->idx - buf->tap[n].tapnum) % CBUF_SIZE] * buf->tap[n].gback;
	}

	buf->data[(++buf->idx) % CBUF_SIZE] = bsum + x;

	/* x87 fistps: overflow -> -32768 */
	return (i386_fists (fsum + bsum));
}

static unsigned int	click1 (uint64_t i)
{
		unsigned int outl = 0, b, c;

		b = i % 278627;
		c = i % 19327;

		if ( !((i % 90237 > 100) || (i % 8914 < 3)) )
		outl = i386_fistu (2910.0  * (sin ((i+b)/70.0) * (cos (c/1900.0) * sin (i/140.0))));

		return (outl);
}

static unsigned int	click2 (uint64_t i)
{
		unsigned int outl = 0;

		if ( (i % 10000  < 3000) && (i % 7347 < 500) )
		{
			outl = i386_fistu (outl + (2500.0 * sin (i/7.0)));
		}

		if ( (i % 80000  < 5930) )
		{
			outl = i386_fistu (outl + (5000.0 * sin(i/90.0)));
		}

		return (outl);
}

static unsigned int	click3 (uint64_t i)
{
		unsigned int outl = 0;

		if ( (i > 270000) && (i < 360000) )
		{
			outl = i386_fistu (outl + (500.0 * ( sin (i / (13000000.0/(float)i))
					 +sin (i / (25000000.0/(float)i)))));
		} else
		if ( (i > 360000) && (i < 397000) )
		{
			outl = i386_fistu (outl + (500.0 * ( sin (i / (13000000.0/(float)(400000.0-i)))
					 +sin (i / (25000000.0/(float)(400000.0-i))))));
		}

		return (outl);
}

static void		clear_cbuf (circBufT *buf)
{
		int	i;

		for (i = 0; i < CBUF_SIZE; i++)
		{
			buf->data[i] = 0;
		}

		buf->tap[0].gforward = 1.0;

		buf->idx = 0;
}

/* ---- harm's chords and swell, re-pitched -------------------------------
 *
 * harm3.c's chords(i) and swell(i) are sums of sin(i/P) terms (P in samples
 * per radian). Here each term keeps harm's P and amplitude envelope, but its
 * phase advances by key_factor/P per sample, so the whole set is transposed
 * (harm's intervals and harmonic motion kept) and glides when the key moves.
 * key_factor puts the lowest note of harm's first chord (P = 70, 100.3 Hz)
 * on the root of past's current key, between 98 and 196 Hz.
 */

static const double chord_P[3][3] = { { 40, 50, 70 }, { 20, 30, 50 }, { 47, 57, 77 } };

void harm_set_key (harm_state *h, int pc)
{
	h->key_pc = pc;
	double f = 110.0 * pow (2.0, pc / 12.0);		/* past's pc 0 = A */
	while (f >= 196.0) f *= 0.5;
	while (f < 98.0) f *= 2.0;
	h->key_target = (float) (f / (44100.0 / (2.0 * M_PI * 70.0)));
}

/* harm3.c chords(i), re-pitched: one chord per 100000 samples, then a rest */
static float chords_rp (harm_state *h, uint64_t i)
{
	int set = i < 100000 ? 0 : i < 200000 ? 1 : i < 300000 ? 2 : 3, n;
	float v = 0;
	h->chord_set = set;
	if (set > 2) set = 2;							/* phases keep running through the rest */
	for (n = 0; n < 3; n++)
	{
		h->ph_ch[n] += h->key_factor / chord_P[set][n];
		if (h->ph_ch[n] > 2 * M_PI) h->ph_ch[n] -= 2 * M_PI;
		v += (float) sin (h->ph_ch[n]);
	}
	return 1000.0f * v;
}

/* harm3.c swell(i), re-pitched (its amplitude envelopes still follow i) */
static float swell_rp (harm_state *h, uint64_t i)
{
	static const double P[5] = { 10, 30, 100, 120, 12 };
	int n;
	float v;
	for (n = 0; n < 5; n++)
	{
		h->ph_sw[n] += h->key_factor / P[n];
		if (h->ph_sw[n] > 2 * M_PI) h->ph_sw[n] -= 2 * M_PI;
	}
	v  = 350.0f * (float) sin (h->ph_sw[0]);
	v += (float) (((400000.0 - i)/100000.0) * 500.0 * sin (h->ph_sw[1]));
	v += (float) ((i/50000.0) * 700.0 * sin (h->ph_sw[2]));
	v += (float) (sin (i/8700.0) * 1550.0 * sin (h->ph_sw[3]));
	v += (float) (sin (i/8000.0) * 750.0 * cos (h->ph_sw[4]));
	return v;
}

/* ---- harm's drums on past's bar (added) ----------------------------------
 *
 * harm's clicks, with harm's clock stretched by SC = 1.6384 so that its
 * 80000-sample period is exactly past's bar (131072 samples). Their gates
 * then fall on the bar, and harm's own numbers make the patterns:
 *   click2, low   5000 sin(i/90) while i % 80000 < 5930  -> the downbeat thump
 *                 (~0.22 s); tuned to the key's root (55-110 Hz)
 *   click2, high  2500 sin(i/7) while i % 10000 < 3000 && i % 7347 < 500:
 *                 10000 -> bar/8, 7347 -> ~bar/11, so the two gates interfere
 *                 in 11 against 8; tuned to the key's fifth (700-1400 Hz)
 *   click1        harm's ring-modulated blip (2910 sin((i+b)/70) cos(c/1900)
 *                 sin(i/140)) for ~100 harm samples once per ~bar, placed off
 *                 the beat at 3/8
 *   click3        harm's chirps over 270000..397000 of its 400000 cycle:
 *                 400000 x SC = exactly 5 bars, so a sweep up and back down
 *                 into every fifth downbeat (phase-corrected to keep pitch)
 * The sounds are harm's formulas; only the gates are re-timed, and every gate
 * edge gets a 2 ms ramp. They go through harm's slot 4-6 networks, as in harm.
 */
#define HSC		1.6384
static float ramp (float *e, int on)
{
	*e += ((on ? 1.0f : 0.0f) - *e) * (1.0f / (0.002f * 44100.0f));
	return *e;
}

static void harm_drums (harm_state *h, float out[3])
{
	uint64_t t = h->t, b = t % HB;
	double R = 280.1 * pow (2.0, h->key_pc / 12.0), f;
	float on_arc[4];
	int k;
	on_arc[0] = h->arc > 0.30f;								/* blip */
	on_arc[1] = t > 2 * HB;									/* thump: after two bars */
	on_arc[2] = h->arc > 0.50f;								/* 11:8 tick */
	on_arc[3] = h->arc > 0.60f;								/* 5-bar sweep */
	for (k = 0; k < 4; k++) h->g_dr[k] += (on_arc[k] - h->g_dr[k]) * (1.0f / (1.5f * 44100.0f));

	/* click1: the blip, off the beat */
	{
		uint64_t bb = t % 278627, cc = t % 19327;
		int on = b >= HB * 3 / 8 && b < HB * 3 / 8 + (uint64_t) (100 * HSC);
		float e = ramp (&h->edge[0], on);
		out[0] = e > 1e-4f ? h->g_dr[0] * e * (float) (2910.0 * (sin ((t + bb) / 70.0) * (cos (cc / 1900.0) * sin (t / 140.0)))) : 0;
	}
	/* click2: the thump on the downbeat, and the 11:8 tick */
	{
		int on_lo = b < (uint64_t) (5930 * HSC);
		int on_hi = (b % (HB / 8)) < (HB / 8) * 3 / 10 && (b % (HB / (uint64_t) h->tdiv)) < (uint64_t) (500 * HSC);
		float elo = ramp (&h->edge[1], on_lo), ehi;
		for (f = R; f >= 110.0; f *= 0.5) ;
		while (f < 55.0) f *= 2.0;
		h->ph_thump += 2.0 * M_PI * f / 44100.0;
		if (h->ph_thump > 2.0 * M_PI) h->ph_thump -= 2.0 * M_PI;
		for (f = R * pow (2.0, 7 / 12.0); f >= 1400.0; f *= 0.5) ;
		while (f < 700.0) f *= 2.0;
		h->ph_tick += 2.0 * M_PI * f / 44100.0;
		if (h->ph_tick > 2.0 * M_PI) h->ph_tick -= 2.0 * M_PI;
		ehi = ramp (&h->edge[2], on_hi);
		out[1] = h->g_dr[1] * elo * 5000.0f * (float) sin (h->ph_thump)
		       + h->g_dr[2] * ehi * 2500.0f * (float) sin (h->ph_tick);
	}
	/* click3: the 5-bar sweep */
	{
		double hp = (t % (5UL * HB)) / HSC, v = 0;
		if (hp > 270000 && hp < 360000)
			v = 500.0 * (sin (HSC * hp * hp / 13000000.0) + sin (HSC * hp * hp / 25000000.0));
		else if (hp > 360000 && hp < 397000)
			v = 500.0 * (sin (HSC * hp * (400000.0 - hp) / 13000000.0) + sin (HSC * hp * (400000.0 - hp) / 25000000.0));
		out[2] = h->g_dr[3] * (float) v;
	}
	h->click[0] = fabsf (out[0]) / 2910.0f;
	h->click[1] = fabsf (out[1]) / 5000.0f;
	h->click[2] = fabsf (out[2]) / 1000.0f;
}

/* ---- set-up, as at the top of harm3.c's main() ---- */

void harm_init (harm_state *h)
{
	uint64_t i;
	memset (h, 0, sizeof *h);
	h->ldiv = 5; h->rdiv = 7; h->tdiv = 11;		/* (added) the genes' first values */
	for (i = 0; i < CHANS; i++)
	{
		clear_cbuf (&h->cbuf[i]);

		h->nofx[i] = -1;
		h->nofxfeed[i] = -1;
		h->chanon[i] = 0;

		h->cbuf[i].tap[1].gback = -0.35;
		h->cbuf[i].tap[1].tapnum = 4 * i + 1;

		if (i % 2)
		{
			h->cbuf[i].tap[2].gback=0.53;
			h->cbuf[i].tap[2].tapnum = i;
		} else
		{
			h->cbuf[i].tap[2].gback=-0.40;
			h->cbuf[i].tap[2].tapnum=2*i;
		}
	}
	h->i = 0; h->k = 0; h->l = 0; h->c = 0; h->q = 37; h->z = 0;
	for (i = 0; i < CHANS; i++) h->wet_s[i] = h->feed_s[i] = 1.0f;
	harm_set_key (h, 0);
	h->key_factor = h->key_target;
}

/* (added) scale a network's feedback so it cannot run away; keep the raw gains */
static void stabilise (harm_state *h, int j)
{
	float s = 0;
	int n;
	for (n = 1; n < MAX_CBUF_TAPS; n++) { h->gback_raw[j][n] = h->cbuf[j].tap[n].gback; s += fabsf (h->cbuf[j].tap[n].gback); }
	if (s > FB_MAX)
		for (n = 1; n < MAX_CBUF_TAPS; n++) h->cbuf[j].tap[n].gback *= FB_MAX / s;
}

static short soft (float x) { return (short) lrintf (32767.0f * tanhf (x / 32767.0f)); }

/* (added) a tap must fit the network's buffer: fold it down by octaves */
static unsigned int fold_tap (uint64_t t)
{
	while (t > 49000) t /= 2;
	return (unsigned int) t;
}

/* (added) harm's convolve() for past's networks, with the mode switches and
   gain changes glided instead of instant (see the top of the file) */
static float convolve_smooth (harm_state *h, int j, short x)
{
	circBufT	*buf = &h->cbuf[j];
	const float	kw = 1.0f / (0.05f * 44100.0f), kf = 1.0f / (0.03f * 44100.0f), kg = 1.0f / (0.01f * 44100.0f);
	float		wet_t, feed_t, fsum, bsum = 0;
	int			n;

	/* harm: nofx -> echoes keep ringing, nothing new goes in;
	         nofxfeed -> dry only; otherwise dry + echoes */
	feed_t = (h->nofx[j] == 1 || h->nofxfeed[j] == 1) ? 0.0f : 1.0f;
	wet_t = (h->nofx[j] != 1 && h->nofxfeed[j] == 1) ? 0.0f : 1.0f;
	if (h->clear_pend[j]) wet_t = feed_t = 0;
	h->wet_s[j] += (wet_t - h->wet_s[j]) * kw;
	h->feed_s[j] += (feed_t - h->feed_s[j]) * kf;
	if (h->clear_pend[j] && h->wet_s[j] < 0.002f)
	{
		clear_cbuf (buf);
		h->clear_pend[j] = 0;
	}

	fsum = buf->tap[0].gforward * x;
	for (n = 1; n < MAX_CBUF_TAPS; n++)
	{
		float d;
		/* (added) past's networks: when a delay grid mutates, the taps glide
		   to their new lengths (at most 1% a sample, 17 cents: a tape-like bend of the
		   echoes, not a jump), read between samples on the way */
		if (j == 2 || j == 3)
		{
			double *p = &h->tp[j][n], tg = (double) buf->tap[n].tapnum;
			if (*p <= 0) *p = tg;
			else if (*p != tg) *p = fabs (tg - *p) <= 0.01 ? tg : *p + (tg > *p ? 0.01 : -0.01);
		}
		if ((j == 2 || j == 3) && h->tp[j][n] != (double) buf->tap[n].tapnum)
		{
			unsigned int ip = (unsigned int) h->tp[j][n];
			float fr = (float) (h->tp[j][n] - ip);
			float d0 = buf->data[(buf->idx - ip) % CBUF_SIZE], d1 = buf->data[(buf->idx - (ip + 1)) % CBUF_SIZE];
			d = d0 + fr * (d1 - d0);
		}
		else d = buf->data[(buf->idx - buf->tap[n].tapnum) % CBUF_SIZE];
		h->gs[j][n] += (buf->tap[n].gback - h->gs[j][n]) * kg;
		fsum += d * buf->tap[n].gforward;
		bsum += d * h->gs[j][n];
	}
	buf->data[(++buf->idx) % CBUF_SIZE] = bsum + h->feed_s[j] * x;
	return fsum + h->wet_s[j] * bsum;
}

/* ---- one pass of harm3.c's main loop ---- */

void harm_step (harm_state *h, short in_l, short in_r, short *out_l, short *out_r, int *copies)
{
	unsigned int	*chan = h->chan;
	char			*chanon = h->chanon, *nofx = h->nofx, *nofxfeed = h->nofxfeed;
	circBufT		*cbuf = h->cbuf;
	uint64_t	j, i, k, c;
	float			side[2] = { 0, 0 }, clicks = 0;
	short			past[2];

	past[0] = in_l; past[1] = in_r;

	for (j = 0; j < CHANS; j++)
	{
		chan[j] = 0;
	}

	i = ++h->i;
	k = ++h->k;
	h->l++;
	c = h->c;

	if ((c >4
) &&  (c < 11))
	{
		chanon[0]=2;
	} else if (k % 100080 == 1)
	{
		chanon[0]--;
	}

	if ((c > 3) && (c % 5) && (c < 8))
	{
		chanon[1] = 2;
	} else if (k % 356378 == 1)
	{
		chanon[1]--;
	}

	if (((c < 3) || !(c % 3)) && (c < 8))
	{
		chanon[2] = 2;
	} else if (k % 378284 == 1)
	{
		chanon[2]--;
	}

	if ((k > 2000000) &&  (c < 7))
	{
		chanon[3] = 2;
	} else if (k % 456289 == 1)
	{
		chanon[3]--;
	}

	if ((c > 1) && (c < 7))
	{
		chanon[4] = 4;
	} else if (k % 4344867 == 1)
	{
		chanon[4] --;
	}

	if (c > 2)
	{
		chanon[6] = 3;
		chanon[5] = 4;
	} else if (k % 2838690 == 1)
	{
		chanon[5]--;
		chanon[6]--;
	}

	/* slots 2 and 3 carry past (always on). Slots 4-6 carry harm's clicks,
	   re-timed to past's bar (harm_drums); harm's own click1-3 timing
	   (unrelated to past's beat) is not used. */
	(void) click1; (void) click2; (void) click3;
	{
		float dr[3];
		int n;
		harm_drums (h, dr);
		for (n = 0; n < 3; n++) chan[4 + n] = (unsigned int) (int) lrintf (0.5f * dr[n]);
	}
	/* harm's chords and swell, faded in and out by harm's own arrangement
	   (chanon[3] = chords, chanon[0] = swell) instead of its gain jumps */
	{
		float ch, sw, ga = 1.0f / (1.5f * 44100.0f), gr = 1.0f / (0.04f * 44100.0f);
		h->key_factor += (h->key_target - h->key_factor) * (1.0f / (1.5f * 44100.0f));	/* glide */
		/* harm's chords were tried and dropped; the swell stays */
		(void) chords_rp;
		ch = 0;
		h->chord_set = i < 100000 ? 0 : i < 200000 ? 1 : i < 300000 ? 2 : 3;
		/* harm's swell was tried and removed */
		(void) swell_rp;
		sw = 0;
		h->amp_ch += ((chanon[3] > 0 ? 1.0f : 0.0f) - h->amp_ch) * ga;
		h->amp_rest += ((h->chord_set < 3 ? 1.0f : 0.0f) - h->amp_rest) * gr;	/* harm's rest, de-clicked */
		h->amp_sw = 0;
		ch *= 0.55f * h->amp_ch * h->amp_rest;
		sw *= 0.35f * h->amp_sw;
		h->chord_level = fabsf (ch) / 1650.0f;
		h->swell_level = fabsf (sw) / 2400.0f;
		h->cycle = (float) (i / 400000.0);
		/* chords into both of past's networks; the swell through slot 0's (harm's own for swell) */
		chan[2] = (unsigned int) (int) (0.5f * past[0] + ch);
		chan[3] = (unsigned int) (int) (0.5f * past[1] + ch);
		chan[0] = (unsigned int) (int) sw;
	}

	if (h->l == 1800000)
	{
		++h->c;
		h->l = 0;
	}

	for (j = 0; j < CHANS; j++)
	{
		int active = (j >= 2) ? 1 : (j == 0) ? h->amp_sw > 0.001f : chanon[j] > 0;
		if (active)
		{
			if ((j == 4) || (j == 5))
			{
				cbuf[j].tap[3].tapnum = j*2000.0 + 500.0 * sin (i/9737.0);
				cbuf[j].tap[3].gback = 0.05 * sin (k / 34787.0) + (c/100.0);
			}

			/* (changed) the taps sit on fractions of past's bar (16 steps of
			   8192 = 131072 samples), so the echoes are rhythms: the left
			   network echoes in 5 per bar, the right in 7 -- against past's
			   4 / 16 and the choir's 3. harm's gain movements are kept, so
			   the trails swell and fade. (harm had 7500 / 17759 / 22500 /
			   45000 / 49990 here, and a 25000 tap on the right that it swept.) */
			/* (added) the grids are genes (past_state.gene 2, 3): 5 and 7 per
			   bar until they mutate; a tap longer than the buffer folds down
			   an octave */
			if (j == 3)
			{
				uint64_t D = (uint64_t) h->rdiv;
				cbuf[j].tap[4].tapnum = fold_tap (HB * 2 / D);	/* 37449: two sevenths */
				cbuf[j].tap[4].gback = 0.20 * sin (k/8571.0) * sin (i/25000.0) + 0.15;
				cbuf[j].tap[5].tapnum = fold_tap (HB / D);		/* 18725 */
				cbuf[j].tap[5].gback = 0.35 + 0.15 * sin (i / 27000.0);
				cbuf[j].tap[6].tapnum = fold_tap (HB / (2 * D));	/* 9362: half sevenths */
				cbuf[j].tap[6].gback = 0.12 * sin (i / 41000.0);
			} else
			if (j == 2)
			{
				uint64_t D = (uint64_t) h->ldiv;
				cbuf[j].tap[2].tapnum = fold_tap (HB / (4 * D));	/* 6553: quintuplet sixteenths */
				cbuf[j].tap[2].gback = 0.2 * sin (i / 1234.0);
				cbuf[j].tap[3].tapnum = fold_tap (HB / (2 * D));	/* 13107 */
				cbuf[j].tap[3].gback = 0.3 * sin (i / 23450.0);
				cbuf[j].tap[4].tapnum = fold_tap (HB / D);			/* 26214: one fifth */
				cbuf[j].tap[4].gback = 0.5 * sin (i / 34560.0) + 0.2;

				cbuf[j].tap[5].tapnum = fold_tap (HB * 3 / (2 * D));	/* 39321 */
				cbuf[j].tap[5].gback = 0.12 * sin (i / 45670.0);
				cbuf[j].tap[6].tapnum = fold_tap (HB * 7 / (4 * D));	/* 45875 */
				cbuf[j].tap[6].gback = 0.3 * sin (i / 56780.0);
			}
			stabilise (h, (int) j);		/* (added) */
		}
		/* q was a 32-bit uint64_t on i386 (wraps via fistpll) */
		h->q = i386_fistu ((double) h->q + 2 + (double) (float) k * sin ((float) k / ((float) h->z+1.0)));

		h->z = h->q % 3137977;

		{
			float out = 0;
			int to = (j == 2) ? 0 : (j == 3) ? 1 : (j == 0) ? 2 : -1;	/* past slots to their side; swell to both */
			/* harm summed channels as unsigned ints and kept the low 16 bits when
			   writing the sound card buffer; do the same per channel */
			#define CH16(v)	((float) (short) (int) (v))
			if (j >= 2)
			{
				/* past's networks and the drums': the events only change the
				   mode, which convolve_smooth glides into */
				if ((h->z == 0) || (h->z == 235)) { h->clear_pend[j] = 1; h->event_clear[j]++; }
				else if ((h->z == 1) || (h->z == 374)) nofx[j] *= -1;
				else if ((h->z == 2) || (h->z == 8493)) nofxfeed[j] *= -1;
				out = convolve_smooth (h, (int) j, (short) (int) chan[j]);
			} else
			if ((h->z == 0) || (h->z == 235))
			{
				clear_cbuf (&cbuf[j]);
				out = CH16 (chan[j]);
				h->event_clear[j]++;
			} else
			if ((h->z == 1) || (h->z == 374) )
			{
				nofx[j] *= -1;
			} else
			if ((h->z == 2) || (h->z == 8493) )
			{
				nofxfeed[j] *= -1;
			} else if (active)
			{
				if (j >= 4) chan[j] = i386_fistu ((double) chan[j] * (2.0 / ((float) chanon[j])));
				if (nofx[j] == 1)
				{
					out = (float) convolve (0, &cbuf[j]) + CH16 (chan[j]);
				} else
				if (nofxfeed[j] == 1)
				{
					out = CH16 (chan[j]);
				} else
				{
					out = (float) convolve ((short) (int) chan[j], &cbuf[j]);
				}
			}
			if (to == 2) { side[0] += out; side[1] += out; }
			else if (to >= 0) side[to] += out;
			else if (j >= 4) clicks += out;
		}
	}

	if (h->i > 400000.0)
	{
		h->i = 0;
	}

	/* the mix: harm's networks carry past (dry + echoes); a little dry past on
	   top; the clicks in the centre; a soft limit instead of the 16-bit wrap */
	/* harm's drums, in the centre */
	side[0] += 0.8f * clicks;
	side[1] += 0.8f * clicks;
	/* harm's 400000-sample (9.07 s) cycle as a breath: the networks swell a
	   little towards the middle of each cycle, the dry past a little less */
	{
		float b = (float) sin (2.0 * M_PI * h->cycle);
		float wet = 1.2f * (1.0f + 0.15f * b), dry = 0.35f * (1.0f - 0.15f * b);
		*out_l = soft (wet * side[0] + dry * past[0]);
		*out_r = soft (wet * side[1] + dry * past[1]);
	}

	/* harm's time shift (writing samples again in its windows) is left out:
	   each sample is written once */
	*copies = 1;
	h->repeat = 0;
}
