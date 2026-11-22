/* p10m1-past
 *
 * The p10m1 scene (Mar 2012) driven by the generative music of past
 * (before 2003). The music runs unmodified (past_engine.c); the scene reads its
 * internal state -- voices, scales, tick, feedback-filter coefficients -- for
 * what is audible right now, and uses it in place of p10m1's clock:
 *
 *   voice envelopes        -> cube sizes (each cube belongs to one of 16 voices)
 *   scale / scale1/scale2  -> cube placement (radius, height), and the key,
 *                             which sets the palette (circle of fifths -> hue)
 *   mean voice frequency   -> rotation of the cube cloud (its running integral)
 *   each voice's note      -> its cubes' own orbit: axis (pitch class on the circle
 *                             of fifths, octave tilt), direction, rate (envelope)
 *   number of voices       -> zoom, inversely (x5 at none, x1 at 10 or more)
 *   tick                   -> camera nudge + grid flash on each tick
 *   feedback coefficients  -> background radial warp (r1, r2), the spread of
 *                             the three glow renders, blur kernel, feedback decay
 *   output level, u/v mix  -> brightness, saturation
 *   min:sec sections       -> slow dolly over the 8 minutes
 *
 *   ./p10m1-past [-f] [-mute] [-loop] [-shot out.ppm -at seconds]
 *   keys: f fullscreen, Esc quit
 */
#ifdef __EMSCRIPTEN__
#include "web/gl_compat.h"				/* the browser: WebGL2 (see web/) */
#include <emscripten/html5.h>
#else
#include <GLUT/glut.h>
#include <OpenGL/gl.h>
#include <OpenGL/glu.h>
#include <OpenGL/glext.h>
#include <OpenGL/OpenGL.h>
#include <objc/runtime.h>
#include <objc/message.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include "audio.h"
#include "title.h"

#define NC	180		/* cubes (p10m1 had 100) */

/* ------------------------------------------------------------------ */
/*  state                                                              */
/* ------------------------------------------------------------------ */

static int		W = 1280, H = 720;
static int		fullscreen, mute, loop_play;
static const char *shot_path;
static const char *video_path;			/* -video out.mp4: render offline (see video_render) */
static int		offline_frames;				/* the browser's ?at=: stepped like the video, for comparing */
static float	vfps = 30;
static int		video_w = 4096, video_h = 2304;
static double	vsecs = 480;
static FILE		*vpipe;
static unsigned char *vbuf;
static double	shot_at;
static float	swing = 0.10f;		/* past's step-grid swing (0 = original) */

static int		frames;

static float	ALPHA = 1, SATURATION = 1, VALUE_BOOST = 0, HUE = 215;

/* smoothed visual parameters (all eased towards the music's targets) */
typedef struct
{
	float	hue, sat, val;			/* palette */
	float	r1, r2;					/* background radial warp */
	float	spread;					/* fov spread of the glow renders */
	float	blur_c;					/* blur kernel centre weight */
	float	decay;					/* feedback mix */
	float	cam_z, tick_pulse, level, uv;
	float	zoom_v, zoom;			/* voices (eased) -> zoom: x5 at none, x1 at 10+ */
	float	cube_r[NC], cube_h[NC], cube_s[NC];
	float	cube_g[NC];			/* 0 = old way (hard test, white bleed), 1 = new way (coverage) */
	float	new_frac;				/* share of cubes done the new way */
	double	rot;
	int		last_tick;
	int		key_pc, key_minor, key_hold;
	float	key_score;
	/* harm */
	int		clears[2];
	float	breath;				/* harm's 9 s cycle */
	/* the orange lattice in the black: one layer per loud voice (up to 4) */
	float	lay_n[4], lay_w[4], lay_b[4], lay_h[4];	/* cells across, px, brightness, hue */
	/* the lattice's variant: each time it comes back after disappearing it is
	   a little different -- or, now and then, very different */
	float	lv_rot, lv_shx, lv_sx, lv_sy;	/* affine: rotation (rad), shear, stretch */
	float	lv_wa, lv_wk, lv_wph;			/* warp: amplitude (px at 720p), wavenumber, phase */
	float	lv_den, lv_hue, lv_sat;			/* density factor, base hue, saturation */
	int		lv_hidden, lv_count;
	float	lv_gone;						/* seconds it has been out of sight */
	float	reveal_frac;					/* share of the frame where it showed, last frame */
	float	poly[PAST_POLY];		/* the polyrhythm layers' hits, eased */
	float	glass_k, glass_disp;	/* refraction offset (px at 720p), dispersion */
	/* each voice's cubes turn as a group: orientation, axis, rate (deg/s) */
	float	vq[PAST_OSCS][4], vaxis[PAST_OSCS][3], vomega[PAST_OSCS];
} visT;
static visT		V;
static past_snap P;
static double	now_s, last_wall;
static char		app_dir[1024] = ".";	/* where the program (and fonts/) live */
static float	frame_dt = 1.0f / 30;
static float	fps;

static float	cube_base[NC][4];		/* p10m1's srandom(0) layout (first 100), continued */
static int		cube_rank[NC];
static char		cube_glass[NC];			/* glass cubes: they refract and disperse what is behind them */
static unsigned char glass_q[NC];		/* their quality: 0 perfect, 1-2 bumpy, 3 fractured */
static float	glass_tc[NC][3];		/* their flaw texture's scale and offset */
static float	glass_crack[NC];		/* when a fractured one cracks (s); whole until then */
static GLuint	flaw_tex[4], pr_flaw;			/* the flaw textures (normal perturbations) */
#define MIRRORS		0						/* 1: a third of the glass cubes are mirrors */
static char		cube_mirror[NC];		/* glass cubes that are mirrors: they reflect what they see */			/* order in which cubes switch to the new way */


/* ------------------------------------------------------------------ */
/*  colour (p10m1's pSetHSV)                                           */
/* ------------------------------------------------------------------ */

static void hsv (float h, float s, float v, float a, float *r, float *g, float *b)
{
	int i;
	float f, p, q, t;
	h = fmodf (h, 360.0f); if (h < 0) h += 360.0f;
	i = (int) floorf (h / 60.0f) % 6;
	f = h / 60.0f - floorf (h / 60.0f);
	p = v * (1 - s); q = v * (1 - s * f); t = v * (1 - (1 - f) * s);
	switch (i)
	{
	case 0: *r = v; *g = t; *b = p; break;
	case 1: *r = q; *g = v; *b = p; break;
	case 2: *r = p; *g = v; *b = t; break;
	case 3: *r = p; *g = q; *b = v; break;
	case 4: *r = t; *g = p; *b = v; break;
	default: *r = v; *g = p; *b = q; break;
	}
	(void) a;
}

static void pSetHSV (float h, float s, float v, float a)
{
	float r, g, b;
	hsv (h, s, v, a, &r, &g, &b);
	glColor4f (r, g, b, a);
}

static float clampf (float x) { return x >= 0.9999f ? 0.9999f : x <= 0 ? 0 : x; }

/* ------------------------------------------------------------------ */
/*  the key of the moment -> palette                                   */
/* ------------------------------------------------------------------ */

/* circle-of-fifths position -> hue; anchored so A major is p10m1's blue (215) */
static float key_hue (int pc, int minor)
{
	/* the circle of fifths becomes a gentle swing of about +-20 degrees around
	   p10m1's blue (A major = 215): neighbouring keys differ by ~10 degrees,
	   distant keys by at most ~40 */
	int fifths = (pc * 7) % 12;
	return 215.0f + 20.0f * sinf (2.0f * (float) M_PI * fifths / 12.0f) + (minor ? -5.0f : 0.0f);
}

/* ------------------------------------------------------------------ */
/*  easing                                                             */
/* ------------------------------------------------------------------ */

static float ease (float cur, float target, float dt, float tau) { return cur + (target - cur) * (1.0f - expf (-dt / tau)); }

static float ease_hue (float cur, float target, float dt, float tau)
{
	float d = fmodf (target - cur + 540.0f, 360.0f) - 180.0f;	/* shortest way round */
	return cur + d * (1.0f - expf (-dt / tau));
}

/* Each voice's cubes orbit the centre as a group, on an axis and in a
   direction taken from the note the voice is playing:
     axis       the note's pitch class around the circle of fifths (related
                notes turn on nearby axes), tilted by its octave
     direction  the note's whole-tone set (a semitone step reverses it) times
                the sign of past's atan drive for that voice (drive_u for odd
                voices, drive_v for even, swinging over ~45-55 s)
     rate       the voice's envelope
   Axis and rate glide, and the orientation is integrated, so nothing jumps:
   a reversal is a slowing down and turning back. Silent voices drift. */
static void qmul (const float *a, const float *b, float *o)
{
	float r[4];
	r[0] = a[0]*b[0] - a[1]*b[1] - a[2]*b[2] - a[3]*b[3];
	r[1] = a[0]*b[1] + a[1]*b[0] + a[2]*b[3] - a[3]*b[2];
	r[2] = a[0]*b[2] - a[1]*b[3] + a[2]*b[0] + a[3]*b[1];
	r[3] = a[0]*b[3] + a[1]*b[2] - a[2]*b[1] + a[3]*b[0];
	memcpy (o, r, sizeof r);
}

static void voice_spins (float dt)
{
	static const float drift_axis[3] = { -0.12f, 0.92f, 0.37f };
	int j, k;
	for (j = 0; j < PAST_OSCS; j++)
	{
		float ax[3], w, len, dq[4], half, sn;
		float env = P.env[j] > 1.5f ? 1.5f : P.env[j];
		if (P.on[j])
		{
			int   note = P.note[j], pc = ((note % 12) + 12) % 12, oc = ((note / 12) % 4 + 4) % 4;
			float th = 2.0f * (float) M_PI * ((pc * 7) % 12) / 12.0f;
			float ph = (float) M_PI / 180.0f * (22.0f + 16.0f * oc);
			float drive = (j % 2) ? P.drive_u : P.drive_v;
			float dir = (pc % 2 ? -1.0f : 1.0f) * (drive < 0 ? -1.0f : 1.0f);
			ax[0] = sinf (ph) * cosf (th); ax[1] = cosf (ph); ax[2] = sinf (ph) * sinf (th);
			w = dir * (7.0f + 30.0f * env);
		}
		else
		{
			memcpy (ax, drift_axis, sizeof ax);
			w = (V.vomega[j] < 0 ? -1.0f : 1.0f) * 3.0f;
		}
		for (k = 0; k < 3; k++) V.vaxis[j][k] = ease (V.vaxis[j][k], ax[k], dt, 1.2f);
		V.vomega[j] = ease (V.vomega[j], w, dt, 1.0f);
		len = sqrtf (V.vaxis[j][0] * V.vaxis[j][0] + V.vaxis[j][1] * V.vaxis[j][1] + V.vaxis[j][2] * V.vaxis[j][2]);
		if (len < 1e-4f) continue;
		half = 0.5f * V.vomega[j] * dt * (float) M_PI / 180.0f;
		sn = sinf (half) / len;
		dq[0] = cosf (half); dq[1] = V.vaxis[j][0] * sn; dq[2] = V.vaxis[j][1] * sn; dq[3] = V.vaxis[j][2] * sn;
		qmul (dq, V.vq[j], V.vq[j]);
		len = sqrtf (V.vq[j][0]*V.vq[j][0] + V.vq[j][1]*V.vq[j][1] + V.vq[j][2]*V.vq[j][2] + V.vq[j][3]*V.vq[j][3]);
		for (k = 0; k < 4; k++) V.vq[j][k] /= len;
	}
}

/* The lattice comes back different. When it has been out of sight (no dark
   to show through, or no voices) for half a second and then shows again, it
   takes a new variant, chosen from the music's tick count:
     repetition (~70%)  a small step from the last: a few degrees of turn, a
                        little shear, slightly sparser, the neighbouring hue
     surprise (~30%)    a jump: up to +-15 degrees, shear, stretch, a strong
                        warp of its lines, a hue across the family
   The family: orange, amber, red-orange, teal, cyan-blue, blue. Each return
   is a little less dense than the last (down to about half). */
static const float lv_family[6] = { 28.0f, 40.0f, 16.0f, 178.0f, 194.0f, 212.0f };

static float lv_rand (unsigned int *s) { *s = *s * 1664525u + 1013904223u; return (*s >> 8) / 16777216.0f; }

static void lattice_variant (void)
{
	unsigned int sd = (unsigned int) (P.tick * 7919 + V.lv_count * 104729 + 17);
	int surprise = lv_rand (&sd) < 0.3f, fi, k;
	V.lv_count++;
	V.lv_den = V.lv_den * (0.86f + 0.06f * lv_rand (&sd));
	if (V.lv_den < 0.5f) V.lv_den = 0.5f + 0.1f * lv_rand (&sd);
	for (fi = 0, k = 1; k < 6; k++) if (fabsf (lv_family[k] - V.lv_hue) < fabsf (lv_family[fi] - V.lv_hue)) fi = k;
	if (surprise)
	{
		V.lv_rot = (lv_rand (&sd) * 2 - 1) * 0.26f;
		V.lv_shx = (lv_rand (&sd) * 2 - 1) * 0.3f;
		V.lv_sx = 0.8f + 0.5f * lv_rand (&sd);
		V.lv_sy = 0.8f + 0.5f * lv_rand (&sd);
		V.lv_wa = 6.0f + 22.0f * lv_rand (&sd);
		V.lv_wk = 0.004f + 0.012f * lv_rand (&sd);
		fi = (fi + 2 + (int) (lv_rand (&sd) * 3)) % 6;			/* across the family */
		V.lv_sat = 0.55f + 0.35f * lv_rand (&sd);
	}
	else
	{
		V.lv_rot += (lv_rand (&sd) * 2 - 1) * 0.06f;
		V.lv_shx = V.lv_shx * 0.7f + (lv_rand (&sd) * 2 - 1) * 0.05f;
		V.lv_sx += (1.0f - V.lv_sx) * 0.3f;
		V.lv_sy += (1.0f - V.lv_sy) * 0.3f;
		V.lv_wa *= 0.6f;
		fi = (fi + (lv_rand (&sd) < 0.5f ? 1 : 5)) % 6;			/* a neighbour */
		V.lv_sat += (0.85f - V.lv_sat) * 0.3f;					/* colour comes back gradually */
	}
	V.lv_wph = 6.2832f * lv_rand (&sd);
	V.lv_hue = lv_family[fi];
	for (k = 0; k < 4; k++) V.lay_h[k] = V.lv_hue + 3.0f * (k - 1.5f);		/* no easing across the change */
}

/* The orange lattice that shows through the black is drawn from the score:
   each of the (up to) 4 loudest voices sounding gives a layer whose
     spacing    is its note against the key over two octaves (6..23 cells
                across) -- the layers stand in the chord's own ratios
     thickness, brightness   follow its envelope
     hue        orange, swung +-10 degrees by the key round the circle of
                fifths (minor keys redder), a few degrees apart per layer
   so the repeats build up as voices enter. Its shading turns with harm's
   9 s cycle. Everything eases. */
