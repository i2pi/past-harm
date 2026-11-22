/* past (before 2003) as a sample engine -- see past_engine.h.
 *
 * Source: old_i2pi/music/past2/past.c (originally old_hard_drives/src/music/
 * past2/past.c). The body of past_next() is that program's main loop with its
 * locals moved into past_state; statements, order, types and constants are
 * unchanged. Additions are marked "(added)". With arc, harmony and choir off
 * (the default) they only record values the visuals need and the output is
 * the original's; p10m1-past turns them on (see voice_cap, snap_note, choir).
 */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "past_engine.h"

#define PI		3.1415926535929323		/* sic, as in the original */

#define PAD_LOG2	18						/* (added) the choir's wavetables */
#define PAD_N		(1 << PAD_LOG2)
#define PAD_TABLES	3

static float note_to_freq (int n)
{
	float	oct = 12.0;
	float	base = 2.0;
	float	freq;

	freq = 880.0 * pow (base, n / oct);

	return (freq);
}

/* (added) the arc: how many voices may sound, over a ~150 s swell whose
   peaks vary over ~330 s; it starts at 2 (space), so the piece opens sparse
   and builds. A voice counts while it is held or still above 0.15 in its
   release, so long tails don't keep new notes out for long. */
static int voice_cap (const past_state *s)
{
	double t = s->i / 44100.0;
	double w = 0.5 - 0.5 * cos (2.0 * PI * t / 150.0);
	w *= 0.8 + 0.2 * (0.5 - 0.5 * cos (2.0 * PI * t / 330.0));
	{
		int cap = 2 + (int) lround (14.0 * w);
		if (s->mood < 0.3f && cap > 1) cap--;		/* the opening: sparser still */
		return cap;
	}
}

/* (added) harmony: snap a new note to the nearest pitch of the current key
   (ties go down). While fewer than 4 voices sound, any note of the major /
   natural-minor scale; with 4 or more, only the pentatonic -- no semitones or
   tritones between the voices, which is what made the fuller passages clash. */
static int snap_note (const past_state *s, int note)
{
	static const char dia[2][12] = { { 1,0,1,0,1,1,0,1,0,1,0,1 }, { 1,0,1,1,0,1,0,1,1,0,1,0 } };
	static const char pen[2][12] = { { 1,0,1,0,1,0,0,1,0,1,0,0 }, { 1,0,0,1,0,1,0,1,0,0,1,0 } };
	int n = 0, o, d;
	const char *ok;
	for (o = 0; o < PAST_OSCS; o++) if (s->osc[o].is_on && !s->osc[o].solo && s->osc[o].env > 0.1f) n++;
	ok = n >= 4 ? pen[s->key_minor ? 1 : 0] : dia[s->key_minor ? 1 : 0];
	/* the oppressive opening: Phrygian (minor with a flat 2nd), and once 4 or
	   more voices sound the in scale (root, b2, 4, 5, b6) -- until the mood
	   is half way open */
	if (s->mood < 0.5f)
	{
		static const char phr[12] = { 1,1,0,1,0,1,0,1,1,0,1,0 };
		static const char ins[12] = { 1,1,0,0,0,1,0,1,1,0,0,0 };
		ok = n >= 4 ? ins : phr;
	}
	for (d = 0; d <= 6; d++)
	{
		if (ok[(((note - d - s->key_pc) % 12) + 12) % 12]) return note - d;
		if (ok[(((note + d - s->key_pc) % 12) + 12) % 12]) return note + d;
	}
	return note;
}

static void trigger (past_state *s, int note, float dur, float amp, float a, float r)
{
	int		o;

	/* (added) the polyrhythm in the music itself:
	   - the choir's notes wait for the next beat in 3 (see poly_step);
	   - past's own notes are accented in groups of 3 steps across its
	     16-step bar (3+3+3+3+4): a 3-against-4 inside its own grid */
	if (s->poly && s->choir && dur >= 0.25f && !s->firing)
	{
		if (s->pend_n < 8)
		{
			float *p = s->pend[s->pend_n++];
			p[0] = (float) note; p[1] = dur; p[2] = amp; p[3] = a; p[4] = r;
		}
		return;
	}
	if (s->poly && dur < 0.25f)
	{
		int st = (int) ((s->i % PAST_BAR) / 8192), g = s->gene[1];
		if (st % g != 0 || st + g > 16) amp *= 0.5f;	/* accents every g steps; the last group takes the rest */
	}
	if (s->endless && note > 44) note -= 12 * ((note - 33) / 12);	/* (added) see past_next; down by octaves to 44 or below (in one step: the rows rise for ever) */
	if (s->arc && s->mood < 0.5f && dur < 0.25f) note -= 12;	/* (added) the opening: an octave down */
	if (s->harmony) note = snap_note (s, note);			/* (added) */

	if (s->arc)											/* (added) */
	{
		int n = 0;
		s->cap = voice_cap (s);
		for (o = 0; o < PAST_OSCS; o++)
			if (s->osc[o].is_on && !s->osc[o].solo && (s->osc[o].sec < s->osc[o].dur || s->osc[o].env > 0.15f)) n++;
		if (n >= s->cap) return;
	}

	o = 0;
	while ((o < PAST_OSCS) && (s->osc[o].is_on)) o++;
	if (o >= PAST_OSCS)
	{
		return;
	}

	s->osc[o].is_on = 1;
	s->osc[o].a = a;
	s->osc[o].r = r;
	s->osc[o].dur = dur;
	s->osc[o].sec = 0.0;
	s->osc[o].amp = amp;
	s->osc[o].acc = note_to_freq (note) / 44100.0;

	s->osc[o].note = note;
	/* (added) the long notes (the bass pairs, dur 0.3) go to the choir */
	s->osc[o].choir = s->choir && dur >= 0.25f;
	s->osc[o].solo = 0;
	if (s->osc[o].choir)
	{
		s->osc[o].cph[0] = (double) (((unsigned) (o * 7919 + (note + 64) * 104729 + s->tick * 31)) % PAD_N);
		s->osc[o].cph[1] = s->osc[o].cph[0] + PAD_N / 2;
		if (s->osc[o].cph[1] >= PAD_N) s->osc[o].cph[1] -= PAD_N;
		s->osc[o].cph[2] = (double) (((unsigned) (s->osc[o].cph[0] + PAD_N / 3)) % PAD_N);
		s->osc[o].cph[3] = (double) (((unsigned) (s->osc[o].cph[0] + 5 * PAD_N / 6)) % PAD_N);
		s->osc[o].cph[4] = (double) (((unsigned) (s->osc[o].cph[0] + PAD_N / 7)) % PAD_N);
		s->osc[o].vph = (float) (o * 0.37);
	}
}

