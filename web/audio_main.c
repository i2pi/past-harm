/* web: the picture's side of the audio (audio.h). The music itself runs in
 * an AudioWorklet (audio.c built with -DAUDIO_WORKLET, see shell.html); the
 * worklet posts its snapshots here, and the page tells us which output frame
 * is audible now. */
#include <emscripten.h>
#include <string.h>
#include "../audio.h"

#define RING	1024
#define EXPORT	EMSCRIPTEN_KEEPALIVE

static past_snap		ring[RING];
static unsigned long	ring_n;
static double			playing;			/* audible output frame (the page: AudioContext time) */

/* the page: a snapshot's bytes (written into the buffer from aw_slot) */
static past_snap		slot;
EXPORT past_snap *aw_slot (void) { return &slot; }
EXPORT int aw_main_snap_size (void) { return (int) sizeof (past_snap); }
EXPORT void aw_push (void) { ring[ring_n % RING] = slot; ring_n++; }
EM_JS (double, js_audible, (void), { return Module.audibleFrame ? Module.audibleFrame () : 0; });
/* audio.c is linked here too, for the frame-exact mode (?at=): the music
   then runs offline on this thread, as in the macOS video renderer */
float audio_get_swing (void);
int   audio_offline_now (past_snap *out, double *seconds);
EXPORT float aw_swing (void) { return audio_get_swing (); }
int  audio_start (int mute, int loop) { (void) mute; (void) loop; return 0; }	/* started by the page, on a click */

int audio_now (past_snap *out, double *seconds)
{
	unsigned long lo, hi, k;
	int found = 0, off = audio_offline_now (out, seconds);
	if (off >= 0) return off;
	playing = js_audible ();
	if (ring_n)
	{
		hi = ring_n - 1;
		lo = ring_n > RING ? ring_n - RING : 0;
		for (k = hi + 1; k-- > lo; )
			if ((double) ring[k % RING].out <= playing || k == lo) { *out = ring[k % RING]; found = 1; break; }
	}
	if (seconds) *seconds = (found ? out->out : (playing > 0 ? playing : 0)) / (double) PAST_RATE;
	return found;
}