static void lattice_update (float dt)
{
	int order[PAST_OSCS], n = 0, i, j, k;
	/* out of sight, then back: a new variant */
	if (V.reveal_frac < 0.0005f) { V.lv_gone += dt; if (V.lv_gone > 0.5f) V.lv_hidden = 1; }
	else if (V.reveal_frac > 0.002f)
	{
		if (V.lv_hidden) lattice_variant ();
		V.lv_hidden = 0; V.lv_gone = 0;
	}
	float base_h = V.lv_hue + 10.0f * sinf (2.0f * (float) M_PI * ((V.key_pc * 7) % 12) / 12.0f) - (V.key_minor ? 6.0f : 0.0f);
	for (j = 0; j < PAST_OSCS; j++) if (P.on[j] && P.env[j] > 0.03f) order[n++] = j;
	for (i = 1; i < n; i++)								/* loudest first */
		for (k = i; k > 0 && P.env[order[k]] > P.env[order[k - 1]]; k--) { int t = order[k]; order[k] = order[k - 1]; order[k - 1] = t; }
	for (k = 0; k < 4; k++)
	{
		if (k < n)
		{
			int   v = order[k], rel = (((P.note[v] - V.key_pc) % 24) + 24) % 24;
			float env = P.env[v] > 1.5f ? 1.5f : P.env[v];
			float tn = 6.0f * powf (2.0f, rel / 12.0f) * V.lv_den;
			V.lay_n[k] = V.lay_n[k] > 0 ? ease (V.lay_n[k], tn, dt, 0.6f) : tn;
			V.lay_w[k] = ease (V.lay_w[k], 0.7f + 2.3f * env / 1.5f, dt, 0.25f);
			V.lay_b[k] = ease (V.lay_b[k], 0.35f + 0.65f * (env > 1 ? 1 : env), dt, 0.25f);
		}
		else V.lay_b[k] = ease (V.lay_b[k], 0, dt, 0.8f);
		V.lay_h[k] = ease (V.lay_h[k], base_h + 3.0f * (k - 1.5f), dt, 4.0f);
	}
}

#ifdef FILM_CPU		/* the film pass (CPU), set aside for now */
/* ------------------------------------------------------------------ */
/*  film: a 3D grain field stirred by the cubes; halation; tone; grain */
/* ------------------------------------------------------------------ */

/* The grain field is a 24^3 grid of scalars around the cloud (world space,
   before the camera). Each frame every cube deposits into it where it is, in
   proportion to how fast it is moving (and its size), so the cubes swooshing
   around leave grainy wakes; the field spreads a little to its neighbours and
   fades over ~2 s. It is projected through the cubes' camera into a soft
   screen map that sets how grainy each region of the picture is. */
#define FG		24
#define FG_EXT	0.95f				/* the grid spans -FG_EXT..FG_EXT */
#define FM_W	96
#define FM_H	54
static float	fgrid[FG][FG][FG], ftmp[FG][FG][FG];
static int		film_on = 0;			/* the film pass is set aside for now (grain field, halation, tone) */
static float	cube_prev[NC][3];
static int		cube_prev_ok;
static float	fmap[FM_H][FM_W];

/* the global rotation (V.rot degrees about (-0.1, 0.7, 0.3)) as a matrix */
static void global_rot (float m[9])
{
	float ax = -0.1f, ay = 0.7f, az = 0.3f, l = sqrtf (ax * ax + ay * ay + az * az);
	float a = (float) V.rot * (float) M_PI / 180.0f, c = cosf (a), sn = sinf (a), t = 1 - c;
	ax /= l; ay /= l; az /= l;
	m[0] = t*ax*ax + c;    m[1] = t*ax*ay - sn*az; m[2] = t*ax*az + sn*ay;
	m[3] = t*ax*ay + sn*az; m[4] = t*ay*ay + c;    m[5] = t*ay*az - sn*ax;
	m[6] = t*ax*az - sn*ay; m[7] = t*ay*az + sn*ax; m[8] = t*az*az + c;
}

/* a cube's centre in world space: global rotation * its voice's rotation * p */
static void cube_world (int i, const float gm[9], float out[3])
{
	const float *q = V.vq[i % PAST_OSCS];
	float w = q[0], a = q[1], b = q[2], c = q[3], p[3], v[3];
	p[0] = cube_base[i][0] * V.cube_r[i]; p[1] = cube_base[i][1] * V.cube_r[i] + V.cube_h[i]; p[2] = cube_base[i][2] * V.cube_r[i];
	v[0] = (1 - 2*(b*b + c*c)) * p[0] + 2*(a*b - w*c) * p[1] + 2*(a*c + w*b) * p[2];
	v[1] = 2*(a*b + w*c) * p[0] + (1 - 2*(a*a + c*c)) * p[1] + 2*(b*c - w*a) * p[2];
	v[2] = 2*(a*c - w*b) * p[0] + 2*(b*c + w*a) * p[1] + (1 - 2*(a*a + b*b)) * p[2];
	out[0] = gm[0] * v[0] + gm[1] * v[1] + gm[2] * v[2];
	out[1] = gm[3] * v[0] + gm[4] * v[1] + gm[5] * v[2];
	out[2] = gm[6] * v[0] + gm[7] * v[1] + gm[8] * v[2];
}

static void fgrid_splat (const float p[3], float amt)
{
	float g[3], f[3];
	int c[3], k, dx, dy, dz;
	for (k = 0; k < 3; k++)
	{
		g[k] = (p[k] + FG_EXT) / (2 * FG_EXT) * (FG - 1);
		if (g[k] < 0 || g[k] >= FG - 1) return;
		c[k] = (int) g[k]; f[k] = g[k] - c[k];
	}
	for (dz = 0; dz < 2; dz++) for (dy = 0; dy < 2; dy++) for (dx = 0; dx < 2; dx++)
		fgrid[c[2] + dz][c[1] + dy][c[0] + dx] += amt * (dx ? f[0] : 1 - f[0]) * (dy ? f[1] : 1 - f[1]) * (dz ? f[2] : 1 - f[2]);
}

static void film_update (float dt)
{
	float gm[9], decay = expf (-dt / 2.0f), fov, fy, fx;
	int i, x, y, z;
	global_rot (gm);
	/* the cubes stir the field: speed x size, where they are */
	for (i = 0; i < NC; i++)
	{
		float p[3], sp;
		cube_world (i, gm, p);
		if (cube_prev_ok && dt > 0)
		{
			float vx = p[0] - cube_prev[i][0], vy = p[1] - cube_prev[i][1], vz = p[2] - cube_prev[i][2];
			sp = sqrtf (vx * vx + vy * vy + vz * vz) / dt;		/* units / s */
			if (sp > 3) sp = 3;
			fgrid_splat (p, sp * dt * (2.0f + 40.0f * cube_base[i][3] * V.cube_s[i]));
		}
		memcpy (cube_prev[i], p, sizeof p);
	}
	cube_prev_ok = 1;
	/* spread to the neighbours a little, and fade */
	for (z = 0; z < FG; z++) for (y = 0; y < FG; y++) for (x = 0; x < FG; x++)
	{
		float c = fgrid[z][y][x], n = 0;
		int m = 0;
		if (x > 0) { n += fgrid[z][y][x - 1]; m++; } if (x < FG - 1) { n += fgrid[z][y][x + 1]; m++; }
		if (y > 0) { n += fgrid[z][y - 1][x]; m++; } if (y < FG - 1) { n += fgrid[z][y + 1][x]; m++; }
		if (z > 0) { n += fgrid[z - 1][y][x]; m++; } if (z < FG - 1) { n += fgrid[z + 1][y][x]; m++; }
		ftmp[z][y][x] = (c + (n / m - c) * 0.18f) * decay;
	}
	memcpy (fgrid, ftmp, sizeof fgrid);
	/* project the field through the camera into the screen map */
	memset (fmap, 0, sizeof fmap);
	fov = 2.0f * atanf (tanf (45.0f * (float) M_PI / 360.0f) / V.zoom);
	fy = 1.0f / tanf (fov / 2); fx = fy * H / (float) W;
	for (z = 0; z < FG; z++) for (y = 0; y < FG; y++) for (x = 0; x < FG; x++)
	{
		float v = fgrid[z][y][x], wx, wy, wz, ez, sx, sy, ax, ay;
		int ix, iy;
		if (v < 1e-3f) continue;
		wx = -FG_EXT + 2 * FG_EXT * x / (FG - 1); wy = -FG_EXT + 2 * FG_EXT * y / (FG - 1); wz = -FG_EXT + 2 * FG_EXT * z / (FG - 1);
		ez = wz + V.cam_z;
		if (ez > -0.05f) continue;							/* behind the camera */
		sx = (fx * wx / -ez * 0.5f + 0.5f) * (FM_W - 1);
		sy = (fy * wy / -ez * 0.5f + 0.5f) * (FM_H - 1);
		if (sx < 0 || sy < 0 || sx >= FM_W - 1 || sy >= FM_H - 1) continue;
		ix = (int) sx; iy = (int) sy; ax = sx - ix; ay = sy - iy;
		fmap[iy][ix] += v * (1 - ax) * (1 - ay); fmap[iy][ix + 1] += v * ax * (1 - ay);
		fmap[iy + 1][ix] += v * (1 - ax) * ay; fmap[iy + 1][ix + 1] += v * ax * ay;
	}
	/* soften the map (two passes of a 3x3 blur), then 0..1 */
	for (i = 0; i < 2; i++)
	{
		static float t[FM_H][FM_W];
		for (y = 0; y < FM_H; y++) for (x = 0; x < FM_W; x++)
		{
			float sum = 0; int m = 0, ddx, ddy;
			for (ddy = -1; ddy <= 1; ddy++) for (ddx = -1; ddx <= 1; ddx++)
			{
				int yy = y + ddy, xx = x + ddx;
				if (yy < 0 || xx < 0 || yy >= FM_H || xx >= FM_W) continue;
				sum += fmap[yy][xx]; m++;
			}
			t[y][x] = sum / m;
		}
		memcpy (fmap, t, sizeof fmap);
	}
	/* relative to the field's recent peak (eased over ~3 s), so the slow and
	   the fast regions always differ, whatever the overall amount */
	{
		static float peak = 1e-3f;
		float mx = 1e-4f, inv;
		for (y = 0; y < FM_H; y++) for (x = 0; x < FM_W; x++) if (fmap[y][x] > mx) mx = fmap[y][x];
		peak = mx > peak ? peak + (mx - peak) * (1.0f - expf (-dt / 0.3f)) : peak + (mx - peak) * (1.0f - expf (-dt / 3.0f));
		inv = 1.0f / (peak > 1e-4f ? peak : 1e-4f);
		for (y = 0; y < FM_H; y++) for (x = 0; x < FM_W; x++)
		{
			float v = fmap[y][x] * inv;
			v = v > 1 ? 1 : v;
			fmap[y][x] = v * v * (3 - 2 * v);
		}
	}
}

/* the film finish, on the shown frame only (not fed back):
     halation      highlights bleed a warm red-orange glow into their
                   surroundings, as light scattered back through the film
     compression   a filmic curve: lifted toe, soft highlight shoulder, a
                   touch of warm highlights / cool shadows
     grain         fresh every frame, strongest in the midtones, a little
                   colour in it; how strong and how coarse comes from the
                   grain field (fmap) */
static unsigned int film_rng = 1234567u;
static inline unsigned int xr (void) { film_rng ^= film_rng << 13; film_rng ^= film_rng >> 17; film_rng ^= film_rng << 5; return film_rng; }

static void film_apply (unsigned char *img)
{
	static float *hal, *hal2, *coarse;
	static int hw, hh, cw, ch;
	static float lut[256];							/* the tone curve, on luminance */
	static int lut_ok;
	int x, y, c, i;
	int HW = W / 4 + 1, HH = H / 4 + 1, CW = W / 2 + 2, CH = H / 2 + 2;
	if (hw != HW || hh != HH)
	{
		free (hal); free (hal2);
		hal = (float *) calloc ((size_t) HW * HH, sizeof *hal); hal2 = (float *) calloc ((size_t) HW * HH, sizeof *hal2);
		hw = HW; hh = HH;
	}
	if (cw != CW || ch != CH) { free (coarse); coarse = (float *) calloc ((size_t) CW * CH, sizeof *coarse); cw = CW; ch = CH; }
	if (!lut_ok)
	{
		for (i = 0; i < 256; i++)
		{
			float v = i / 255.0f, e = v * 1.25f, y2;
			y2 = e / (1.0f + 0.55f * e) * 1.55f;				/* soft shoulder */
			y2 = 0.025f + 0.975f * y2;						/* lifted toe */
			y2 = y2 * y2 * (3 - 2 * y2) * 0.35f + y2 * 0.65f;	/* a little S */
			lut[i] = y2 > 1 ? 1 : y2;
		}
		lut_ok = 1;
	}
	/* halation: highlights at quarter resolution, blurred wide */
	for (y = 0; y < HH; y++) for (x = 0; x < HW; x++)
	{
		int sx = x * 4 < W ? x * 4 : W - 1, sy = y * 4 < H ? y * 4 : H - 1;
		const unsigned char *p = img + ((size_t) sy * W + sx) * 3;
		float l = (p[0] * 0.3f + p[1] * 0.59f + p[2] * 0.11f) / 255.0f;
		hal[y * HW + x] = l > 0.62f ? (l - 0.62f) / 0.38f : 0;
	}
	for (i = 0; i < 3; i++)									/* separable box blurs, radius 3 */
	{
		for (y = 0; y < HH; y++) for (x = 0; x < HW; x++)
		{
			float sum = 0; int m = 0, d;
			for (d = -3; d <= 3; d++) if (x + d >= 0 && x + d < HW) { sum += hal[y * HW + x + d]; m++; }
			hal2[y * HW + x] = sum / m;
		}
		for (y = 0; y < HH; y++) for (x = 0; x < HW; x++)
		{
			float sum = 0; int m = 0, d;
			for (d = -3; d <= 3; d++) if (y + d >= 0 && y + d < HH) { sum += hal2[(y + d) * HW + x]; m++; }
			hal[y * HW + x] = sum / m;
		}
	}
	/* coarse grain: half resolution, for the clumpy grain where the field is dense */
	for (i = 0; i < CW * CH; i++) coarse[i] = ((xr () & 0xffff) + (xr () & 0xffff)) / 65535.0f - 1.0f;
	for (y = 0; y < H; y++)
	{
		float my = (float) y / H * (FM_H - 1), hy = y / 4.0f;
		int iy = (int) my, hy0 = (int) hy;
		float ay = my - iy, by = hy - hy0;
		if (iy >= FM_H - 1) { iy = FM_H - 2; ay = 1; }
		if (hy0 >= HH - 1) { hy0 = HH - 2; by = 1; }
		for (x = 0; x < W; x++)
		{
			unsigned char *p = img + ((size_t) y * W + x) * 3;
			float mx = (float) x / W * (FM_W - 1), hx = x / 4.0f;
			int ix = (int) mx, hx0 = (int) hx;
			float ax = mx - ix, bx = hx - hx0, field, h, lum, amp, n, coarse_w, cn;
			if (ix >= FM_W - 1) { ix = FM_W - 2; ax = 1; }
			if (hx0 >= HW - 1) { hx0 = HW - 2; bx = 1; }
			field = (fmap[iy][ix] * (1 - ax) + fmap[iy][ix + 1] * ax) * (1 - ay) + (fmap[iy + 1][ix] * (1 - ax) + fmap[iy + 1][ix + 1] * ax) * ay;
			h = (hal[hy0 * HW + hx0] * (1 - bx) + hal[hy0 * HW + hx0 + 1] * bx) * (1 - by)
			  + (hal[(hy0 + 1) * HW + hx0] * (1 - bx) + hal[(hy0 + 1) * HW + hx0 + 1] * bx) * by;
			lum = (p[0] * 0.3f + p[1] * 0.59f + p[2] * 0.11f) / 255.0f;
			/* grain: fine everywhere, coarser and stronger where the field is dense */
			n = ((xr () & 0xffff) + (xr () & 0xffff)) / 65535.0f - 1.0f;
			cn = coarse[(y / 2) * CW + x / 2];
			coarse_w = field;
			amp = (5.0f + 26.0f * field) * (0.35f + 2.6f * lum * (1 - lum));
			{
				float v[3], L, Lc, k;
				for (c = 0; c < 3; c++)
				{
					/* halation: red-orange, strongest in red */
					v[c] = p[c] + h * (c == 0 ? 150.0f : c == 1 ? 60.0f : 22.0f) * (1.0f - p[c] / 330.0f);
					if (v[c] > 255) v[c] = 255;
				}
				/* the tone curve on luminance; the colour is scaled with it
				   (hue and saturation kept), then saturation lifted ~25% */
				L = v[0] * 0.3f + v[1] * 0.59f + v[2] * 0.11f;
				Lc = 255.0f * lut[(int) (L > 255 ? 255 : L)];
				k = L > 1 ? Lc / L : 1;
				for (c = 0; c < 3; c++)
				{
					float o = v[c] * k, cg = ((int) (xr () & 0xff) - 128) / 128.0f;
					o = Lc + (o - Lc) * 1.25f;
					/* warm highlights, cool shadows (small) */
					o += (c == 0 ? 3.0f : c == 2 ? -2.5f : 0) * (Lc / 255.0f) + (c == 2 ? 4.0f : c == 0 ? -2.0f : 0) * (1 - Lc / 255.0f);
					o += amp * ((n * (1 - coarse_w) + cn * coarse_w * 1.3f) + 0.12f * cg);
					p[c] = (unsigned char) (o < 0 ? 0 : o > 255 ? 255 : o);
				}
			}
		}
	}
}