/* (added) the choir: five detuned saws per note (polyBLEP), gentle vibrato,
   spread across the stereo field, through three vowel formants that drift
   between "ah" and "oh" with past's own slow drive. It is added after
   past's atan stage, so it stays clean. */
/* The choir's sound: PADsynth-style wavetables. Each is one inverse FFT of a
   spectrum made of a saw's overtone series (harmonic h at 1/h) in which every
   harmonic is a gaussian band, wider as h rises, with random phases -- a
   seamless ~5.9 s loop that sounds like many slightly different voices. The
   ensemble's bands are wide (45 cents), the solo's narrow (12 cents). One
   table per octave of base pitch (140, 280, 560 Hz), read at the note's
   pitch, ratio 1..2, so the upper harmonics stay below Nyquist. */
static float	*pad[2][PAD_TABLES];				/* [solo][octave] */
static const double pad_base[PAD_TABLES] = { 140.0, 280.0, 560.0 };

#define MOOD_CYCLE	780.0				/* (added) s: the mood's cycle, endless */

static void fft_inverse (double *re, double *im, int n)
{
	int i, j, k, m;
	for (i = 1, j = 0; i < n; i++)					/* bit reversal */
	{
		int bit = n >> 1;
		for (; j & bit; bit >>= 1) j ^= bit;
		j ^= bit;
		if (i < j) { double t = re[i]; re[i] = re[j]; re[j] = t; t = im[i]; im[i] = im[j]; im[j] = t; }
	}
	for (m = 2; m <= n; m <<= 1)
	{
		double a = 2.0 * M_PI / m, wr = cos (a), wi = sin (a);
		for (k = 0; k < n; k += m)
		{
			double cr = 1, ci = 0;
			for (j = 0; j < m / 2; j++)
			{
				int p = k + j, q = p + m / 2;
				double tr = re[q] * cr - im[q] * ci, ti = re[q] * ci + im[q] * cr, t;
				re[q] = re[p] - tr; im[q] = im[p] - ti;
				re[p] += tr; im[p] += ti;
				t = cr * wr - ci * wi; ci = cr * wi + ci * wr; cr = t;
			}
		}
	}
}

static float *pad_table (double f0, double bw_cents, double bw_scale, unsigned int seed)
{
	double *re = calloc (PAD_N, sizeof *re), *im = calloc (PAD_N, sizeof *im), *mag = calloc (PAD_N / 2, sizeof *mag);
	float  *t = malloc (PAD_N * sizeof *t);
	double sum = 0;
	int h, k;
	for (h = 1; h * f0 < 10000.0; h++)
	{
		double fh = h * f0, bw = (pow (2.0, bw_cents / 1200.0) - 1.0) * f0 * pow (h, bw_scale);
		double c = fh * PAD_N / 44100.0, w = bw * PAD_N / 44100.0;	/* in bins */
		int lo = (int) (c - 4 * w) - 1, hi = (int) (c + 4 * w) + 1;
		if (w < 0.5) w = 0.5;
		for (k = lo < 1 ? 1 : lo; k <= hi && k < PAD_N / 2; k++)
		{
			double x = (k - c) / w;
			mag[k] += (1.0 / h) * exp (-x * x) / sqrt (w);	/* equal energy per harmonic */
		}
	}
	for (k = 1; k < PAD_N / 2; k++)
	{
		double ph;
		seed = seed * 1664525u + 1013904223u;
		ph = 2.0 * M_PI * (seed >> 8) / 16777216.0;
		re[k] = mag[k] * cos (ph); im[k] = mag[k] * sin (ph);
		re[PAD_N - k] = re[k]; im[PAD_N - k] = -im[k];		/* real output */
	}
	fft_inverse (re, im, PAD_N);
	for (k = 0; k < PAD_N; k++) sum += re[k] * re[k];
	sum = sqrt (sum / PAD_N);
	for (k = 0; k < PAD_N; k++) t[k] = (float) (0.5 * re[k] / (sum > 0 ? sum : 1));	/* rms 0.5 */
	free (re); free (im); free (mag);
	return t;
}

void past_prepare_choir (void)
{
	int k;
	if (pad[0][0]) return;
	for (k = 0; k < PAD_TABLES; k++)
	{
		pad[0][k] = pad_table (pad_base[k], 45.0, 1.0, 12345u + k);
		pad[1][k] = pad_table (pad_base[k], 12.0, 0.9, 777u + k);
	}
}

static void choir_formants (past_state *s)
{
	static const float ah[3] = { 730, 1090, 2440 }, oh[3] = { 570, 840, 2410 }, bw[3] = { 90, 110, 150 };
	float m = 0.5f + 0.5f * (float) sin (s->i / 319732.0);		/* past's drive_u */
	int f;
	for (f = 0; f < 3; f++)
	{
		float fr = ah[f] + (oh[f] - ah[f]) * m, w = 2.0f * (float) PI * fr / 44100.0f;
		float al = sinf (w) * (bw[f] / fr) * 0.5f, a0 = 1.0f + al;
		s->fcoef[f][0] = al / a0;
		s->fcoef[f][1] = -2.0f * cosf (w) / a0;
		s->fcoef[f][2] = (1.0f - al) / a0;
	}
}

/* (added) the choir's bass: one heavy, solid voice under the lowest choir
   note sounding, three octaves down and folded into 20-50 Hz -- thick (see below), a
   little second harmonic, gently saturated, gliding between notes (~80 ms)
   and following the choir's loudest envelope (~60 ms). Not under the
   opening solo. Added after the vowel formants, so it keeps its weight. */
