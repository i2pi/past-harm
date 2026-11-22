/* web: desktop OpenGL 2.1 immediate mode and fixed function on WebGL2 --
 * see gl_compat.h */
#define GC_IMPL
#include "gl_compat.h"
#include <emscripten/html5.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* ---- vertices: clip position, colour, eye normal, tc0, eye tangents ---- */
enum { A_POS, A_COL, A_NRM, A_TC0, A_TC1, A_TC2 };
#define VF		20						/* floats per vertex: 4 4 3 3 3 3 */
typedef struct { float p[4], c[4], n[3], t0[3], t1[3], t2[3]; } vtx_t;

static vtx_t	*buf;					/* the pending batch */
static int		nbuf, cap;
static int		batch_kind;				/* 0 none, GL_TRIANGLES or GL_LINES */
static vtx_t	*prim;					/* the primitive being built (glBegin .. glEnd) */
static int		nprim, primcap;
static GLenum	prim_mode;
static float	cur_c[4] = { 1, 1, 1, 1 }, cur_n[3] = { 0, 0, 1 }, cur_t0[3], cur_t1[3], cur_t2[3];

static GLuint	vbo, vao, ff_prog, user_prog;

/* ---- matrices ---- */
#define DEPTH	32
static float	mstack[2][DEPTH][16];
static int		msp[2];
static int		mmode;					/* 0 projection, 1 modelview */
static float	mvp[16], nm[9];
static int		mdirty = 1;

static void mident (float *m) { memset (m, 0, 64); m[0] = m[5] = m[10] = m[15] = 1; }
static void mmul (const float *a, const float *b, float *o)		/* o = a * b, column major */
{
	float r[16];
	int i, j, k;
	for (j = 0; j < 4; j++) for (i = 0; i < 4; i++)
	{
		float s = 0;
		for (k = 0; k < 4; k++) s += a[k * 4 + i] * b[j * 4 + k];
		r[j * 4 + i] = s;
	}
	memcpy (o, r, 64);
}
static float *top (void) { return mstack[mmode][msp[mmode]]; }
static void mpost (const float *m) { mmul (top (), m, top ()); mdirty = 1; }

static void update_matrices (void)
{
	const float *P = mstack[0][msp[0]], *M = mstack[1][msp[1]];
	float a, b, c, d, e, f, g, h, i, det;
	if (!mdirty) return;
	mmul (P, M, mvp);
	/* normal matrix: inverse transpose of the modelview's upper 3x3 */
	a = M[0]; b = M[4]; c = M[8]; d = M[1]; e = M[5]; f = M[9]; g = M[2]; h = M[6]; i = M[10];
	det = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
	if (fabsf (det) < 1e-20f) det = 1e-20f;
	/* (inverse)^T, stored column major: nm[col*3+row] */
	nm[0] = (e * i - f * h) / det; nm[3] = -(d * i - f * g) / det; nm[6] = (d * h - e * g) / det;
	nm[1] = -(b * i - c * h) / det; nm[4] = (a * i - c * g) / det; nm[7] = -(a * h - b * g) / det;
	nm[2] = (b * f - c * e) / det; nm[5] = -(a * f - c * d) / det; nm[8] = (a * e - b * d) / det;
	mdirty = 0;
}

static void xform3 (const float *v, float *o)
{
	float x = v[0], y = v[1], z = v[2];
	o[0] = nm[0] * x + nm[3] * y + nm[6] * z;
	o[1] = nm[1] * x + nm[4] * y + nm[7] * z;
	o[2] = nm[2] * x + nm[5] * y + nm[8] * z;
}

/* ---- shaders ---- */
static const char *ff_vs =
	"#version 300 es\n"
	"in vec4 a_pos; in vec4 a_col; out vec4 v_col;\n"
	"void main () { gl_Position = a_pos; v_col = a_col; }\n";
static const char *ff_fs =
	"#version 300 es\nprecision highp float;\n"
	"in vec4 v_col; out vec4 o;\n"
	"void main () { o = v_col; }\n";

static GLuint compile (GLenum type, const char *src)
{
	GLuint s = glCreateShader (type);
	GLint ok = 0;
	glShaderSource (s, 1, &src, NULL);
	glCompileShader (s);
	glGetShaderiv (s, GL_COMPILE_STATUS, &ok);
	if (!ok) { char log[2048]; glGetShaderInfoLog (s, sizeof log, NULL, log); fprintf (stderr, "gl_compat shader: %s\n", log); }
	return s;
}