#endif

static void update_visuals (float dt)
{
	int i;
	float level, piece = (float) (now_s / 480.0);

	/* key: tracked on the audio thread */
	V.key_pc = P.key_pc; V.key_minor = P.key_minor;

	/* palette: slow and narrow (see key_hue) */
	V.hue = ease_hue (V.hue, key_hue (V.key_pc, V.key_minor), dt, 10.0f);
	level = (P.rms_l + P.rms_r) * 0.5f;
	V.level = ease (V.level, level, dt, 2.5f);
	V.uv = ease (V.uv, P.uv, dt, 4.0f);
	V.sat = ease (V.sat, 0.42f + 0.06f * V.uv + (V.key_minor ? -0.02f : 0), dt, 6.0f);
	V.val = ease (V.val, 0.03f + 0.35f * V.level, dt, 2.5f);

	/* background warp from past's slowest terms (45-62 s periods) */
	V.r1 = ease (V.r1, 0.4f + 0.2f * P.drive_v, dt, 3.0f);
	V.r2 = ease (V.r2, 0.3f * P.drive_u, dt, 3.0f);
	/* glow: spread of the three FOVs follows the x feedback gain,
	   blur sharpness follows the ox3 term, trails follow the slow term */
	/* harm's 9 s cycle breathes the glow */
	V.breath = sinf (2.0f * (float) M_PI * P.cycle);
	V.spread = ease (V.spread, (0.6f + 0.8f * fabsf (P.fb_x_gain)) * (1.0f + 0.12f * V.breath), dt, 0.8f);
	V.blur_c = ease (V.blur_c, 1.0f + 4.0f * (0.5f + 0.5f * P.fb_y_ox3 / 0.47f), dt, 1.0f);
	V.decay = ease (V.decay, 0.55f + 0.45f * (0.5f + 0.5f * P.fb_slow / 0.3f), dt, 2.0f);

	/* tick: a faint, slow flash of the fine grid and a small nudge */
	if (P.tick != V.last_tick) { V.tick_pulse = 1.0f; V.last_tick = P.tick; }
	V.tick_pulse *= expf (-dt / 1.2f);
	/* zoom inversely to the number of voices sounding */
	V.zoom_v = ease (V.zoom_v, (float) (P.voices > 7  ? 7 : P.voices), dt, 2.0f);
	V.zoom = 10.0f / (1.0f + 4.0f * V.zoom_v / 10.0f);
	/* the polyrhythm: a fast rise to each hit, a short fall */
	for (i = 0; i < PAST_POLY; i++)
		V.poly[i] = P.poly[i] > V.poly[i] ? ease (V.poly[i], P.poly[i], dt, 0.01f) : ease (V.poly[i], P.poly[i], dt, 0.12f);
	/* glass: the choir bends the light (refraction grows with its level), the
	   bell layer (the 5) spreads it into colours (dispersion pulses with it) */
	V.glass_k = ease (V.glass_k, 10.0f + 16.0f * (P.choir > 1 ? 1 : P.choir), dt, 0.8f);
	V.glass_disp = ease (V.glass_disp, 0.05f + 0.06f * (V.poly[3] > 1 ? 1 : V.poly[3]), dt, 0.08f);
	/* the heartbeat (the layer in 2) nudges the camera with the downbeat */
	V.cam_z = -1.85f + 0.2f * sinf ((float) M_PI * (piece < 1 ? piece : 1)) + 0.015f * V.tick_pulse + 0.02f * (P.click[1] > 1 ? 1 : P.click[1]);


	/* rotation of the whole cloud: the integral of the voices' mean frequency,
	   plus a slow drift (gentler than before: the voices' own turning leads) */
	V.rot = P.spin * 0.006 + now_s * 0.2;

	/* every cube is done the new way: antialiased, coverage-composited.
	   (The old way -- p10m1's hard test with its white bleed -- used to take
	   a share of the cubes that grew with the number of voices.) */
	V.new_frac = 1.0f;
	for (i = 0; i < NC; i++) V.cube_g[i] = 1.0f;

	voice_spins (dt);
	lattice_update (dt);

	/* cubes: placement from the scales, size from their voice */
	for (i = 0; i < NC; i++)
	{
		int j = i % PAST_OSCS, s = i % PAST_SCALE_LEN;
		float env = P.env[j] > 2.0f ? 2.0f : P.env[j];
		/* the scales drift upward in octaves as they swap, so use pitch class
		   for the radius and the octave difference for height */
		int   pc = ((P.scale[s] % 12) + 12) % 12;
		int   oc = (int) floorf (P.scale2[s] / 12.0f) - (int) floorf (P.scale1[s] / 12.0f);
		float tr = 0.88f + 0.24f * pc / 11.0f;
		float th = 0.03f * (oc < -2 ? -2 : oc > 2 ? 2 : oc);
		float ts = 0.6f + 0.4f * (env > 1.5f ? 1.5f : env);
		V.cube_r[i] = ease (V.cube_r[i], tr, dt, 0.9f);
		V.cube_h[i] = ease (V.cube_h[i], th, dt, 0.9f);
		V.cube_s[i] = ts > V.cube_s[i] ? ease (V.cube_s[i], ts, dt, 0.03f) : ease (V.cube_s[i], ts, dt, 0.45f);
	}
#ifdef FILM_CPU
	if (film_on) film_update (dt);
#endif
}

/* ------------------------------------------------------------------ */
/*  drawing (p10m1)                                                    */
/* ------------------------------------------------------------------ */

static void radial (float x, float y, float r0, float r1, float r2, float *ox, float *oy)
{
	float d = sqrtf (x * x + y * y), z = r0 + d * r1 + d * d * r2;
	*ox = x / z; *oy = y / z;
}

static void gradient_color (float x, float y)
{
	float X = 2 * (x - 0.5f) + 0.6f, Y = 2 * (y - 0.5f), S, Vv;
	radial (X, Y, 0.9f, V.r1, V.r2, &X, &Y);
	X = clampf (0.5f * X + 0.5f);
	Y = clampf (0.5f * Y + 0.5f);
	Vv = clampf (1.0f - Y + VALUE_BOOST);
	S = clampf (X * SATURATION * V.sat);
	/* a few degrees of hue across the field keeps it from looking flat */
	pSetHSV (HUE + 14.0f * (X - 0.5f), S, Vv, ALPHA);
}

/* one line of the lattice as a quad (pixel space), shaded along its length */
static void lattice_line (float x0, float y0, float x1, float y1, float w, float h, float b, float ca, float sa)
{
	float dx = x1 - x0, dy = y1 - y0, l = sqrtf (dx * dx + dy * dy), nx, ny, r, g, bl, s0, s1;
	if (l < 1e-3f) return;
	nx = -dy / l * 0.5f * w; ny = dx / l * 0.5f * w;
	/* shading: a gradient across the frame whose direction turns */
	s0 = 0.55f + 0.45f * (0.5f + 0.5f * (((x0 / W) - 0.5f) * 2 * ca + ((y0 / H) - 0.5f) * 2 * sa));
	s1 = 0.55f + 0.45f * (0.5f + 0.5f * (((x1 / W) - 0.5f) * 2 * ca + ((y1 / H) - 0.5f) * 2 * sa));
	hsv (h, V.lv_sat, 1, 1, &r, &g, &bl);
	glColor4f (r * b * s0, g * b * s0, bl * b * s0, 1);
	glVertex2f (x0 + nx, y0 + ny); glVertex2f (x0 - nx, y0 - ny);
	glColor4f (r * b * s1, g * b * s1, bl * b * s1, 1);
	glVertex2f (x1 - nx, y1 - ny); glVertex2f (x1 + nx, y1 + ny);
}

/* the lattice: p10m1's triangle tiling (base S, height S) as its three
   families of parallel lines, per layer */
/* a lattice point (frame units 0..1) through the variant's affine and warp, in pixels */
static void lv_map (float u, float v, float *x, float *y)
{
	float px = H / 720.0f, X = (u - 0.5f) * W, Y = (v - 0.5f) * H, c = cosf (V.lv_rot), s = sinf (V.lv_rot), a, b;
	a = V.lv_sx * X + V.lv_shx * Y; b = V.lv_sy * Y;
	X = c * a - s * b; Y = s * a + c * b;
	if (V.lv_wa > 0.1f)
	{
		float k = V.lv_wk / px, wa = V.lv_wa * px, X0 = X;
		X += wa * sinf (Y * k + V.lv_wph);
		Y += wa * sinf (X0 * k * 0.8f + V.lv_wph * 1.7f);
	}
	*x = X + 0.5f * W; *y = Y + 0.5f * H;
}

/* a lattice line from (u0, v0) to (u1, v1), cut into segments so the warp bends it */
static void lv_line (float u0, float v0, float u1, float v1, float w, float h, float b, float ca, float sa)
{
	int n, segs = V.lv_wa > 0.1f ? 20 : 1;
	float xa, ya, xb, yb;
	lv_map (u0, v0, &xa, &ya);
	for (n = 1; n <= segs; n++)
	{
		float t = n / (float) segs;
		lv_map (u0 + (u1 - u0) * t, v0 + (v1 - v0) * t, &xb, &yb);
		lattice_line (xa, ya, xb, yb, w, h, b, ca, sa);
		xa = xb; ya = yb;
	}
}

static void draw_lattice (void)
{
	float ang = 2.0f * (float) M_PI * P.cycle, ca = cosf (ang), sa = sinf (ang), px = H / 720.0f;
	int k, i;
	glMatrixMode (GL_PROJECTION); glLoadIdentity (); gluOrtho2D (0, W, 0, H);
	glBegin (GL_QUADS);
	for (k = 0; k < 4; k++)
	{
		/* each lattice layer pulses with one polyrhythm layer (3, 4, 5, 7) */
		float pk = V.poly[k + 1] > 1 ? 1 : V.poly[k + 1];
		float S = V.lay_n[k] > 1 ? 1.0f / V.lay_n[k] : 1, w = V.lay_w[k] * px * (1.0f + 0.8f * pk), b = V.lay_b[k] * (0.75f + 0.6f * pk), h = V.lay_h[k];
		/* drawn over a wider area than the frame, so it still covers it once turned */
		int i0 = (int) floorf (-0.45f / S), i1 = (int) ceilf (1.45f / S);
		if (b < 0.01f) continue;
		for (i = i0; i <= i1; i++)							/* horizontals */
			lv_line (-0.45f, i * S, 1.45f, i * S, w, h, b, ca, sa);
		for (i = i0 - (int) (1.0f / S); i <= i1 + (int) (1.0f / S); i++)	/* the two diagonals (slope +-2 in tile units) */
		{
			float x = i * S, y0 = -0.45f, y1 = 1.45f;
			lv_line (x + y0 / 2, y0, x + y1 / 2, y1, w, h, b, ca, sa);
			lv_line (x - y0 / 2, y0, x - y1 / 2, y1, w, h, b, ca, sa);
		}
	}
	glEnd ();
	glMatrixMode (GL_PROJECTION); glLoadIdentity (); gluOrtho2D (0, 1, 0, 1);
}

static void draw_grid (int N, GLenum mode)
{
	int i, j;
	float x, y, S = 1.0f / N, ii;
	for (j = 0; j < N; j++)
		for (i = -1; i < N + 1; i++)
		{
			ii = i; if (j % 2 == 1) ii += 0.5f;
			glBegin (mode);
			x = ii * S;             y = j * S;       gradient_color (x, y); glVertex2d (x, y);
			x = (ii + 1) * S;       y = j * S;       gradient_color (x, y); glVertex2d (x, y);
			x = (ii + 0.5f) * S;    y = (j + 1) * S; gradient_color (x, y); glVertex2d (x, y);
			glEnd ();
			ii -= 0.5f;
			glBegin (mode);
			x = ii * S;             y = (j + 1) * S; gradient_color (x, y); glVertex2d (x, y);
			x = (ii + 1) * S;       y = (j + 1) * S; gradient_color (x, y); glVertex2d (x, y);
			x = (ii + 0.5f) * S;    y = j * S;       gradient_color (x, y); glVertex2d (x, y);
			glEnd ();
		}
}

static void cube (float x, float y, float z, float d)
{
	glBegin (GL_QUADS);
	glVertex3f (x+d,y+d,z-d); glVertex3f (x-d,y+d,z-d); glVertex3f (x-d,y+d,z+d); glVertex3f (x+d,y+d,z+d);
	glVertex3f (x+d,y-d,z+d); glVertex3f (x-d,y-d,z+d); glVertex3f (x-d,y-d,z-d); glVertex3f (x+d,y-d,z-d);
	glVertex3f (x+d,y+d,z+d); glVertex3f (x-d,y+d,z+d); glVertex3f (x-d,y-d,z+d); glVertex3f (x+d,y-d,z+d);
	glVertex3f (x+d,y-d,z-d); glVertex3f (x-d,y-d,z-d); glVertex3f (x-d,y+d,z-d); glVertex3f (x+d,y+d,z-d);
	glVertex3f (x-d,y+d,z+d); glVertex3f (x-d,y+d,z-d); glVertex3f (x-d,y-d,z-d); glVertex3f (x-d,y-d,z+d);
	glVertex3f (x+d,y+d,z-d); glVertex3f (x+d,y+d,z+d); glVertex3f (x+d,y-d,z+d); glVertex3f (x+d,y-d,z-d);
	glEnd ();
}