static float choir_bass (past_state *s)
{
	double lo = 0;
	float  env = 0, y;
	int    j;
	for (j = 0; j < PAST_OSCS; j++)
	{
		past_osc *o = &s->osc[j];
		if (o->is_on && o->choir && !o->solo && o->env > 0.02f)
		{
			double f = 2.0 * note_to_freq (o->note) / (2.0 * PI) * (j % 2 + 1);
			if (lo == 0 || f < lo) lo = f;
			if (o->env > env) env = o->env;
		}
	}
	if (lo > 0)
	{
		double fb = lo / 8.0;						/* an octave below where it was: 20-50 Hz */
		while (fb > 50.0) fb *= 0.5;
		while (fb < 20.0) fb *= 2.0;
		if (s->bass_f <= 0) s->bass_f = (float) fb;
		s->bass_f += (float) ((fb - s->bass_f) * (1.0 / (0.08 * 44100.0)));
	}
	s->bass_amp += (env - s->bass_amp) * (1.0f / (0.06f * 44100.0f));
	/* thick: a pure fundamental for weight, plus three slightly detuned
	   voices (-6, 0, +6 cents) each with the 2nd and 3rd harmonics, beating
	   slowly against each other; driven into saturation for density, then
	   rounded by a ~320 Hz low-pass. Mono. */
	{
		static const double det[3] = { 0.99654, 1.0, 1.00347 };
		double body = 0;
		int v;
		s->bass_ph += 2.0 * PI * s->bass_f / 44100.0;
		if (s->bass_ph > 2.0 * PI) s->bass_ph -= 2.0 * PI;
		for (v = 0; v < 3; v++)
		{
			double ph;
			s->bass_ph3[v] += 2.0 * PI * s->bass_f * det[v] / 44100.0;
			if (s->bass_ph3[v] > 2.0 * PI) s->bass_ph3[v] -= 2.0 * PI;
			ph = s->bass_ph3[v];
			body += sin (ph) + 0.5 * sin (2.0 * ph) + 0.28 * sin (3.0 * ph);
		}
		/* the bass's weight (bass_rich, 0..1; 0.5 = as it was): the
		   harmonic body, the drive and the low-pass open up with it, and
		   its level swells from about a third to 1.35x */
		{
			float br = s->bass_rich, drive = 0.9f + 0.8f * br;
			y = (float) (1.0 * sin (s->bass_ph) + (0.05 + 0.4 * br) * body / 3.0);	/* leaner: it masked past's chords */
			y = tanhf (drive * y) / tanhf (drive);
			s->bass_lp += (y - s->bass_lp) * (float) (1.0 - exp (-2.0 * PI * (150.0 + 140.0 * br) / 44100.0));
		}
	}
	return 2800.0f * (s->bass_rich < 0.5f ? 0.35f + 1.3f * s->bass_rich : 1.0f + 0.7f * (s->bass_rich - 0.5f)) * s->bass_amp * (s->poly ? 0.8f + 0.2f * s->choir_pulse : 1.0f) * s->bass_lp;
}

/* (added) polyrhythm. past's step grid (8192 samples) grouped into bars of
   16 steps; five layers divide each bar into 2, 3, 4, 5 and 7 hits, all
   meeting on the downbeat. They enter with the arc of voices -- a heartbeat
   in 2, then 3 against it, then 4, then 5 -- and the 7 comes in only at the
   top of the arc, in the last bar of each 4-bar phrase, resolving on the
   next downbeat. Each layer has its own timbre, register and place in the
   stereo field, so each can be followed as its own stream, and its pitch
   comes from the key (root, fifth, third...), so they stay in the harmony:
     2  deep tuned drum on the root (40-90 Hz), centre
     3  log drum on the fifth, a little left
     4  short rim: band-passed noise + a tone on the root, a little right
     5  FM bell on the third (major / minor), alternating sides
     7  bright tick on the ninth, right
   Downbeats are accented. */
/* each layer's hits per bar: the heartbeat's 2, then the genes (the rim's
   is its grouping in steps, see poly_index) */
static int poly_div (const past_state *s, int k)
{
	return k == 0 ? 2 : s->gene[k == 1 ? 0 : k == 2 ? 1 : k == 3 ? 2 : 3];
}

/* which hit of its bar layer k is on at bar position b */
static int poly_index (const past_state *s, int k, uint64_t b)
{
	if (k == 2)		/* the rim: every g steps (3: steps 0, 3, 6, 9, 12), the last group takes the rest */
	{
		int g = s->gene[1], st = (int) (b / 8192) / g, last = (16 - g) / g;
		return st > last ? last : st;
	}
	return (int) ((b * (uint64_t) poly_div (s, k)) / PAST_BAR);
}

/* (added) the mutations. Each gene has its pool; a new value must differ
   from the old and (for the beats and delay grids: genes 0, 2, 3, 4) be
   coprime with the others, so the streams never lock together. */
static int gcd_i (int a, int b) { while (b) { int t = a % b; a = b; b = t; } return a; }
static unsigned int mut_rand (past_state *s)
{
	s->mut_rng = s->mut_rng * 1664525u + 1013904223u;
	return s->mut_rng >> 8;
}
static void mutate (past_state *s)
{
	static const int pool[5][6] = { {3, 4, 5, 7, 0}, {3, 5, 7, 0}, {3, 5, 7, 9, 11, 0}, {5, 7, 9, 11, 13, 0}, {9, 11, 13, 0} };
	int tries, g0 = (int) (mut_rand (s) % 5);
	for (tries = 0; tries < 5; tries++)
	{
		int g = (g0 + tries) % 5, cand[6], n = 0, c, o;
		for (c = 0; pool[g][c]; c++)
		{
			int v = pool[g][c], ok = v != s->gene[g];
			if (g != 1)
				for (o = 0; o < 5 && ok; o++)
					if (o != g && o != 1 && gcd_i (v, s->gene[o]) != 1) ok = 0;
			if (ok) cand[n++] = v;
		}
		if (n)
		{
			s->gene[g] = cand[mut_rand (s) % (unsigned) n];
			s->mut_gene = g;
			s->mut_count++;
			/* its drum guides the ear to the new rhythm again */
			if (g < 4) s->poly_bars[g + 1] = 0;
			if (s->verbose) printf ("mutation %d at bar %d: gene %d -> %d\n", s->mut_count, s->bar, g, s->gene[g]);
			return;
		}
	}
}
static const float poly_pan[PAST_POLY] = { 0.5f, 0.32f, 0.68f, 0.5f, 0.8f };

static double fold (double f, double lo)
{
	while (f >= 2 * lo) f *= 0.5;
	while (f < lo) f *= 2.0;
	return f;
}

static float poly_noise (past_state *s)
{
	s->poly_rng = s->poly_rng * 1664525u + 1013904223u;
	return (float) (s->poly_rng >> 8) / 8388608.0f - 1.0f;
}