static void bind_attribs (GLuint p)
{
	glBindAttribLocation (p, A_POS, "a_pos"); glBindAttribLocation (p, A_COL, "a_col");
	glBindAttribLocation (p, A_NRM, "a_nrm"); glBindAttribLocation (p, A_TC0, "a_tc0");
	glBindAttribLocation (p, A_TC1, "a_tc1"); glBindAttribLocation (p, A_TC2, "a_tc2");
}

void gc_init (void)
{
	int k;
	for (k = 0; k < 2; k++) { msp[k] = 0; mident (mstack[k][0]); }
	glGenVertexArrays (1, &vao);
	glBindVertexArray (vao);
	glGenBuffers (1, &vbo);
	glBindBuffer (GL_ARRAY_BUFFER, vbo);
	glEnableVertexAttribArray (A_POS); glVertexAttribPointer (A_POS, 4, GL_FLOAT, GL_FALSE, VF * 4, (void *) 0);
	glEnableVertexAttribArray (A_COL); glVertexAttribPointer (A_COL, 4, GL_FLOAT, GL_FALSE, VF * 4, (void *) 16);
	glEnableVertexAttribArray (A_NRM); glVertexAttribPointer (A_NRM, 3, GL_FLOAT, GL_FALSE, VF * 4, (void *) 32);
	glEnableVertexAttribArray (A_TC0); glVertexAttribPointer (A_TC0, 3, GL_FLOAT, GL_FALSE, VF * 4, (void *) 44);
	glEnableVertexAttribArray (A_TC1); glVertexAttribPointer (A_TC1, 3, GL_FLOAT, GL_FALSE, VF * 4, (void *) 56);
	glEnableVertexAttribArray (A_TC2); glVertexAttribPointer (A_TC2, 3, GL_FLOAT, GL_FALSE, VF * 4, (void *) 68);
	ff_prog = glCreateProgram ();
	glAttachShader (ff_prog, compile (GL_VERTEX_SHADER, ff_vs));
	glAttachShader (ff_prog, compile (GL_FRAGMENT_SHADER, ff_fs));
	bind_attribs (ff_prog);
	glLinkProgram (ff_prog);
	glUseProgram (ff_prog);
}

/* ---- batches ---- */
void gc_flush (void)
{
	if (!nbuf) return;
	glBindBuffer (GL_ARRAY_BUFFER, vbo);
	glBufferData (GL_ARRAY_BUFFER, (GLsizeiptr) nbuf * sizeof (vtx_t), buf, GL_STREAM_DRAW);
	glDrawArrays (batch_kind, 0, nbuf);
	nbuf = 0; batch_kind = 0;
}

static void push (const vtx_t *v)
{
	if (nbuf == cap) { cap = cap ? cap * 2 : 4096; buf = (vtx_t *) realloc (buf, (size_t) cap * sizeof *buf); }
	buf[nbuf++] = *v;
}

void glBegin (GLenum mode) { prim_mode = mode; nprim = 0; }

void glEnd (void)
{
	int k, kind = prim_mode == GL_LINE_LOOP || prim_mode == GL_LINES || prim_mode == GL_LINE_STRIP ? GL_LINES : GL_TRIANGLES;
	if (batch_kind && batch_kind != kind) gc_flush ();
	batch_kind = kind;
	switch (prim_mode)
	{
	case GL_QUADS:
		for (k = 0; k + 3 < nprim; k += 4)
		{
			push (&prim[k]); push (&prim[k + 1]); push (&prim[k + 2]);
			push (&prim[k]); push (&prim[k + 2]); push (&prim[k + 3]);
		}
		break;
	case GL_TRIANGLES:
		for (k = 0; k + 2 < nprim; k += 3) { push (&prim[k]); push (&prim[k + 1]); push (&prim[k + 2]); }
		break;
	case GL_TRIANGLE_STRIP:
		for (k = 0; k + 2 < nprim; k++) { push (&prim[k]); push (&prim[k + 1 + (k & 1)]); push (&prim[k + 2 - (k & 1)]); }
		break;
	case GL_LINE_LOOP:
		for (k = 0; k < nprim && nprim > 1; k++) { push (&prim[k]); push (&prim[(k + 1) % nprim]); }
		break;
	case GL_LINE_STRIP:
		for (k = 0; k + 1 < nprim; k++) { push (&prim[k]); push (&prim[k + 1]); }
		break;
	case GL_LINES:
		for (k = 0; k + 1 < nprim; k += 2) { push (&prim[k]); push (&prim[k + 1]); }
		break;
	}
	nprim = 0;
}