/* a cube's place: its voice's orientation (column-major rotation of q) is
   pushed on the matrix stack; returns the centre and half size */
static void cube_push (int i, float *x, float *y, float *z, float *d)
{
	const float *q = V.vq[i % PAST_OSCS];
	float w = q[0], a = q[1], b = q[2], c = q[3];
	float m[16] = { 1 - 2*(b*b + c*c), 2*(a*b + w*c), 2*(a*c - w*b), 0,
	                2*(a*b - w*c), 1 - 2*(a*a + c*c), 2*(b*c + w*a), 0,
	                2*(a*c + w*b), 2*(b*c - w*a), 1 - 2*(a*a + b*b), 0,
	                0, 0, 0, 1 };
	*x = cube_base[i][0] * V.cube_r[i]; *y = cube_base[i][1] * V.cube_r[i] + V.cube_h[i];
	*z = cube_base[i][2] * V.cube_r[i]; *d = cube_base[i][3] * V.cube_s[i];
	glPushMatrix ();
	glMultMatrixf (m);
}

static void scene_camera (float fov)
{
	glMatrixMode (GL_PROJECTION);
	glLoadIdentity ();
	fov = 2.0f * atanf (tanf (fov * (float) M_PI / 360.0f) / V.zoom) * 360.0f / (float) M_PI;
	gluPerspective (fov, (GLfloat) W / (GLfloat) H, 0.1f, 100.0f);
	glMatrixMode (GL_MODELVIEW);
	glLoadIdentity ();
	glTranslatef (0, 0, V.cam_z);
	glRotatef ((float) V.rot, -0.1f, 0.7f, 0.3f);
}

/* The glass pass: the opaque cubes into depth only, then the glass cubes,
   each face coloured with its eye-space normal (R, G = x, y; B = how
   squarely it faces the camera), A = coverage. glass_screen() turns that
   into refraction, dispersion and diffraction fringes. */
/* Flaw textures, tiling, 256 x 256, in the face's own (tangent) frame:
   R, G = the surface's slope along u and v (signed around 0.5, scaled by
   1 / FLAW_S), B = its height (0 .. 1; thicker glass where high). The glass
   normal pass (fs_flaw) tilts each face's normal by the slope along the
   face's tangents, so the bumps refract, catch the light and thicken the
   glass the way the face is actually turned.
     1  gentle bumps     a few low, rolling swells per face
     2  strong bumps     deeper, a little finer
     3  fractured        Voronoi shards, each tilted its own way, with sharp
                         cracks (grooves) between them, over light bumps */
#define FT		256
#define FLAW_S	0.6f
static unsigned int fhash (int x, int y, int seed)
{
	unsigned int h = (unsigned int) (x * 374761393 + y * 668265263 + seed * 2246822519u);
	h = (h ^ (h >> 13)) * 1274126177u;
	return h ^ (h >> 16);
}

static float vnoise (float x, float y, int per, int seed)	/* tiling value noise, 0..1 */
{
	int x0 = (int) floorf (x), y0 = (int) floorf (y);
	float fx = x - x0, fy = y - y0, a, b, c, d;
	int x1 = ((x0 + 1) % per + per) % per, y1 = ((y0 + 1) % per + per) % per;
	x0 = (x0 % per + per) % per; y0 = (y0 % per + per) % per;
	fx = fx * fx * (3 - 2 * fx); fy = fy * fy * (3 - 2 * fy);
	a = (fhash (x0, y0, seed) & 0xffff) / 65535.0f; b = (fhash (x1, y0, seed) & 0xffff) / 65535.0f;
	c = (fhash (x0, y1, seed) & 0xffff) / 65535.0f; d = (fhash (x1, y1, seed) & 0xffff) / 65535.0f;
	return (a + (b - a) * fx) * (1 - fy) + (c + (d - c) * fx) * fy;
}

static float bumps (float u, float v, int lvl)		/* height, tiling over 0..1 */
{
	/* broad: two low octaves, the second faint */
	int base = lvl == 2 ? 3 : 2, o;
	float h = 0, amp = 1;
	for (o = 0; o < 2; o++, base *= 2, amp *= 0.3f) h += amp * vnoise (u * base, v * base, base, 17 + o + lvl * 7);
	return h;
}

static void make_flaw_textures (void)
{
	static unsigned char img[FT * FT * 4];
	static float gx[FT * FT], gy[FT * FT], hh[FT * FT];
	enum { NS = 14 };
	float sx[NS], sy[NS], tx[NS], ty[NS];
	int lvl, x, y, k, i;
	for (k = 0; k < NS; k++)
	{
		sx[k] = (fhash (k, 1, 99) & 0xffff) / 65535.0f; sy[k] = (fhash (k, 2, 99) & 0xffff) / 65535.0f;
		tx[k] = ((fhash (k, 3, 99) & 0xffff) / 65535.0f - 0.5f) * 0.24f;
		ty[k] = ((fhash (k, 4, 99) & 0xffff) / 65535.0f - 0.5f) * 0.24f;
	}
	glGenTextures (4, flaw_tex);
	for (lvl = 1; lvl < 4; lvl++)
	{
		/* the bumps' slope, normalised to a peak tilt per level */
		float peak = lvl == 1 ? 0.07f : lvl == 2 ? 0.13f : 0.025f, gm = 0, hmin = 9, hmax = -9;
		float e = 1.0f / FT;
		for (y = 0; y < FT; y++) for (x = 0; x < FT; x++)
		{
			float u = x / (float) FT, v = y / (float) FT;
			i = y * FT + x;
			gx[i] = bumps (u + e, v, lvl) - bumps (u - e, v, lvl);
			gy[i] = bumps (u, v + e, lvl) - bumps (u, v - e, lvl);
			hh[i] = bumps (u, v, lvl);
			if (fabsf (gx[i]) > gm) gm = fabsf (gx[i]);
			if (fabsf (gy[i]) > gm) gm = fabsf (gy[i]);
			if (hh[i] < hmin) hmin = hh[i];
			if (hh[i] > hmax) hmax = hh[i];
		}
		for (y = 0; y < FT; y++) for (x = 0; x < FT; x++)
		{
			float u = x / (float) FT, v = y / (float) FT, dx, dy, h;
			unsigned char *p = img + (y * FT + x) * 4;
			i = y * FT + x;
			dx = gx[i] / gm * peak; dy = gy[i] / gm * peak;
			h = (hh[i] - hmin) / (hmax - hmin);
			if (lvl == 3)
			{
				/* the nearest two shard centres (tiling): tilt from the nearest,
				   a groove where the two are about equally near */
				float d1 = 9, d2 = 9, w = 0.008f;
				int n1 = 0;
				h = 0.6f + 0.25f * (h - 0.5f);
				for (k = 0; k < NS; k++)
				{
					float ddx = u - sx[k], ddy = v - sy[k], dd;
					ddx -= roundf (ddx); ddy -= roundf (ddy);
					dd = sqrtf (ddx * ddx + ddy * ddy);
					if (dd < d1) { d2 = d1; d1 = dd; n1 = k; } else if (dd < d2) d2 = dd;
				}
				dx += tx[n1]; dy += ty[n1];
				if (d2 - d1 < w)
				{
					float c = 1.0f - (d2 - d1) / w;
					c = c * c * (3 - 2 * c);
					dx += 0.15f * c * (((fhash (n1, 5, 3) & 1) ? 1 : -1));
					dy += 0.15f * c * (((fhash (n1, 6, 3) & 1) ? 1 : -1));
					h *= 1.0f - 0.7f * c;
				}
			}
			dx /= FLAW_S; dy /= FLAW_S;
			dx = dx < -1 ? -1 : dx > 1 ? 1 : dx;
			dy = dy < -1 ? -1 : dy > 1 ? 1 : dy;
			p[0] = (unsigned char) (127.5f + 127.5f * dx);
			p[1] = (unsigned char) (127.5f + 127.5f * dy);
			p[2] = (unsigned char) (255.0f * (h < 0 ? 0 : h > 1 ? 1 : h));
			p[3] = 255;
		}
		glBindTexture (GL_TEXTURE_2D, flaw_tex[lvl]);
		glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
		glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
		glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
		glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		gluBuild2DMipmaps (GL_TEXTURE_2D, GL_RGBA, FT, FT, GL_RGBA, GL_UNSIGNED_BYTE, img);
	}
	glBindTexture (GL_TEXTURE_2D, 0);
}

/* a face with texture coordinates (the flaw texture), in cube_face's order */
static void cube_face_tc (float x, float y, float z, float d, int f, const float *tc)
{
	static const float uv[4][2] = { {0,0}, {1,0}, {1,1}, {0,1} };
	float v[4][3];
	int k;
	switch (f)
	{
	case 0: { float t[4][3] = {{x+d,y+d,z-d},{x-d,y+d,z-d},{x-d,y+d,z+d},{x+d,y+d,z+d}}; memcpy (v, t, sizeof v); break; }
	case 1: { float t[4][3] = {{x+d,y-d,z+d},{x-d,y-d,z+d},{x-d,y-d,z-d},{x+d,y-d,z-d}}; memcpy (v, t, sizeof v); break; }
	case 2: { float t[4][3] = {{x+d,y+d,z+d},{x-d,y+d,z+d},{x-d,y-d,z+d},{x+d,y-d,z+d}}; memcpy (v, t, sizeof v); break; }
	case 3: { float t[4][3] = {{x+d,y-d,z-d},{x-d,y-d,z-d},{x-d,y+d,z-d},{x+d,y+d,z-d}}; memcpy (v, t, sizeof v); break; }
	case 4: { float t[4][3] = {{x-d,y+d,z+d},{x-d,y+d,z-d},{x-d,y-d,z-d},{x-d,y-d,z+d}}; memcpy (v, t, sizeof v); break; }
	default: { float t[4][3] = {{x+d,y+d,z-d},{x+d,y+d,z+d},{x+d,y-d,z+d},{x+d,y-d,z-d}}; memcpy (v, t, sizeof v); break; }
	}
	glMultiTexCoord3f (GL_TEXTURE1, v[1][0] - v[0][0], v[1][1] - v[0][1], v[1][2] - v[0][2]);
	glMultiTexCoord3f (GL_TEXTURE2, v[3][0] - v[0][0], v[3][1] - v[0][1], v[3][2] - v[0][2]);
	for (k = 0; k < 4; k++)
	{
		glTexCoord2f (tc[1] + f * 0.37f + uv[k][0] * tc[0], tc[2] + f * 0.61f + uv[k][1] * tc[0]);
		glVertex3fv (v[k]);
	}
}

static void scene_glass (int mirrors)
{
	int i, f;
	static const float fn[6][3] = { {0,1,0}, {0,-1,0}, {0,0,1}, {0,0,-1}, {-1,0,0}, {1,0,0} };
	glClearColor (0.5f, 0.5f, 0, 0);
	glClear (GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT);
	glDisable (GL_BLEND);
	scene_camera (45.0f);
	/* everything else (opaque cubes, and the glass of the other kind) into depth */
	glColorMask (0, 0, 0, 0);
	for (i = 0; i < NC; i++)
	{
		float x, y, z, d;
		if (cube_glass[i] && cube_mirror[i] == mirrors) continue;
		cube_push (i, &x, &y, &z, &d); cube (x, y, z, d); glPopMatrix ();
	}
	glColorMask (1, 1, 1, 1);
	if (!flaw_tex[1]) make_flaw_textures ();
	/* fs_flaw: each face's eye-space normal, tilted by its flaw texture along
	   the face's own tangents -> R, G = normal x, y; B = glass height */
	glUseProgram (pr_flaw);
	glUniform1i (glGetUniformLocation (pr_flaw, "flaw"), 0);
	glActiveTexture (GL_TEXTURE0);
	for (i = 0; i < NC; i++)
	{
		float x, y, z, d;
		int q;
		if (!cube_glass[i] || cube_mirror[i] != mirrors) continue;
		cube_push (i, &x, &y, &z, &d);
		/* the fractured ones are whole for the first minute, then crack */
		q = glass_q[i] == 3 && now_s < glass_crack[i] ? 0 : glass_q[i];
		glBindTexture (GL_TEXTURE_2D, flaw_tex[q ? q : 1]);
		glUniform1f (glGetUniformLocation (pr_flaw, "amt"), q ? (mirrors ? 0.1f : 1.0f) : 0.0f);
		/* cube() draws its faces in fn's order, 4 vertices each */
		glBegin (GL_QUADS);
		for (f = 0; f < 6; f++)
		{
			glNormal3fv (fn[f]);
			cube_face_tc (x, y, z, d, f, glass_tc[i]);
		}
		glEnd ();
		glPopMatrix ();
	}
	glBindTexture (GL_TEXTURE_2D, 0);
	glUseProgram (0);
	glEnable (GL_BLEND);
	glClearColor (0, 0, 0, 1);
}

static void scene_3d (float fov)
{
	int i;
	float cr, cg, cb;
	glClearColor (1, 1, 1, 0);				/* alpha 0: no cube */
	glClear (GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT);
	glDisable (GL_BLEND);					/* alpha carries each cube's new-way weight */
	/* zoom: scene_camera narrows the field of view (keeps the glow renders' spread in proportion) */
	scene_camera (fov);
	hsv (HUE, 1, 1, 1, &cr, &cg, &cb);
	for (i = 0; i < NC; i++)
	{
		float x, y, z, d;
		if (cube_glass[i]) continue;			/* glass: see scene_glass */
		cube_push (i, &x, &y, &z, &d);
		glColor4f (cr, cg, cb, V.cube_g[i]);
		cube (x, y, z, d);
		glPopMatrix ();
	}
	glEnable (GL_BLEND);
}

/* ------------------------------------------------------------------ */
/*  software compositing (p10m1's blur / matte / mask, made fast)       */
/* ------------------------------------------------------------------ */

/* Two ways of compositing a cube, mixed per pixel by the cube's tag (the
   alpha written by scene_3d):
   - old way: p10m1's hard "not white" test. It also copies the pale halo of
     the blurred cube renders -- the white bleed into the glow and echoes.
   - new way: a coverage weight (how far from white towards the cube colour),
     so multisampled edges blend smoothly and the halo is dropped. */

static int	cov_span = 300;					/* 765 - cube colour sum, per frame */

static void set_coverage (float hue)
{
	float r, g, b;
	hsv (hue, 1, 1, 1, &r, &g, &b);
	cov_span = 765 - (int) ((r + g + b) * 255.0f);
	if (cov_span < 32) cov_span = 32;
}

/* ------------------------------------------------------------------ */
/*  GPU compositing                                                    */
/* ------------------------------------------------------------------ */
/* The geometry (grids, lattice, the three cube renders, the glass and
   mirror normal passes) renders into a multisampled offscreen buffer that is
   resolved into textures; every per-pixel step -- p10m1's blur, mattes and
   feedback, the glass and mirrors, the lattice showing through, the title,
   harm's frame echoes -- is a fragment shader pass. p10m1's blur, mattes and
   feedback were integer maths with truncation (which is part of how the
   picture drifts to black); the shaders emulate that arithmetic exactly. */