static void poly_step (past_state *s, float *ol, float *or_)
{
	static const float level[PAST_POLY] = { 4200, 2600, 1500, 1050, 850 };
	static const float tau[PAST_POLY] = { 0.38f, 0.22f, 0.05f, 0.9f, 0.025f };
	double R = 280.1 * pow (2.0, s->key_pc / 12.0);	/* the key's root, as the choir hears it */
	uint64_t b = s->i % PAST_BAR;
	float  gate[PAST_POLY];
	int    k;

	s->bar = (int) (s->i / PAST_BAR);
	s->phrase_bar = s->bar % 4;
	if (s->i % 64 == 0)
	{
		double t = s->i / 44100.0, w = 0.5 - 0.5 * cos (2.0 * PI * t / 150.0);
		w *= 0.8 + 0.2 * (0.5 - 0.5 * cos (2.0 * PI * t / 330.0));
		s->arc_w = (float) w;
	}
	gate[0] = 0.0f;		/* the heartbeat: retired -- harm's thump (harm_fx.c) holds the downbeat */
	gate[1] = s->arc_w > 0.18f;
	gate[2] = s->arc_w > 0.42f;
	gate[3] = s->arc_w > 0.66f;
	gate[4] = s->arc_w > 0.84f;
	/* the beats in 3: the choir's waiting notes are sung, and it breathes */
	{
		int b3 = (int) ((b * (uint64_t) s->gene[0]) / PAST_BAR);
		if (b3 != s->poly_last[1] || s->poly_last[1] < 0)
		{
			int n;
			s->choir_pulse = 1.0f;
			s->firing = 1;
			for (n = 0; n < s->pend_n; n++)
				trigger (s, (int) s->pend[n][0], s->pend[n][1], s->pend[n][2], s->pend[n][3], s->pend[n][4]);
			s->firing = 0;
			s->pend_n = 0;
		}
		s->choir_pulse *= expf (-1.0f / (0.35f * 44100.0f));
	}
	if (b == 0)											/* a new bar: how long each drum has played */
	{
		for (k = 0; k < PAST_POLY; k++) s->poly_bars[k] = gate[k] ? s->poly_bars[k] + 1 : 0;
		/* (added) about every 7 minutes (6-8; 141 bars ~ 7:00), one gene mutates */
		if (s->endless && s->bar > 0 && s->bar >= s->mut_next_bar)
		{
			mutate (s);
			s->mut_next_bar = s->bar + 121 + (int) (mut_rand (s) % 41);
		}
	}
	for (k = 0; k < PAST_POLY; k++)
	{
		int idx = poly_index (s, k, b);
		float g_t = gate[k], v, dec;
		/* fades over a bar or so. Each drum guides: it comes in at full
		   while its stream is new to the ear, then after 4 bars recedes
		   to a light doubling, leaving the music to carry the rhythm */
		s->poly_g[k] += (g_t - s->poly_g[k]) * (1.0f / (1.5f * 44100));
		s->poly_guide[k] += ((s->poly_bars[k] < 4 ? 1.0f : 0.4f) - s->poly_guide[k]) * (1.0f / (3.0f * 44100));
		if (idx != s->poly_last[k])
		{
			float acc = idx == 0 ? 1.0f : (k == 1 && idx == 2) || (k == 2 && idx == 2) ? 0.8f : 0.62f;
			s->poly_last[k] = idx;
			if (s->poly_g[k] > 0.01f)
			{
				int alt = (s->bar * poly_div (s, k) + idx) % 2;
				s->poly_env[k] = acc;
				s->poly_pe[k] = 1.0f;
				switch (k)
				{
				case 0: s->poly_f[k] = (float) fold (R, 45.0); break;
				case 1: s->poly_f[k] = (float) fold (R * pow (2.0, 7 / 12.0), 150.0); break;
				case 2: s->poly_f[k] = (float) fold (R * (alt ? 1.0 : pow (2.0, 7 / 12.0)), 700.0); break;
				case 3:
				{
					/* the bell walks the chord: third, fifth, octave, fifth,
					   root over its 5 hits, the pattern turning with the bar
					   of the phrase; while sparse, only third and fifth */
					int third = s->key_minor ? 3 : 4;
					int full[5] = { third, 7, 12, 7, 0 };
					int deg = s->arc_w > 0.75f ? full[(idx + s->phrase_bar) % 5] : ((idx + s->phrase_bar) % 2 ? 7 : third);
					s->poly_f[k] = (float) fold (R * pow (2.0, deg / 12.0), 450.0);
					s->poly_att = 0;
					if (idx != 0) s->poly_env[k] *= 0.8f;		/* softer off the downbeat */
					break;
				}
				default: s->poly_f[k] = (float) fold (R * pow (2.0, 2 / 12.0), 2400.0); break;
				}
				s->poly_acc[k] = alt ? 1.0f : -1.0f;		/* side, for the bell */
			}
		}
		if (s->poly_env[k] < 1e-4f) continue;
		dec = expf (-1.0f / ((k == 3 ? 2.2f - 1.2f * s->arc_w : tau[k]) * 44100.0f));	/* the bell rings longer when sparse */
		switch (k)
		{
		case 0:		/* tuned drum: the pitch falls an octave and a half into the note */
			s->poly_ph[k] += 2.0 * PI * s->poly_f[k] * (1.0 + 1.8 * s->poly_pe[k]) / 44100.0;
			s->poly_pe[k] *= expf (-1.0f / (0.03f * 44100.0f));
			v = (float) sin (s->poly_ph[k]);
			break;
		case 1:		/* log drum: a tone and an inharmonic partial */
			s->poly_ph[k] += 2.0 * PI * s->poly_f[k] / 44100.0;
			s->poly_ph2[k] += 2.0 * PI * s->poly_f[k] * 2.76 / 44100.0;
			s->poly_pe[k] *= expf (-1.0f / (0.04f * 44100.0f));
			v = (float) (sin (s->poly_ph[k]) + 0.45 * s->poly_pe[k] * sin (s->poly_ph2[k]));
			break;
		case 2:		/* rim: band-passed noise and a short tone */
		{
			float n = poly_noise (s);
			s->poly_bp[0] += (n - s->poly_bp[0]) * 0.45f;
			s->poly_bp[1] += (s->poly_bp[0] - s->poly_bp[1]) * 0.45f;
			s->poly_ph[k] += 2.0 * PI * s->poly_f[k] / 44100.0;
			v = 0.6f * (s->poly_bp[0] - s->poly_bp[1]) * 3.0f + 0.7f * (float) sin (s->poly_ph[k]);
			break;
		}
		case 3:		/* bell: mellow FM -- near-harmonic ratio drifting between
					   1 (round) and 2 (hollow) with past's slow drive, a low
					   index that opens a little as the arc fills, a soft
					   8 ms attack and a low-pass that also opens with the arc */
		{
			double ratio = 1.5 + 0.5 * sin (s->i / 319732.0);
			float  idxm = (0.35f + 0.6f * s->arc_w) * (0.4f + 0.6f * s->poly_pe[k]);
			float  cut = 1400.0f + 2200.0f * s->arc_w;
			s->poly_ph2[k] += 2.0 * PI * s->poly_f[k] * ratio / 44100.0;
			s->poly_ph[k] += 2.0 * PI * s->poly_f[k] / 44100.0;
			s->poly_pe[k] *= expf (-1.0f / (0.6f * 44100.0f));
			s->poly_att += (1.0f - s->poly_att) * (1.0f / (0.008f * 44100.0f));
			v = s->poly_att * (float) (sin (s->poly_ph[k] + idxm * sin (s->poly_ph2[k])) + 0.18 * sin (2.0 * s->poly_ph[k]));
			s->poly_lp += (v - s->poly_lp) * (1.0f - expf (-2.0f * (float) PI * cut / 44100.0f));
			v = s->poly_lp;
			break;
		}
		default:	/* tick */
			s->poly_ph[k] += 2.0 * PI * s->poly_f[k] / 44100.0;
			v = (float) sin (s->poly_ph[k]) + 0.3f * poly_noise (s);
			break;
		}
		if (s->poly_ph[k] > 2.0 * PI * 64) s->poly_ph[k] -= 2.0 * PI * 64;
		if (s->poly_ph2[k] > 2.0 * PI * 64) s->poly_ph2[k] -= 2.0 * PI * 64;
		v *= level[k] * s->poly_env[k] * s->poly_g[k] * s->poly_guide[k];
		s->poly_env[k] *= dec;
		{
			float pan = k == 3 ? 0.5f + 0.3f * s->poly_acc[k] : poly_pan[k];
			*ol += v * (1.0f - pan) * 2.0f * 0.7071f;
			*or_ += v * pan * 2.0f * 0.7071f;
		}
	}
}