void glVertex3f (float x, float y, float z)
{
	vtx_t *v;
	if (nprim == primcap) { primcap = primcap ? primcap * 2 : 256; prim = (vtx_t *) realloc (prim, (size_t) primcap * sizeof *prim); }
	v = &prim[nprim++];
	update_matrices ();
	v->p[0] = mvp[0] * x + mvp[4] * y + mvp[8] * z + mvp[12];
	v->p[1] = mvp[1] * x + mvp[5] * y + mvp[9] * z + mvp[13];
	v->p[2] = mvp[2] * x + mvp[6] * y + mvp[10] * z + mvp[14];
	v->p[3] = mvp[3] * x + mvp[7] * y + mvp[11] * z + mvp[15];
	memcpy (v->c, cur_c, sizeof cur_c);
	xform3 (cur_n, v->n);
	memcpy (v->t0, cur_t0, sizeof cur_t0);
	xform3 (cur_t1, v->t1);
	xform3 (cur_t2, v->t2);
}
void glVertex2f (float x, float y) { glVertex3f (x, y, 0); }
void glVertex2d (double x, double y) { glVertex3f ((float) x, (float) y, 0); }
void glVertex3fv (const float *v) { glVertex3f (v[0], v[1], v[2]); }
void glColor4f (float r, float g, float b, float a) { cur_c[0] = r; cur_c[1] = g; cur_c[2] = b; cur_c[3] = a; }
void glNormal3fv (const float *n) { memcpy (cur_n, n, sizeof cur_n); }
void glTexCoord2f (float s, float t) { cur_t0[0] = s; cur_t0[1] = t; cur_t0[2] = 0; }
void glMultiTexCoord3f (GLenum unit, float s, float t, float r)
{
	float *d = unit == GL_TEXTURE1 ? cur_t1 : unit == GL_TEXTURE2 ? cur_t2 : cur_t0;
	d[0] = s; d[1] = t; d[2] = r;
}

/* ---- matrices ---- */
void glMatrixMode (GLenum m) { mmode = m == GL_PROJECTION ? 0 : 1; }
void glLoadIdentity (void) { mident (top ()); mdirty = 1; }
void glPushMatrix (void) { if (msp[mmode] + 1 < DEPTH) { memcpy (mstack[mmode][msp[mmode] + 1], top (), 64); msp[mmode]++; } }
void glPopMatrix (void) { if (msp[mmode] > 0) msp[mmode]--; mdirty = 1; }
void glMultMatrixf (const float *m) { mpost (m); }
void glTranslatef (float x, float y, float z)
{
	float m[16]; mident (m); m[12] = x; m[13] = y; m[14] = z; mpost (m);
}
void glRotatef (float a, float x, float y, float z)
{
	float m[16], l = sqrtf (x * x + y * y + z * z), c, s, t;
	if (l < 1e-12f) return;
	x /= l; y /= l; z /= l;
	a *= (float) M_PI / 180.0f; c = cosf (a); s = sinf (a); t = 1 - c;
	mident (m);
	m[0] = x * x * t + c;     m[4] = x * y * t - z * s; m[8]  = x * z * t + y * s;
	m[1] = y * x * t + z * s; m[5] = y * y * t + c;     m[9]  = y * z * t - x * s;
	m[2] = x * z * t - y * s; m[6] = y * z * t + x * s; m[10] = z * z * t + c;
	mpost (m);
}
void gluOrtho2D (double l, double r, double b, double t)
{
	float m[16]; mident (m);
	m[0] = (float) (2 / (r - l)); m[5] = (float) (2 / (t - b)); m[10] = -1;
	m[12] = (float) (-(r + l) / (r - l)); m[13] = (float) (-(t + b) / (t - b));
	mpost (m);
}
void gluPerspective (double fovy, double aspect, double zn, double zf)
{
	float m[16], f = (float) (1.0 / tan (fovy * M_PI / 360.0));
	memset (m, 0, sizeof m);
	m[0] = f / (float) aspect; m[5] = f;
	m[10] = (float) ((zf + zn) / (zn - zf)); m[11] = -1;
	m[14] = (float) (2 * zf * zn / (zn - zf));
	mpost (m);
}

/* ---- programs: GLSL 1.20 -> GLSL ES 3.00 ---- */
static char *replace_all (char *s, const char *a, const char *b)
{
	size_t la = strlen (a), lb = strlen (b), n = 0;
	char *p, *o, *q;
	for (p = strstr (s, a); p; p = strstr (p + la, a)) n++;
	if (!n) return s;
	o = (char *) malloc (strlen (s) + n * (lb > la ? lb - la : 0) + 1);
	for (p = s, q = o; ; )
	{
		char *m = strstr (p, a);
		if (!m) { strcpy (q, p); break; }
		memcpy (q, p, (size_t) (m - p)); q += m - p;
		memcpy (q, b, lb); q += lb;
		p = m + la;
	}
	free (s);
	return o;
}

