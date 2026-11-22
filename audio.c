/* audio: AudioQueue playback of past -> harm effects -> reverb, + state snapshots */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#ifndef AUDIO_WORKLET
#include <pthread.h>
#include <unistd.h>
#include <time.h>
#include <AudioToolbox/AudioToolbox.h>
#define LOCK()		pthread_mutex_lock (&mx)
#define UNLOCK()	pthread_mutex_unlock (&mx)
#else
/* web: built alone into the AudioWorklet's WebAssembly (see web/), where the
   snapshots are read between render calls on the same thread */
#define LOCK()
#define UNLOCK()
#endif
#include "audio.h"
#include "key.h"

#define RING		1024			/* snapshots kept: ~12 s */
#define AQ_FRAMES	1024
#define AQ_BUFS		3

static past_state		eng;
static harm_state		*hfx;			/* ~1.4 MB of delay lines */
static key_tracker		key;
static reverb_state		*rv;			/* ~200 KB of delay lines */
static past_snap		ring[RING];
static uint64_t	ring_n;			/* snapshots written */
#ifndef AUDIO_WORKLET
static pthread_mutex_t	mx = PTHREAD_MUTEX_INITIALIZER;
static AudioQueueRef	aq;
#endif
static int				offline;
static double			spin;
static uint64_t			t0_ns;			/* wall clock of output frame 0 */
static uint64_t	out_n, offline_now;
static int				swing_samples;
static FILE				*cap;				/* offline capture of the output */

void audio_capture (FILE *f) { cap = f; }

void audio_set_swing (float frac) { swing_samples = (int) lroundf (frac * 8192.0f); }

/* pending output frames of the current generator sample (time shift: 1-3) */
static short	pend_l, pend_r;
static int		pend;

/* block accumulators */
static int		blk_n, blk_shift;
static double	blk_l2, blk_r2, blk_uv, blk_xy;
static float	blk_env[PAST_OSCS], blk_click[3], blk_chord, blk_swell, blk_choir;

#ifndef AUDIO_WORKLET
static uint64_t now_ns (void) { return clock_gettime_nsec_np (CLOCK_UPTIME_RAW); }
#else
static uint64_t now_ns (void) { return 0; }
#endif

static void reset (void)
{
	past_init (&eng);
	eng.swing = swing_samples;
	eng.arc = 1; eng.harmony = 1; eng.choir = 1; eng.poly = 1; eng.endless = 1;
	past_prepare_choir ();
	if (!hfx) hfx = (harm_state *) malloc (sizeof *hfx);
	harm_init (hfx);
	if (!rv) rv = (reverb_state *) malloc (sizeof *rv);
	reverb_init (rv);
	key_init (&key);
	spin = 0; out_n = 0; ring_n = 0; pend = 0;
}

static void publish (void)
{
	past_snap *p = &ring[ring_n % RING];
	int j, s, n;
	past_coefficients (&eng);
	key_update (&key, &eng, blk_env);
	harm_set_key (hfx, key.pc);
	eng.key_pc = key.pc; eng.key_minor = key.minor;		/* past's new notes snap to it */
	LOCK ();
	p->out = out_n;
	p->sample = eng.i;
	p->tick = eng.tick; p->min = eng.min; p->sec = eng.sec; p->done = eng.done;
	memcpy (p->scale, eng.scale, sizeof p->scale);
	memcpy (p->scale1, eng.scale1, sizeof p->scale1);
	memcpy (p->scale2, eng.scale2, sizeof p->scale2);
	for (j = 0; j < PAST_OSCS; j++)
	{
		p->env[j] = blk_env[j];
		p->note[j] = eng.osc[j].note;
		p->on[j] = eng.osc[j].is_on;
		blk_env[j] = 0;
	}
	p->voices = eng.voices;
	p->mean_acc = eng.mean_acc;
	p->spin = spin;
	p->fb_slow = eng.fb_slow; p->fb_y_gain = eng.fb_y_gain; p->fb_x_gain = eng.fb_x_gain;
	p->fb_y_mod = eng.fb_y_mod; p->fb_x_mod = eng.fb_x_mod;
	p->fb_y_ox3 = eng.fb_y_ox3; p->fb_x_oy3 = eng.fb_x_oy3;
	p->drive_u = eng.drive_u; p->drive_v = eng.drive_v;
	p->rms_l = blk_n ? (float) sqrt (blk_l2 / blk_n) : 0;
	p->rms_r = blk_n ? (float) sqrt (blk_r2 / blk_n) : 0;
	p->uv = (blk_uv + blk_xy) > 0 ? (float) (blk_uv / (blk_uv + blk_xy)) : 0;

	p->section = (int) hfx->c;
	memcpy (p->chanon, hfx->chanon, sizeof p->chanon);
	for (s = 0; s < 2; s++)
	{
		p->nofx[s] = hfx->nofx[2 + s];
		p->nofxfeed[s] = hfx->nofxfeed[2 + s];
		p->clears[s] = hfx->event_clear[2 + s];
		for (n = 0; n < HARM_TAPS; n++)
		{
			p->tapnum[s][n] = hfx->cbuf[2 + s].tap[n].tapnum;
			p->gback[s][n] = hfx->gback_raw[2 + s][n];
		}
	}
	p->shift = blk_n ? blk_shift / (float) blk_n : 0;
	for (j = 0; j < 3; j++) { p->click[j] = blk_click[j]; blk_click[j] = 0; }
	p->key_pc = key.pc; p->key_minor = key.minor;
	p->cycle = hfx->cycle;
	p->chord_set = hfx->chord_set;
	p->chord_level = blk_chord; p->swell_level = blk_swell;
	memcpy (p->poly, eng.poly_env, sizeof p->poly);
	p->bar = eng.bar; p->phrase_bar = eng.phrase_bar;
	p->rv_sparse = rv->sparse; p->rv_wet = rv->wet; p->rv_rt60 = rv->rt60; p->rv_level = rv->level;
	blk_chord = blk_swell = 0;
	p->choir = blk_choir; blk_choir = 0;
	ring_n++;
	UNLOCK ();
	blk_n = blk_shift = 0; blk_l2 = blk_r2 = blk_uv = blk_xy = 0;
}

