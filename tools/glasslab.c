/* glass lab: the glass pipeline alone, over a known background.
   columns: perfect, gentle bumps, strong bumps, fractured, perfect mirror, bumpy mirror
   rows: three orientations, or (anim) spinning.
   usage: glasslab W H out.ppm [frames out_dir] */
#include <OpenGL/OpenGL.h>
#define main p10_main
#define glutSwapBuffers fake_swap
#include "../main.c"
#undef main
void fake_swap (void) { glFinish (); }

static void save (rt_t *r, const char *path)
{
	unsigned char *b = malloc ((size_t) r->w * r->h * 3); FILE *f = fopen (path, "wb"); int y;
	glBindFramebufferEXT (GL_FRAMEBUFFER_EXT, r->fbo); glPixelStorei (GL_PACK_ALIGNMENT, 1);
	glReadPixels (0, 0, r->w, r->h, GL_RGB, GL_UNSIGNED_BYTE, b);
	fprintf (f, "P6\n%d %d 255\n", r->w, r->h);
	for (y = r->h - 1; y >= 0; y--) fwrite (b + (size_t) y * r->w * 3, 1, (size_t) r->w * 3, f);
	fclose (f); free (b);
}

/* a background: checker, fine grid lines, coloured stripes */
static void background (rt_t *r)
{
	unsigned char *b = malloc ((size_t) W * H * 4);
	int x, y;
	for (y = 0; y < H; y++) for (x = 0; x < W; x++)
	{
		unsigned char *p = b + ((size_t) y * W + x) * 4;
		int c = ((x / 48) + (y / 48)) & 1, v = c ? 70 : 30;
		p[0] = v; p[1] = v; p[2] = v + 10; p[3] = 255;
		if (x % 24 == 0 || y % 24 == 0) { p[0] = 200; p[1] = 200; p[2] = 200; }
		if (((x + y) / 12) % 16 == 0) { p[0] = 230; p[1] = 120; p[2] = 30; }
		if (((x - y + 4000) / 12) % 21 == 0) { p[0] = 40; p[1] = 170; p[2] = 200; }
	}
	glBindTexture (GL_TEXTURE_2D, r->tex);
	glTexSubImage2D (GL_TEXTURE_2D, 0, 0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, b);
	glBindTexture (GL_TEXTURE_2D, 0);
	free (b);
}

static const float fn[6][3] = { {0,1,0}, {0,-1,0}, {0,0,1}, {0,0,-1}, {-1,0,0}, {1,0,0} };
static float rx[3] = { 12, 30, 40 }, ry[3] = { 10, 35, 45 };

static void draw_cubes (int mirrors, float spin)
{
	int c, r, f, ncol = 6;
	float tc[3] = { 0.8f, 0.13f, 0.71f };
	glClearColor (0.5f, 0.5f, 0, 0);
	glClear (GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT);
	glDisable (GL_BLEND);
	glMatrixMode (GL_PROJECTION); glLoadIdentity ();
	gluPerspective (45.0, (GLfloat) W / (GLfloat) H, 0.1f, 100.0f);
	glMatrixMode (GL_MODELVIEW); glLoadIdentity ();
	if (!flaw_tex[1]) make_flaw_textures ();
	glUseProgram (pr_flaw);
	glUniform1i (glGetUniformLocation (pr_flaw, "flaw"), 0);
	for (r = 0; r < 3; r++) for (c = 0; c < ncol; c++)
	{
		int q = c < 4 ? c : c == 4 ? 0 : 2, ismir = c >= 4;
		if (ismir != mirrors) continue;
		glPushMatrix ();
		glTranslatef ((c - (ncol - 1) * 0.5f) * 1.3f, (1 - r) * 1.5f, -6.0f);
		glRotatef (rx[r] + spin * 0.6f, 1, 0, 0);
		glRotatef (ry[r] + spin, 0, 1, 0);
		glBindTexture (GL_TEXTURE_2D, flaw_tex[q ? q : 1]);
		glUniform1f (glGetUniformLocation (pr_flaw, "amt"), q ? (mirrors ? 0.1f : 1.0f) : 0.0f);
		glBegin (GL_QUADS);
		for (f = 0; f < 6; f++) { glNormal3fv (fn[f]); cube_face_tc (0, 0, 0, 0.5f, f, tc); }
		glEnd ();
		glPopMatrix ();
	}
	glBindTexture (GL_TEXTURE_2D, 0);
	glUseProgram (0);
}

static float g_spin;
static void draw_cubes_m (int mirrors) { draw_cubes (mirrors, g_spin); }

static void frame (float spin, const char *out, const char *nout)
{
	rt_t *A = &pool[0], *E = &pool[4];
	g_spin = spin;
	glass_normals (draw_cubes_m);
	if (nout) save (&nrm_rt[0], nout);
	pass (E, pr_glass);
	tex ("src", A->tex); tex ("tg", nrm_rt[1].tex); tex ("tm", nrm_rt[2].tex);
	uni ("k", 26.0f * H / 720.0f); uni ("disp", 0.08f); uni ("ph", 0.3f);
	uni ("fpx", (H * 0.5f) / tanf (22.5f * (float) M_PI / 180.0f));
	draw ();
	save (E, out);
}

int main (int argc, char **argv)
{
	CGLPixelFormatAttribute at[] = { kCGLPFAAccelerated, kCGLPFAColorSize, 24, kCGLPFAAlphaSize, 8, 0 };
	CGLPixelFormatObj pf; GLint n; CGLContextObj ctx;
	CGLChoosePixelFormat (at, &pf, &n); CGLCreateContext (pf, NULL, &ctx); CGLSetCurrentContext (ctx);
	W = atoi (argv[1]); H = atoi (argv[2]);
	glClearDepth (1.0); glDepthFunc (GL_LESS);
	gpu_alloc ();
	background (&pool[0]);
	if (argc > 4)
	{
		int i, nf = atoi (argv[4]);
		char p[1024];
		for (i = 0; i < nf; i++) { snprintf (p, sizeof p, "%s/f%04d.ppm", argv[5], i); frame (i * 1.2f, p, NULL); }
	}
	else
	{
		char np[1024]; snprintf (np, sizeof np, "%s.n.ppm", argv[3]);
		frame (0, argv[3], np);
	}
	fprintf (stderr, "GL error 0x%x\n", glGetError ());
	return 0;
}
