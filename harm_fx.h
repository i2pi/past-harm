/* harm's convolution and rhythm (dsp/harm3.c, before 2003) applied to
 * past's output. See harm_fx.c for exactly what is copied and what changed. */
#ifndef HARM_FX_H
#define HARM_FX_H

#define HARM_CHANS		7
#define HARM_CBUF_SIZE	50000
#define HARM_TAPS		7

typedef struct
{
	unsigned int	tapnum;
	float			gforward;
	float			gback;
} harm_tap;

typedef struct
{
	int		idx;
	float	data[HARM_CBUF_SIZE];
	harm_tap tap[HARM_TAPS];
} harm_cbuf;

typedef struct
{
	harm_cbuf	cbuf[HARM_CHANS];
	unsigned int chan[HARM_CHANS];
	char		nofx[HARM_CHANS], nofxfeed[HARM_CHANS], chanon[HARM_CHANS];
	uint64_t i, k, l, c, q, z;

	/* (added) what the visuals read */
	float		gback_raw[HARM_CHANS][HARM_TAPS];	/* gains before the stability scaling */
	int			event_clear[HARM_CHANS];			/* counts of events, for edge detection */
	int			repeat;								/* extra copies of this sample (time shift) */
	float		click[3];							/* |click1..3| this sample, 0..1 */

	/* harm's chords and swell, re-pitched into past's key */
	float		key_factor, key_target;				/* transposition of harm's pitches */
	double		ph_ch[3], ph_sw[5];					/* oscillator phases (so pitch can glide) */
	float		amp_ch, amp_rest, amp_sw;			/* arrangement fades */
	int			chord_set;							/* 0-2, or 3 = harm's rest */
	float		chord_level, swell_level;			/* 0..1, for the visuals */
	float		cycle;								/* position in harm's 400000-sample cycle */

	/* (added) de-clicking of past's networks: harm switched these in one sample */
	float		wet_s[HARM_CHANS], feed_s[HARM_CHANS];	/* echo level, input to the network */
	float		gs[HARM_CHANS][HARM_TAPS];			/* feedback gains, glided */
	char		clear_pend[HARM_CHANS];				/* clear once the echoes have faded out */

	/* (added) harm's drums, re-timed to past's bar -- set by the caller */
	uint64_t t;								/* past's sample index */
	float		arc;								/* past's arc of voices, 0..1 */
	int			key_pc;
	/* (added) the rhythm genes (past_state.gene): the left and right delay
	   grids (hits per bar) and the high tick (against 8) -- set by the caller */
	int			ldiv, rdiv, tdiv;
	double		tp[HARM_CHANS][HARM_TAPS];			/* tap positions, gliding to the grid (0: unset) */
	/* their state */
	double		ph_thump, ph_tick;
	float		g_dr[4];							/* fades: blip, thump, 11:8 tick, 5-bar sweep */
	float		edge[3];							/* 2 ms ramps on the gates */
} harm_state;

void	harm_init (harm_state *h);
/* the key to put harm's chords and swell in (past's pitch class: 0 = A) */
void	harm_set_key (harm_state *h, int pc);
/* one generator sample: past's (l, r) in, harm's mix out; *copies = how many
   times to write it (always 1: harm's time shift is not used) */
void	harm_step (harm_state *h, short in_l, short in_r, short *out_l, short *out_r, int *copies);

#endif