static float pad_read (const float *t, double pos)
{
	int i0 = (int) pos;
	float f = (float) (pos - i0);
	return t[i0] + f * (t[(i0 + 1) & (PAD_N - 1)] - t[i0]);
}

static void choir_voice (past_state *s, past_osc *o, int j, float amp, float *cl, float *cr)
{
	if (s->poly && !o->solo) amp *= 0.7f + 0.3f * s->choir_pulse;	/* (added) breathing in 3 */
	double f0 = 2.0 * note_to_freq (o->note) / (2.0 * PI) * (o->solo ? 1 : j % 2 + 1);	/* past's pitch, an octave up */
	float vib = (o->solo ? 0.003f : 0.0012f) * sinf (o->vph);
	const float *t;
	double rate;
	int k, side;
	(void) s;
	if (!pad[0][0]) return;							/* past_prepare_choir not called */
	if (f0 > 900.0) f0 *= 0.5;
	for (k = PAD_TABLES - 1; k > 0 && pad_base[k] > f0; k--) ;
	t = pad[o->solo ? 1 : 0][k];
	rate = f0 / pad_base[k] * (1.0 + vib);
	o->vph += (float) (2.0 * PI * (4.6 + 0.3 * (j % 3)) / 44100.0);
	if (o->vph > 2.0f * (float) PI) o->vph -= 2.0f * (float) PI;
	/* two read positions half a loop apart: decorrelated left and right
	   (the solo is one singer: the same position, centred) */
	for (side = 0; side < 2; side++)
	{
		o->cph[side] += rate;
		if (o->cph[side] >= PAD_N) o->cph[side] -= PAD_N;
	}
	if (o->solo)
	{
		float v = 1.8f * amp * pad_read (t, o->cph[0]);
		*cl += v; *cr += v;
	}
	else
	{
		/* the choir's richness (choir_rich, 0..1; 0.5 = as it was): thin, it
		   narrows towards mono and thins out; rich, a second rank an octave up
		   and a third a fifth up join, the stereo opens fully, and it swells */
		float cr_ = s->choir_rich, a0 = 1.4f * amp * (0.7f + 0.6f * cr_);
		float l = pad_read (t, o->cph[0]), r = pad_read (t, o->cph[1]), w = 0.35f + 1.3f * cr_;
		float up = cr_ > 0.5f ? (cr_ - 0.5f) * 2.0f : 0.0f;
		if (w > 1) w = 1;
		if (up > 0)
		{
			const float *t2 = pad[0][k < PAD_TABLES - 1 ? k + 1 : k];
			double r2 = 2.0 * f0 / pad_base[k < PAD_TABLES - 1 ? k + 1 : k] * (1.0 + vib);
			double r3 = 1.5 * rate;
			int sd;
			for (sd = 2; sd < 5; sd++)
			{
				o->cph[sd] += sd < 4 ? r2 : r3;
				if (o->cph[sd] >= PAD_N) o->cph[sd] -= PAD_N;
			}
			l += up * (0.42f * pad_read (t2, o->cph[2]) + 0.3f * pad_read (t, o->cph[4]));
			r += up * (0.42f * pad_read (t2, o->cph[3]) + 0.3f * pad_read (t, o->cph[4]));
		}
		{
			float m = 0.5f * (l + r);
			*cl += a0 * (m + w * (l - m));
			*cr += a0 * (m + w * (r - m));
		}
	}
}

/* (added) the opening: one voice of the choir alone, singing past's scale
   slowly (a note every 6 s, ~1.4 s swell, ~1 s fade) until the choir proper
   enters with the bass pairs at 1:00. It is not held to the voice cap. */
