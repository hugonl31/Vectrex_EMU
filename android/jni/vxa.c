/* vxa.c - Vectrex_EMU for Android, our own app around the core (no RetroArch), as the iOS app (ios/App/VXApp.m): the
 * libretro core compiled into the app and hosted as RetroArch does - the same environment calls; the picture by OpenGL
 * ES 3 on a thread of its own (the core draws into our framebuffer, half floats - room above white -, shown 1:1 on the
 * window, HDR where the screen has it: an extended linear (scRGB) surface); the sound through a ring buffer to AAudio; the
 * controllers, keys and touch come from the Java side (VX.java). Everything that happens goes into the app's log
 * (files/applog.txt) and the core's (system/vectrex_emu/vectrex_emu.log).
 * Copyright (c) 2026 Hugo Bokhorst. Part of Vectrex_EMU; see LICENSE.md. */
#define _GNU_SOURCE                             /* (sched_setaffinity, cpu_set_t) */
#include <jni.h>
#include <android/native_window_jni.h>
#include <android/log.h>
#include <aaudio/AAudio.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <dlfcn.h>
#include <sys/stat.h>
#include "../../libretro/testcart.h"
#include "../../libretro/pitrex_tiles.h"
#include <zlib.h>
#include "libretro.h"

/* ---------------------------------------------------------------- the log */
static int vxa_lfd = -1, vxa_lfd2 = -1; static pthread_mutex_t vxa_lmx = PTHREAD_MUTEX_INITIALIZER;   /* (2: next to the core's log - reachable without the app's private folder) */
static char vxa_pend[65536]; static size_t vxa_pendn;          /* (lines not yet fetched by the Java side's live sender) */
static double vxa_now (void) { struct timespec t; clock_gettime (CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec * 1e-9; }
static void vlog (const char *fmt, ...) __attribute__ ((format (printf, 1, 2)));
static void vlog (const char *fmt, ...)
{
	char b[1200]; va_list a; va_start (a, fmt); vsnprintf (b, sizeof b, fmt, a); va_end (a);
	struct timespec ts; clock_gettime (CLOCK_REALTIME, &ts); struct tm tm; localtime_r (&ts.tv_sec, &tm);
	char line[1300]; int n = snprintf (line, sizeof line, "[app %02d:%02d:%02d.%03d] %s\n", tm.tm_hour, tm.tm_min, tm.tm_sec, (int) (ts.tv_nsec / 1000000), b);
	if (n < 0) return; if (n >= (int) sizeof line) n = sizeof line - 1;
	__android_log_write (ANDROID_LOG_INFO, "VectrexEMU", b);
	pthread_mutex_lock (&vxa_lmx);
	if (vxa_lfd >= 0) write (vxa_lfd, line, (size_t) n);
	if (vxa_lfd2 >= 0) write (vxa_lfd2, line, (size_t) n);
	if (vxa_pendn + (size_t) n < sizeof vxa_pend) { memcpy (vxa_pend + vxa_pendn, line, (size_t) n); vxa_pendn += (size_t) n; }
	pthread_mutex_unlock (&vxa_lmx);
}

/* ---------------------------------------------------------------- the core's settings (kept in files/options.txt) */
#define VXA_NOPT 256
static struct { char key[64]; char val[160]; char def[160]; char vals[1200]; char desc[160]; char cat[32]; char info[600]; } vxa_opt[VXA_NOPT];
static int vxa_nopt; static volatile int vxa_opt_dirty;
static struct { char key[32]; char desc[64]; char info[200]; } vxa_cat[16]; static int vxa_ncat;
static pthread_mutex_t vxa_omx = PTHREAD_MUTEX_INITIALIZER;
static char vxa_files[1024], vxa_sys[1100], vxa_saves[1100];
static char vxa_odefs[4096];                                    /* (this device's own defaults from the Java side: key=value lines) */
struct vxa_ov { const char *value, *label; }; struct vxa_oc { const char *key, *desc, *info; };
struct vxa_od { const char *key, *desc, *desc_categorized, *info, *info_categorized, *category_key; struct vxa_ov values[128]; const char *default_value; };
struct vxa_o2 { struct vxa_oc *categories; struct vxa_od *definitions; };
static int vxa_opt_find (const char *k) { for (int i = 0; i < vxa_nopt; i++) if (!strcmp (vxa_opt[i].key, k)) return i; return -1; }
static void vxa_opt_save (void)
{
	char p[1100], t[1110]; snprintf (p, sizeof p, "%s/options.txt", vxa_files); snprintf (t, sizeof t, "%s.new", p);
	FILE *f = fopen (t, "w"); if (!f) return;
	for (int i = 0; i < vxa_nopt; i++) if (strcmp (vxa_opt[i].val, vxa_opt[i].def)) fprintf (f, "%s=%s\n", vxa_opt[i].key, vxa_opt[i].val);
	fclose (f); rename (t, p);
}
static int vxa_saved_val (const char *key, char *out, size_t n)   /* (1: the user chose it - the file has it) */
{
	char p[1100]; snprintf (p, sizeof p, "%s/options.txt", vxa_files); FILE *f = fopen (p, "r"); if (!f) return 0;
	char l[1400]; size_t kl = strlen (key); int got = 0;
	while (fgets (l, sizeof l, f)) if (!strncmp (l, key, kl) && l[kl] == '=') { l[strcspn (l, "\r\n")] = 0; snprintf (out, n, "%s", l + kl + 1); got = 1; }
	fclose (f); return got;
}
static void vxa_opts_defaults (void)
{
	/* (this app's own defaults, as the iOS app's: the core's modern control deck below the picture, the screen's rate -
	 *  given by the Java side - and the picture upright as the screen stands) */
	for (char *l = vxa_odefs; *l; ) { char *e = strchr (l, '\n'); char line[400]; size_t n = e ? (size_t) (e - l) : strlen (l); if (n >= sizeof line) n = sizeof line - 1;
		memcpy (line, l, n); line[n] = 0; char *eq = strchr (line, '=');
		if (eq) { *eq = 0; int i = vxa_opt_find (line); if (i >= 0) { snprintf (vxa_opt[i].def, sizeof vxa_opt[i].def, "%s", eq + 1); snprintf (vxa_opt[i].val, sizeof vxa_opt[i].val, "%s", eq + 1); } }
		l += n; if (*l == '\n') l++; }
	for (int i = 0; i < vxa_nopt; i++) { char v[160]; if (vxa_saved_val (vxa_opt[i].key, v, sizeof v)) snprintf (vxa_opt[i].val, sizeof vxa_opt[i].val, "%s", v); }
	int r = vxa_opt_find ("vectrex_emu_rate");
	vlog ("core options: %d (rate %s)", vxa_nopt, r >= 0 ? vxa_opt[r].val : "?");
}
static void vxa_opts_v2 (const struct vxa_o2 *o)
{
	pthread_mutex_lock (&vxa_omx); vxa_nopt = 0; vxa_ncat = 0;
	for (const struct vxa_oc *c = o->categories; c && c->key && vxa_ncat < 16; c++, vxa_ncat++) {
		snprintf (vxa_cat[vxa_ncat].key, sizeof vxa_cat[0].key, "%s", c->key); snprintf (vxa_cat[vxa_ncat].desc, sizeof vxa_cat[0].desc, "%s", c->desc ? c->desc : c->key);
		snprintf (vxa_cat[vxa_ncat].info, sizeof vxa_cat[0].info, "%s", c->info ? c->info : ""); }
	for (const struct vxa_od *d = o->definitions; d && d->key && vxa_nopt < VXA_NOPT; d++) {
		int i = vxa_nopt++; snprintf (vxa_opt[i].key, sizeof vxa_opt[i].key, "%s", d->key);
		snprintf (vxa_opt[i].desc, sizeof vxa_opt[i].desc, "%s", d->desc_categorized ? d->desc_categorized : d->desc);
		snprintf (vxa_opt[i].info, sizeof vxa_opt[i].info, "%s", d->info_categorized ? d->info_categorized : (d->info ? d->info : ""));
		snprintf (vxa_opt[i].cat, sizeof vxa_opt[i].cat, "%s", d->category_key ? d->category_key : "");
		size_t u = 0; vxa_opt[i].vals[0] = 0;
		for (int k = 0; k < 128 && d->values[k].value; k++) u += (size_t) snprintf (vxa_opt[i].vals + u, u < sizeof vxa_opt[i].vals ? sizeof vxa_opt[i].vals - u : 0, "%s%s", k ? "|" : "", d->values[k].value);
		snprintf (vxa_opt[i].def, sizeof vxa_opt[i].def, "%s", d->default_value ? d->default_value : d->values[0].value);
		snprintf (vxa_opt[i].val, sizeof vxa_opt[i].val, "%s", vxa_opt[i].def); }
	vxa_opts_defaults (); pthread_mutex_unlock (&vxa_omx);
}
static void vxa_opts_set (const struct retro_variable *v)
{
	pthread_mutex_lock (&vxa_omx); vxa_nopt = 0;
	for (; v && v->key && vxa_nopt < VXA_NOPT; v++) {
		const char *s = v->value ? strstr (v->value, "; ") : NULL; if (!s) continue; s += 2;
		int i = vxa_nopt++; snprintf (vxa_opt[i].key, sizeof vxa_opt[i].key, "%s", v->key); snprintf (vxa_opt[i].vals, sizeof vxa_opt[i].vals, "%s", s);
		snprintf (vxa_opt[i].desc, sizeof vxa_opt[i].desc, "%.*s", (int) (s - 2 - v->value), v->value);
		const char *bar = strchr (s, '|'); size_t n = bar ? (size_t) (bar - s) : strlen (s); if (n >= sizeof vxa_opt[i].def) n = sizeof vxa_opt[i].def - 1;
		memcpy (vxa_opt[i].def, s, n); vxa_opt[i].def[n] = 0; snprintf (vxa_opt[i].val, sizeof vxa_opt[i].val, "%s", vxa_opt[i].def); }
	vxa_opts_defaults (); pthread_mutex_unlock (&vxa_omx);
}

/* ---------------------------------------------------------------- the picture: EGL on the window, the core's framebuffer */
static struct retro_hw_render_callback vxa_hw; static int vxa_hw_on;
static retro_keyboard_event_t vxa_kbcb;
static struct retro_system_av_info vxa_av;
static GLuint vxa_cfbo, vxa_ctex, vxa_cdepth; static unsigned vxa_cw, vxa_ch; static int vxa_half;
static EGLDisplay vxa_dpy = EGL_NO_DISPLAY; static EGLContext vxa_ctx = EGL_NO_CONTEXT; static EGLSurface vxa_surf = EGL_NO_SURFACE, vxa_pbuf = EGL_NO_SURFACE;
static EGLConfig vxa_cfg; static int vxa_hdr_surf;               /* (the window's surface: extended linear, half floats) */
static ANativeWindow *vxa_win, *vxa_win_new; static volatile int vxa_win_changed;
static int vxa_sw, vxa_sh;                                       /* (the window's pixels) */
static volatile int vxa_ax, vxa_ay, vxa_aw, vxa_ah;               /* (the room for the picture there, from the Java side) */
static volatile int vxa_top_align = 1;                           /* (upright: the picture at the top of its room) */
static volatile float vxa_px, vxa_py, vxa_pw, vxa_ph;              /* (where it is now: for the touches) */
static unsigned vxa_fw, vxa_fh; static int vxa_have, vxa_nodraw;
static GLuint vxa_prog, vxa_vao; static GLint vxa_u_rect, vxa_u_uv, vxa_u_cfg;
static volatile float vxa_headroom = 1.0f, vxa_hdr_frac = 1.0f;   /* (the screen's HDR headroom; Settings > HDR brightness) */
static uintptr_t vxa_get_fb (void) { return vxa_cfbo; }
static retro_proc_address_t vxa_get_proc (const char *s)
{
	static void *gl; if (!gl) gl = dlopen ("libGLESv3.so", RTLD_NOW); void *p = gl ? dlsym (gl, s) : NULL;
	if (!p) p = (void *) eglGetProcAddress (s); return (retro_proc_address_t) p;
}
static void vxa_corelog (enum retro_log_level l, const char *fmt, ...) { char b[1500]; va_list a; va_start (a, fmt); vsnprintf (b, sizeof b, fmt, a); va_end (a); (void) l; fputs (b, stderr); }
static void vxa_core_fb (unsigned w, unsigned h)
{
	if (w <= vxa_cw && h <= vxa_ch && vxa_cfbo) return;
	if (w < vxa_cw) w = vxa_cw; if (h < vxa_ch) h = vxa_ch;
	if (!vxa_cfbo) { glGenFramebuffers (1, &vxa_cfbo); glGenTextures (1, &vxa_ctex); }
	glBindTexture (GL_TEXTURE_2D, vxa_ctex);
	vxa_half = 1; while (glGetError ()) ;
	glTexImage2D (GL_TEXTURE_2D, 0, GL_RGBA16F, (GLsizei) w, (GLsizei) h, 0, GL_RGBA, GL_HALF_FLOAT, NULL);
	glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glBindFramebuffer (GL_FRAMEBUFFER, vxa_cfbo); glFramebufferTexture2D (GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, vxa_ctex, 0);
	if (glGetError () || glCheckFramebufferStatus (GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {   /* (no half floats to draw into: 8 bits) */
		vxa_half = 0; glTexImage2D (GL_TEXTURE_2D, 0, GL_RGBA8, (GLsizei) w, (GLsizei) h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
		glFramebufferTexture2D (GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, vxa_ctex, 0); }
	if (vxa_hw.depth || vxa_hw.stencil) {
		if (!vxa_cdepth) glGenRenderbuffers (1, &vxa_cdepth);
		glBindRenderbuffer (GL_RENDERBUFFER, vxa_cdepth); glRenderbufferStorage (GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, (GLsizei) w, (GLsizei) h);
		glFramebufferRenderbuffer (GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, vxa_cdepth); }
	vxa_cw = w; vxa_ch = h;
	vlog ("core framebuffer %u x %u, %s: %s", w, h, vxa_half ? "half floats (HDR room)" : "8-bit", glCheckFramebufferStatus (GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE ? "complete" : "NOT COMPLETE");
}
/* (the picture to the window: the core's values as they would go to an ordinary screen - sRGB, 1.0 white; on an HDR
 *  surface (extended linear) made linear by the exact sRGB curve, as the iOS app, up to the screen's headroom) */
static const char *VXA_VS = "#version 300 es\nuniform vec4 r; uniform vec4 u; out vec2 t;\n"
	"void main () { vec2 q = vec2 (float (gl_VertexID & 1), float ((gl_VertexID >> 1) & 1));\n"
	"  gl_Position = vec4 (mix (r.x, r.z, q.x), mix (r.y, r.w, q.y), 0.0, 1.0);\n"
	"  t = vec2 (q.x * u.x, (u.z > 0.5 ? 1.0 - q.y : q.y) * u.y); }\n";
static const char *VXA_FS = "#version 300 es\nprecision highp float; uniform sampler2D s; uniform vec4 c; in vec2 t; out vec4 o;\n"
	"void main () { vec3 x = max (texture (s, t).rgb, 0.0);\n"
	"  if (c.y > 0.5) { x = mix (pow ((x + 0.055) / 1.055, vec3 (2.4)), x / 12.92, vec3 (lessThanEqual (x, vec3 (0.04045)))); x = min (x, vec3 (max (c.x, 1.0))); }\n"
	"  else x = min (x, vec3 (1.0));\n"
	"  o = vec4 (x, 1.0); }\n";
static GLuint vxa_sh_make (GLenum k, const char *src)
{
	GLuint s = glCreateShader (k); glShaderSource (s, 1, &src, NULL); glCompileShader (s); GLint ok = 0; glGetShaderiv (s, GL_COMPILE_STATUS, &ok);
	if (!ok) { char b[800]; glGetShaderInfoLog (s, sizeof b, NULL, b); vlog ("present shader: %s", b); } return s;
}
static void vxa_present_setup (void)
{
	vxa_prog = glCreateProgram (); glAttachShader (vxa_prog, vxa_sh_make (GL_VERTEX_SHADER, VXA_VS)); glAttachShader (vxa_prog, vxa_sh_make (GL_FRAGMENT_SHADER, VXA_FS));
	glLinkProgram (vxa_prog); GLint ok = 0; glGetProgramiv (vxa_prog, GL_LINK_STATUS, &ok); vlog ("present shader: %s", ok ? "ready" : "FAILED");
	vxa_u_rect = glGetUniformLocation (vxa_prog, "r"); vxa_u_uv = glGetUniformLocation (vxa_prog, "u"); vxa_u_cfg = glGetUniformLocation (vxa_prog, "c");
	glGenVertexArrays (1, &vxa_vao);
}
static int vxa_has_ext (const char *list, const char *e) { const char *p = list; size_t n = strlen (e); while (p && (p = strstr (p, e))) { if ((p == list || p[-1] == ' ') && (p[n] == ' ' || !p[n])) return 1; p += n; } return 0; }
static volatile int vxa_want_hdr = 1;                            /* (the Java side: the screen can show HDR) */
static int vxa_egl_init (void)
{
	vxa_dpy = eglGetDisplay (EGL_DEFAULT_DISPLAY); EGLint ma, mi; if (!eglInitialize (vxa_dpy, &ma, &mi)) { vlog ("EGL: no display"); return 0; }
	const char *ex = eglQueryString (vxa_dpy, EGL_EXTENSIONS);
	const int scrgb = vxa_has_ext (ex, "EGL_EXT_gl_colorspace_scrgb_linear") && vxa_has_ext (ex, "EGL_EXT_pixel_format_float");
	vlog ("EGL %d.%d; extended linear (scRGB) %s; HDR screen %s", ma, mi, scrgb ? "yes" : "no", vxa_want_hdr ? "yes" : "no");
	EGLint n = 0; vxa_hdr_surf = 0;
	if (scrgb && vxa_want_hdr) {
		const EGLint a[] = { EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT_KHR, EGL_SURFACE_TYPE, EGL_WINDOW_BIT | EGL_PBUFFER_BIT, EGL_COLOR_COMPONENT_TYPE_EXT, EGL_COLOR_COMPONENT_TYPE_FLOAT_EXT,
			EGL_RED_SIZE, 16, EGL_GREEN_SIZE, 16, EGL_BLUE_SIZE, 16, EGL_ALPHA_SIZE, 16, EGL_NONE };
		if (eglChooseConfig (vxa_dpy, a, &vxa_cfg, 1, &n) && n == 1) vxa_hdr_surf = 1; }
	if (!vxa_hdr_surf) {
		const EGLint a[] = { EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT_KHR, EGL_SURFACE_TYPE, EGL_WINDOW_BIT | EGL_PBUFFER_BIT, EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_NONE };
		if (!eglChooseConfig (vxa_dpy, a, &vxa_cfg, 1, &n) || n != 1) { vlog ("EGL: no config"); return 0; } }
	const EGLint ca[] = { EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE };
	vxa_ctx = eglCreateContext (vxa_dpy, vxa_cfg, EGL_NO_CONTEXT, ca); if (vxa_ctx == EGL_NO_CONTEXT) { vlog ("EGL: no context"); return 0; }
	const EGLint pa[] = { EGL_WIDTH, 16, EGL_HEIGHT, 16, EGL_NONE }; vxa_pbuf = eglCreatePbufferSurface (vxa_dpy, vxa_cfg, pa);
	eglMakeCurrent (vxa_dpy, vxa_pbuf, vxa_pbuf, vxa_ctx);
	vlog ("GL: %s / %s / %s", glGetString (GL_VENDOR), glGetString (GL_RENDERER), glGetString (GL_VERSION));
	return 1;
}
static void vxa_surface_update (void)               /* (the window came, changed or went: the EGL surface made anew) */
{
	if (!vxa_win_changed) return; vxa_win_changed = 0;
	eglMakeCurrent (vxa_dpy, vxa_pbuf, vxa_pbuf, vxa_ctx);
	if (vxa_surf != EGL_NO_SURFACE) { eglDestroySurface (vxa_dpy, vxa_surf); vxa_surf = EGL_NO_SURFACE; }
	if (vxa_win) { ANativeWindow_release (vxa_win); vxa_win = NULL; }
	vxa_win = vxa_win_new; vxa_win_new = NULL; if (!vxa_win) { vlog ("window gone"); return; }
	const EGLint hdr[] = { EGL_GL_COLORSPACE_KHR, EGL_GL_COLORSPACE_SCRGB_LINEAR_EXT, EGL_NONE };
	vxa_surf = eglCreateWindowSurface (vxa_dpy, vxa_cfg, vxa_win, vxa_hdr_surf ? hdr : NULL);
	if (vxa_surf == EGL_NO_SURFACE && vxa_hdr_surf) { vlog ("window: no extended linear surface (0x%x) - ordinary", eglGetError ()); vxa_surf = eglCreateWindowSurface (vxa_dpy, vxa_cfg, vxa_win, NULL); vxa_hdr_surf = 2; }
	if (vxa_surf == EGL_NO_SURFACE) { vlog ("window: no surface (0x%x)", eglGetError ()); return; }
	eglMakeCurrent (vxa_dpy, vxa_surf, vxa_surf, vxa_ctx); eglSwapInterval (vxa_dpy, 1);
	EGLint w = 0, h = 0; eglQuerySurface (vxa_dpy, vxa_surf, EGL_WIDTH, &w); eglQuerySurface (vxa_dpy, vxa_surf, EGL_HEIGHT, &h); vxa_sw = w; vxa_sh = h;
	vlog ("window %d x %d px, %s", w, h, vxa_hdr_surf == 1 ? "HDR (extended linear)" : "SDR");
}
extern float vx_hdr_peak;
static void vxa_hdr_update (void)
{
	const double hr = vxa_hdr_surf == 1 ? vxa_headroom : 1.0, fr = vxa_hdr_frac;
	float pk = hr > 1.05 && fr > 0.01 ? (float) (1.055 * pow (1.0 + (hr - 1.0) * fr, 1 / 2.4) - 0.055) : 1.0f;
	if (fabsf (pk - vx_hdr_peak) > 0.005f) { vlog ("HDR: %s (screen headroom %.2f, setting %.0f %%, knee top %.2f)", pk > 1 ? "on" : "off", hr, fr * 100, pk); vx_hdr_peak = pk; }
}
extern int vx_app_pich;
static volatile int vxa_snap_n = -1, vxa_snap_fixed; static char vxa_snap_path[1100]; static long vxa_since_load;   /* (the tests' snapshot: below) */
static void vxa_present (void)
{
	if (!vxa_have || vxa_surf == EGL_NO_SURFACE || !vxa_fw || !vxa_fh) return;
	const double aw = vxa_aw > 0 ? vxa_aw : vxa_sw, ah = vxa_ah > 0 ? vxa_ah : vxa_sh, ax = vxa_aw > 0 ? vxa_ax : 0, ay = vxa_ah > 0 ? vxa_ay : 0;
	const double k = fmin (1.0, fmin (aw / vxa_fw, ah / vxa_fh)), w = vxa_fw * k, h = vxa_fh * k, x = ax + (aw - w) / 2, top = vxa_top_align ? ay : ay + (ah - h) / 2;
	{ const double fit = fmin (ah, aw * vxa_fh / vxa_fw), r = fit / vxa_fh;   /* (the core draws the picture at the size there is room for: 1:1) */
	  static unsigned asked_fh;                         /* (asked once per picture size: the core's answer waited for - a fixed size never comes) */
	  if (fabs (r - 1) > 0.01 && vxa_snap_n < 0 && !vxa_snap_fixed && vxa_fh != asked_fh) { int want = (int) lround ((vx_app_pich > 0 ? vx_app_pich : (int) vxa_fh) * r);
		if (want > 2400) want = 2400;
		if (want != vx_app_pich) { vlog ("picture: room for %.0f px high, %u now - asking the core for %d", fit, vxa_fh, want); vx_app_pich = want; asked_fh = vxa_fh; } } }
	if (fabs (x - vxa_px) > 0.5 || fabs (top - vxa_py) > 0.5 || fabs (w - vxa_pw) > 0.5 || fabs (h - vxa_ph) > 0.5) {
		vxa_px = (float) x; vxa_py = (float) top; vxa_pw = (float) w; vxa_ph = (float) h;
		vlog ("picture %u x %u px at %.0f,%.0f %.0f x %.0f (%s)", vxa_fw, vxa_fh, x, top, w, h, k == 1.0 ? "1:1" : "scaled"); }
	glBindFramebuffer (GL_FRAMEBUFFER, 0); glViewport (0, 0, vxa_sw, vxa_sh);
	glDisable (GL_BLEND); glDisable (GL_DEPTH_TEST); glDisable (GL_SCISSOR_TEST); glDisable (GL_STENCIL_TEST); glDisable (GL_CULL_FACE);
	glClearColor (0, 0, 0, 1); glClear (GL_COLOR_BUFFER_BIT);
	glUseProgram (vxa_prog); glBindVertexArray (vxa_vao);
	glActiveTexture (GL_TEXTURE0); glBindTexture (GL_TEXTURE_2D, vxa_ctex);
	const GLint flt = k == 1.0 ? GL_NEAREST : GL_LINEAR; glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, flt); glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, flt);
	glUniform1i (glGetUniformLocation (vxa_prog, "s"), 0);
	glUniform4f (vxa_u_rect, (float) (x / vxa_sw * 2 - 1), (float) (1 - (top + h) / vxa_sh * 2), (float) ((x + w) / vxa_sw * 2 - 1), (float) (1 - top / vxa_sh * 2));
	glUniform4f (vxa_u_uv, (float) vxa_fw / vxa_cw, (float) vxa_fh / vxa_ch, vxa_hw.bottom_left_origin ? 0.f : 1.f, 0);
	glUniform4f (vxa_u_cfg, vxa_hdr_surf == 1 ? vxa_headroom : 1.0f, vxa_hdr_surf == 1 ? 1.f : 0.f, 0, 0);
	glDrawArrays (GL_TRIANGLE_STRIP, 0, 4);
	glBindVertexArray (0); glUseProgram (0);
}

/* ---------------------------------------------------------------- the sound: a ring buffer to AAudio */
#define VXA_RING 16384
static int16_t vxa_ring[VXA_RING * 2]; static volatile uint32_t vxa_rd, vxa_wr; static volatile uint32_t vxa_under, vxa_drop;
static uint32_t vxa_ring_fill (void) { return __atomic_load_n (&vxa_wr, __ATOMIC_ACQUIRE) - __atomic_load_n (&vxa_rd, __ATOMIC_ACQUIRE); }
static size_t vxa_audio_batch (const int16_t *d, size_t frames)
{
	uint32_t w = vxa_wr;
	for (size_t i = 0; i < frames; i++) {
		if (w - __atomic_load_n (&vxa_rd, __ATOMIC_ACQUIRE) >= VXA_RING) { vxa_drop++; continue; }
		vxa_ring[(w % VXA_RING) * 2] = d[2 * i]; vxa_ring[(w % VXA_RING) * 2 + 1] = d[2 * i + 1]; w++; }
	__atomic_store_n (&vxa_wr, w, __ATOMIC_RELEASE); return frames;
}
static void vxa_audio_sample (int16_t l, int16_t r) { int16_t d[2] = { l, r }; vxa_audio_batch (d, 1); }
static AAudioStream *vxa_as;
static aaudio_data_callback_result_t vxa_audio_cb (AAudioStream *s, void *u, void *data, int32_t n)
{
	(void) s; (void) u; int16_t *o = data; uint32_t rd = vxa_rd, have = __atomic_load_n (&vxa_wr, __ATOMIC_ACQUIRE) - rd;
	for (int32_t i = 0; i < n; i++) {
		if (have) { o[2 * i] = vxa_ring[(rd % VXA_RING) * 2]; o[2 * i + 1] = vxa_ring[(rd % VXA_RING) * 2 + 1]; rd++; have--; }
		else { o[2 * i] = o[2 * i + 1] = 0; vxa_under++; } }
	__atomic_store_n (&vxa_rd, rd, __ATOMIC_RELEASE); return AAUDIO_CALLBACK_RESULT_CONTINUE;
}
static void vxa_audio_err (AAudioStream *s, void *u, aaudio_result_t e);
static double vxa_rate;
static void vxa_audio_start (double rate)
{
	vxa_rate = rate; AAudioStreamBuilder *b = NULL; if (AAudio_createStreamBuilder (&b) != AAUDIO_OK) { vlog ("sound: no AAudio"); return; }
	AAudioStreamBuilder_setFormat (b, AAUDIO_FORMAT_PCM_I16); AAudioStreamBuilder_setChannelCount (b, 2); AAudioStreamBuilder_setSampleRate (b, (int32_t) lround (rate));
	AAudioStreamBuilder_setPerformanceMode (b, AAUDIO_PERFORMANCE_MODE_LOW_LATENCY); AAudioStreamBuilder_setSharingMode (b, AAUDIO_SHARING_MODE_SHARED);
	AAudioStreamBuilder_setUsage (b, AAUDIO_USAGE_GAME); AAudioStreamBuilder_setContentType (b, AAUDIO_CONTENT_TYPE_SONIFICATION);
	AAudioStreamBuilder_setDataCallback (b, vxa_audio_cb, NULL); AAudioStreamBuilder_setErrorCallback (b, vxa_audio_err, NULL);
	aaudio_result_t r = AAudioStreamBuilder_openStream (b, &vxa_as); AAudioStreamBuilder_delete (b);
	if (r != AAUDIO_OK) { vlog ("sound: %s", AAudio_convertResultToText (r)); vxa_as = NULL; return; }
	r = AAudioStream_requestStart (vxa_as);
	vlog ("sound: %.0f Hz -> %d Hz, burst %d frames: %s", rate, AAudioStream_getSampleRate (vxa_as), AAudioStream_getFramesPerBurst (vxa_as), AAudio_convertResultToText (r));
}
static volatile int vxa_audio_lost;
static void vxa_audio_err (AAudioStream *s, void *u, aaudio_result_t e) { (void) s; (void) u; vxa_audio_lost = 1; __android_log_print (ANDROID_LOG_WARN, "VectrexEMU", "sound: %s", AAudio_convertResultToText (e)); }
static void vxa_audio_check (void)                   /* (a headset in or out ends the stream: opened again on this thread) */
{
	if (!vxa_audio_lost) return; vxa_audio_lost = 0; vlog ("sound: the output changed - opened again");
	if (vxa_as) { AAudioStream_close (vxa_as); vxa_as = NULL; } vxa_audio_start (vxa_rate);
}

/* ---------------------------------------------------------------- input (set by the Java side) */
typedef struct { uint32_t btn; int16_t ax[2][2]; } vxa_pad;
static vxa_pad vxa_pads[2], vxa_tpad; static pthread_mutex_t vxa_imx = PTHREAD_MUTEX_INITIALIZER;
static vxa_pad vxa_cur[2];
static uint8_t vxa_kb[RETROK_LAST];
static struct { int on; double x, y; } vxa_ptr, vxa_ptr_cur;
static void vxa_input_poll (void)
{
	pthread_mutex_lock (&vxa_imx);
	for (int port = 0; port < 2; port++) { vxa_pad p = vxa_pads[port];
		if (port == 0) { p.btn |= vxa_tpad.btn; if (vxa_tpad.ax[0][0] || vxa_tpad.ax[0][1]) { p.ax[0][0] = vxa_tpad.ax[0][0]; p.ax[0][1] = vxa_tpad.ax[0][1]; } }
		vxa_cur[port] = p; }
	vxa_ptr_cur = vxa_ptr; pthread_mutex_unlock (&vxa_imx);
}
static int16_t vxa_input_state (unsigned port, unsigned device, unsigned index, unsigned id)
{
	switch (device & RETRO_DEVICE_MASK) {
	case RETRO_DEVICE_JOYPAD: return port < 2 && id < 16 ? (vxa_cur[port].btn >> id & 1) : 0;
	case RETRO_DEVICE_ANALOG: return port < 2 && index < 2 && id < 2 ? vxa_cur[port].ax[index][id] : 0;
	case RETRO_DEVICE_KEYBOARD: return id < RETROK_LAST ? vxa_kb[id] : 0;
	case RETRO_DEVICE_POINTER:
		if (port || index) return 0;
		if (id == RETRO_DEVICE_ID_POINTER_PRESSED) return (int16_t) vxa_ptr_cur.on;
		if (id == RETRO_DEVICE_ID_POINTER_X) return (int16_t) (vxa_ptr_cur.x * 32767);
		if (id == RETRO_DEVICE_ID_POINTER_Y) return (int16_t) (vxa_ptr_cur.y * 32767);
		return 0;
	default: return 0;
	}
}

/* ---------------------------------------------------------------- the core's environment */
static volatile int vxa_holding, vxa_land;
static char vxa_msg[512]; static double vxa_msg_t; static volatile int vxa_msg_n;   /* (the core's messages: the Java side shows them) */
static void vxa_message (const char *m, double secs) { pthread_mutex_lock (&vxa_lmx); snprintf (vxa_msg, sizeof vxa_msg, "%s", m); vxa_msg_t = secs; vxa_msg_n++; pthread_mutex_unlock (&vxa_lmx); }
static bool vxa_env (unsigned cmd, void *data)
{
	switch (cmd) {
	case RETRO_ENVIRONMENT_GET_CAN_DUPE: *(bool *) data = true; return true;
	case RETRO_ENVIRONMENT_SET_MESSAGE: { const struct retro_message *m = data; vlog ("core message: %s", m->msg); vxa_message (m->msg, m->frames / 60.0); return true; }
	case 60: { const char *m = *(const char **) data; unsigned ms = ((const unsigned *) ((const char **) data + 1))[0];   /* (SET_MESSAGE_EXT) */
		vlog ("core message: %s", m); vxa_message (m, ms / 1000.0); return true; }
	case RETRO_ENVIRONMENT_SET_PERFORMANCE_LEVEL: return true;
	case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY: *(const char **) data = vxa_sys; return true;
	case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY: *(const char **) data = vxa_saves; return true;
	case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT: return *(const int *) data == RETRO_PIXEL_FORMAT_XRGB8888;
	case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS: case RETRO_ENVIRONMENT_SET_CONTROLLER_INFO: case RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME: return true;
	case RETRO_ENVIRONMENT_SET_KEYBOARD_CALLBACK: vxa_kbcb = ((const struct retro_keyboard_callback *) data)->callback; return true;
	case RETRO_ENVIRONMENT_SET_HW_RENDER: {
		struct retro_hw_render_callback *c = data;
		vlog ("hw render: context %d, version %u.%u, depth %d stencil %d bottom-left %d", c->context_type, c->version_major, c->version_minor, c->depth, c->stencil, c->bottom_left_origin);
		if (c->context_type != RETRO_HW_CONTEXT_OPENGLES3 && !(c->context_type == RETRO_HW_CONTEXT_OPENGLES_VERSION && c->version_major == 3)) return false;
		c->get_current_framebuffer = vxa_get_fb; c->get_proc_address = vxa_get_proc; vxa_hw = *c; vxa_hw_on = 1; return true; }
	case 56: *(unsigned *) data = RETRO_HW_CONTEXT_OPENGLES3; return true;
	case RETRO_ENVIRONMENT_SET_VARIABLES: vxa_opts_set (data); return true;
	case 52: *(unsigned *) data = 2; return true;
	case 67: vxa_opts_v2 (data); return true;
	case 68: return true;
	case RETRO_ENVIRONMENT_GET_VARIABLE: { struct retro_variable *v = data; pthread_mutex_lock (&vxa_omx); int i = vxa_opt_find (v->key);
		v->value = i >= 0 ? vxa_opt[i].val : NULL;
		if (i >= 0 && !strcmp (v->key, "vectrex_emu_deck")) v->value = "off";   /* (the app draws its own deck - DeckView.java, as the iOS app: the core's stays off) */ pthread_mutex_unlock (&vxa_omx); return i >= 0; }
	case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE: *(bool *) data = vxa_opt_dirty; vxa_opt_dirty = 0; return true;
	case 70: { const struct retro_variable *v = data; if (!v || !v->key) return true;
		pthread_mutex_lock (&vxa_omx); int i = vxa_opt_find (v->key);
		if (i >= 0 && v->value) { snprintf (vxa_opt[i].val, sizeof vxa_opt[i].val, "%s", v->value); vxa_opt_save (); }
		pthread_mutex_unlock (&vxa_omx); if (i >= 0) vlog ("core set option %s = %s", v->key, v->value ? v->value : "?"); return i >= 0 && v->value; }
	case RETRO_ENVIRONMENT_GET_LOG_INTERFACE: ((struct retro_log_callback *) data)->log = vxa_corelog; return true;
	case RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO: vxa_av = *(const struct retro_system_av_info *) data;
		vlog ("av info: %u x %u (max %u x %u), %.2f Hz", vxa_av.geometry.base_width, vxa_av.geometry.base_height, vxa_av.geometry.max_width, vxa_av.geometry.max_height, vxa_av.timing.fps);
		if (vxa_cfbo) vxa_core_fb (vxa_av.geometry.max_width, vxa_av.geometry.max_height); return true;
	case RETRO_ENVIRONMENT_SET_GEOMETRY: vxa_av.geometry = *(const struct retro_game_geometry *) data; return true;
	case RETRO_ENVIRONMENT_GET_LANGUAGE: *(unsigned *) data = RETRO_LANGUAGE_ENGLISH; return true;
	case RETRO_ENVIRONMENT_GET_AUDIO_VIDEO_ENABLE: *(int *) data = vxa_nodraw ? 2 : 3; return true;
	default: return false;
	}
}
static void vxa_video (const void *d, unsigned w, unsigned h, size_t pitch)
{
	(void) pitch;
	if (d == RETRO_HW_FRAME_BUFFER_VALID) { vxa_fw = w; vxa_fh = h; vxa_have = 1; return; }
	if (!d) return;
	static int told; if (!told) { told = 1; vlog ("a picture without the GPU (%u x %u) - not shown", w, h); }
}
static void vxa_poll (void) {}

/* ---------------------------------------------------------------- the tests' snapshot: the window's picture as shown (after the
 * app's present - its colour curve included), read back at the Nth run after a game's load; while one is asked for, no
 * catch-up runs (each run drawn, as a test frontend does) */

static void vxa_snap_take (void)
{
	const int x = (int) lroundf (vxa_px), w = (int) lroundf (vxa_pw), h = (int) lroundf (vxa_ph), y = vxa_sh - (int) lroundf (vxa_py) - h; if (w <= 0 || h <= 0) return;
	unsigned char *b = malloc ((size_t) w * h * 4); if (!b) return;
	glPixelStorei (GL_PACK_ALIGNMENT, 1);
	if (vxa_hdr_surf == 1) { float *f = malloc ((size_t) w * h * 16); if (f) { glReadPixels (x, y, w, h, GL_RGBA, GL_FLOAT, f);   /* (extended linear: back to sRGB, as an ordinary screen shows it) */
			for (size_t i = 0; i < (size_t) w * h * 4; i++) { float v = f[i] < 0 ? 0 : f[i] > 1 ? 1 : f[i]; v = v <= 0.0031308f ? v * 12.92f : 1.055f * powf (v, 1 / 2.4f) - 0.055f; b[i] = (unsigned char) lroundf (v * 255); } free (f); } }
	else glReadPixels (x, y, w, h, GL_RGBA, GL_UNSIGNED_BYTE, b);
	FILE *o = fopen (vxa_snap_path, "wb");
	if (o) { fprintf (o, "P6\n%d %d\n255\n", w, h); for (int r = h - 1; r >= 0; r--) for (int c = 0; c < w; c++) fwrite (b + ((size_t) r * w + c) * 4, 1, 3, o); fclose (o); }
	free (b); vlog ("snapshot at run %ld: %d x %d -> %s", vxa_since_load, w, h, vxa_snap_path);
}

/* ---------------------------------------------------------------- the commands from the Java side: run on the core's thread */
enum { C_NONE, C_LOAD, C_RESET, C_EJECT, C_KEY, C_STATE_SAVE, C_STATE_LOAD, C_POWER, C_TURN, C_NET };
typedef struct { int what, a, b; double d; char s[1100]; } vxa_cmd;
#define VXA_NCMD 32
static vxa_cmd vxa_cq[VXA_NCMD]; static int vxa_cqr, vxa_cqw; static pthread_mutex_t vxa_cmx = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t vxa_ccv = PTHREAD_COND_INITIALIZER; static int vxa_cres, vxa_cdone;
static void vxa_cmd_put (int what, int a, int b, double d, const char *s)
{
	pthread_mutex_lock (&vxa_cmx);
	if ((vxa_cqw + 1) % VXA_NCMD != vxa_cqr) { vxa_cmd *c = &vxa_cq[vxa_cqw]; c->what = what; c->a = a; c->b = b; c->d = d; snprintf (c->s, sizeof c->s, "%s", s ? s : ""); vxa_cqw = (vxa_cqw + 1) % VXA_NCMD; }
	pthread_mutex_unlock (&vxa_cmx);
}
extern void vx_app_load (const char *path), vx_app_reset (void), vx_app_eject (void), vx_app_power (void), vx_app_turn (double d);
extern const char *vx_app_game (void); extern int vx_app_browser_wanted (void);
static int vxa_state_file (int save, int slot);
static void vxa_cmd_run (void)
{
	for (;;) {
		pthread_mutex_lock (&vxa_cmx); if (vxa_cqr == vxa_cqw) { pthread_mutex_unlock (&vxa_cmx); return; }
		vxa_cmd c = vxa_cq[vxa_cqr]; vxa_cqr = (vxa_cqr + 1) % VXA_NCMD; pthread_mutex_unlock (&vxa_cmx);
		int res = 0;
		switch (c.what) {
		case C_LOAD: vlog ("load %s", c.s); vx_app_load (c.s); vxa_since_load = -1; res = 1; break;   /* (-1: the switch happens in the next run) */
		case C_RESET: vx_app_reset (); break;
		case C_EJECT: vx_app_eject (); break;
		case C_POWER: vx_app_power (); break;
		case C_TURN: vx_app_turn (c.d); break;
		case C_NET: { extern void vx_net_prepare (void), vx_net_go (void), vx_net_prepare_hot (void);   /* (online: in order with the game's load - the go before it refused the load) */
			if (c.a == 1) vx_net_prepare (); else if (c.a == 2) vx_net_go (); else if (c.a == 8) vx_net_prepare_hot (); break; }
		case C_KEY: { const unsigned k = (unsigned) c.a; if (k < RETROK_LAST) { vxa_kb[k] = c.b & 1 ? 1 : 0;
			if (vxa_kbcb) vxa_kbcb (c.b & 1, k, (uint32_t) (c.b >> 8), (uint16_t) ((c.b >> 1) & 0x7f)); } break; }
		case C_STATE_SAVE: res = vxa_state_file (1, c.a); break;
		case C_STATE_LOAD: res = vxa_state_file (0, c.a); break;
		}
		if (c.what == C_STATE_SAVE || c.what == C_STATE_LOAD) { pthread_mutex_lock (&vxa_cmx); vxa_cres = res; vxa_cdone++; pthread_cond_broadcast (&vxa_ccv); pthread_mutex_unlock (&vxa_cmx); }
	}
}
static char vxa_states[1100];
static void vxa_state_path (char *out, size_t n, int slot)
{
	const char *g = vx_app_game (); const char *b = g ? strrchr (g, '/') : NULL; b = b ? b + 1 : (g ? g : "none");
	char base[400]; snprintf (base, sizeof base, "%s", b); char *dot = strrchr (base, '.'); if (dot) *dot = 0;
	snprintf (out, n, "%s/%s.state%d", vxa_states, base, slot);
}
static int vxa_state_file (int save, int slot)
{
	size_t n = retro_serialize_size (); if (!n) { vlog ("state: this game has none"); return 0; }
	char p[1600]; vxa_state_path (p, sizeof p, slot); int ok = 0; void *d = malloc (n); if (!d) return 0;
	if (save) { ok = retro_serialize (d, n); if (ok) { char t[1610]; snprintf (t, sizeof t, "%s.new", p); FILE *f = fopen (t, "wb"); ok = f && fwrite (d, 1, n, f) == n; if (f) fclose (f); if (ok) rename (t, p); } }
	else { FILE *f = fopen (p, "rb"); ok = f && fread (d, 1, n, f) == n; if (f) fclose (f); if (ok) ok = retro_unserialize (d, n); }
	free (d); vlog ("state %s slot %d: %s (%zu bytes)", save ? "save" : "load", slot, ok ? "done" : "FAILED", n); return ok;
}

/* ---------------------------------------------------------------- the core's thread: start, then a refresh each vsync */
static pthread_t vxa_th; static volatile int vxa_quit, vxa_running = 1, vxa_started, vxa_ok;
static char vxa_game0[1100]; static volatile int vxa_browser;     /* (the core's ROM browser asked for: the library instead) */
static volatile int vxa_stat_rps, vxa_stat_pps, vxa_stat_ms;
extern int vx_app_noshape, vx_app_osd_on;
/* the game's thread on the fast cores: Android runs a plain thread wherever it likes - on the Nova (Snapdragon 8 Gen 2) the
 * slow cores took 15-16 ms of a 16.7 ms refresh for Armor Attack (the iPad: 4); RetroArch's game thread is kept on the
 * fast ones. The cores near the top clock speed; the threads the core starts from here (UVMC2 / PiTrex, the drawing) follow */
static int vxa_tid;
static cpu_set_t vxa_top_set; static int vxa_top_ok;   /* (the fastest cores: the emulation thread's - UVMC2 / PiTrex) */
static void vxa_fast_cores (void)
{
	long mx[64]; int n = 0; long top = 0;
	for (int c = 0; c < 64; c++) { char p[96]; snprintf (p, sizeof p, "/sys/devices/system/cpu/cpu%d/cpufreq/cpuinfo_max_freq", c);
		FILE *f = fopen (p, "r"); if (!f) { mx[c] = 0; if (c > 15) break; continue; } long v = 0; if (fscanf (f, "%ld", &v) != 1) v = 0; fclose (f); mx[c] = v; n = c + 1; if (v > top) top = v; }
	if (!top || n < 2) { vlog ("cores: their speeds unknown - left to Android"); return; }
	cpu_set_t set; CPU_ZERO (&set); int k = 0; char list[200] = ""; size_t o = 0;
	for (int c = 0; c < n; c++) if (mx[c] >= top * 3 / 4) { CPU_SET (c, &set); k++; o += (size_t) snprintf (list + o, o < sizeof list ? sizeof list - o : 0, "%s%d", o ? "," : "", c); }
	CPU_ZERO (&vxa_top_set); for (int c = 0; c < n; c++) if (mx[c] == top) CPU_SET (c, &vxa_top_set); vxa_top_ok = 1;
	const int r = sched_setaffinity (0, sizeof set, &set);
	vlog ("cores: the game's thread on %d fast ones (%s, up to %ld MHz)%s", k, list, top / 1000, r ? " - not allowed" : "");
}
/* (Android 13+: the game thread says how long each refresh's work took - the clocks follow) */
static int vxa_emu_tid; static double vxa_hint_fps;
typedef void *(*vxa_ph_mgr_t) (void); typedef void *(*vxa_ph_new_t) (void *, const int32_t *, size_t, int64_t); typedef int (*vxa_ph_rep_t) (void *, int64_t);
static void *vxa_hint; static vxa_ph_rep_t vxa_ph_rep;
static void vxa_perf_hint (double fps)
{
	vxa_hint_fps = fps;
	void *l = dlopen ("libandroid.so", RTLD_NOW); if (!l) return;
	vxa_ph_mgr_t mg = (vxa_ph_mgr_t) dlsym (l, "APerformanceHint_getManager"); vxa_ph_new_t mk = (vxa_ph_new_t) dlsym (l, "APerformanceHint_createSession");
	vxa_ph_rep = (vxa_ph_rep_t) dlsym (l, "APerformanceHint_reportActualWorkDuration");
	if (!mg || !mk || !vxa_ph_rep) { vlog ("performance hints: not on this Android"); return; }
	void *m = mg (); int32_t t[2] = { vxa_tid, vxa_emu_tid }; const int nt = vxa_emu_tid ? 2 : 1;
	if (vxa_hint) { void (*cl) (void *) = (void (*) (void *)) dlsym (l, "APerformanceHint_closeSession"); if (cl) cl (vxa_hint); vxa_hint = NULL; }
	vxa_hint = m ? mk (m, t, nt, (int64_t) (1e9 / (fps > 0 ? fps : 60))) : NULL;
	vlog ("performance hints: %s%s", vxa_hint ? "on" : "not given", nt > 1 ? " (the emulation thread too)" : "");
}
/* the app's busiest threads over the last stat period (/proc/self/task: user+system ticks) - what loads the processor */
#include <dirent.h>
#define VXA_TH 96
static int vxa_th_id[VXA_TH]; static long vxa_th_t[VXA_TH]; static int vxa_th_n;
static void vxa_threads (double secs)
{
	int id[VXA_TH]; long tk[VXA_TH]; char nm[VXA_TH][20]; int n = 0; long all = 0;
	DIR *d = opendir ("/proc/self/task"); if (!d) return; struct dirent *de;
	while ((de = readdir (d)) && n < VXA_TH) { if (de->d_name[0] < '0' || de->d_name[0] > '9') continue;
		char f[64], b[512]; snprintf (f, sizeof f, "/proc/self/task/%s/stat", de->d_name); int fd = open (f, O_RDONLY); if (fd < 0) continue;
		int r = (int) read (fd, b, sizeof b - 1); close (fd); if (r <= 0) continue; b[r] = 0;
		char *o = strchr (b, '('), *c = strrchr (b, ')'); if (!o || !c) continue;
		int l = (int) (c - o - 1); if (l > 19) l = 19; memcpy (nm[n], o + 1, l); nm[n][l] = 0;
		long ut = 0, st = 0; if (sscanf (c + 2, "%*c %*d %*d %*d %*d %*d %*u %*u %*u %*u %*u %ld %ld", &ut, &st) != 2) continue;
		id[n] = atoi (de->d_name); tk[n] = ut + st;
		if (!strcmp (nm[n], "vx-emu") && id[n] != vxa_emu_tid) { vxa_emu_tid = id[n];   /* (the emulation's own thread: on the fastest core, in the hints) */
			const int e = vxa_top_ok ? sched_setaffinity (vxa_emu_tid, sizeof vxa_top_set, &vxa_top_set) : -1;
			vlog ("cores: the emulation thread on the fastest%s", e ? " - not allowed" : ""); if (vxa_hint) vxa_perf_hint (vxa_hint_fps); }
		n++; }
	closedir (d);
	long dl[VXA_TH]; for (int i = 0; i < n; i++) { dl[i] = 0; for (int j = 0; j < vxa_th_n; j++) if (vxa_th_id[j] == id[i]) { dl[i] = tk[i] - vxa_th_t[j]; break; } all += dl[i]; }
	const int first = vxa_th_n == 0; vxa_th_n = n; memcpy (vxa_th_id, id, n * sizeof *id); memcpy (vxa_th_t, tk, n * sizeof *tk);
	if (first || secs <= 0) return;
	const double hz = sysconf (_SC_CLK_TCK); char out[400]; int k = snprintf (out, sizeof out, "threads: all %.0f%% of a core", all / hz / secs * 100);
	for (int m = 0; m < 6; m++) { int bi = -1; for (int i = 0; i < n; i++) if (dl[i] > 0 && (bi < 0 || dl[i] > dl[bi])) bi = i; if (bi < 0) break;
		k += snprintf (out + k, sizeof out - k, ", %s %.0f%%", nm[bi], dl[bi] / hz / secs * 100); dl[bi] = 0; if (k >= (int) sizeof out - 30) break; }
	vlog ("%s", out);
}
static void *vxa_main (void *u)
{
	(void) u; vxa_tid = gettid (); vxa_fast_cores (); pthread_setname_np (pthread_self (), "vx-game");
	if (!vxa_egl_init ()) { vxa_ok = -1; return NULL; }
	vxa_present_setup ();
	vx_app_noshape = 1;                                  /* (the app places the picture and the deck: no bars to the screen's shape) */
	vx_app_osd_on = 1;
	retro_set_environment (vxa_env);
	retro_set_video_refresh (vxa_video); retro_set_audio_sample (vxa_audio_sample); retro_set_audio_sample_batch (vxa_audio_batch);
	retro_set_input_poll (vxa_poll); retro_set_input_state (vxa_input_state);
	retro_init ();
	/* (the window first: the core measures the screen at its start - the EGL surface current) */
	for (int i = 0; i < 300 && !vxa_win_changed && !vxa_quit; i++) usleep (10000);
	vxa_surface_update (); vxa_hdr_update ();
	struct retro_game_info gi = { vxa_game0, NULL, 0, NULL };
	bool ok = retro_load_game (&gi);
	vlog ("game %s: %s", vxa_game0, ok ? "loaded" : "FAILED");
	if (!ok) { vxa_ok = -1; return NULL; }
	retro_get_system_av_info (&vxa_av);
	vlog ("av: %u x %u (max %u x %u), aspect %.3f, %.2f Hz, sound %.0f Hz", vxa_av.geometry.base_width, vxa_av.geometry.base_height, vxa_av.geometry.max_width, vxa_av.geometry.max_height, vxa_av.geometry.aspect_ratio, vxa_av.timing.fps, vxa_av.timing.sample_rate);
	if (vxa_hw_on) { vxa_core_fb (vxa_av.geometry.max_width, vxa_av.geometry.max_height); if (vxa_hw.context_reset) vxa_hw.context_reset (); vlog ("GL context handed to the core"); }
	vxa_audio_start (vxa_av.timing.sample_rate);
	vxa_perf_hint (vxa_av.timing.fps);
	vxa_ok = 1;
	double last = 0, acc = 0, stat_t = vxa_now (), t_run = 0, t_max = 0, t_cpu = 0; long runs = 0, pres = 0, ticks = 0, late = 0; int was_running = 1;
	while (!vxa_quit) {
		vxa_surface_update ();
		vxa_cmd_run ();
		if (!vxa_running || vxa_surf == EGL_NO_SURFACE) {   /* (in the background: nothing runs; the sound stops) */
			if (was_running) { was_running = 0; if (vxa_as) AAudioStream_requestPause (vxa_as); vlog ("paused (the app in the background)"); }
			usleep (20000); last = 0; continue; }
		if (!was_running) { was_running = 1; if (vxa_as) AAudioStream_requestStart (vxa_as); vlog ("resumed"); }
		vxa_audio_check ();
		const double now = vxa_now (); double dt = last ? now - last : 0; last = now; ticks++;
		const double fps = vxa_av.timing.fps > 0 ? vxa_av.timing.fps : 60;
		if (dt > 1.5 / fps) late++;
		if (dt > 0.25) dt = 0.25;
		acc += dt * fps; int n = (int) acc; acc -= n;
		const double buf = vxa_ring_fill () / (vxa_av.timing.sample_rate > 0 ? vxa_av.timing.sample_rate : 44100);   /* (the sound decides: never far ahead, never dry) */
		extern int vx_net_playing_now (void); const int onl = vx_net_playing_now ();
		if (buf > 0.12 && n > 0) n--; else if (buf < 0.03 && n < 3 && !onl) n++;
		if (n > (onl ? 2 : 4)) n = onl ? 2 : 4;            /* (online: the lockstep waits - no runs to fill the dry sound, as the iOS app) */
		if (vxa_snap_n >= 0) n = 1;                         /* (a snapshot asked for: every run drawn) */
		vxa_hdr_update ();
		if (!vxa_holding) {
			const double t0 = vxa_now ();
			struct timespec c0, c1; clock_gettime (CLOCK_THREAD_CPUTIME_ID, &c0);
			{ extern int vx_owe_hidden; vx_owe_hidden = 1; }   /* (a catch-up run's game frame: drawn with the next picture - no gap in a trail) */
			for (int i = 0; i < n; i++) { vxa_nodraw = i < n - 1; vxa_input_poll (); retro_run (); runs++; vxa_since_load++; }
			clock_gettime (CLOCK_THREAD_CPUTIME_ID, &c1); t_cpu += (c1.tv_sec - c0.tv_sec) + (c1.tv_nsec - c0.tv_nsec) * 1e-9;   /* (the thread's own processor time: waiting for the GPU is not) */
			vxa_nodraw = 0; const double tr = vxa_now () - t0; t_run += tr; if (tr > t_max) t_max = tr;
			if (vx_app_browser_wanted ()) { vlog ("the core's ROM browser asked for: the library instead"); vxa_browser = 1; } }
		vxa_present (); pres++;
		if (vxa_hint && vxa_ph_rep) vxa_ph_rep (vxa_hint, (int64_t) ((vxa_now () - now) * 1e9));   /* (this refresh's work: the hint) */
		if (vxa_snap_n >= 0 && vxa_since_load >= vxa_snap_n) { vxa_snap_take (); vxa_snap_n = -1; }
		eglSwapBuffers (vxa_dpy, vxa_surf);
		if (now - stat_t >= 5) { const double t = now - stat_t; stat_t = now;
			vxa_stat_rps = (int) (runs / t); vxa_stat_pps = (int) (pres / t); vxa_stat_ms = ticks ? (int) (t_run * 1000 / ticks) : 0;
			vlog ("stat: %.0f runs/s, %.0f pictures/s, emulator %.1f ms a refresh (its processor %.1f ms; longest %.1f), %ld late of %ld, sound %.0f ms, %u dry, %u dropped%s",
				runs / t, pres / t, ticks ? t_run * 1000 / ticks : 0, ticks ? t_cpu * 1000 / ticks : 0, t_max * 1000, late, ticks, buf * 1000, vxa_under, vxa_drop, vxa_holding ? ", held" : "");
			vxa_threads (t);
			{ extern int vx_net_diag (char *, int), vx_net_playing_now (void); if (vx_net_playing_now ()) { char d[600]; vx_net_diag (d, sizeof d); vlog ("online: %s", d); } }   /* (the lockstep's figures: the delay, ahead, waits) */
			runs = pres = ticks = late = 0; t_run = t_max = t_cpu = 0; vxa_under = vxa_drop = 0; }
	}
	vlog ("the core ends"); if (vxa_as) { AAudioStream_requestStop (vxa_as); AAudioStream_close (vxa_as); vxa_as = NULL; }
	retro_unload_game (); retro_deinit ();
	return NULL;
}

/* ---------------------------------------------------------------- the content pack: .tgz unpacked into a folder */
typedef struct { const char *root; FILE *f; unsigned char hdr[512]; int hn, mode, files; uint64_t left, pad; char pax[1024]; char *pb; size_t pn, pcap; int bad; } vxa_untar;
static uint64_t vxa_oct (const unsigned char *s, int n) { uint64_t v = 0; for (int i = 0; i < n && s[i]; i++) if (s[i] >= '0' && s[i] <= '7') v = v * 8 + (uint64_t) (s[i] - '0'); return v; }
static void vxa_mkdirs (const char *path, int last)
{
	char b[1400]; snprintf (b, sizeof b, "%s", path);
	for (char *p = b + 1; *p; p++) if (*p == '/') { *p = 0; mkdir (b, 0755); *p = '/'; }
	if (last) mkdir (b, 0755);
}
static void vxa_untar_feed (vxa_untar *u, const unsigned char *d, size_t n)
{
	while (n) {
		if (u->mode == 0) {
			size_t k = 512 - (size_t) u->hn; if (k > n) k = n; memcpy (u->hdr + u->hn, d, k); u->hn += (int) k; d += k; n -= k;
			if (u->hn < 512) return; u->hn = 0;
			int zero = 1; for (int i = 0; i < 512; i++) if (u->hdr[i]) { zero = 0; break; } if (zero) continue;
			uint64_t size = vxa_oct (u->hdr + 124, 12); char type = (char) u->hdr[156];
			char name[1100];
			if (u->pax[0]) { snprintf (name, sizeof name, "%s", u->pax); u->pax[0] = 0; }
			else if (u->hdr[345]) snprintf (name, sizeof name, "%.155s/%.100s", (const char *) u->hdr + 345, (const char *) u->hdr);
			else snprintf (name, sizeof name, "%.100s", (const char *) u->hdr);
			u->left = size; u->pad = (512 - size % 512) % 512; u->f = NULL;
			if (type == 'x' || type == 'g' || type == 'L') { u->mode = 3; u->pn = 0; continue; }
			if (strstr (name, "..") || name[0] == '/') { u->bad++; u->mode = size ? 1 : (u->pad ? 2 : 0); continue; }
			char full[1400]; snprintf (full, sizeof full, "%s/%s", u->root, name);
			if (type == '5') { vxa_mkdirs (full, 1); u->mode = 0; continue; }
			if (type == '0' || type == 0) { vxa_mkdirs (full, 0); u->f = fopen (full, "wb"); u->files++; if (!u->f) u->bad++; }
			u->mode = size ? 1 : (u->pad ? 2 : 0); if (!size && u->f) { fclose (u->f); u->f = NULL; }
		} else if (u->mode == 1) {
			size_t k = n < u->left ? n : (size_t) u->left; if (u->f) fwrite (d, 1, k, u->f); d += k; n -= k; u->left -= k;
			if (!u->left) { if (u->f) { fclose (u->f); u->f = NULL; } u->mode = u->pad ? 2 : 0; }
		} else if (u->mode == 2) {
			size_t k = n < u->pad ? n : (size_t) u->pad; d += k; n -= k; u->pad -= k; if (!u->pad) u->mode = 0;
		} else {
			size_t k = n < u->left ? n : (size_t) u->left;
			if (u->pn + k + 1 > u->pcap) { u->pcap = (u->pn + k + 1) * 2; u->pb = realloc (u->pb, u->pcap); }
			memcpy (u->pb + u->pn, d, k); u->pn += k; d += k; n -= k; u->left -= k;
			if (!u->left) {
				u->pb[u->pn] = 0; char type = (char) u->hdr[156];
				if (type == 'L') snprintf (u->pax, sizeof u->pax, "%s", u->pb);
				else if (type == 'x') { const char *p = strstr (u->pb, " path="); if (p) { p += 6; const char *e = strchr (p, '\n'); snprintf (u->pax, sizeof u->pax, "%.*s", (int) (e ? e - p : (long) strlen (p)), p); } }
				u->mode = u->pad ? 2 : 0; }
		}
	}
}
static int vxa_untgz (const char *tgz, const char *root, int *files)
{
	FILE *f = fopen (tgz, "rb"); if (!f) return -1;
	z_stream z; memset (&z, 0, sizeof z); if (inflateInit2 (&z, 16 + MAX_WBITS) != Z_OK) { fclose (f); return -2; }
	vxa_untar u; memset (&u, 0, sizeof u); u.root = root;
	unsigned char *in = malloc (1 << 20), *out = malloc (1 << 20); int r = Z_OK;
	while (r != Z_STREAM_END) {
		if (!z.avail_in) { z.avail_in = (uInt) fread (in, 1, 1 << 20, f); z.next_in = in; if (!z.avail_in) break; }
		z.next_out = out; z.avail_out = 1 << 20; r = inflate (&z, Z_NO_FLUSH);
		if (r != Z_OK && r != Z_STREAM_END) break;
		vxa_untar_feed (&u, out, (1 << 20) - z.avail_out);
	}
	inflateEnd (&z); fclose (f); free (in); free (out); free (u.pb); if (u.f) fclose (u.f);
	*files = u.files; return r == Z_STREAM_END && !u.bad ? 0 : -3;
}

/* ---------------------------------------------------------------- JNI: what the Java side (VX.java) calls */
#define JF(name) Java_com_hugob_vectrexemu_VX_##name
static jstring js (JNIEnv *e, const char *s) { return (*e)->NewStringUTF (e, s ? s : ""); }
#define JSTR(var, j) const char *var = (j) ? (*e)->GetStringUTFChars (e, j, NULL) : NULL
#define JREL(var, j) if (var) (*e)->ReleaseStringUTFChars (e, j, var)
JNIEXPORT void JNICALL JF(logStart) (JNIEnv *e, jclass c, jstring files)
{
	(void) c; JSTR (f, files); snprintf (vxa_files, sizeof vxa_files, "%s", f); JREL (f, files);
	char p[1100], q[1110]; snprintf (p, sizeof p, "%s/applog.txt", vxa_files); snprintf (q, sizeof q, "%s/applog_previous.txt", vxa_files); rename (p, q);
	vxa_lfd = open (p, O_WRONLY | O_CREAT | O_TRUNC, 0644);
}
JNIEXPORT void JNICALL JF(log) (JNIEnv *e, jclass c, jstring s) { (void) c; JSTR (t, s); vlog ("%s", t ? t : ""); JREL (t, s); }
JNIEXPORT jstring JNICALL JF(takeLog) (JNIEnv *e, jclass c)
{
	(void) c; pthread_mutex_lock (&vxa_lmx); vxa_pend[vxa_pendn < sizeof vxa_pend ? vxa_pendn : sizeof vxa_pend - 1] = 0;
	jstring r = js (e, vxa_pend); vxa_pendn = 0; pthread_mutex_unlock (&vxa_lmx); return r;
}
JNIEXPORT void JNICALL JF(start) (JNIEnv *e, jclass c, jstring sys, jstring saves, jstring game, jstring defs)
{
	(void) c; if (vxa_started) return; vxa_started = 1;
	JSTR (s1, sys); JSTR (s2, saves); JSTR (s3, game); JSTR (s4, defs);
	snprintf (vxa_sys, sizeof vxa_sys, "%s", s1); snprintf (vxa_saves, sizeof vxa_saves, "%s", s2); snprintf (vxa_game0, sizeof vxa_game0, "%s", s3); snprintf (vxa_odefs, sizeof vxa_odefs, "%s", s4 ? s4 : "");
	JREL (s1, sys); JREL (s2, saves); JREL (s3, game); JREL (s4, defs);
	snprintf (vxa_states, sizeof vxa_states, "%s/states", vxa_files); vxa_mkdirs (vxa_states, 1); vxa_mkdirs (vxa_saves, 1);
	{ char p[1200]; snprintf (p, sizeof p, "%s/vectrex_emu/vectrex_emu_app.log", vxa_sys); vxa_lfd2 = open (p, O_WRONLY | O_CREAT | O_TRUNC, 0644);   /* (this run's app log there too, from the start) */
	  if (vxa_lfd2 >= 0) { char q[1100]; snprintf (q, sizeof q, "%s/applog.txt", vxa_files); FILE *f = fopen (q, "r"); if (f) { char b[4096]; size_t k; while ((k = fread (b, 1, sizeof b, f)) > 0) write (vxa_lfd2, b, k); fclose (f); } } }
	vlog ("start: system %s, saves %s, game %s", vxa_sys, vxa_saves, vxa_game0);
	pthread_attr_t a; pthread_attr_init (&a); pthread_attr_setstacksize (&a, 8 << 20);
	pthread_create (&vxa_th, &a, vxa_main, NULL);
}
JNIEXPORT jint JNICALL JF(status) (JNIEnv *e, jclass c) { (void) e; (void) c; return vxa_ok; }
JNIEXPORT void JNICALL JF(setSurface) (JNIEnv *e, jclass c, jobject surface)
{
	(void) c; ANativeWindow *w = surface ? ANativeWindow_fromSurface (e, surface) : NULL;
	if (vxa_win_new) ANativeWindow_release (vxa_win_new); vxa_win_new = w; vxa_win_changed = 1;
	if (!surface) for (int i = 0; i < 100 && vxa_win_changed && vxa_ok == 1; i++) usleep (5000);   /* (the surface going: let go of it before it is gone) */
}
JNIEXPORT void JNICALL JF(setRoom) (JNIEnv *e, jclass c, jint x, jint y, jint w, jint h, jboolean top) { (void) e; (void) c; vxa_ax = x; vxa_ay = y; vxa_aw = w; vxa_ah = h; vxa_top_align = top; }
JNIEXPORT jfloatArray JNICALL JF(picRect) (JNIEnv *e, jclass c)
{
	(void) c; jfloatArray a = (*e)->NewFloatArray (e, 4); float v[4] = { vxa_px, vxa_py, vxa_pw, vxa_ph }; (*e)->SetFloatArrayRegion (e, a, 0, 4, v); return a;
}
JNIEXPORT void JNICALL JF(setLandscape) (JNIEnv *e, jclass c, jboolean on) { (void) e; (void) c; if (vxa_land != on) { vxa_land = on; vxa_opt_dirty = 1; } }
JNIEXPORT void JNICALL JF(setRunning) (JNIEnv *e, jclass c, jboolean on) { (void) e; (void) c; vxa_running = on; }
JNIEXPORT void JNICALL JF(setHold) (JNIEnv *e, jclass c, jboolean on) { (void) e; (void) c; if (vxa_holding != on) { vxa_holding = on; vlog ("game %s", on ? "held (a screen over it)" : "running again"); } }
JNIEXPORT void JNICALL JF(setHdr) (JNIEnv *e, jclass c, jboolean can, jfloat headroom, jfloat frac) { (void) e; (void) c; vxa_want_hdr = can; vxa_headroom = headroom; vxa_hdr_frac = frac; }
JNIEXPORT void JNICALL JF(setTrailMul) (JNIEnv *e, jclass c, jfloat k) { (void) e; (void) c; extern float vx_trail_mul; vx_trail_mul = k; }
JNIEXPORT void JNICALL JF(testCart) (JNIEnv *e, jclass c, jstring path) { (void) c; JSTR (p, path); if (p) vx_testcart_write (p); JREL (p, path); }   /* (the Test Cart, written into roms/Tests) */
JNIEXPORT void JNICALL JF(pitrexTiles) (JNIEnv *e, jclass c, jstring thumbs) { (void) c; JSTR (p, thumbs); if (p) vx_pitrex_tiles_write (p); JREL (p, thumbs); }   /* (the PiTrex's cartridge and box, written into the thumbs folder) */
unsigned char *vx_genart_make (int look, const char *name, int *w, int *h); void vx_genart_free (void *p);   /* (libretro/genart.c, in the core) */
unsigned char *vx_genart_make_w (int look, const char *name, int width, int *w, int *h); unsigned char *vx_genart_label_w (const char *path, int width, int *w, int *h);
unsigned char *vx_genart_fit (const char *path, int maxw, int *w, int *h); int vx_genart_size (int px);
/* (genart.c's RGBA as { width, height, the pixels as ARGB } - Bitmap's colours; the pixels freed) */
static jintArray ga_argb (JNIEnv *e, unsigned char *px, int w, int h)
{
	jintArray r = NULL; if (!px) return NULL;
	const size_t n = (size_t) w * (size_t) h; jint *a = malloc ((n + 2) * sizeof (jint));
	if (a) { a[0] = w; a[1] = h;
		for (size_t i = 0; i < n; i++) { const unsigned char *q = px + i * 4; a[i + 2] = (jint) ((uint32_t) q[3] << 24 | (uint32_t) q[0] << 16 | (uint32_t) q[1] << 8 | (uint32_t) q[2]); }
		r = (*e)->NewIntArray (e, (jsize) (n + 2)); if (r) (*e)->SetIntArrayRegion (e, r, 0, (jsize) (n + 2), a);
		free (a); }
	vx_genart_free (px); return r;
}
/* (a cartridge (0) or box (1) for a game with no picture of its own, width px wide) */
JNIEXPORT jintArray JNICALL JF(genart) (JNIEnv *e, jclass c, jint look, jstring name, jint width)
{
	(void) c; jintArray r = NULL; JSTR (p, name);
	if (p) { int w = 0, h = 0; unsigned char *px = vx_genart_make_w ((int) look, p, (int) width, &w, &h); r = ga_argb (e, px, w, h); }
	JREL (p, name); return r;
}
/* (the cartridge with the player's own label, thumbs/labels/<game>.png) */
JNIEXPORT jintArray JNICALL JF(genlabel) (JNIEnv *e, jclass c, jstring path, jint width)
{
	(void) c; jintArray r = NULL; JSTR (p, path);
	if (p) { int w = 0, h = 0; unsigned char *px = vx_genart_label_w (p, (int) width, &w, &h); r = ga_argb (e, px, w, h); }
	JREL (p, path); return r;
}
JNIEXPORT jint JNICALL JF(gensize) (JNIEnv *e, jclass c, jint px) { (void) e; (void) c; return vx_genart_size ((int) px); }
/* (a picture of the player's own (cartridge, box, overlay) made at most maxw wide, once) */
JNIEXPORT jintArray JNICALL JF(genfit) (JNIEnv *e, jclass c, jstring path, jint maxw)
{
	(void) c; jintArray r = NULL; JSTR (p, path);
	if (p) { int w = 0, h = 0; unsigned char *px = vx_genart_fit (p, (int) maxw, &w, &h); r = ga_argb (e, px, w, h); }
	JREL (p, path); return r;
}
/* (the same two from a picture's bytes - PNG or JPEG: the ones that come with the app, in its assets (shipped/)) */
unsigned char *vx_genart_label_png_w (const unsigned char *png, int len, int width, int *w, int *h); unsigned char *vx_genart_fit_png (const unsigned char *png, int len, int maxw, int *w, int *h);
static jintArray ga_bytes (JNIEnv *e, jbyteArray b, int label, int width)
{
	jintArray r = NULL; if (!b) return NULL; const jsize n = (*e)->GetArrayLength (e, b); jbyte *p = (*e)->GetByteArrayElements (e, b, NULL); if (!p) return NULL;
	int w = 0, h = 0; unsigned char *px = label ? vx_genart_label_png_w ((const unsigned char *) p, (int) n, width, &w, &h) : vx_genart_fit_png ((const unsigned char *) p, (int) n, width, &w, &h);
	(*e)->ReleaseByteArrayElements (e, b, p, JNI_ABORT); r = ga_argb (e, px, w, h); return r;
}
JNIEXPORT jintArray JNICALL JF(genlabelb) (JNIEnv *e, jclass c, jbyteArray b, jint width) { (void) c; return ga_bytes (e, b, 1, (int) width); }
JNIEXPORT jintArray JNICALL JF(genfitb) (JNIEnv *e, jclass c, jbyteArray b, jint maxw) { (void) c; return ga_bytes (e, b, 0, (int) maxw); }
JNIEXPORT jint JNICALL JF(palette) (JNIEnv *e, jclass c) { (void) e; (void) c; extern int vx_palette; return vx_palette; }   /* (the colour scheme: 0 blue, 1 green) */
JNIEXPORT void JNICALL JF(setBright) (JNIEnv *e, jclass c, jint on) { (void) e; (void) c; extern float vx_bright; vx_bright = on ? 1.0f : 0.0f; { extern int vx_phone_trail; vx_phone_trail = 1; }   /* (the afterglow's shape for the apps) */ vlog ("look %s", on ? "Bright" : "Vectrex"); }   /* (the look: Vectrex, or Bright - lifted and wider) */
JNIEXPORT void JNICALL JF(setHdrLook) (JNIEnv *e, jclass c, jint look) { (void) e; (void) c; extern float vx_hdr_lift; extern int vx_app_dots; vx_hdr_lift = (float) look; vx_app_dots = 1; vlog ("HDR look %d", (int) look); }
JNIEXPORT void JNICALL JF(snap) (JNIEnv *e, jclass c, jint n, jstring path) { (void) c; JSTR (s, path); snprintf (vxa_snap_path, sizeof vxa_snap_path, "%s", s ? s : ""); JREL (s, path); vx_app_pich = 0; vxa_snap_fixed = 1; vxa_snap_n = n; }   /* (the picture at the size the options give - Picture size - as any frontend) */
JNIEXPORT jint JNICALL JF(hdrActive) (JNIEnv *e, jclass c) { (void) e; (void) c; return vxa_hdr_surf == 1; }
JNIEXPORT void JNICALL JF(setPad) (JNIEnv *e, jclass c, jint port, jint btn, jint lx, jint ly, jint rx, jint ry)
{
	(void) e; (void) c; if (port < 0 || port > 1) return; pthread_mutex_lock (&vxa_imx);
	vxa_pads[port].btn = (uint32_t) btn; vxa_pads[port].ax[0][0] = (int16_t) lx; vxa_pads[port].ax[0][1] = (int16_t) ly; vxa_pads[port].ax[1][0] = (int16_t) rx; vxa_pads[port].ax[1][1] = (int16_t) ry;
	pthread_mutex_unlock (&vxa_imx);
}
JNIEXPORT void JNICALL JF(setTouchPad) (JNIEnv *e, jclass c, jint btn, jint ax, jint ay)
{ (void) e; (void) c; pthread_mutex_lock (&vxa_imx); vxa_tpad.btn = (uint32_t) btn; vxa_tpad.ax[0][0] = (int16_t) ax; vxa_tpad.ax[0][1] = (int16_t) ay; pthread_mutex_unlock (&vxa_imx); }
JNIEXPORT void JNICALL JF(setPointer) (JNIEnv *e, jclass c, jboolean on, jfloat x, jfloat y)
{ (void) e; (void) c; pthread_mutex_lock (&vxa_imx); vxa_ptr.on = on; vxa_ptr.x = x; vxa_ptr.y = y; pthread_mutex_unlock (&vxa_imx); }
JNIEXPORT void JNICALL JF(key) (JNIEnv *e, jclass c, jint k, jboolean down, jint ch, jint mods) { (void) e; (void) c; vxa_cmd_put (C_KEY, k, (down ? 1 : 0) | ((mods & 0x7f) << 1) | (ch << 8), 0, NULL); }
JNIEXPORT jstring JNICALL JF(takeMessage) (JNIEnv *e, jclass c)
{
	(void) c; static int seen; pthread_mutex_lock (&vxa_lmx); jstring r = NULL; if (vxa_msg_n != seen) { seen = vxa_msg_n; r = js (e, vxa_msg); } pthread_mutex_unlock (&vxa_lmx); return r;
}
JNIEXPORT jboolean JNICALL JF(takeBrowser) (JNIEnv *e, jclass c) { (void) e; (void) c; if (!vxa_browser) return 0; vxa_browser = 0; return 1; }
JNIEXPORT jintArray JNICALL JF(stats) (JNIEnv *e, jclass c)
{
	(void) c; jintArray a = (*e)->NewIntArray (e, 3); jint v[3] = { vxa_stat_rps, vxa_stat_pps, vxa_stat_ms }; (*e)->SetIntArrayRegion (e, a, 0, 3, v); return a;
}
/* (the settings) */
JNIEXPORT jint JNICALL JF(nopts) (JNIEnv *e, jclass c) { (void) e; (void) c; return vxa_nopt; }
JNIEXPORT jstring JNICALL JF(opt) (JNIEnv *e, jclass c, jint i, jint what)
{
	(void) c; if (i < 0 || i >= vxa_nopt) return js (e, ""); pthread_mutex_lock (&vxa_omx); char b[1300];
	switch (what) { case 0: snprintf (b, sizeof b, "%s", vxa_opt[i].key); break; case 1: snprintf (b, sizeof b, "%s", vxa_opt[i].desc); break;
	case 2: snprintf (b, sizeof b, "%s", vxa_opt[i].cat); break; case 3: snprintf (b, sizeof b, "%s", vxa_opt[i].info); break; case 4: snprintf (b, sizeof b, "%s", vxa_opt[i].vals); break;
	case 5: snprintf (b, sizeof b, "%s", vxa_opt[i].val); break; default: snprintf (b, sizeof b, "%s", vxa_opt[i].def); }
	pthread_mutex_unlock (&vxa_omx); return js (e, b);
}
JNIEXPORT jint JNICALL JF(ofind) (JNIEnv *e, jclass c, jstring k) { (void) c; JSTR (s, k); pthread_mutex_lock (&vxa_omx); int i = s ? vxa_opt_find (s) : -1; pthread_mutex_unlock (&vxa_omx); JREL (s, k); return i; }
JNIEXPORT void JNICALL JF(oset) (JNIEnv *e, jclass c, jint i, jstring v)
{
	(void) c; JSTR (s, v); if (s && i >= 0 && i < vxa_nopt) { pthread_mutex_lock (&vxa_omx);
		if (strcmp (vxa_opt[i].val, s)) { snprintf (vxa_opt[i].val, sizeof vxa_opt[i].val, "%s", s); vxa_opt_save (); vxa_opt_dirty = 1; pthread_mutex_unlock (&vxa_omx); vlog ("setting %s = %s", vxa_opt[i].key, s); }
		else pthread_mutex_unlock (&vxa_omx); }
	JREL (s, v);
}
JNIEXPORT jint JNICALL JF(ncats) (JNIEnv *e, jclass c) { (void) e; (void) c; return vxa_ncat; }
JNIEXPORT jstring JNICALL JF(cat) (JNIEnv *e, jclass c, jint i, jint what) { (void) c; if (i < 0 || i >= vxa_ncat) return js (e, ""); return js (e, what == 0 ? vxa_cat[i].key : what == 1 ? vxa_cat[i].desc : vxa_cat[i].info); }
/* (actions) */
JNIEXPORT void JNICALL JF(load) (JNIEnv *e, jclass c, jstring p) { (void) c; JSTR (s, p); vxa_cmd_put (C_LOAD, 0, 0, 0, s); JREL (s, p); }
JNIEXPORT void JNICALL JF(act) (JNIEnv *e, jclass c, jint what, jdouble d) { (void) e; (void) c; vxa_cmd_put (what == 1 ? C_RESET : what == 2 ? C_EJECT : what == 3 ? C_POWER : C_TURN, 0, 0, d, NULL); }
JNIEXPORT jstring JNICALL JF(game) (JNIEnv *e, jclass c) { (void) c; return js (e, vx_app_game ()); }
JNIEXPORT jint JNICALL JF(state) (JNIEnv *e, jclass c, jboolean save, jint slot)
{
	(void) e; (void) c; if (vxa_ok != 1) return 0; pthread_mutex_lock (&vxa_cmx); const int want = vxa_cdone + 1; pthread_mutex_unlock (&vxa_cmx);
	vxa_cmd_put (save ? C_STATE_SAVE : C_STATE_LOAD, slot, 0, 0, NULL);
	pthread_mutex_lock (&vxa_cmx); struct timespec t; clock_gettime (CLOCK_REALTIME, &t); t.tv_sec += 3;
	while (vxa_cdone < want) if (pthread_cond_timedwait (&vxa_ccv, &vxa_cmx, &t)) break;
	int r = vxa_cdone >= want ? vxa_cres : 0; pthread_mutex_unlock (&vxa_cmx); return r;
}
JNIEXPORT jint JNICALL JF(stateExists) (JNIEnv *e, jclass c, jint slot) { (void) e; (void) c; char p[1600]; vxa_state_path (p, sizeof p, slot); struct stat st; return stat (p, &st) == 0; }
JNIEXPORT jint JNICALL JF(stateSupported) (JNIEnv *e, jclass c) { (void) e; (void) c; return vxa_ok == 1 && retro_serialize_size () > 0; }
extern int vx_app_port2 (void), vx_app_cart_out (void), vx_app_power_on (void), vx_app_paused (void); extern double vx_app_volume (void);
JNIEXPORT jintArray JNICALL JF(deckState) (JNIEnv *e, jclass c)
{
	(void) c; jintArray a = (*e)->NewIntArray (e, 5); jint v[5] = { vx_app_port2 (), vx_app_cart_out (), vx_app_power_on (), vx_app_paused (), (jint) lround (vx_app_volume () * 100) };
	(*e)->SetIntArrayRegion (e, a, 0, 5, v); return a;
}
extern void vx_app_osd (char *fps, char *hz, char *emu, int *cpu, int *gpu);
JNIEXPORT jstring JNICALL JF(osd) (JNIEnv *e, jclass c)   /* (the deck's figures: "fps|hz|emu|cpu|gpu") */
{ (void) c; char f[24], h[24], m[24]; int cp = -1, gp = -1; vx_app_osd (f, h, m, &cp, &gp); char b[120]; snprintf (b, sizeof b, "%s|%s|%s|%d|%d", f, h, m, cp, gp); return js (e, b); }
JNIEXPORT jint JNICALL JF(untgz) (JNIEnv *e, jclass c, jstring tgz, jstring root)
{ (void) c; JSTR (a, tgz); JSTR (b, root); int files = 0; int r = vxa_untgz (a, b, &files); vlog ("unpacked %s: %d files%s", a, files, r ? " - NOT WHOLE" : ""); JREL (a, tgz); JREL (b, root); return r ? -1 : files; }
/* (online play: the core's netvx.c, as the iOS app) */
extern int vx_net_host (int mode, const char *gamer, const char *key, const char *rom, int skip_boot, const char *cart_hw, double dacofs);
extern int vx_net_join (const char *where, const char *gamer, const char *key);
extern void vx_net_accept (int ok), vx_net_prepare (void), vx_net_go (void), vx_net_leave (void), vx_net_chat (const char *text), vx_net_pause (int on), vx_net_room (void), vx_net_lan (int on);
extern const char *vx_net_status (void);
extern int vx_net_regame (const char *rom, int skip_boot, const char *cart_hw, double dacofs); extern void vx_net_regame_answer (const char *path);
extern int vx_net_go_hot (void); extern void vx_net_prepare_hot (void), vx_net_take_port (int k);
extern int vx_net_md5_file (const char *path, char *out33, long long *size);
JNIEXPORT jint JNICALL JF(netHost) (JNIEnv *e, jclass c, jint mode, jstring gamer, jstring key, jstring rom, jboolean skip)
{ (void) c; JSTR (g, gamer); JSTR (k, key); JSTR (r, rom); int x = vx_net_host (mode, g, k, r, skip, "auto", 0.0); JREL (g, gamer); JREL (k, key); JREL (r, rom); return x; }
JNIEXPORT jint JNICALL JF(netJoin) (JNIEnv *e, jclass c, jstring where, jstring gamer, jstring key)
{ (void) c; JSTR (w, where); JSTR (g, gamer); JSTR (k, key); int x = vx_net_join (w, g, k); JREL (w, where); JREL (g, gamer); JREL (k, key); return x; }
JNIEXPORT jint JNICALL JF(netRegame) (JNIEnv *e, jclass c, jstring rom, jboolean skip) { (void) c; JSTR (r, rom); int x = vx_net_regame (r, skip, "auto", 0.0); JREL (r, rom); return x; }
JNIEXPORT void JNICALL JF(netRegameAnswer) (JNIEnv *e, jclass c, jstring p) { (void) c; JSTR (s, p); vx_net_regame_answer (s && *s ? s : NULL); JREL (s, p); }
JNIEXPORT void JNICALL JF(netChat) (JNIEnv *e, jclass c, jstring t) { (void) c; JSTR (s, t); if (s) vx_net_chat (s); JREL (s, t); }
JNIEXPORT jstring JNICALL JF(netStatus) (JNIEnv *e, jclass c) { (void) c; return js (e, vx_net_status ()); }
JNIEXPORT jint JNICALL JF(net) (JNIEnv *e, jclass c, jint what, jint arg)
{
	(void) e; (void) c;
	switch (what) {
	case 0: vx_net_accept (arg); return 0; case 1: case 2: case 8: vxa_cmd_put (C_NET, what, 0, 0, NULL); return 0; case 3: vx_net_leave (); return 0;
	case 4: vx_net_pause (arg); return 0; case 5: vx_net_room (); return 0; case 6: vx_net_lan (arg); return 0;
	case 7: return vx_net_go_hot (); case 9: vx_net_take_port (arg); return 0;
	}
	return 0;
}
extern int vx_net_claim (const char *name, const char *key, const char *old, char *out, int outn), vx_net_whoami (const char *key, char *out, int outn);
JNIEXPORT jstring JNICALL JF(netClaim) (JNIEnv *e, jclass c, jstring name, jstring key, jstring old)   /* ("1NAME" reserved, "0" another's, "-1" not allowed, "-2" no answer) */
{ (void) c; JSTR (n, name); JSTR (k, key); JSTR (o, old); char got[64] = ""; const int r = vx_net_claim (n, k, o, got, sizeof got); JREL (n, name); JREL (k, key); JREL (o, old);
  char b[80]; snprintf (b, sizeof b, "%d%s", r, r == 1 ? got : ""); return js (e, b); }
JNIEXPORT jstring JNICALL JF(netWhoami) (JNIEnv *e, jclass c, jstring key)   /* (the name kept for this device's key, "" none, null no answer) */
{ (void) c; JSTR (k, key); char got[64] = ""; const int r = vx_net_whoami (k, got, sizeof got); JREL (k, key); return r == -2 ? NULL : js (e, got); }
JNIEXPORT jstring JNICALL JF(md5) (JNIEnv *e, jclass c, jstring p) { (void) c; JSTR (s, p); char h[33] = ""; long long n = 0; int ok = s && vx_net_md5_file (s, h, &n); JREL (s, p); return ok ? js (e, h) : NULL; }
