/* A soft stereo reverb after harm's networks that swells to fill the space
 * when past's voices thin out: its level and tail length follow how much of
 * past is sounding (the sum of the voice envelopes, smoothed). */
#ifndef REVERB_H
#define REVERB_H

#define RV_LINES	8
#define RV_MAXDEL	6000
#define RV_AP		4

typedef struct
{
	float	line[RV_LINES][RV_MAXDEL];
	int		len[RV_LINES], pos[RV_LINES];
	float	lp[RV_LINES];				/* damping filter state */
	float	ap[2][RV_AP][1200];			/* input diffusers, per side */
	int		ap_len[2][RV_AP], ap_pos[2][RV_AP];
	float	hp_l, hp_r;					/* input high-pass state */
	float	density;					/* smoothed envelope sum */
	float	sparse;						/* 0 = past is full, 1 = (nearly) silent */
	float	wet;						/* current send level */
	float	rt60;						/* current tail length, seconds */
	float	mood;						/* set by the caller: 0 dark .. 1 open (see reverb_step) */
	float	tone_l, tone_r;				/* the darkening low-pass */
	float	level;						/* |reverb output|, 0..1, smoothed (for the visuals) */
} reverb_state;

void	reverb_init (reverb_state *r);
/* env_sum: the sum of past's voice envelopes this sample (0..~12) */
void	reverb_step (reverb_state *r, float env_sum, short *l, short *rr);

#endif