typedef struct { GLuint fbo, tex; int w, h; } rt_t;

#define NPOOL	7
static rt_t		pool[NPOOL];				/* full-resolution working targets */
static rt_t		prev_rt[2];					/* p10m1's feedback memory, ping-pong */
static int		prev_cur;
static rt_t		count_rt;					/* 128 x 72: how much of the lattice shows */
static rt_t		integ_rt[2];				/* the title's integrity, ping-pong (float) */
static int		integ_cur;
static GLuint	cov_tex;					/* the title's coverage */
static int		tbx, tby, tbw, tbh;			/* its box */
static GLuint	msaa_fbo, msaa_col, msaa_dep;
/* the glass / mirror normal passes: their own multisampled buffer and
   targets in half float -- 8 bits per normal component steps a bumpy
   mirror's reflection by ~14 px at a time (terraces) */
static GLuint	msaa_nfbo, msaa_ncol, msaa_ndep;
static rt_t		nrm_rt[3];					/* resolved; glass, softened; mirrors, softened */
static int		msaa_samples = 4;
static int		gpu_w, gpu_h;
#define ECHO_FRAMES	80
static rt_t		echo_rt[ECHO_FRAMES];
static int		echo_pos, echo_fill, echo_w, echo_h;

static GLuint	pr_nsoften, pr_blur, pr_matte, pr_feedback, pr_soften, pr_glass, pr_reveal, pr_count,
				pr_integ, pr_stamp, pr_echo, pr_copy;

static const char *vs_src = "#version 120\nvoid main () { gl_Position = ftransform (); }\n";

#define GLSL_COMMON \
	"#version 120\n" \
	"float tr (float x) { return sign (x) * floor (abs (x) + 1e-4); }\n" \
	"vec4 I (sampler2D t, vec2 p, vec2 sz) { return floor (texture2D (t, p / sz) * 255.0 + 0.5); }\n" \
	"vec3 hsv2rgb (float h, float s, float v) { vec3 k = clamp (abs (mod (h / 60.0 + vec3 (0.0, 4.0, 2.0), 6.0) - 3.0) - 1.0, 0.0, 1.0); return v * mix (vec3 (1.0), k, s); }\n"

/* p10m1's 3x3 box blur with centre weight iw/256, all four channels (the
   alpha tags are blurred with the same kernel), rows 0 and H-1 untouched */
static const char *fs_blur = GLSL_COMMON
	"uniform sampler2D src; uniform vec2 sz; uniform float iw;\n"
	"void main () { vec2 p = gl_FragCoord.xy; vec4 c = I (src, p, sz);\n"
	" if (p.y < 1.0 || p.y > sz.y - 1.0) { gl_FragColor = c / 255.0; return; }\n"
	" vec4 b = vec4 (0.0); for (int y = -1; y <= 1; y++) for (int x = -1; x <= 1; x++) b += I (src, p + vec2 (x, y), sz);\n"
	" gl_FragColor = floor (((b - c) * 256.0 + iw * c) / (2048.0 + iw) + 1e-4) / 255.0; }\n";

/* where j is covered (composite_weight: the old hard test mixed with the
   coverage weight by j's alpha tag), i <- k; alpha is i's */
static const char *fs_matte = GLSL_COMMON
	"uniform sampler2D ti, tj, tk; uniform vec2 sz; uniform float cs;\n"
	"void main () { vec2 p = gl_FragCoord.xy; vec4 i = I (ti, p, sz), j = I (tj, p, sz), k = I (tk, p, sz);\n"
	" float d = 765.0 - (j.r + j.g + j.b), w = 0.0;\n"
	" if (d > 0.0) { float hard = d > 13.0 ? 256.0 : 0.0;\n"
	"  float cov = clamp (tr ((d - 13.0) * 256.0 / (cs - 13.0)), 0.0, 256.0);\n"
	"  float g = floor (j.a * cs / max (d, 1.0) + 1e-4); g = g > 255.0 ? 256.0 : g;\n"
	"  w = hard + floor ((cov - hard) * g / 256.0 + 1e-6); }\n"
	" vec3 o = w <= 0.0 ? i.rgb : w >= 256.0 ? k.rgb : i.rgb + floor ((k.rgb - i.rgb) * w / 256.0 + 1e-6);\n"
	" gl_FragColor = vec4 (o, i.a) / 255.0; }\n";

/* feedback: prev + (cur - prev) * im / 256, C's truncating division */
static const char *fs_feedback = GLSL_COMMON
	"uniform sampler2D tp, tc; uniform vec2 sz; uniform float im;\n"
	"void main () { vec2 p = gl_FragCoord.xy; vec4 a = I (tp, p, sz), c = I (tc, p, sz);\n"
	" vec3 d = (c.rgb - a.rgb) * im / 256.0;\n"
	" gl_FragColor = vec4 (a.rgb + vec3 (tr (d.r), tr (d.g), tr (d.b)), 255.0) / 255.0; }\n";

/* the glass normal pass: tangent-space bump mapping */
static const char *vs_flaw = "#version 120\n"
	"varying vec3 vn, vt, vb;\n"
	"void main () { gl_Position = ftransform (); gl_TexCoord[0] = gl_MultiTexCoord0;\n"
	" vn = gl_NormalMatrix * gl_Normal; vt = gl_NormalMatrix * gl_MultiTexCoord1.xyz; vb = gl_NormalMatrix * gl_MultiTexCoord2.xyz; }\n";
static const char *fs_flaw = "#version 120\n"
	"uniform sampler2D flaw; uniform float amt; varying vec3 vn, vt, vb;\n"
	"void main () { vec4 f = texture2D (flaw, gl_TexCoord[0].st);\n"
	" vec2 s = (f.rg * 2.0 - 1.0) * 0.6 * amt;\n"
	" vec3 n = normalize (vn), t = normalize (vt - dot (vt, n) * n), b = normalize (vb - dot (vb, n) * n);\n"
	" n = normalize (n - s.x * t - s.y * b);\n"
	" if (n.z < 0.0) n = -n;\n"
	" gl_FragColor = vec4 (0.5 + 0.5 * n.x, 0.5 + 0.5 * n.y, mix (0.5, f.b, amt), 1.0); }\n";

/* the glass / mirror normal passes, softened (1 2 1)^2 / 16 where covered */
static const char *fs_soften = GLSL_COMMON
	"uniform sampler2D src; uniform vec2 sz;\n"
	"void main () { vec2 p = gl_FragCoord.xy; vec4 c = I (src, p, sz);\n"
	" float a = c.a + I (src, p + vec2 (-1, 0), sz).a + I (src, p + vec2 (1, 0), sz).a + I (src, p + vec2 (0, -1), sz).a + I (src, p + vec2 (0, 1), sz).a;\n"
	" if (a <= 0.0 || p.x < 1.0 || p.y < 1.0 || p.x > sz.x - 1.0 || p.y > sz.y - 1.0) { gl_FragColor = c / 255.0; return; }\n"
	" vec4 s = vec4 (0.0); for (float y = -1.0; y <= 1.0; y += 1.0) for (float x = -1.0; x <= 1.0; x += 1.0) s += I (src, p + vec2 (x, y), sz) * (2.0 - abs (x)) * (2.0 - abs (y));\n"
	" gl_FragColor = floor (s / 16.0 + 1e-4) / 255.0; }\n";

/* the same softening for the (half float) normal passes, unrounded */
static const char *fs_nsoften = GLSL_COMMON
	"uniform sampler2D src; uniform vec2 sz;\n"
	"vec4 F (vec2 p) { return texture2D (src, p / sz); }\n"
	"void main () { vec2 p = gl_FragCoord.xy; vec4 c = F (p);\n"
	" float a = c.a + F (p + vec2 (-1, 0)).a + F (p + vec2 (1, 0)).a + F (p + vec2 (0, -1)).a + F (p + vec2 (0, 1)).a;\n"
	" if (a <= 0.0 || p.x < 1.0 || p.y < 1.0 || p.x > sz.x - 1.0 || p.y > sz.y - 1.0) { gl_FragColor = c; return; }\n"
	" vec4 s = vec4 (0.0); for (float y = -1.0; y <= 1.0; y += 1.0) for (float x = -1.0; x <= 1.0; x += 1.0) s += F (p + vec2 (x, y)) * (2.0 - abs (x)) * (2.0 - abs (y));\n"
	" gl_FragColor = s / 16.0; }\n";

/* glass (refraction, dispersion, lossy, Fresnel, rainbow fringes at the
   edges) and mirrors (the view ray reflected off the face onto the frame) */
static const char *fs_glass = GLSL_COMMON
	"uniform sampler2D src, tg, tm; uniform vec2 sz; uniform float k, disp, ph, fpx;\n"
	"vec3 blend (vec3 q, vec3 v, float a) { v = floor (min (v, 255.0)); return q + vec3 (tr ((v.r - q.r) * a / 255.0), tr ((v.g - q.g) * a / 255.0), tr ((v.b - q.b) * a / 255.0)); }\n"
	"void main () { vec2 p = gl_FragCoord.xy, x = p - 0.5; vec3 q = I (src, p, sz).rgb;\n"
	" vec4 g = texture2D (tg, p / sz) * 255.0;\n"
	" if (g.a >= 8.0 && x.x >= 1.0 && x.y >= 1.0 && x.x < sz.x - 1.0 && x.y < sz.y - 1.0) {\n"
	"  float nx = (g.r - 127.5) / 127.5, ny = (g.g - 127.5) / 127.5, h = g.b / 255.0;\n"
	"  vec3 n = vec3 (nx, ny, sqrt (max (1.0 - nx * nx - ny * ny, 0.0)));\n"
	"  vec3 v = normalize (vec3 ((x - sz * 0.5) / fpx, -1.0));\n"
	"  float c = max (-dot (v, n), 0.0);\n"
	"  vec4 gx = texture2D (tg, (p + vec2 (1, 0)) / sz) * 255.0, gy = texture2D (tg, (p + vec2 (0, 1)) / sz) * 255.0;\n"
	"  float edge = min ((abs (g.r - gx.r) + abs (g.g - gx.g) + abs (g.r - gy.r) + abs (g.g - gy.g)) / 255.0, 1.0);\n"
	/* Snell at the face (n = 1.5): the ray's bend on screen, times the glass's
	   thickness there (thicker where its surface is high) */
	"  vec3 t = refract (v, n, 1.0 / 1.5);\n"
	"  vec2 dv = (t.xy / min (t.z, -0.45) - v.xy / v.z) * k * 3.0 * (0.6 + 0.8 * h);\n"
	"  vec3 sh = vec3 (1.0 - disp, 1.0, 1.0 + disp), o;\n"
	"  o.r = texture2D (src, (p + dv * sh.r) / sz).r * 255.0;\n"
	"  o.g = texture2D (src, (p + dv * sh.g) / sz).g * 255.0;\n"
	"  o.b = texture2D (src, (p + dv * sh.b) / sz).b * 255.0;\n"
	"  float path = (0.6 + 0.8 * h) / (0.35 + 0.65 * c);\n"
	"  vec3 T = exp (-vec3 (0.17, 0.12, 0.08) * path);\n"
	"  float fr = (1.0 - c) * (1.0 - c) * 30.0;\n"
	"  float spec = pow (max (dot (reflect (v, n), normalize (vec3 (-0.35, 0.55, 0.75))), 0.0), 48.0) * 70.0;\n"
	"  vec3 rb = vec3 (0.0);\n"
	"  if (edge > 0.05) rb = hsv2rgb (360.0 * fract ((x.x + x.y) / 23.0 * 0.1 + nx * 0.5 + ph), 0.8, 1.0) * 70.0 * edge;\n"
	"  q = blend (q, o * T + fr + spec + rb, g.a); }\n"
	" vec4 m = texture2D (tm, p / sz) * 255.0;\n"
	" if (m.a >= 8.0 && x.x >= 1.0 && x.y >= 1.0 && x.x < sz.x - 1.0 && x.y < sz.y - 1.0) {\n"
	"  vec2 mn = (m.rg - 127.5) / 127.5; vec3 n = vec3 (mn, sqrt (max (1.0 - dot (mn, mn), 0.0)));\n"
	"  if (length (n) > 1e-3) { n = normalize (n);\n"
	"   vec3 v = normalize (vec3 ((x - sz * 0.5) / fpx, -1.0));\n"
	"   vec3 r = v - 2.0 * dot (v, n) * n;\n"
	"   vec2 s = vec2 (sz.x * 0.5 - r.x * sz.x * 0.7, sz.y * 0.5 - r.y * sz.x * 0.7);\n"
	"   vec2 mu = (s + 0.5) / sz; mu = 1.0 - abs (1.0 - mod (mu, 2.0));\n"
	"   vec3 c = texture2D (src, mu).rgb * 255.0;\n"
	"   float fr = (1.0 - n.z) * (1.0 - n.z) * 45.0;\n"
	"   q = blend (q, c * vec3 (0.86, 0.86, 0.9) + fr + 6.0, m.a); } }\n"
	" gl_FragColor = vec4 (q / 255.0, 1.0); }\n";

/* the lattice showing through where the picture is near black */
static const char *fs_reveal = GLSL_COMMON
	"uniform sampler2D ti, tl; uniform vec2 sz;\n"
	"void main () { vec2 p = gl_FragCoord.xy; vec3 i = I (ti, p, sz).rgb, l = I (tl, p, sz).rgb;\n"
	" float lum = i.r + i.g + i.b;\n"
	" if (lum < 150.0 && l.r + l.g + l.b > 0.0) { float w = lum <= 30.0 ? 256.0 : tr ((150.0 - lum) * 256.0 / 120.0);\n"
	"  i = min (i + floor (l * w / 256.0 + 1e-6), 255.0); }\n"
	" gl_FragColor = vec4 (i / 255.0, 1.0); }\n";

/* how much of the lattice shows (sampled 8 x 8 per cell of a 128 x 72 grid) */
static const char *fs_count = GLSL_COMMON
	"uniform sampler2D ti, tl; uniform vec2 sz, cell;\n"
	"void main () { vec2 c0 = floor (gl_FragCoord.xy) * cell; float n = 0.0;\n"
	" for (int y = 0; y < 8; y++) for (int x = 0; x < 8; x++) { vec2 p = floor (c0 + (vec2 (x, y) + 0.5) * cell / 8.0) + 0.5;\n"
	"  vec3 i = I (ti, p, sz).rgb, l = I (tl, p, sz).rgb; float lum = i.r + i.g + i.b, ls = l.r + l.g + l.b;\n"
	"  if (lum < 150.0 && ls > 90.0) { float w = lum <= 30.0 ? 256.0 : tr ((150.0 - lum) * 256.0 / 120.0); if (w > 64.0) n += 1.0; } }\n"
	" gl_FragColor = vec4 (n / 64.0); }\n";

/* the title's integrity: eaten where the picture differs from the plain
   background behind it, raggedly; the damage creeps from bitten edges */
