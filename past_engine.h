/* past (before 2003) as a sample engine.
 *
 * This is the main loop of old_i2pi/music/past2/past.c turned inside out: the
 * same state and arithmetic, one stereo sample per call. Its output is
 * sample-identical to the original program (see README, "Verification").
 * Everything the music computes is in past_state so the visuals can read it.
 */
#ifndef PAST_ENGINE_H
#define PAST_ENGINE_H

#include <stdint.h>

#define PAST_OSCS		16
#define PAST_SCALE_LEN	9
#define PAST_RATE		44100
#define PAST_POLY		5		/* (added) polyrhythm layers: 2, 3, 4, 5, 7 per bar */
#define PAST_BAR		131072	/* (added) 16 of past's 8192-sample steps (2.97 s) */

typedef struct
{
	char	is_on;
	float	dur, sec;
	float	a, r;
	float	amp;			/* peak amplitude given at trigger */
	double	phase;
	int		note;
	double	acc;			/* phase increment per sample (freq / 44100) */
	float	env;			/* (added) the envelope value computed this sample */
	char	choir;			/* (added) sung by the choir instead of past's timbre */
	double	cph[5];			/* (added) the choir's wavetable read positions (left, right) */
	float	vph;			/* (added) its vibrato phase */
	char	solo;			/* (added) the opening's single choir voice */
} past_osc;

typedef struct
{
	/* --- the original program's state --- */
	past_osc	osc[PAST_OSCS];
	uint64_t i;		/* sample counter */
	float	x, y, ox1, oy1, ox2, oy2, ox3, oy3;
	float	u, v;
	int		tick, otick;
	int		scale[PAST_SCALE_LEN], scale1[PAST_SCALE_LEN], scale2[PAST_SCALE_LEN];
	int		n, k;
	int		min, sec;
	int		num_on;
	double	acc, phase;		/* mean voice increment; its running integral */
	short	outl, outr;

	/* --- (added) derived values the original computed inline --- */
	int		voices;			/* oscillators sounding this sample */
	double	mean_acc;		/* acc / voices, 0 when silent (the original's was NaN) */
	float	fb_y_gain;		/* 0.75*sin(i/894712+tick) + 0.23   (y feedback) */
	float	fb_x_gain;		/* 0.75*sin(i/749732+tick) + 0.26   (x feedback) */
	float	fb_y_mod;		/* 0.3*sin(i/20127 + tick%7) */
	float	fb_x_mod;		/* 0.3*sin(i/42129 + tick%3) */
	float	fb_y_ox3;		/* 0.47*sin(i/33391) */
	float	fb_x_oy3;		/* 0.53*sin(i/33385) */
	float	fb_slow;		/* 0.30*sin(i/441000) */
	float	drive_u;		/* sin(i/319732) -- atan drive of the odd voices */
	float	drive_v;		/* cos(i/384972) -- atan drive of the even voices */
	int		done;
	int		verbose;		/* print the original's console output */
	float	mood;			/* (added) 0 = sparse and oppressive (the opening) .. 1 = open */
	float	choir_rich;		/* (added) the choir's richness, 0..1: a sine of period 5 min (endless) */
	float	bass_rich;		/* (added) the deep bass's weight, 0..1: a sine of period 9 min (endless) */
	int		endless;		/* (added) 1: carry on past 8:00 (see past_next) */
	int		arc;			/* (added) 1: limit the sounding voices to a slow arc
							   that starts sparse, builds, recedes and builds
							   again (see voice_cap); 0: past's original */
	int		cap;			/* (added) the current limit (PAST_OSCS without arc) */
	int		harmony;		/* (added) 1: snap new notes into the key (see snap_note) */
	int		key_pc, key_minor;	/* (added) the key to snap to (pc 0 = past's note 0), set by the caller */
	int		choir;			/* (added) 1: the sustained bass pairs are sung by a choir */
	float	fz[2][3][2];	/* (added) the choir's formant filters: side, formant, state */
	float	fcoef[3][3];	/* (added) their coefficients: b0, a1, a2 (b2 = -b0) */
	float	body[2];		/* (added) the choir's low-passed body, per side */
	float	choir_level;	/* (added) |choir| this sample, for the visuals */
	float	bass_f, bass_amp;	/* (added) the choir's bass: frequency, level */
	int		poly;			/* (added) 1: the polyrhythm layers (see poly_step) */
	float	poly_env[PAST_POLY];	/* (added) each layer's envelope (for the visuals) */
	float	poly_g[PAST_POLY];		/* (added) each layer's fade in / out */
	float	poly_pe[PAST_POLY];		/* (added) pitch / FM envelope */
	double	poly_ph[PAST_POLY], poly_ph2[PAST_POLY];
	float	poly_f[PAST_POLY], poly_acc[PAST_POLY];
	int		poly_last[PAST_POLY];
	unsigned int poly_rng;
	float	poly_lp, poly_att;		/* (added) the bell's low-pass and attack */
	float	poly_bp[2];			/* (added) the rim layer's noise filter state */
	float	poly_l, poly_r;		/* (added) this sample's polyrhythm, kept apart from past's
								   output so the caller can add it after harm's echoes */
	float	poly_guide[PAST_POLY];	/* (added) a drum leads its stream in, then recedes */
	int		poly_bars[PAST_POLY];	/* (added) bars since each drum came in */
	int		pend_n, firing;			/* (added) choir notes waiting for the next beat in 3 */
	float	pend[8][5];				/* note, dur, amp, a, r */
	float	choir_pulse;			/* (added) the choir breathing in 3 */
	float	arc_w;				/* (added) the arc's position, 0 (sparse) .. 1 (full) */
	int		bar, phrase_bar;	/* (added) bars of 16 steps (131072 samples); bar in its 4-bar phrase */
	/* (added) the rhythm's genes, mutating (endless): every ~7 minutes one,
	   picked at random, takes a new value coprime with the others
	     0  the choir's beat = the log drum, hits per bar      (3)
	     1  past's accent grouping = the rim, steps per group   (3: 3+3+3+3+4)
	     2  harm's left delay grid = the bell, per bar          (5)
	     3  harm's right delay grid = the tick, per bar         (7)
	     4  harm's high tick, against 8 per bar                (11) */
	int		gene[5];
	int		mut_next_bar;		/* when the next mutation is due */
	int		mut_gene, mut_count;	/* the last one changed (-1: none yet), how many so far */
	unsigned int mut_rng;
	double	bass_ph, bass_ph3[3];
	float	bass_lp;
	int		swing;			/* (added) delay, in samples, of the triggers on every
							   other 8192-sample step; 0 = the original timing */
} past_state;

void	past_init (past_state *s);
/* (added) build the choir's wavetables (once, ~6 MB; call before using choir) */
void	past_prepare_choir (void);
/* advance one sample; returns 0 once the piece has ended (8:00) */
int		past_next (past_state *s, short *left, short *right);
/* (added) enter the piece at sample `target` (after past_init): the clock,
   ticks, scale rows and mutations exact; voices and filters fresh */
void	past_warp (past_state *s, uint64_t target);
/* (added) fill the fb_* / drive_* fields from the current sample position */
void	past_coefficients (past_state *s);

#endif