/* next output frame: run past + harm for a new sample, or repeat the last
   one while harm's time shift is writing it again */
static void frame (short *l, short *r)
{
	if (pend == 0)
	{
		short pl, pr;
		int j, copies;
		float env_sum = 0;
		if (!past_next (&eng, &pl, &pr))
		{
			pl = pr = 0; eng.done = 1;
		}
		spin += eng.mean_acc;
		for (j = 0; j < PAST_OSCS; j++)
		{
			if (eng.osc[j].env > blk_env[j]) blk_env[j] = eng.osc[j].env;
			if (eng.osc[j].is_on) env_sum += eng.osc[j].env;
		}
		blk_uv += fabs (eng.u) + fabs (eng.v);
		blk_xy += fabs (eng.x) + fabs (eng.y);
		hfx->t = eng.i; hfx->arc = eng.arc_w;			/* harm's drums ride past's bar and arc */
		hfx->ldiv = eng.gene[2]; hfx->rdiv = eng.gene[3]; hfx->tdiv = eng.gene[4];	/* and its rhythm genes */
		harm_step (hfx, pl, pr, &pend_l, &pend_r, &copies);
		/* the polyrhythm after harm's echoes (they would smear it off the beat) */
		pend_l = (short) lrintf (32767.0f * tanhf ((pend_l + eng.poly_l) / 32767.0f));
		pend_r = (short) lrintf (32767.0f * tanhf ((pend_r + eng.poly_r) / 32767.0f));
		rv->mood = eng.mood;
		reverb_step (rv, env_sum, &pend_l, &pend_r);
		for (j = 0; j < 3; j++) if (hfx->click[j] > blk_click[j]) blk_click[j] = hfx->click[j];
		if (hfx->chord_level > blk_chord) blk_chord = hfx->chord_level;
		if (eng.choir_level > blk_choir) blk_choir = eng.choir_level;
		if (hfx->swell_level > blk_swell) blk_swell = hfx->swell_level;
		pend = copies;
	}
	*l = pend_l; *r = pend_r;
	if (hfx->repeat) blk_shift++;
	pend--;
	out_n++;
	if (cap) { short b[2]; b[0] = *l; b[1] = *r; fwrite (b, 2, 2, cap); }
	blk_l2 += (*l / 32768.0) * (*l / 32768.0);
	blk_r2 += (*r / 32768.0) * (*r / 32768.0);
	if (++blk_n >= SNAP_FRAMES) publish ();
}

void audio_seek_offline (double seconds)
{
	uint64_t target = (uint64_t) (seconds * PAST_RATE);
	short l, r;
	if (!offline) { reset (); offline = 1; }
	while (out_n < target && !eng.done) frame (&l, &r);
	offline_now = out_n;
}

#ifdef AUDIO_WORKLET
/* the browser's frame-exact mode (web/audio_main.c): the music run offline on
   the page's thread, the snapshot for the frame (-1: not offline) */