static void trigger_solo (past_state *s, int note)
{
	int o = 0;
	while ((o < PAST_OSCS) && (s->osc[o].is_on)) o++;
	if (o >= PAST_OSCS) return;
	if (s->endless && note > 44) note -= 12 * ((note - 33) / 12);
	if (s->harmony) note = snap_note (s, note);
	s->osc[o].is_on = 1;
	s->osc[o].a = 0.25f;
	s->osc[o].r = 1.0f;
	s->osc[o].dur = 5.5f;
	s->osc[o].sec = 0.0f;
	s->osc[o].amp = 1.0f;
	s->osc[o].acc = note_to_freq (note) / 44100.0;
	s->osc[o].note = note;
	s->osc[o].choir = 1;
	s->osc[o].solo = 1;
	s->osc[o].cph[0] = s->osc[o].cph[1] = (double) (((unsigned) ((note + 64) * 104729 + o * 7919)) % PAD_N);
	s->osc[o].vph = 0;
}

static float choir_filter (past_state *s, int side, float x)
{
	static const float g[3] = { 1.0f, 0.55f, 0.28f };
	float y = 0;
	int f;
	for (f = 0; f < 3; f++)
	{
		float *z = s->fz[side][f], b0 = s->fcoef[f][0];
		float o = b0 * x + z[0];						/* transposed direct form II */
		z[0] = -s->fcoef[f][1] * o + z[1];
		z[1] = -b0 * x - s->fcoef[f][2] * o;
		y += g[f] * o;
	}
	s->body[side] += (x - s->body[side]) * 0.12f;		/* ~900 Hz low-pass: chest */
	return y + 0.12f * s->body[side];
}

void past_init (past_state *s)
{
	static const int sc[]  = {0, 3, 0, 5,  0, 1, 7, 4, 5};
	static const int sc1[] = {0, 2, 7, 10, 2, 5, 2, 0, 7};
	static const int sc2[] = {3, 4, 7,  3, 4, 7, 2, 4, 6};
	int j;

	memset (s, 0, sizeof *s);
	s->gene[0] = 3; s->gene[1] = 3; s->gene[2] = 5; s->gene[3] = 7; s->gene[4] = 11;	/* (added) */
	s->mut_next_bar = 121 + 20;		/* the first at ~7:00 */
	s->mut_gene = -1;
	s->mut_rng = 0x2545f491u;
	memcpy (s->scale, sc, sizeof sc);
	memcpy (s->scale1, sc1, sizeof sc1);
	memcpy (s->scale2, sc2, sizeof sc2);

	s->x = 0; s->ox1 = 0.0; s->ox2 = 0.0; s->ox3 = 0.0;
	s->y = 0; s->oy1 = 0.0; s->oy2 = 0.0; s->oy3 = 0.0;
	s->u = 0; s->v = 0;
	s->i = 0;
	s->tick = 0;
	s->phase = 0.0;

	for (j = 0; j < PAST_OSCS; j++)
	{
		s->osc[j].phase = 0.0;
		s->osc[j].is_on = 0;
		s->osc[j].amp = 1.0;
	}

	s->n = 0;
	s->k = 1;
	s->cap = PAST_OSCS;
	s->poly_rng = 2003u;
	for (j = 0; j < PAST_POLY; j++) { s->poly_last[j] = -1; s->poly_guide[j] = 1; }
	s->bass_ph3[0] = 0.0; s->bass_ph3[1] = 2.1; s->bass_ph3[2] = 4.2;
	s->min = 0;
	s->sec = 0;
}