void gc_ShaderSource (GLuint sh, GLsizei n, const GLchar *const *src, const GLint *len)
{
	GLint type = 0;
	size_t total = 1;
	int k, tc;
	char *s, *out;
	const char *hdr;
	gc_flush ();
	for (k = 0; k < n; k++) total += len && len[k] >= 0 ? (size_t) len[k] : strlen (src[k]);
	s = (char *) malloc (total); s[0] = 0;
	for (k = 0; k < n; k++) strncat (s, src[k], len && len[k] >= 0 ? (size_t) len[k] : strlen (src[k]));
	glGetShaderiv (sh, GL_SHADER_TYPE, &type);
	s = replace_all (s, "#version 120\n", "");
	tc = strstr (s, "gl_TexCoord[0]") != NULL;
	s = replace_all (s, "gl_TexCoord[0]", "v_tc0");
	s = replace_all (s, "texture2D", "texture");
	if (type == GL_VERTEX_SHADER)
	{
		s = replace_all (s, "ftransform ()", "a_pos");
		s = replace_all (s, "gl_NormalMatrix * ", "");
		s = replace_all (s, "gl_Normal", "a_nrm");
		s = replace_all (s, "gl_MultiTexCoord0", "vec4 (a_tc0, 1.0)");
		s = replace_all (s, "gl_MultiTexCoord1.xyz", "a_tc1");
		s = replace_all (s, "gl_MultiTexCoord2.xyz", "a_tc2");
		s = replace_all (s, "varying", "out");
		hdr = tc ? "#version 300 es\nprecision highp float;\nin vec4 a_pos; in vec3 a_nrm, a_tc0, a_tc1, a_tc2; out vec4 v_tc0;\n"
		         : "#version 300 es\nprecision highp float;\nin vec4 a_pos; in vec3 a_nrm, a_tc0, a_tc1, a_tc2;\n";
	}
	else
	{
		s = replace_all (s, "gl_FragColor", "o_col");
		s = replace_all (s, "varying", "in");
		hdr = tc ? "#version 300 es\nprecision highp float;\nprecision highp sampler2D;\nout vec4 o_col; in vec4 v_tc0;\n"
		         : "#version 300 es\nprecision highp float;\nprecision highp sampler2D;\nout vec4 o_col;\n";
	}
	out = (char *) malloc (strlen (hdr) + strlen (s) + 1);
	strcpy (out, hdr); strcat (out, s);
	{
		const GLchar *p = out;
		glShaderSource (sh, 1, &p, NULL);
	}
	free (out); free (s);
}

void gc_LinkProgram (GLuint p) { gc_flush (); bind_attribs (p); glLinkProgram (p); }

void gc_UseProgram (GLuint p)
{
	gc_flush ();
	user_prog = p;
	glUseProgram (p ? p : ff_prog);
}

/* ---- state ---- */
void gc_Enable (GLenum c)
{
	if (c == GL_MULTISAMPLE) return;
	gc_flush (); glEnable (c);
}
void gc_Disable (GLenum c)
{
	if (c == GL_MULTISAMPLE) return;
	gc_flush (); glDisable (c);
}
void gc_Hint (GLenum t, GLenum m) { (void) t; (void) m; }

static int float_linear = -1;
void gc_TexImage2D (GLenum t, GLint lvl, GLint ifmt, GLsizei w, GLsizei h, GLint b, GLenum fmt, GLenum type, const void *d)
{
	gc_flush ();
	if (ifmt == GL_LUMINANCE8) { ifmt = GL_R8; fmt = GL_RED; }
	glTexImage2D (t, lvl, ifmt, w, h, b, fmt, type, d);
	if (ifmt == GL_RGBA32F)
	{
		/* 32-bit float textures can only be filtered with an extension
		   (missing on phones): sample them nearest, they are read at texel
		   centres anyway */
		if (float_linear < 0) float_linear = emscripten_webgl_enable_extension (emscripten_webgl_get_current_context (), "OES_texture_float_linear");
		if (!float_linear)
		{
			glTexParameteri (t, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
			glTexParameteri (t, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		}
	}
}

void gc_RenderbufferStorageMultisample (GLenum t, GLsizei s, GLenum f, GLsizei w, GLsizei h)
{
	gc_flush ();
	glRenderbufferStorageMultisample (t, s, f, w, h);
}

int gluBuild2DMipmaps (GLenum target, GLint ifmt, GLsizei w, GLsizei h, GLenum fmt, GLenum type, const void *data)
{
	gc_flush ();
	glTexImage2D (target, 0, ifmt == GL_RGBA ? GL_RGBA8 : ifmt, w, h, 0, fmt, type, data);
	glGenerateMipmap (target);
	return 0;
}