static const char *fs_integ = GLSL_COMMON
	"uniform sampler2D tprev, tf, tb; uniform vec2 box, fsz, isz; uniform float dt, rate, seed;\n"
	"float h (vec2 p, float s) { return fract (sin (dot (p, vec2 (12.9898, 78.233)) + s * 0.618) * 43758.5453); }\n"
	"void main () { vec2 p = gl_FragCoord.xy; float v = texture2D (tprev, p / isz).r;\n"
	" if (v > 0.0) { vec2 f = box + p; vec3 a = I (tf, f, fsz).rgb, b = I (tb, f, fsz).rgb;\n"
	"  float d = (abs (a.r - b.r) + abs (a.g - b.g) + abs (a.b - b.b)) / 3.0, act = clamp ((d - 18.0) / 70.0, 0.0, 1.0);\n"
	"  if (act > 0.0) v -= dt * rate * act * (0.4 + 1.6 * h (p, seed));\n"
	"  float m = (texture2D (tprev, (p + vec2 (-1, 0)) / isz).r + texture2D (tprev, (p + vec2 (1, 0)) / isz).r\n"
	"           + texture2D (tprev, (p + vec2 (0, -1)) / isz).r + texture2D (tprev, (p + vec2 (0, 1)) / isz).r) * 0.25;\n"
	"  if (m < v) v -= dt * 0.5 * (v - m) * (0.5 + h (p + 17.0, seed));\n"
	"  v = max (v, 0.0); }\n"
	" gl_FragColor = vec4 (v); }\n";

/* the title stamped into the frame (what is left of it) */
static const char *fs_stamp = GLSL_COMMON
	"uniform sampler2D ti, tc, tint; uniform vec2 sz, box, isz;\n"
	"void main () { vec2 p = gl_FragCoord.xy; vec3 i = I (ti, p, sz).rgb; vec2 q = p - box;\n"
	" if (q.x >= 0.0 && q.y >= 0.0 && q.x < isz.x && q.y < isz.y) {\n"
	"  float a = floor (floor (texture2D (tc, q / isz).r * 255.0 + 0.5) * max (texture2D (tint, q / isz).r, 0.0) * 0.94);\n"
	"  if (a > 0.0) { vec3 d = (vec3 (242.0, 239.0, 231.0) - i) * a / 255.0; i += vec3 (tr (d.r), tr (d.g), tr (d.b)); } }\n"
	" gl_FragColor = vec4 (i / 255.0, 1.0); }\n";

/* harm's frame echoes: the image kept by (1 - the networks' weights) plus
   12 delayed frames (6 taps per network, left / right crossfaded) */
static const char *fs_echo = GLSL_COMMON
	"uniform sampler2D ti, e0, e1, e2, e3, e4, e5, e6, e7, e8, e9, e10, e11; uniform vec2 sz; uniform float w[12]; uniform float ws0, ws1;\n"
	"void main () { vec2 p = gl_FragCoord.xy, u = p / sz; vec3 i = I (ti, p, sz).rgb;\n"
	" float t = clamp ((u.x - 0.4) / 0.2, 0.0, 1.0), L = 1.0 - t, R = t;\n"
	" vec3 a = (w[0] * texture2D (e0, u).rgb + w[1] * texture2D (e1, u).rgb + w[2] * texture2D (e2, u).rgb\n"
	"         + w[3] * texture2D (e3, u).rgb + w[4] * texture2D (e4, u).rgb + w[5] * texture2D (e5, u).rgb) * L\n"
	"        + (w[6] * texture2D (e6, u).rgb + w[7] * texture2D (e7, u).rgb + w[8] * texture2D (e8, u).rgb\n"
	"         + w[9] * texture2D (e9, u).rgb + w[10] * texture2D (e10, u).rgb + w[11] * texture2D (e11, u).rgb) * R;\n"
	" float keep = 1.0 - (L * ws0 + R * ws1);\n"
	" gl_FragColor = vec4 (floor (min (keep * i + a * 255.0, 255.0)) / 255.0, 1.0); }\n";

static const char *fs_copy = "#version 120\nuniform sampler2D src; uniform vec2 sz;\nvoid main () { gl_FragColor = texture2D (src, gl_FragCoord.xy / sz); }\n";

static GLuint shader (GLenum type, const char *src)
{
	GLuint s = glCreateShader (type);
	GLint ok = 0;
	glShaderSource (s, 1, &src, NULL);
	glCompileShader (s);
	glGetShaderiv (s, GL_COMPILE_STATUS, &ok);
	if (!ok) { char log[4096]; glGetShaderInfoLog (s, sizeof log, NULL, log); fprintf (stderr, "shader:\n%s\n%s\n", src, log); exit (1); }
	return s;
}

static GLuint program_vs (const char *vs, const char *fs)
{
	GLuint p = glCreateProgram ();
	GLint ok = 0;
	glAttachShader (p, shader (GL_VERTEX_SHADER, vs));
	glAttachShader (p, shader (GL_FRAGMENT_SHADER, fs));
	glLinkProgram (p);
	glGetProgramiv (p, GL_LINK_STATUS, &ok);
	if (!ok) { char log[4096]; glGetProgramInfoLog (p, sizeof log, NULL, log); fprintf (stderr, "link: %s\n", log); exit (1); }
	return p;
}
static GLuint program (const char *fs) { return program_vs (vs_src, fs); }

static void rt_make (rt_t *r, int w, int h, int fl)
{
	if (r->fbo) { glDeleteFramebuffersEXT (1, &r->fbo); glDeleteTextures (1, &r->tex); }
	r->w = w; r->h = h;
	glGenTextures (1, &r->tex);
	glBindTexture (GL_TEXTURE_2D, r->tex);
	glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexImage2D (GL_TEXTURE_2D, 0, fl == 2 ? GL_RGBA16F_ARB : fl ? GL_RGBA32F_ARB : GL_RGBA8, w, h, 0, GL_RGBA, fl ? GL_FLOAT : GL_UNSIGNED_BYTE, NULL);
	glGenFramebuffersEXT (1, &r->fbo);
	glBindFramebufferEXT (GL_FRAMEBUFFER_EXT, r->fbo);
	glFramebufferTexture2DEXT (GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT, GL_TEXTURE_2D, r->tex, 0);
	if (glCheckFramebufferStatusEXT (GL_FRAMEBUFFER_EXT) != GL_FRAMEBUFFER_COMPLETE_EXT)
		fprintf (stderr, "framebuffer %dx%d incomplete\n", w, h);
	glBindFramebufferEXT (GL_FRAMEBUFFER_EXT, 0);
}

static void rt_clear (rt_t *r, float v)
{
	glBindFramebufferEXT (GL_FRAMEBUFFER_EXT, r->fbo);
	glViewport (0, 0, r->w, r->h);
	glClearColor (v, v, v, v);
	glClear (GL_COLOR_BUFFER_BIT);
}

/* (re)make everything for a W x H frame */
static void gpu_alloc (void)
{
	int k, ms = 0;
	if (gpu_w == W && gpu_h == H) return;
	if (!pr_blur)
	{
		pr_blur = program (fs_blur); pr_matte = program (fs_matte); pr_feedback = program (fs_feedback);
		pr_soften = program (fs_soften); pr_nsoften = program (fs_nsoften); pr_glass = program (fs_glass); pr_flaw = program_vs (vs_flaw, fs_flaw); pr_reveal = program (fs_reveal);
		pr_count = program (fs_count); pr_integ = program (fs_integ); pr_stamp = program (fs_stamp);
		pr_echo = program (fs_echo); pr_copy = program (fs_copy);
	}
	for (k = 0; k < NPOOL; k++) rt_make (&pool[k], W, H, 0);
	for (k = 0; k < 2; k++) rt_make (&prev_rt[k], W, H, 0);
	rt_make (&count_rt, 128, 72, 0);
	/* the echo ring: half resolution, at most 960 wide */
	echo_w = W / 2 > 960 ? 960 : W / 2; echo_h = (int) lroundf (echo_w * (float) H / W);
	for (k = 0; k < ECHO_FRAMES; k++) rt_make (&echo_rt[k], echo_w, echo_h, 0);
	echo_fill = 0; echo_pos = 0;
	/* the multisampled geometry buffer */
	glGetIntegerv (GL_MAX_SAMPLES_EXT, &ms);
	if (msaa_samples > ms) msaa_samples = ms;
	if (msaa_fbo) { glDeleteFramebuffersEXT (1, &msaa_fbo); glDeleteRenderbuffersEXT (1, &msaa_col); glDeleteRenderbuffersEXT (1, &msaa_dep); }
	glGenFramebuffersEXT (1, &msaa_fbo);
	glBindFramebufferEXT (GL_FRAMEBUFFER_EXT, msaa_fbo);
	glGenRenderbuffersEXT (1, &msaa_col);
	glBindRenderbufferEXT (GL_RENDERBUFFER_EXT, msaa_col);
	glRenderbufferStorageMultisampleEXT (GL_RENDERBUFFER_EXT, msaa_samples, GL_RGBA8, W, H);
	glFramebufferRenderbufferEXT (GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT, GL_RENDERBUFFER_EXT, msaa_col);
	glGenRenderbuffersEXT (1, &msaa_dep);
	glBindRenderbufferEXT (GL_RENDERBUFFER_EXT, msaa_dep);
	glRenderbufferStorageMultisampleEXT (GL_RENDERBUFFER_EXT, msaa_samples, GL_DEPTH_COMPONENT24, W, H);
	glFramebufferRenderbufferEXT (GL_FRAMEBUFFER_EXT, GL_DEPTH_ATTACHMENT_EXT, GL_RENDERBUFFER_EXT, msaa_dep);
	if (glCheckFramebufferStatusEXT (GL_FRAMEBUFFER_EXT) != GL_FRAMEBUFFER_COMPLETE_EXT)
		fprintf (stderr, "multisample framebuffer incomplete\n");
	/* and the normals' (half float) */
	if (msaa_nfbo) { glDeleteFramebuffersEXT (1, &msaa_nfbo); glDeleteRenderbuffersEXT (1, &msaa_ncol); glDeleteRenderbuffersEXT (1, &msaa_ndep); }
	glGenFramebuffersEXT (1, &msaa_nfbo);
	glBindFramebufferEXT (GL_FRAMEBUFFER_EXT, msaa_nfbo);
	glGenRenderbuffersEXT (1, &msaa_ncol);
	glBindRenderbufferEXT (GL_RENDERBUFFER_EXT, msaa_ncol);
	glRenderbufferStorageMultisampleEXT (GL_RENDERBUFFER_EXT, msaa_samples, GL_RGBA16F_ARB, W, H);
	glFramebufferRenderbufferEXT (GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT, GL_RENDERBUFFER_EXT, msaa_ncol);
	glGenRenderbuffersEXT (1, &msaa_ndep);
	glBindRenderbufferEXT (GL_RENDERBUFFER_EXT, msaa_ndep);
	glRenderbufferStorageMultisampleEXT (GL_RENDERBUFFER_EXT, msaa_samples, GL_DEPTH_COMPONENT24, W, H);
	glFramebufferRenderbufferEXT (GL_FRAMEBUFFER_EXT, GL_DEPTH_ATTACHMENT_EXT, GL_RENDERBUFFER_EXT, msaa_ndep);
	if (glCheckFramebufferStatusEXT (GL_FRAMEBUFFER_EXT) != GL_FRAMEBUFFER_COMPLETE_EXT)
		fprintf (stderr, "multisample normal framebuffer incomplete\n");
	for (k = 0; k < 3; k++) rt_make (&nrm_rt[k], W, H, 2);
	/* no mirrors: their normal pass stays empty */
	glBindFramebufferEXT (GL_FRAMEBUFFER_EXT, nrm_rt[2].fbo);
	{
		GLfloat cc[4];
		glGetFloatv (GL_COLOR_CLEAR_VALUE, cc);
		glClearColor (0, 0, 0, 0); glClear (GL_COLOR_BUFFER_BIT);
		glClearColor (cc[0], cc[1], cc[2], cc[3]);
	}
	glBindFramebufferEXT (GL_FRAMEBUFFER_EXT, 0);
	glBindFramebufferEXT (GL_FRAMEBUFFER_EXT, 0);
	if (getenv ("P10M1_DEBUG")) fprintf (stderr, "frame %dx%d, %d samples, echo %dx%d\n", W, H, msaa_samples, echo_w, echo_h);
	/* the title */
	title_build (W, H, app_dir);
	{
		const unsigned char *cv;
		if (title_info (&tbx, &tby, &tbw, &tbh, &cv))
		{
			if (!cov_tex) glGenTextures (1, &cov_tex);
			glBindTexture (GL_TEXTURE_2D, cov_tex);
			glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
			glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
			glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
			glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
			glPixelStorei (GL_UNPACK_ALIGNMENT, 1);
			glTexImage2D (GL_TEXTURE_2D, 0, GL_LUMINANCE8, tbw, tbh, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, cv);
			for (k = 0; k < 2; k++) { rt_make (&integ_rt[k], tbw, tbh, 1); rt_clear (&integ_rt[k], 1.0f); }
		}
	}
	gpu_w = W; gpu_h = H;
	frames = 0;
}

/* geometry: into the multisampled buffer, then resolved into a target */
static void geom_begin (void)
{
	glUseProgram (0);
	glBindFramebufferEXT (GL_FRAMEBUFFER_EXT, msaa_fbo);
	glViewport (0, 0, W, H);
	glEnable (GL_DEPTH_TEST);
	glEnable (GL_BLEND);
	glEnable (GL_MULTISAMPLE);
}

static void geom_resolve (rt_t *dst)
{
	glBindFramebufferEXT (GL_READ_FRAMEBUFFER_EXT, msaa_fbo);
	glBindFramebufferEXT (GL_DRAW_FRAMEBUFFER_EXT, dst->fbo);
	glBlitFramebufferEXT (0, 0, W, H, 0, 0, W, H, GL_COLOR_BUFFER_BIT, GL_NEAREST);
	glBindFramebufferEXT (GL_FRAMEBUFFER_EXT, msaa_fbo);
}

/* the glass and mirror normal passes (drawn by draw (mirrors)), softened
   into nrm_rt[1] (glass) and nrm_rt[2] (mirrors) */
static void pass (rt_t *dst, GLuint prog);
static void tex (const char *name, GLuint t);
static void draw (void);
static void glass_normals (void (*drawfn) (int))
{
	int m;
	for (m = 0; m < 1 + MIRRORS; m++)
	{
		geom_begin ();
		glBindFramebufferEXT (GL_FRAMEBUFFER_EXT, msaa_nfbo);
		drawfn (m);
		glBindFramebufferEXT (GL_READ_FRAMEBUFFER_EXT, msaa_nfbo);
		glBindFramebufferEXT (GL_DRAW_FRAMEBUFFER_EXT, nrm_rt[0].fbo);
		glBlitFramebufferEXT (0, 0, W, H, 0, 0, W, H, GL_COLOR_BUFFER_BIT, GL_NEAREST);
		pass (&nrm_rt[1 + m], pr_nsoften); tex ("src", nrm_rt[0].tex); draw ();
	}
}

