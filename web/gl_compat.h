/* web: the subset of desktop OpenGL 2.1 (+ EXT framebuffers, GLU, GLUT) that
 * main.c uses, on WebGL2 (OpenGL ES 3.0).
 *
 *  - immediate mode (glBegin .. glEnd, glVertex / glColor / glNormal /
 *    glTexCoord / glMultiTexCoord) is collected into batches: each vertex is
 *    transformed on the CPU by the current projection x modelview (normals
 *    and tangents by the normal matrix), so batches survive matrix changes
 *    and run as a few large draws. Quads become triangles, line loops lines.
 *  - the fixed-function pipeline (no program bound) is a built-in shader
 *    that draws the per-vertex colour.
 *  - GLSL 1.20 shaders are translated to GLSL ES 3.00 when their source is
 *    set (ftransform, gl_Normal, gl_NormalMatrix, gl_MultiTexCoordN,
 *    gl_TexCoord[0], texture2D, gl_FragColor, varying).
 *  - every other call used by main.c is wrapped so that pending geometry is
 *    drawn first, in order.
 */
#ifndef GL_COMPAT_H
#define GL_COMPAT_H

#include <GLES3/gl3.h>
#include <emscripten.h>
#include <stdint.h>

/* ---- the engine's clock and libc differences ---- */
#define CLOCK_UPTIME_RAW 0
#define clock_gettime_nsec_np(c) ((uint64_t) (emscripten_get_now () * 1e6))
long bsd_random (void);
void bsd_srandom (unsigned int);
#define random bsd_random
#define srandom bsd_srandom

/* ---- types and enums from desktop GL ---- */
#define GL_QUADS					0x0007
#define GL_SMOOTH					0x1D01
#define GL_MULTISAMPLE				0x809D
#define GL_LINE_SMOOTH_HINT			0x0C52
#define GL_LUMINANCE8				0x8040
#define GL_RGBA16F_ARB				GL_RGBA16F
#define GL_RGBA32F_ARB				GL_RGBA32F
#define GL_PROJECTION				0x1701
#define GL_MODELVIEW				0x1700
#define GL_FRAMEBUFFER_EXT			GL_FRAMEBUFFER
#define GL_READ_FRAMEBUFFER_EXT		GL_READ_FRAMEBUFFER
#define GL_DRAW_FRAMEBUFFER_EXT		GL_DRAW_FRAMEBUFFER
#define GL_RENDERBUFFER_EXT			GL_RENDERBUFFER
#define GL_COLOR_ATTACHMENT0_EXT	GL_COLOR_ATTACHMENT0
#define GL_DEPTH_ATTACHMENT_EXT		GL_DEPTH_ATTACHMENT
#define GL_FRAMEBUFFER_COMPLETE_EXT	GL_FRAMEBUFFER_COMPLETE
#define GL_MAX_SAMPLES_EXT			GL_MAX_SAMPLES
typedef double GLdouble;

/* ---- immediate mode, matrices ---- */
void gc_init (void);
void gc_flush (void);
void glBegin (GLenum mode);
void glEnd (void);
void glVertex2f (float x, float y);
void glVertex2d (double x, double y);
void glVertex3f (float x, float y, float z);
void glVertex3fv (const float *v);
void glColor4f (float r, float g, float b, float a);
void glNormal3fv (const float *n);
void glTexCoord2f (float s, float t);
void glMultiTexCoord3f (GLenum unit, float s, float t, float r);
void glMatrixMode (GLenum m);
void glLoadIdentity (void);
void glPushMatrix (void);
void glPopMatrix (void);
void glMultMatrixf (const float *m);
void glTranslatef (float x, float y, float z);
void glRotatef (float a, float x, float y, float z);
void gluOrtho2D (double l, double r, double b, double t);
void gluPerspective (double fovy, double aspect, double zn, double zf);
int  gluBuild2DMipmaps (GLenum target, GLint ifmt, GLsizei w, GLsizei h, GLenum fmt, GLenum type, const void *data);
static inline void glShadeModel (GLenum m) { (void) m; }