int past_next (past_state *s, short *left, short *right)
{
	int			j, k;
	const int	scale_len = PAST_SCALE_LEN;
	int			*scale = s->scale, *scale1 = s->scale1, *scale2 = s->scale2;
	uint64_t i;

	/* (changed) endless: past stopped at 8:00; carried on, its maths keeps
	   going -- the tick count, the scale swaps, every sin(i/...) -- except
	   that its sections (gated by the minute) cycle every 8 minutes, its
	   tick period stops shrinking at 8192 samples, and notes fold down by
	   octaves above its 8-minute range (the swaps lift them ~3 semitones a
	   minute, out of hearing within the hour). */
	if (s->done || (!s->endless && s->min >= 8)) { s->done = 1; *left = *right = 0; return 0; }
	{
	int smin = s->endless ? s->min % 8 : s->min;				/* (added) the section clock */
	long tper = 26384 - s->min*30 - s->sec%3;					/* (added) */
	if (tper < 8192) tper = 8192;

	i = ++s->i;

	/* (added) the mood: oppressive at the start, opening over ~0:20-3:20.
	   Endless, it cycles every 13 minutes: open until ~10:50, closing over
	   the next two minutes, ~30 s dark, then opening again as at the start
	   (the first eight minutes are as they were) */
	if (s->arc)
	{
		double t = s->endless ? fmod (i / 44100.0, MOOD_CYCLE) : i / 44100.0;
		float x = (float) ((t - 20.0) / 180.0), y;
		x = x < 0 ? 0 : x > 1 ? 1 : x;
		s->mood = x * x * (3 - 2 * x);
		if (s->endless && t > MOOD_CYCLE - 130.0)
		{
			y = (float) ((t - (MOOD_CYCLE - 130.0)) / 120.0);
			y = y > 1 ? 1 : y;
			s->mood *= 1.0f - y * y * (3 - 2 * y);
		}
	}
	else s->mood = 1.0f;

	/* (added) the long swells: the choir's richness and the deep bass's
	   weight each ride a sine, of 5 and 9 minutes (coprime: the pair only
	   repeats every 45). The choir starts thin and the bass halfway, rising,
	   so the oppressive opening is low and bare. Without the endless clock
	   both sit at their middle, the sound before they were added. */
	if (s->endless)
	{
		if (i % 64 == 1)
		{
			double t = i / 44100.0;
			s->choir_rich = (float) (0.5 - 0.5 * cos (2.0 * PI * t / 300.0));
			s->bass_rich  = (float) (0.5 + 0.5 * sin (2.0 * PI * t / 540.0));
		}
	}
	else s->choir_rich = s->bass_rich = 0.5f;

	if (i % 44100 == 44099)
	{
		s->sec ++;
		if (s->sec >= 60) { s->sec = 0; s->min++; }
		if (s->verbose) printf ("%02d:%02d - \n", s->min, s->sec);
	}

	s->otick = s->tick;
	if ((i % (uint64_t) tper == 1))
	{
		s->tick++;
		if (s->tick % 17 == 0)
		{
			j = (s->tick % (scale_len - 1));
			{
				k = scale[j];
				scale[j] = scale[(j+1) % scale_len];
				scale[(j+1) % scale_len] = k;
			}
		}

		if (s->tick % 19 == 18)
		{
			j = (s->tick % (scale_len - 1));
			{
				k = scale2[j];
				scale2[j] = scale[(j+1) % scale_len];
				scale[(j+1) % scale_len] = k;
			}
		}

		if (s->tick % 23 == 22)
		{
			j = (s->tick % (scale_len - 1));
			{
				k = scale1[j];
				scale1[j] = scale2[(j+1) % scale_len];
				scale2[(j+1) % scale_len] = k + 12 - scale[j % scale_len]%6;
			}
		}
		if (s->verbose)
		{
			printf ("\n");
			for (j=0; j<scale_len; j++) printf ("%d", scale[j]);
		}
	}

	/* (added) swing: on odd 8192-sample steps the three step-grid triggers
	   below fire `swing` samples late. With swing = 0, ii == i (original). */
	{
	uint64_t ii = ((i / 8192) % 2) ? i - (uint64_t) s->swing : i;
	if ((ii % 8192== 0) && ((s->tick/13) % 3) && (s->sec < 20))
	{
		trigger (s, scale[s->n%scale_len] + 12, 0.01,  1.0 - 0.5*sin(i/123497.0), 0.1, 3.0);
		if (s->verbose) { printf ("."); fflush (stdout); }
	}
	if ((ii % 8192== 1024)  && ((s->tick/17) % 3) && (smin < 4))
	{
		trigger (s, scale[s->n%scale_len] + 12, 0.01,  0.5 - 0.25*sin(i/13497.0), 0.1, 3.0);
		if (s->verbose) { printf ("+"); fflush (stdout); }
	}
	if (ii % 8192== (s->tick % 17) * 64 + 1024 && (s->sec < 45))
	{
		trigger (s, scale[s->n%scale_len] + 12, 0.03 + 0.02*sin(i/23443.0),  0.7 - 0.45*sin(i/23497.0), 0.1, 3.0);
		if (s->verbose) { printf (","); fflush (stdout); }
	}
	}

	if ((s->otick != s->tick) && (smin < 6) && smin )
	{
		if ((smin < 4) || (s->sec % 15 == 0))
		if ( (s->tick % 16 == 3) ||
			 (s->tick % 16 == 7) ||
			 (s->tick % 16 == 9) ||
			 (s->tick % 16 == 12) )
		{
			s->n = (s->n+1) % scale_len;
			trigger (s, scale[(s->n+0)%scale_len]-12, 0.3, 2.0 - 0.5*sin(i/38497.0), 0.1, 0.3);
			trigger (s, scale[(s->n+2)%scale_len]-12, 0.3, 2.0 - 0.5*sin(i/38497.0), 0.1, 0.3);
		}
	}

	if ((s->otick != s->tick) && (s->sec < 25))
	{
		if (i % 3 == 0)
		{
			trigger (s, scale2[(s->n%17)%scale_len] - 12, 0.03, 1.0, 0.1,
				0.7 + 0.6*sin(i/341897.0 + s->tick*3));
			trigger (s, scale2[(s->n%15)%scale_len] - 12, 0.03, 1.0, 0.1,
				0.7 + 0.6*sin(i/343897.0 + s->tick*2));
		}
	}

	/* (added) every 2 bars, in the first 54 s (endless: of each mood cycle) */
	if (s->choir && (s->endless ? fmod (i / 44100.0, MOOD_CYCLE) < 54.0 : i < 54UL * 44100) && i % (2UL * PAST_BAR) == 1)
	{
		/* the opening solo: while the mood is dark, a low lament around the
		   key's root (root, b2, root, b7 below, root, b2, b6 below, root);
		   later past's scale as before */
		static const int lament[8] = { 0, 1, 0, -2, 0, 1, -4, 0 };
		int k = (int) (i / (2UL * PAST_BAR));
		trigger_solo (s, s->mood < 0.5f ? s->key_pc - 12 + lament[k % 8] : scale[k % scale_len]);
	}

	s->outl = s->outr = 0;

	s->ox3 = s->ox2; s->oy3 = s->oy2;
	s->ox2 = s->ox1; s->oy2 = s->oy1;
	s->ox1 = s->x; s->oy1 = s->y;

	s->y = s->x = 0.0;
	s->u = s->v = 0.0;

	s->num_on = 0;
	s->acc = 0.0;
	if (s->choir && i % 64 == 1) choir_formants (s);		/* (added) */
	{ float cl = 0, cr = 0;									/* (added) choir bus */
	for (j=0; j<PAST_OSCS; j++)
	{
		past_osc *o = &s->osc[j];
		o->env = 0.0f;					/* (added) */
		if (o->is_on)
		{
			float	sum, amp;
			int		k, num;

			s->num_on ++;
			o->sec += 1.0 / 44100.0;
			o->phase += o->acc;
			s->acc += o->acc;
			if (o->phase >= 164.0 * PI) o->phase = 0.0;

			if (o->sec < o->dur*o->a)
			{
				amp = o->amp * (o->sec / (o->dur*o->a));
			} else
			if (o->sec < o->dur)
			{
				amp = o->amp;
			} else
			{
				amp = o->amp - (o->sec-o->dur)*o->r;
			}

			if (amp < 0.0)
			{
				o->is_on = 0;
				amp = 0.0;
			}
			o->env = amp;				/* (added) */
			if (o->choir) { choir_voice (s, o, j, amp, &cl, &cr); continue; }	/* (added) */

			num = 2 + (j % 7);
			sum = num * (num-1) * 0.5;
			for (k = 0; k < num; k++)
			if (k % 2 == 1)
			{
				if  (j % 2 == 1)
				{
					s->u += ((num-k) / sum) * amp *
						(
						atan(
							sin(o->phase*k*0.25*(j%2 + 1)) *
							sin(i / 319732.0) * 7.0
							)
						) ;
				} else
				{
					s->v += ((num-k) / sum) * amp *
						(
						atan(
							sin(o->phase*0.25*k*(j%2 + 1)) *
							cos(i / 384972.0) * 7.0
							)
						) ;
				}
			} else
			{
				if  (j % 2 == 1)
				{
					s->x += ((num-k) / sum) * amp *
						(
							sin(o->phase*k*(j%2 + 1))
						) ;
				} else
				{
					s->y += ((num-k) / sum) * amp *
						(
							sin(o->phase*k*(j%2 + 1))
						) ;
				}
			}
		}
	}

	s->voices = s->num_on;										/* (added) */
	s->mean_acc = s->num_on ? s->acc / s->num_on : 0.0;			/* (added) */
	s->acc /= (float) s->num_on;
	s->phase += s->acc;
	s->num_on = PAST_OSCS;
	s->y =  s->y + 0.30*sin(i / 441000.0)*s->oy1 - (0.75*sin(i/894712.0 + s->tick) + 0.23 )*
					(0.3*sin(i/20127.0 + s->tick%7)*s->ox2 - 0.47*sin(i/33391.0)*s->ox3);
	s->x =  s->x+ 0.30*sin(i / 441000.0)*s->ox1 - (0.75*sin(i/749732.0 + s->tick) + 0.26)*
					(0.3*sin(i/42129.0 + s->tick%3)*s->oy2 - 0.53*sin(i/33385.0)*s->oy3);

	s->outl += 32768 * (atan((s->x+s->v) / (float)s->num_on) / 1.6 );
	s->outr += 32768 * (atan((s->y+s->u) / (float)s->num_on) / 1.6 );

	if (s->choir || s->poly)									/* (added) */
	{
		float l = 0, r = 0;
		if (s->choir)
		{
			float b = choir_bass (s);
			l = 650.0f * choir_filter (s, 0, cl) + b;
			r = 650.0f * choir_filter (s, 1, cr) + b;
			s->choir_level = (fabsf (l) + fabsf (r)) / 16384.0f;
		}
		if (s->poly) { s->poly_l = s->poly_r = 0; poly_step (s, &s->poly_l, &s->poly_r); }
		s->outl = (short) lrintf (32767.0f * tanhf ((s->outl + l) / 32767.0f));
		s->outr = (short) lrintf (32767.0f * tanhf ((s->outr + r) / 32767.0f));
	}
	}

	}															/* (added) the section clock's block */
	*left = s->outl;
	*right = s->outr;
	return 1;
}