/* a full-target shader pass */
static GLuint cur_prog;
static int cur_unit;
static void pass (rt_t *dst, GLuint prog)
{
	glBindFramebufferEXT (GL_FRAMEBUFFER_EXT, dst ? dst->fbo : 0);
	glViewport (0, 0, dst ? dst->w : W, dst ? dst->h : H);
	glDisable (GL_DEPTH_TEST);
	glDisable (GL_BLEND);
	glUseProgram (prog);
	cur_prog = prog; cur_unit = 0;
	glMatrixMode (GL_PROJECTION); glLoadIdentity (); gluOrtho2D (0, 1, 0, 1);
	glMatrixMode (GL_MODELVIEW); glLoadIdentity ();
	glUniform2f (glGetUniformLocation (prog, "sz"), (float) (dst ? dst->w : W), (float) (dst ? dst->h : H));
}
static void tex (const char *name, GLuint t)
{
	glActiveTexture (GL_TEXTURE0 + cur_unit);
	glBindTexture (GL_TEXTURE_2D, t);
	glUniform1i (glGetUniformLocation (cur_prog, name), cur_unit);
	cur_unit++;
}
static void uni (const char *name, float v) { glUniform1f (glGetUniformLocation (cur_prog, name), v); }
static void uni2 (const char *name, float a, float b) { glUniform2f (glGetUniformLocation (cur_prog, name), a, b); }
static void draw (void)
{
	glBegin (GL_QUADS); glVertex2f (0, 0); glVertex2f (1, 0); glVertex2f (1, 1); glVertex2f (0, 1); glEnd ();
	while (cur_unit > 0) { glActiveTexture (GL_TEXTURE0 + --cur_unit); glBindTexture (GL_TEXTURE_2D, 0); }
	glActiveTexture (GL_TEXTURE0);
}

static void blur_pass (rt_t *dst, rt_t *src, float wc)
{
	pass (dst, pr_blur); tex ("src", src->tex); uni ("iw", (float) (int) (wc * 256)); draw ();
}
static void matte_pass (rt_t *dst, rt_t *i, rt_t *j, rt_t *k)
{
	pass (dst, pr_matte); tex ("ti", i->tex); tex ("tj", j->tex); tex ("tk", k->tex); uni ("cs", (float) cov_span); draw ();
}
static void copy_pass (rt_t *dst, GLuint src)
{
	pass (dst, pr_copy); tex ("src", src); draw ();
}

static void echo_reset (GLuint img)
{
	int k;
	for (k = 0; k < ECHO_FRAMES; k++) copy_pass (&echo_rt[k], img);
	echo_fill = ECHO_FRAMES;
}

/* harm's frame echoes: the taps of past's two audio networks as frame delays */
static void echo_pass (rt_t *dst, rt_t *img, float frame_rate)
{
	float	w[2][HARM_TAPS], wsum[2] = { 0, 0 }, wl[12];
	int		back[2][HARM_TAPS], s, n;
	char	name[8];
	if (echo_fill == 0) echo_reset (img->tex);
	for (s = 0; s < 2; s++)
	{
		if (P.clears[s] != V.clears[s]) { V.clears[s] = P.clears[s]; echo_reset (img->tex); }
		for (n = 0; n < HARM_TAPS; n++)
		{
			int fb = (int) lroundf (P.tapnum[s][n] / (float) PAST_RATE * frame_rate);
			float g = n ? fabsf (P.gback[s][n]) : 0;
			if (fb < 1 || fb >= echo_fill) g = 0;		/* sub-frame taps: the current image */
			back[s][n] = fb;
			w[s][n] = g * 0.30f;
			wsum[s] += w[s][n];
		}
		if (P.nofxfeed[s] == 1) { for (n = 0; n < HARM_TAPS; n++) w[s][n] = 0; wsum[s] = 0; }
		else if (P.nofx[s] == 1) { for (n = 0; n < HARM_TAPS; n++) w[s][n] *= 1.6f; wsum[s] *= 1.6f; }
		if (wsum[s] > 0.65f) { for (n = 0; n < HARM_TAPS; n++) w[s][n] *= 0.65f / wsum[s]; wsum[s] = 0.65f; }
	}
	pass (dst, pr_echo);
	tex ("ti", img->tex);
	for (s = 0; s < 2; s++)
		for (n = 1; n < HARM_TAPS; n++)
		{
			int slot = s * 6 + n - 1;
			snprintf (name, sizeof name, "e%d", slot);
			tex (name, echo_rt[(echo_pos - back[s][n] + 2 * ECHO_FRAMES) % ECHO_FRAMES].tex);
			wl[slot] = w[s][n];
		}
	glUniform1fv (glGetUniformLocation (cur_prog, "w"), 12, wl);
	uni ("ws0", wsum[0]); uni ("ws1", wsum[1]);
	draw ();
	/* store (after the echoes: recursive, like the audio network) */
	echo_pos = (echo_pos + 1) % ECHO_FRAMES;
	copy_pass (&echo_rt[echo_pos], dst->tex);
	if (echo_fill < ECHO_FRAMES) echo_fill++;
}

static void write_ppm (const char *path)
{
	FILE *f = fopen (path, "wb");
	unsigned char *buf;
	int y;
	if (!f) { perror (path); return; }
	buf = (unsigned char *) malloc ((size_t) W * H * 3);
	glBindFramebufferEXT (GL_FRAMEBUFFER_EXT, 0);
	glPixelStorei (GL_PACK_ALIGNMENT, 1);
	glReadPixels (0, 0, W, H, GL_RGB, GL_UNSIGNED_BYTE, buf);
	fprintf (f, "P6\n%d %d 255\n", W, H);
	for (y = H - 1; y >= 0; y--) fwrite (buf + (size_t) y * W * 3, 1, (size_t) W * 3, f);
	fclose (f);
	free (buf);
}

static void scene (void)
{
	double wall = clock_gettime_nsec_np (CLOCK_UPTIME_RAW) / 1e9;
	float dt = last_wall > 0 ? (float) (wall - last_wall) : 1.0f / 60;
	rt_t *bg = &pool[0], *lines = &pool[1], *c0 = &pool[2], *A = &pool[3], *B = &pool[4], *C = &pool[5], *D = &pool[6];
	rt_t *prv = &prev_rt[prev_cur], *nxt = &prev_rt[prev_cur ^ 1];
	if (dt > 0.1f) dt = 0.1f;
	last_wall = wall;
	fps = fps * 0.95f + 0.05f / (dt > 1e-4f ? dt : 1e-4f);

	if (video_path || offline_frames)
	{
		/* offline video: the music run to this frame's moment */
		dt = 1.0f / vfps;
		audio_seek_offline (frames / (double) vfps + SNAP_FRAMES / (double) PAST_RATE);	/* one snapshot block on, so there is one */
	}
	else if (shot_path)
	{
		/* deterministic still: run the music offline up to the moment */
		double t = shot_at - 3.0 + frames / 30.0;
		dt = 1.0f / 30;
		audio_seek_offline (t > 0 ? t : 0);
	}
	frame_dt = dt;
	if (!audio_now (&P, &now_s)) { glBindFramebufferEXT (GL_FRAMEBUFFER_EXT, 0); glClearColor (0, 0, 0, 1); glClear (GL_COLOR_BUFFER_BIT); glutSwapBuffers (); return; }
	if (P.done && !loop_play)
	{
		static double end_t;
		if (!end_t) end_t = wall;
		if (wall - end_t > 4) exit (0);
	}
	gpu_alloc ();
	update_visuals (dt);
	HUE = V.hue;
	set_coverage (HUE);

	/* background: p10m1's four grids, the fine one flashing on the tick */
	geom_begin ();
	glClearColor (0, 0, 0, 1);
	glClear (GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT);
	glMatrixMode (GL_PROJECTION); glLoadIdentity (); gluOrtho2D (0, 1, 0, 1);
	glMatrixMode (GL_MODELVIEW); glLoadIdentity ();
	ALPHA = 1; SATURATION = 1.1f; VALUE_BOOST = V.val * 0.3f;                          draw_grid (5, GL_LINE_LOOP);
	ALPHA = 1; SATURATION = 0.99f; VALUE_BOOST = 0.005f + 0.02f * V.tick_pulse + V.val * 0.3f; draw_grid (20, GL_LINE_LOOP);
	ALPHA = 1; SATURATION = 1.02f; VALUE_BOOST = V.val * 0.3f;                         draw_grid (31, GL_LINE_LOOP);
	ALPHA = 1; SATURATION = 1.0f; VALUE_BOOST = V.val * 0.25f + 0.012f * V.breath; draw_grid (5, GL_TRIANGLES);
	geom_resolve (bg);
	if (frames == 0) copy_pass (prv, bg->tex);

	/* the lattice, from the score, on black -- to show through where the
	   picture goes dark */
	geom_begin ();
	glClearColor (0, 0, 0, 1);
	glClear (GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT);
	glMatrixMode (GL_MODELVIEW); glLoadIdentity ();
	draw_lattice ();
	geom_resolve (lines);

	/* three renders of the cubes at slightly different fields of view */
	geom_begin (); scene_3d (45.0f);                   geom_resolve (c0);
	geom_begin (); scene_3d (45.0f + V.spread);        geom_resolve (A);
	geom_begin (); scene_3d (45.0f + 1.5f * V.spread); geom_resolve (B);

	/* p10m1's composite: cubes show the (blurred) previous frame, with glow */
	blur_pass (C, A, V.blur_c);					/* C = c5, blurred (colour and tags) */
	blur_pass (D, B, V.blur_c);					/* D = c6, blurred */
	/* p10m1 blurred its feedback memory in place: the memory itself is
	   re-blurred (and truncated) every frame, which is what sinks the cube
	   interiors towards black */
	blur_pass (nxt, prv, V.blur_c);				/* nxt = the memory, blurred */
	matte_pass (B, bg, c0, nxt);				/* B = bg, with the cubes showing the past */
	matte_pass (c0, C, D, B);					/* c0 = the c5 glow, with c6 showing B */
	matte_pass (A, B, c0, c0);					/* A = B, with the glow over it (mask) */

	/* the glass cubes and the mirrors: their normal passes, softened */
	glass_normals (scene_glass);
	pass (B, pr_glass);
	tex ("src", A->tex); tex ("tg", nrm_rt[1].tex); tex ("tm", nrm_rt[2].tex);
	uni ("k", V.glass_k * H / 720.0f); uni ("disp", V.glass_disp); uni ("ph", (float) fmod (now_s * 0.15, 1.0));
	uni ("fpx", (H * 0.5f) * V.zoom / tanf (22.5f * (float) M_PI / 180.0f));
	draw ();														/* B = with glass and mirrors */

	/* the lattice through the black; how much of it shows (for its variants) */
	pass (C, pr_reveal); tex ("ti", B->tex); tex ("tl", lines->tex); draw ();	/* C */
	pass (&count_rt, pr_count); tex ("ti", B->tex); tex ("tl", lines->tex);
	uni2 ("sz", (float) W, (float) H); uni2 ("cell", W / 128.0f, H / 72.0f); draw ();
	{
		static unsigned char cnt[128 * 72 * 4];
		double sum = 0;
		int k;
		glBindFramebufferEXT (GL_FRAMEBUFFER_EXT, count_rt.fbo);
		glReadPixels (0, 0, 128, 72, GL_RGBA, GL_UNSIGNED_BYTE, cnt);
		for (k = 0; k < 128 * 72; k++) sum += cnt[k * 4];
		V.reveal_frac = (float) (sum / 255.0 / (128 * 72));
	}

	/* the title, eaten where the picture has moved over it, before the feedback */
	if (cov_tex)
	{
		rt_t *ip = &integ_rt[integ_cur], *in = &integ_rt[integ_cur ^ 1];
		pass (in, pr_integ);
		tex ("tprev", ip->tex); tex ("tf", C->tex); tex ("tb", bg->tex);
		uni2 ("box", (float) tbx, (float) tby); uni2 ("fsz", (float) W, (float) H); uni2 ("isz", (float) tbw, (float) tbh);
		uni ("dt", frame_dt); uni ("rate", (float) (now_s < 10.0 ? now_s / 10.0 : 1.0) * 0.065f); uni ("seed", (float) (frames % 9973));
		draw ();
		integ_cur ^= 1;
		pass (D, pr_stamp);
		tex ("ti", C->tex); tex ("tc", cov_tex); tex ("tint", in->tex);
		uni2 ("box", (float) tbx, (float) tby); uni2 ("isz", (float) tbw, (float) tbh);
		draw ();
	}
	else copy_pass (D, C->tex);										/* D = the composed frame */

	/* p10m1's feedback: the blurred memory moved towards this frame */
	if (frames == 0) copy_pass (prv, D->tex);
	else { pass (prv, pr_feedback); tex ("tp", nxt->tex); tex ("tc", D->tex); uni ("im", (float) (int) (V.decay * 256)); draw (); }

	/* harm: convolution of frames (on the shown image) */
	echo_pass (A, D, video_path || offline_frames ? vfps : shot_path ? 30.0f : (fps > 5 ? fps : 30.0f));

	if (video_path)
	{
		/* the finished frame to the encoder */
		glBindFramebufferEXT (GL_FRAMEBUFFER_EXT, A->fbo);
		glPixelStorei (GL_PACK_ALIGNMENT, 1);
		glReadPixels (0, 0, W, H, GL_RGB, GL_UNSIGNED_BYTE, vbuf);
		fwrite (vbuf, 1, (size_t) W * H * 3, vpipe);
		glUseProgram (0);
		frames++;
		return;
	}

	/* to the screen */
	copy_pass (NULL, A->tex);
	glUseProgram (0);
	glEnable (GL_DEPTH_TEST);
	glEnable (GL_BLEND);

	frames++;
	if (shot_path && frames >= 90) { write_ppm (shot_path); exit (0); }
	glutSwapBuffers ();
}

/* ------------------------------------------------------------------ */

/* Offline video: the same pipeline in an offscreen GL context (no window),
   a fixed 1/fps step with the music run offline to each frame's moment, the
   frames piped to ffmpeg, and the soundtrack captured from that same run of
   the music (so picture and sound are one computation), muxed at the end. */
static void put32 (unsigned char *p, unsigned int v) { p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24; }

