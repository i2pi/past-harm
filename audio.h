/* Runs past -> harm's effects (on the audio thread, or offline) and publishes a
 * snapshot of both every SNAP_FRAMES output frames, so the visuals can read
 * what is being heard right now. */
#ifndef AUDIO_H
#define AUDIO_H

#include <stdio.h>
#include "past_engine.h"
#include "harm_fx.h"
#include "reverb.h"

#define SNAP_FRAMES	512

typedef struct
{
	unsigned long out;			/* output frame index at the end of this block */
	unsigned long sample;		/* past's sample index (slower during the time shift) */
	int		tick, min, sec, done;
	int		scale[PAST_SCALE_LEN], scale1[PAST_SCALE_LEN], scale2[PAST_SCALE_LEN];
	float	env[PAST_OSCS];		/* voice envelopes (max over the block) */
	int		note[PAST_OSCS];
	char	on[PAST_OSCS];
	int		voices;
	double	mean_acc;
	double	spin;				/* running integral of the mean voice increment */
	float	fb_slow, fb_y_gain, fb_x_gain, fb_y_mod, fb_x_mod, fb_y_ox3, fb_x_oy3;
	float	drive_u, drive_v;
	float	rms_l, rms_r;		/* output level of the block, 0..1 */
	float	uv;					/* energy of the atan (u,v) voices vs the plain ones */

	/* harm */
	int		section;			/* harm's c */
	char	chanon[HARM_CHANS];
	char	nofx[2], nofxfeed[2];			/* past's two networks (harm slots 2, 3) */
	int		clears[2];						/* clear events so far on those networks */
	unsigned int tapnum[2][HARM_TAPS];		/* their taps (samples) */
	float	gback[2][HARM_TAPS];			/* and feedback gains (unscaled) */
	float	shift;				/* (unused: harm's time shift was removed) */
	float	click[3];			/* (unused: harm's clicks were removed) */
	int		key_pc, key_minor;	/* past's key (0 = A), as tracked by the audio thread */
	float	cycle;				/* position in harm's 9.07 s cycle, 0..1 */
	int		chord_set;			/* harm's chord 0-2, or 3 = its rest */
	float	chord_level, swell_level;	/* 0..1 (max over the block) */
	float	choir;					/* the choir's level (max over the block) */
	float	poly[PAST_POLY];		/* the polyrhythm layers' envelopes (2, 3, 4, 5, 7) */
	int		bar, phrase_bar;
	float	rv_sparse, rv_wet, rv_rt60, rv_level;	/* the reverb: past's sparseness, send, tail (s), output 0..1 */
} past_snap;

/* swing: delay of every other 8192-sample step's triggers, as a fraction of
   a step (0 = past's original timing); call before audio_start / seek */
void	audio_set_swing (float frac);
/* start real-time playback (mute: no device, a real-time thread instead) */
int		audio_start (int mute, int loop);
/* the snapshot for what is audible now; returns 0 before the first one */
int		audio_now (past_snap *out, double *seconds);
/* offline: run up to `seconds` of output and make that the "now" (no audio) */
void	audio_seek_offline (double seconds);
/* offline: also write every output frame (16-bit stereo, L R) to f */
void	audio_capture (FILE *f);

#endif