/* (added) the feedback / drive coefficients, evaluated for the visuals only */
void past_coefficients (past_state *s)
{
	double i = (double) s->i;
	s->fb_slow   = 0.30 * sin (i / 441000.0);
	s->fb_y_gain = 0.75 * sin (i / 894712.0 + s->tick) + 0.23;
	s->fb_x_gain = 0.75 * sin (i / 749732.0 + s->tick) + 0.26;
	s->fb_y_mod  = 0.3 * sin (i / 20127.0 + s->tick % 7);
	s->fb_x_mod  = 0.3 * sin (i / 42129.0 + s->tick % 3);
	s->fb_y_ox3  = 0.47 * sin (i / 33391.0);
	s->fb_x_oy3  = 0.53 * sin (i / 33385.0);
	s->drive_u   = sin (i / 319732.0);
	s->drive_v   = cos (i / 384972.0);
}

/* (added) enter the piece at sample `target` without playing up to it (the
   transport's long jumps). The structure is computed exactly, as if every
   sample before had been played: the clock (min, sec), the tick count, the
   scale rows' swaps on the 17 / 19 / 23-tick clocks, and the rhythm's
   mutations; the mood, swells and voice arc follow from the sample index.
   What only exists by playing -- the sounding voices, the feedback
   filters' memory -- starts fresh and settles within seconds. Call after
   past_init. Ten years is ~1.7e9 ticks, ~2.6e8 row swaps: about a second. */
void past_warp (past_state *s, uint64_t target)
{
	const int L = PAST_SCALE_LEN;
	uint64_t c, cmax, tick = 0, t, n17, n19, n23;
	int64_t bar;
	if (target == 0) return;
	/* ticks: sample i (1..target) ticks when i % tper == 1, tper set at the
	   top of past_next from the clock as it stood: second c = i / 44100 */
	cmax = target / 44100;
	for (c = 0; c <= cmax; c++)
	{
		uint64_t m = c / 60, sc = c % 60, lo = c * 44100, hi = c * 44100 + 44099, tper;
		long tp = 26384 - (long) m * 30 - (long) (sc % 3);
		if (lo < 1) lo = 1;
		if (hi > target) hi = target;
		tper = (uint64_t) (tp < 8192 ? 8192 : tp);
		if (tp <= 8192 - 2 && m >= 607)
		{
			/* from here on tper stays 8192: the rest in one count */
			hi = target;
			tick += (hi + tper - 1) / tper - (lo + tper - 2) / tper;
			break;
		}
		/* count of i in [lo, hi] with i % tper == 1 */
		if (hi >= lo) tick += (hi + tper - 1) / tper - (lo + tper - 2) / tper;
	}
	s->i = target;
	s->sec = (int) (((target + 1) / 44100) % 60);
	s->min = (int) ((target + 1) / 44100 / 60);
	/* the rows: every swap in order (at a tick, 17's before 19's before 23's) */
	n17 = 17; n19 = 18; n23 = 22;
	for (;;)
	{
		int j, k;
		t = n17 < n19 ? (n17 < n23 ? n17 : n23) : (n19 < n23 ? n19 : n23);
		if (t > tick) break;
		j = (int) (t % (uint64_t) (L - 1));
		if (t == n17)
		{
			k = s->scale[j]; s->scale[j] = s->scale[(j + 1) % L]; s->scale[(j + 1) % L] = k;
			n17 += 17;
		}
		if (t == n19)
		{
			k = s->scale2[j]; s->scale2[j] = s->scale[(j + 1) % L]; s->scale[(j + 1) % L] = k;
			n19 += 19;
		}
		if (t == n23)
		{
			k = s->scale1[j]; s->scale1[j] = s->scale2[(j + 1) % L];
			s->scale2[(j + 1) % L] = k + 12 - s->scale[j % L] % 6;
			n23 += 23;
		}
	}
	s->tick = (int) tick; s->otick = s->tick;
	/* the bars, and the mutations at their downbeats (sample bar * PAST_BAR) */
	bar = (int64_t) (target / PAST_BAR);
	s->bar = (int) bar; s->phrase_bar = s->bar % 4;
	if (s->endless && s->poly)
		while (s->mut_next_bar <= bar)
		{
			s->bar = s->mut_next_bar;
			mutate (s);
			s->mut_next_bar = s->bar + 121 + (int) (mut_rand (s) % 41);
		}
	s->bar = (int) bar; s->phrase_bar = s->bar % 4;
}