#ifndef __EMSCRIPTEN__
static int video_render (void)
{
	CGLPixelFormatAttribute at[] = { kCGLPFAAccelerated, kCGLPFAColorSize, 24, kCGLPFAAlphaSize, 8, 0 };
	CGLPixelFormatObj pf; GLint npf; CGLContextObj ctx;
	unsigned char hdr[44] = { 'R','I','F','F',0,0,0,0,'W','A','V','E','f','m','t',' ',16,0,0,0,1,0,2,0,
		0x44,0xAC,0,0, 0x10,0xB1,2,0, 4,0,16,0, 'd','a','t','a',0,0,0,0 };
	char wav[2048], tmp[2048], cmd[8192];
	FILE *wf;
	long nfr = (long) (vsecs * vfps), f, bytes;
	double t0 = clock_gettime_nsec_np (CLOCK_UPTIME_RAW) / 1e9;

	if (CGLChoosePixelFormat (at, &pf, &npf) || !pf) { fprintf (stderr, "no GL pixel format\n"); return 1; }
	CGLCreateContext (pf, NULL, &ctx);
	CGLSetCurrentContext (ctx);
	glClearDepth (1.0); glDepthFunc (GL_LESS); glShadeModel (GL_SMOOTH);
	glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); glHint (GL_LINE_SMOOTH_HINT, GL_NICEST);
	vbuf = (unsigned char *) malloc ((size_t) W * H * 3);

	snprintf (wav, sizeof wav, "%s.wav", video_path);
	snprintf (tmp, sizeof tmp, "%s.video.mp4", video_path);
	wf = fopen (wav, "wb");
	if (!wf) { perror (wav); return 1; }
	fwrite (hdr, 1, 44, wf);
	audio_set_swing (swing);
	audio_capture (wf);

	snprintf (cmd, sizeof cmd, "ffmpeg -loglevel error -y -f rawvideo -pix_fmt rgb24 -s %dx%d -r %g -i - "
		"-vf vflip -c:v h264_videotoolbox -b:v 60M -maxrate 80M -pix_fmt yuv420p -profile:v high -movflags +faststart '%s'",
		W, H, (double) vfps, tmp);
	vpipe = popen (cmd, "w");
	if (!vpipe) { perror ("ffmpeg"); return 1; }
	fprintf (stderr, "rendering %ld frames at %dx%d, %g fps\n", nfr, W, H, (double) vfps);
	for (f = 0; f < nfr; f++)
	{
		scene ();
		if (f % 300 == 299)
		{
			double t = clock_gettime_nsec_np (CLOCK_UPTIME_RAW) / 1e9 - t0;
			fprintf (stderr, "  %5.1f s of %g  (%.0f%%, %.1f min so far, ~%.1f min left)\n",
				(f + 1) / vfps, vsecs, 100.0 * (f + 1) / nfr, t / 60, t / (f + 1) * (nfr - f - 1) / 60);
		}
	}
	if (pclose (vpipe)) { fprintf (stderr, "ffmpeg (video) failed\n"); return 1; }
	/* the soundtrack to exactly the length */
	audio_seek_offline (vsecs);
	audio_capture (NULL);
	bytes = ftell (wf) - 44;
	put32 (hdr + 4, (unsigned int) (36 + bytes)); put32 (hdr + 40, (unsigned int) bytes);
	fseek (wf, 0, SEEK_SET); fwrite (hdr, 1, 44, wf); fclose (wf);
	snprintf (cmd, sizeof cmd, "ffmpeg -loglevel error -y -i '%s' -i '%s' -map 0:v -map 1:a -c:v copy -c:a aac -b:a 320k -shortest -movflags +faststart '%s'",
		tmp, wav, video_path);
	if (system (cmd)) { fprintf (stderr, "ffmpeg (mux) failed; video in %s, audio in %s\n", tmp, wav); return 1; }
	remove (tmp);
	fprintf (stderr, "done: %s (%.1f min); soundtrack also in %s\n", video_path,
		(clock_gettime_nsec_np (CLOCK_UPTIME_RAW) / 1e9 - t0) / 60, wav);
	return 0;
}

/* Retina: ask for a full-resolution drawing surface (so full screen on a
   4096 x 2304 panel renders every pixel); the window's size comes in points */
static id		gl_view, gl_ctx;
static int		retina;					/* -retina: full-resolution surface (off by default) */
static double backing_scale (void)
{
	id win;
	BOOL best;
	if (!gl_view) return 1.0;
	/* only if the surface really is full resolution */
	best = ((BOOL (*) (id, SEL)) objc_msgSend) (gl_view, sel_registerName ("wantsBestResolutionOpenGLSurface"));
	if (!best) return 1.0;
	win = ((id (*) (id, SEL)) objc_msgSend) (gl_view, sel_registerName ("window"));
	return win ? ((double (*) (id, SEL)) objc_msgSend) (win, sel_registerName ("backingScaleFactor")) : 1.0;
}
static void hidpi_init (void)
{
	if (!retina) return;
	gl_ctx = ((id (*) (id, SEL)) objc_msgSend) ((id) objc_getClass ("NSOpenGLContext"), sel_registerName ("currentContext"));
	if (!gl_ctx) return;
	gl_view = ((id (*) (id, SEL)) objc_msgSend) (gl_ctx, sel_registerName ("view"));
	if (gl_view)
	{
		((void (*) (id, SEL, BOOL)) objc_msgSend) (gl_view, sel_registerName ("setWantsBestResolutionOpenGLSurface:"), YES);
		((void (*) (id, SEL)) objc_msgSend) (gl_ctx, sel_registerName ("update"));
	}
}

static void reshape (int w, int h)
{
	double sc = backing_scale ();
	if (gl_ctx) ((void (*) (id, SEL)) objc_msgSend) (gl_ctx, sel_registerName ("update"));
	W = (int) lround ((w > 16 ? w : 16) * sc);
	H = (int) lround ((h > 16 ? h : 16) * sc);
	glViewport (0, 0, W, H);
}

static void key (unsigned char k, int x, int y)
{
	(void) x; (void) y;
	if (k == 27 || k == 'q') exit (0);
	if (k == 'f')
	{
		fullscreen = !fullscreen;
		if (fullscreen) { glutFullScreen (); glutSetCursor (GLUT_CURSOR_NONE); } else { glutReshapeWindow (1280, 720); glutPositionWindow (60, 60); }
		glutSetCursor (fullscreen ? GLUT_CURSOR_NONE : GLUT_CURSOR_INHERIT);
	}
}

#else
/* the browser: the page sizes the canvas (its CSS size, as the macOS window
   renders in points; capped); the frame follows it. The picture is a
   per-frame process (blur, feedback, echoes), made at 60 fps: on faster
   displays (120 Hz) every other refresh is skipped. */
static void web_frame (void)
{
	static double next, prev, period = 1000.0 / 60.0;
	double now = emscripten_get_now ();
	int w = 0, h = 0;
	if (offline_frames)
	{
		/* ?at=s: step exactly as the macOS video renderer does (1/vfps, the
		   music run offline to each frame), then hold that frame */
		emscripten_get_canvas_element_size ("#canvas", &w, &h);
		W = w > 16 ? w : 16; H = h > 16 ? h : 16;
		if (frames < offline_frames) scene ();
		else { copy_pass (NULL, pool[3].tex); glUseProgram (0); EM_ASM ({ document.title = 'frame ready'; }); }	/* A: the last frame */
		return;
	}
	if (prev > 0 && now - prev < 100.0) period += 0.1 * (now - prev - period);	/* the display's refresh */
	prev = now;
	if (now < next - 0.5 * period) return;				/* due at the nearest refresh */
	next = (next > 0 && now - next < 50.0 ? next : now) + 1000.0 / 60.0;
	{
		static int nf; static double t0;					/* (?fps: log the rate drawn) */
		if (!t0) t0 = now;
		if (++nf % 120 == 0 && EM_ASM_INT ({ return new URLSearchParams (location.search).has ('fps'); }))
			emscripten_log (EM_LOG_CONSOLE, "drawn %.1f fps (refresh %.1f Hz)", nf * 1000.0 / (now - t0), 1000.0 / period);
	}
	emscripten_get_canvas_element_size ("#canvas", &w, &h);
	W = w > 16 ? w : 16; H = h > 16 ? h : 16;
	scene ();
}

static int web_start (void)
{
	EmscriptenWebGLContextAttributes a;
	EMSCRIPTEN_WEBGL_CONTEXT_HANDLE ctx;
	emscripten_webgl_init_context_attributes (&a);
	a.majorVersion = 2; a.minorVersion = 0;
	a.alpha = 0; a.depth = 1; a.antialias = 0; a.premultipliedAlpha = 0;
	a.powerPreference = EM_WEBGL_POWER_PREFERENCE_HIGH_PERFORMANCE;
	ctx = emscripten_webgl_create_context ("#canvas", &a);
	if (ctx <= 0) { fprintf (stderr, "no WebGL2\n"); return 1; }
	emscripten_webgl_make_context_current (ctx);
	/* the macOS app's OpenGL output is not colour matched: its values go
	   to the display as they are (P3 on recent Macs). Tag the canvas the
	   same, where the display is P3 (?srgb for the browser default). */
	EM_ASM ({
		var g = GL.currentContext && GL.currentContext.GLctx;
		if (g && 'drawingBufferColorSpace' in g && matchMedia ('(color-gamut: p3)').matches && !new URLSearchParams (location.search).has ('srgb'))
			g.drawingBufferColorSpace = 'display-p3';
	});
	/* rendering to the float targets (the glass normals, the title) */
	if (!emscripten_webgl_enable_extension (ctx, "EXT_color_buffer_float"))
		fprintf (stderr, "no EXT_color_buffer_float\n");
	emscripten_webgl_enable_extension (ctx, "OES_texture_float_linear");
	gc_init ();
	glClearDepth (1.0);
	glDepthFunc (GL_LESS);
	glEnable (GL_DEPTH_TEST);
	glEnable (GL_BLEND);
	glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	audio_set_swing (swing);
	{
		double at = EM_ASM_DOUBLE ({ return +(new URLSearchParams (location.search).get ('at') || 0); });
		double fr = EM_ASM_DOUBLE ({ return +(new URLSearchParams (location.search).get ('vfps') || 60); });
		if (at > 0) { vfps = (float) fr; offline_frames = (int) lround (at * vfps) + 1; }
	}
	emscripten_set_main_loop (web_frame, 0, 0);
	return 0;
}
#endif

int main (int argc, char **argv)
{
	int i, j;

	{
		/* the program's directory, for fonts/ */
		const char *sl = strrchr (argv[0], '/');
		size_t n;
		if (sl && (size_t) (sl - argv[0]) < sizeof app_dir) { memcpy (app_dir, argv[0], (size_t) (sl - argv[0])); app_dir[sl - argv[0]] = 0; }
		/* inside the .app (opened from the Finder): the fonts are in
		   Contents/Resources, and it opens full screen (Esc quits) */
		n = strlen (app_dir);
		if (n >= 15 && !strcmp (app_dir + n - 15, "/Contents/MacOS") && n + 5 < sizeof app_dir)
		{
			strcpy (app_dir + n - 5, "Resources");
			fullscreen = 1;
		}
	}
	for (i = 1; i < argc; i++)
	{
		if (!strcmp (argv[i], "-f")) fullscreen = 1;
		else if (!strcmp (argv[i], "-mute")) mute = 1;
		else if (!strcmp (argv[i], "-loop")) loop_play = 1;
		else if (!strcmp (argv[i], "-shot") && i + 1 < argc) shot_path = argv[++i];
		else if (!strcmp (argv[i], "-at") && i + 1 < argc) shot_at = atof (argv[++i]);
		else if (!strcmp (argv[i], "-swing") && i + 1 < argc) swing = (float) atof (argv[++i]);
#ifndef __EMSCRIPTEN__
		else if (!strcmp (argv[i], "-retina")) retina = 1;
#endif
		else if (!strcmp (argv[i], "-video") && i + 1 < argc) video_path = argv[++i];
		else if (!strcmp (argv[i], "-vsize") && i + 1 < argc) { const char *x; video_w = atoi (argv[++i]); x = strchr (argv[i], 'x'); if (x) video_h = atoi (x + 1); }
		else if (!strcmp (argv[i], "-vsecs") && i + 1 < argc) vsecs = atof (argv[++i]);
		else if (!strcmp (argv[i], "-vfps") && i + 1 < argc) vfps = (float) atof (argv[++i]);
		else if (!strcmp (argv[i], "-samples") && i + 1 < argc) msaa_samples = atoi (argv[++i]);
		else { fprintf (stderr, "usage: %s [-f] [-mute] [-loop] [-retina] [-samples n] [-video out.mp4 [-vsize 4096x2304] [-vsecs 480] [-vfps 30]] [-swing 0..0.33] [-shot out.ppm -at seconds]\n", argv[0]); return 1; }
	}

	/* p10m1's cube layout: srandom(0) and the same draws, in the same order */
	srandom (0);
	for (i = 0; i < NC; i++)
	{
		float x = -0.5f + random () / (float) RAND_MAX;
		float y = -0.5f + random () / (float) RAND_MAX;
		float z = -0.5f + random () / (float) RAND_MAX;
		float d = random () / (float) RAND_MAX;
		d *= (2 - fabsf (x) - fabsf (y)) * 0.25f;
		d *= d;
		cube_base[i][0] = x; cube_base[i][1] = y; cube_base[i][2] = z; cube_base[i][3] = d;
		V.cube_r[i] = 1; V.cube_s[i] = 0.55f;
	}
	/* about one cube in five is glass (a fixed scatter through the cloud) */
	for (i = 0; i < NC; i++) cube_glass[i] = ((i * 37 + 11) % 9) == 0;	/* about one in nine */
	/* not all glass is perfect: a range, evenly spread through the glass
	   cubes, from perfect to bumpy to fractured, each with its own texture
	   scale and offset */
	for (i = 0, j = 0; i < NC; i++)
		if (cube_glass[i])
		{
			unsigned int hh = (unsigned int) (i * 2654435761u);
			glass_q[i] = (unsigned char) (j % 4);
			/* (the mirror cubes, a third of these, are gone: they are plain
			   cubes again. The mirror code is kept, switched off by MIRRORS.) */
			cube_mirror[i] = MIRRORS && (j % 3) == 2;
			if (!MIRRORS && (j % 3) == 2) cube_glass[i] = 0;
			glass_crack[i] = 60.0f + 20.0f * (j / 4) / (NC / 9 / 4 + 1);	/* one after another, 1:00-1:20 */
			j++;
			glass_tc[i][0] = 0.5f + 0.6f * ((hh >> 8) & 0xff) / 255.0f;
			glass_tc[i][1] = ((hh >> 16) & 0xff) / 255.0f;
			glass_tc[i][2] = ((hh >> 24) & 0xff) / 255.0f;
		}
	for (i = 0; i < NC; i++) cube_rank[i] = i;
	for (i = NC - 1; i > 0; i--) { int j = (int) (random () % (i + 1)), t = cube_rank[i]; cube_rank[i] = cube_rank[j]; cube_rank[j] = t; }
	V.hue = 215; V.sat = 0.7f; V.r1 = 0.4f; V.spread = 1; V.blur_c = 1; V.decay = 1; V.cam_z = -1.9f; V.zoom = 5; V.zoom_v = 0;
	V.lv_sx = V.lv_sy = 1; V.lv_den = 1; V.lv_hue = 28; V.lv_sat = 0.38f;	/* starts as desaturated orange */
	V.last_tick = -1;
	for (i = 0; i < PAST_OSCS; i++) V.vq[i][0] = 1;

#ifdef __EMSCRIPTEN__
	return web_start ();
#else
	if (video_path) { W = video_w; H = video_h; return video_render (); }

	glutInit (&argc, argv);
	/* the window just shows the finished frame; the multisampling is offscreen */
	glutInitDisplayString ("rgba alpha double depth");
	glutInitWindowSize (W, H);
	glutInitWindowPosition (60, 60);
	glutCreateWindow ("p10m1 / past");
	hidpi_init ();
	glutDisplayFunc (scene);
	glutIdleFunc (scene);
	glutReshapeFunc (reshape);
	glutKeyboardFunc (key);
	if (fullscreen) glutFullScreen ();

	glClearDepth (1.0);
	glDepthFunc (GL_LESS);
	glEnable (GL_DEPTH_TEST);
	glShadeModel (GL_SMOOTH);
	glEnable (GL_BLEND);
	glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glHint (GL_LINE_SMOOTH_HINT, GL_NICEST);
	glEnable (GL_MULTISAMPLE);
	audio_set_swing (swing);
	if (!shot_path) audio_start (mute, loop_play);
	glutMainLoop ();
	return 0;
#endif
}