#ifndef GC_IMPL
/* ---- wrapped: draw what is pending first; a few are adapted ---- */
void gc_UseProgram (GLuint p);
void gc_ShaderSource (GLuint s, GLsizei n, const GLchar *const *src, const GLint *len);
void gc_LinkProgram (GLuint p);
void gc_Enable (GLenum c);
void gc_Disable (GLenum c);
void gc_Hint (GLenum t, GLenum m);
void gc_TexImage2D (GLenum t, GLint lvl, GLint ifmt, GLsizei w, GLsizei h, GLint b, GLenum fmt, GLenum type, const void *d);
void gc_RenderbufferStorageMultisample (GLenum t, GLsizei s, GLenum f, GLsizei w, GLsizei h);

#define glUseProgram(p)						gc_UseProgram (p)
#define glShaderSource(s, n, src, len)		gc_ShaderSource (s, n, (const GLchar *const *) (src), len)
#define glLinkProgram(p)					gc_LinkProgram (p)
#define glEnable(c)							gc_Enable (c)
#define glDisable(c)						gc_Disable (c)
#define glHint(t, m)						gc_Hint (t, m)
#define glTexImage2D(t, l, i, w, h, b, f, ty, d)	gc_TexImage2D (t, l, i, w, h, b, f, ty, d)
#define glClearDepth(d)						(gc_flush (), glClearDepthf ((float) (d)))

#define glBindFramebufferEXT(t, f)			(gc_flush (), glBindFramebuffer (t, f))
#define glGenFramebuffersEXT				glGenFramebuffers
#define glDeleteFramebuffersEXT(n, f)		(gc_flush (), glDeleteFramebuffers (n, f))
#define glFramebufferTexture2DEXT			glFramebufferTexture2D
#define glFramebufferRenderbufferEXT		glFramebufferRenderbuffer
#define glCheckFramebufferStatusEXT			glCheckFramebufferStatus
#define glGenRenderbuffersEXT				glGenRenderbuffers
#define glDeleteRenderbuffersEXT			glDeleteRenderbuffers
#define glBindRenderbufferEXT				glBindRenderbuffer
#define glRenderbufferStorageMultisampleEXT	gc_RenderbufferStorageMultisample
#define glBlitFramebufferEXT(a, b, c, d, e, f, g, h, m, fl)	(gc_flush (), glBlitFramebuffer (a, b, c, d, e, f, g, h, m, fl))

#define glClear(m)							(gc_flush (), glClear (m))
#define glClearColor(r, g, b, a)			(gc_flush (), glClearColor (r, g, b, a))
#define glViewport(x, y, w, h)				(gc_flush (), glViewport (x, y, w, h))
#define glBindTexture(t, x)					(gc_flush (), glBindTexture (t, x))
#define glActiveTexture(u)					(gc_flush (), glActiveTexture (u))
#define glColorMask(r, g, b, a)				(gc_flush (), glColorMask (r, g, b, a))
#define glBlendFunc(s, d)					(gc_flush (), glBlendFunc (s, d))
#define glDepthFunc(f)						(gc_flush (), glDepthFunc (f))
#define glReadPixels(x, y, w, h, f, t, p)	(gc_flush (), glReadPixels (x, y, w, h, f, t, p))
#define glUniform1i(l, v)					(gc_flush (), glUniform1i (l, v))
#define glUniform1f(l, v)					(gc_flush (), glUniform1f (l, v))
#define glUniform2f(l, a, b)				(gc_flush (), glUniform2f (l, a, b))
#define glUniform1fv(l, n, v)				(gc_flush (), glUniform1fv (l, n, v))
#define glTexParameteri(t, p, v)			(gc_flush (), glTexParameteri (t, p, v))

/* ---- GLUT: only what scene() calls ---- */
#define glutSwapBuffers()					gc_flush ()
#endif /* GC_IMPL */

#endif