float audio_get_swing (void) { return swing_samples / 8192.0f; }
int audio_offline_now (past_snap *out, double *seconds)
{
	uint64_t lo, k;
	if (!offline) return -1;
	if (!ring_n) return 0;
	lo = ring_n > RING ? ring_n - RING : 0;
	for (k = ring_n; k-- > lo; )
		if (ring[k % RING].out <= offline_now || k == lo) { *out = ring[k % RING]; break; }
	if (seconds) *seconds = out->out / (double) PAST_RATE;
	return 1;
}

#define EXPORT __attribute__ ((used, visibility ("default")))
/* the worklet's calls: render n frames (interleaved L R floats), and read the
   snapshots published so far (ring index k, the newest RING kept) */
static float	wbuf[2 * 4096];
EXPORT void aw_init (float swing) { audio_set_swing (swing); reset (); }
EXPORT float *aw_render (int n)
{
	int f;
	if (n > 4096) n = 4096;
	for (f = 0; f < n; f++)
	{
		short l, r;
		frame (&l, &r);
		wbuf[2 * f] = l / 32768.0f;
		wbuf[2 * f + 1] = r / 32768.0f;
	}
	return wbuf;
}
EXPORT unsigned int aw_snaps (void) { return (unsigned int) ring_n; }
EXPORT past_snap *aw_snap (unsigned int k) { return &ring[k % RING]; }	/* (32-bit: plain numbers to JS) */
EXPORT int aw_snap_size (void) { return (int) sizeof (past_snap); }
#else
static void aq_cb (void *ud, AudioQueueRef q, AudioQueueBufferRef b)
{
	float *out = (float *) b->mAudioData;
	int f;
	(void) ud;
	for (f = 0; f < AQ_FRAMES; f++)
	{
		short l, r;
		frame (&l, &r);
		out[2 * f] = l / 32768.0f;
		out[2 * f + 1] = r / 32768.0f;
	}
	b->mAudioDataByteSize = AQ_FRAMES * 2 * sizeof (float);
	AudioQueueEnqueueBuffer (q, b, 0, NULL);
}

static void *null_device (void *arg)
{
	uint64_t t = now_ns ();
	(void) arg;
	for (;;)
	{
		short l, r;
		int f;
		for (f = 0; f < AQ_FRAMES; f++) frame (&l, &r);
		t += 1000000000ULL * AQ_FRAMES / PAST_RATE;
		while (now_ns () < t) usleep (1000);
	}
	return NULL;
}

int audio_start (int mute)
{
	AudioStreamBasicDescription fmt = { PAST_RATE, kAudioFormatLinearPCM,
		kLinearPCMFormatFlagIsFloat | kLinearPCMFormatFlagIsPacked, 8, 1, 8, 2, 32, 0 };
	AudioQueueBufferRef b;
	int i;

	reset ();
	t0_ns = now_ns ();
	if (mute || AudioQueueNewOutput (&fmt, aq_cb, NULL, NULL, NULL, 0, &aq))
	{
		pthread_t th;
		if (!mute) fprintf (stderr, "no audio output; running silently\n");
		pthread_create (&th, NULL, null_device, NULL);
		pthread_detach (th);
		return mute ? 0 : -1;
	}
	for (i = 0; i < AQ_BUFS; i++)
		if (!AudioQueueAllocateBuffer (aq, AQ_FRAMES * 2 * sizeof (float), &b))
			aq_cb (NULL, aq, b);
	t0_ns = now_ns ();
	AudioQueueStart (aq, NULL);
	return 0;
}

int audio_now (past_snap *out, double *seconds)
{
	uint64_t playing, lo, hi, k;
	int found = 0;

	if (offline) playing = offline_now;
	else
	{
		/* audible position: wall clock since start, minus the queued buffers */
		double t = (now_ns () - t0_ns) / 1e9;
		long p = (long) (t * PAST_RATE) - (AQ_BUFS - 1) * AQ_FRAMES;
		playing = p > 0 ? (uint64_t) p : 0;
	}
	pthread_mutex_lock (&mx);
	if (ring_n)
	{
		hi = ring_n - 1;
		lo = ring_n > RING ? ring_n - RING : 0;
		/* latest snapshot at or before the audible frame */
		for (k = hi + 1; k-- > lo; )
			if (ring[k % RING].out <= playing || k == lo) { *out = ring[k % RING]; found = 1; break; }
	}
	pthread_mutex_unlock (&mx);
	if (seconds) *seconds = (found ? out->out : playing) / (double) PAST_RATE;
	return found;
}
#endif /* AUDIO_WORKLET */
