/* vxhost.c - a minimal libretro front end to test Vectrex_EMU's core without RetroArch (Linux / WSL; Android: Termux).
 *
 *   vxhost <core.so> <system dir> <game> bench <frames>        ms per retro_run (after a warm-up), as RetroArch runs it
 *   vxhost <core.so> <system dir> <game> shot <frame> <out.ppm> the picture at that refresh
 *   vxhost <core.so> <system dir> <game> state                 save, run on, load, run on again: the same state and sound?
 *
 * The options are the core's defaults (each one's first value, as RetroArch starts); VXH_OPT="key=value,key=value"
 * overrides some. The input is scripted (the same in every run): button 1 now and then, the stick swept.
 * Copyright (c) 2026 Hugo Bokhorst. Part of Vectrex_EMU; see LICENSE.md. */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include "../libretro.h"
/* (VXH_GPU=1, Android: the core's GPU path - an OpenGL ES 3 context of our own (EGL, no window), its framebuffer read
 *  back for a shot; build with -lEGL -lGLESv3 -DVXH_EGL) */
#ifdef VXH_EGL
#include <EGL/egl.h>
#include <GLES3/gl3.h>
static struct retro_hw_render_callback hw; static int hw_on; static GLuint hw_fbo, hw_tex; static uint32_t *hw_pix;
static uintptr_t hw_fb (void) { return hw_fbo; }
static retro_proc_address_t hw_proc (const char *s) { retro_proc_address_t f = (retro_proc_address_t) eglGetProcAddress (s); if (!f) f = (retro_proc_address_t) dlsym (RTLD_DEFAULT, s); return f; }
static int hw_setup (unsigned w, unsigned h)
{
	EGLDisplay d = eglGetDisplay (EGL_DEFAULT_DISPLAY); if (!eglInitialize (d, NULL, NULL)) return 0;
	const EGLint ca[] = { EGL_RENDERABLE_TYPE, 0x40 /* ES3 */, EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_NONE };
	EGLConfig cfg; EGLint n = 0; if (!eglChooseConfig (d, ca, &cfg, 1, &n) || n < 1) return 0;
	int sw_ = 16, sh_ = 16; if (getenv ("VXH_SCREEN")) sscanf (getenv ("VXH_SCREEN"), "%dx%d", &sw_, &sh_);   /* (VXH_SCREEN=WxH: the window's size, as a screen) */
	const EGLint sa[] = { EGL_WIDTH, sw_, EGL_HEIGHT, sh_, EGL_NONE }; EGLSurface su = eglCreatePbufferSurface (d, cfg, sa);
	eglBindAPI (EGL_OPENGL_ES_API); const EGLint xa[] = { EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE };
	EGLContext c = eglCreateContext (d, cfg, EGL_NO_CONTEXT, xa); if (c == EGL_NO_CONTEXT || !eglMakeCurrent (d, su, su, c)) return 0;
	glGenTextures (1, &hw_tex); glBindTexture (GL_TEXTURE_2D, hw_tex); glTexImage2D (GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
	glGenFramebuffers (1, &hw_fbo); glBindFramebuffer (GL_FRAMEBUFFER, hw_fbo); glFramebufferTexture2D (GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, hw_tex, 0);
	fprintf (stderr, "vxhost: EGL %s, framebuffer %ux%u\n", (const char *) glGetString (GL_RENDERER), w, h);
	hw_pix = malloc ((size_t) w * h * 4);
	return glCheckFramebufferStatus (GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
}
#endif

static const char *sysdir;
static struct { char key[64], val[96]; } opt[256]; static int nopt;
static int hidden;
/* (netsim: both players' inputs, made up per frame; ns_in the frame's - port 0 and port 1: buttons (RetroPad bits), x, y) */
static int ns_on, ns_ctx; static unsigned ns_b[2]; static int ns_x[2], ns_y[2];                              /* (a run-ahead style hidden refresh: no video) */
static const uint32_t *frame; static unsigned fw, fh; static size_t fpitch;
static uint64_t audio_hash, audio_n;
static long fno;

static uint64_t fnv (uint64_t h, const void *p, size_t n) { const unsigned char *b = p; while (n--) { h ^= *b++; h *= 1099511628211ull; } return h; }
static bool env (unsigned cmd, void *data)
{
	switch (cmd) {
	case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY: *(const char **) data = sysdir; return true;
	case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY: *(const char **) data = getenv ("VXH_SAVEDIR"); return getenv ("VXH_SAVEDIR") != NULL;   /* (saves: a folder of their own) */
	case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT: return *(enum retro_pixel_format *) data == RETRO_PIXEL_FORMAT_XRGB8888;
	case RETRO_ENVIRONMENT_SET_VARIABLES: {
		const struct retro_variable *v = data;
		for (; v->key && nopt < 256; v++) {
			const char *d = strchr (v->value, ';'); d = d ? d + 1 : v->value; while (*d == ' ') d++;
			const char *e = strchr (d, '|'); size_t n = e ? (size_t) (e - d) : strlen (d);
			snprintf (opt[nopt].key, sizeof opt[nopt].key, "%s", v->key); snprintf (opt[nopt].val, sizeof opt[nopt].val, "%.*s", (int) n, d); nopt++; }
		const char *o = getenv ("VXH_OPT");             /* (overrides) */
		if (o) { char buf[2048]; snprintf (buf, sizeof buf, "%s", o);
			for (char *t = strtok (buf, ","); t; t = strtok (NULL, ",")) { char *eq = strchr (t, '='); if (!eq) continue; *eq = 0;
				for (int i = 0; i < nopt; i++) if (!strcmp (opt[i].key, t)) snprintf (opt[i].val, sizeof opt[i].val, "%s", eq + 1); } }
		return true; }
	case RETRO_ENVIRONMENT_GET_VARIABLE: {
		struct retro_variable *v = data; v->value = NULL;
		for (int i = 0; i < nopt; i++) if (!strcmp (opt[i].key, v->key)) { v->value = opt[i].val; return true; }
		return false; }
	case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE: *(bool *) data = false; return true;
	case RETRO_ENVIRONMENT_GET_AUDIO_VIDEO_ENABLE: *(int *) data = hidden ? 2 : 3; return true;
	case 72 | 0x10000: if (!ns_on) return false; *(int *) data = ns_ctx; return true;   /* (GET_SAVESTATE_CONTEXT: netsim - ROLLBACK_NETPLAY) */
	case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS: case RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME: return true;
#ifdef VXH_EGL
	case RETRO_ENVIRONMENT_SET_HW_RENDER: {
		struct retro_hw_render_callback *c = data; const char *g = getenv ("VXH_GPU");
		if (!g || *g != '1' || c->context_type != RETRO_HW_CONTEXT_OPENGLES3) return false;
		c->get_current_framebuffer = hw_fb; c->get_proc_address = hw_proc; hw = *c; hw_on = 1; return true; }
	case RETRO_ENVIRONMENT_SET_GEOMETRY: return true;
#endif
	default: return false;
	}
}
static int read_back;                           /* (shot: the GPU's picture read back at this refresh) */
static void video (const void *d, unsigned w, unsigned h, size_t pitch)
{
#ifdef VXH_EGL
	if (d == RETRO_HW_FRAME_BUFFER_VALID) {
		{ static int nf = -1; static GLsync fq[8]; static long fi;   /* (bench: the GPU's time counted - VXH_FINISH=N: at most N
		                                                    frames in flight, as a swap chain; 1 (or set): each frame waited for) */
		  if (nf < 0) nf = getenv ("VXH_FINISH") ? (atoi (getenv ("VXH_FINISH")) > 0 ? atoi (getenv ("VXH_FINISH")) : 1) : 0;
		  if (nf > 7) nf = 7;
		  if (nf == 1) glFinish ();
		  else if (nf > 1) { GLsync *q = &fq[fi % nf]; if (*q) { glClientWaitSync (*q, GL_SYNC_FLUSH_COMMANDS_BIT, 1000000000ull); glDeleteSync (*q); }
			*q = glFenceSync (GL_SYNC_GPU_COMMANDS_COMPLETE, 0); glFlush (); fi++; } }
		if (!read_back) return;
		glBindFramebuffer (GL_FRAMEBUFFER, hw_fbo); unsigned char *b = malloc ((size_t) w * h * 4);
		glReadPixels (0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, b);
		for (unsigned y = 0; y < h; y++) for (unsigned x = 0; x < w; x++) { const unsigned char *q = b + ((size_t) (h - 1 - y) * w + x) * 4;
			hw_pix[(size_t) y * w + x] = (uint32_t) q[0] << 16 | (uint32_t) q[1] << 8 | q[2]; }
		free (b); frame = hw_pix; fw = w; fh = h; fpitch = (size_t) w * 4; return; }
#endif
	if (d) { frame = d; fw = w; fh = h; fpitch = pitch; }
}
static void sample (int16_t l, int16_t r) { (void) l; (void) r; }
static size_t audio (const int16_t *d, size_t n) { audio_hash = fnv (audio_hash, d, n * 4); audio_n += n; return n; }
static void poll (void) {}
static int16_t input (unsigned port, unsigned dev, unsigned idx, unsigned id)
{
	if (ns_on) { if (port > 1) return 0;
		if (dev == RETRO_DEVICE_JOYPAD) return (ns_b[port] >> id) & 1;
		if (dev == RETRO_DEVICE_ANALOG && idx == RETRO_DEVICE_INDEX_ANALOG_LEFT) return (int16_t) (id == RETRO_DEVICE_ID_ANALOG_X ? ns_x[port] : ns_y[port]);
		return 0; }
	if (port) return 0;
	if (dev == RETRO_DEVICE_POINTER) {   /* (VXH_TOUCH=frame:x:y:frames,...: a finger there, x y 0..1 over the frame) */
		static int nt = -1; static long tf[16], tl[16]; static double tx[16], ty[16];
		if (nt < 0) { nt = 0; const char *e = getenv ("VXH_TOUCH"); char b[512]; snprintf (b, sizeof b, "%s", e ? e : "");
			for (char *t = strtok (b, ","); t && nt < 16; t = strtok (NULL, ",")) { long f, l = 3; double x, y; if (sscanf (t, "%ld:%lf:%lf:%ld", &f, &x, &y, &l) >= 3) { tf[nt] = f; tx[nt] = x; ty[nt] = y; tl[nt] = l; nt++; } } }
		for (int k = 0; k < nt; k++) if (fno >= tf[k] && fno < tf[k] + tl[k]) {
			if (id == RETRO_DEVICE_ID_POINTER_PRESSED) return 1;
			if (id == RETRO_DEVICE_ID_POINTER_X) return (int16_t) (tx[k] * 65535 - 32768);
			if (id == RETRO_DEVICE_ID_POINTER_Y) return (int16_t) (ty[k] * 65535 - 32768); }
		return 0; }
	{ static int np = -1; static long pf[64], pl[64]; static unsigned pid[64];   /* (VXH_PRESS=frame:id:frames,...: these buttons only) */
	  if (np < 0) { np = 0; const char *e = getenv ("VXH_PRESS"); char b[1024]; snprintf (b, sizeof b, "%s", e ? e : "");
		for (char *t = strtok (b, ","); t && np < 64; t = strtok (NULL, ",")) { long f, l = 1; unsigned i; if (sscanf (t, "%ld:%u:%ld", &f, &i, &l) >= 2) { pf[np] = f; pid[np] = i; pl[np] = l; np++; } }
		if (!e) np = -2; }
	  if (np >= 0) { if (dev != RETRO_DEVICE_JOYPAD) return 0; for (int k = 0; k < np; k++) if (pid[k] == id && fno >= pf[k] && fno < pf[k] + pl[k]) return 1; return 0; } }
	if (dev == RETRO_DEVICE_JOYPAD) {
		if (id == RETRO_DEVICE_ID_JOYPAD_Y) return (fno / 40) % 3 == 1;     /* (button 1: start the game, then fire) */
		if (id == RETRO_DEVICE_ID_JOYPAD_RIGHT) return (fno / 90) % 4 == 2;
		if (id == RETRO_DEVICE_ID_JOYPAD_LEFT) return (fno / 90) % 4 == 0 && fno > 200;
		return 0; }
	if (dev == RETRO_DEVICE_ANALOG && idx == RETRO_DEVICE_INDEX_ANALOG_LEFT && id == RETRO_DEVICE_ID_ANALOG_Y) return (int16_t) (((fno / 70) % 3 - 1) * 20000);
	return 0;
}
static double now (void) { struct timespec t; clock_gettime (CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec * 1e-9; }

#define SYM(n) n = dlsym (h, #n); if (!n) { fprintf (stderr, "no %s\n", #n); return 1; }
int main (int argc, char **argv)
{
	if (argc < 5) { fprintf (stderr, "vxhost core.so sysdir game bench N | shot N out.ppm | state\n"); return 1; }
	sysdir = argv[2];
	void *h = dlopen (argv[1], RTLD_NOW); if (!h) { fprintf (stderr, "%s\n", dlerror ()); return 1; }
	void (*retro_set_environment) (retro_environment_t), (*retro_set_video_refresh) (retro_video_refresh_t),
	     (*retro_set_audio_sample) (retro_audio_sample_t), (*retro_set_audio_sample_batch) (retro_audio_sample_batch_t),
	     (*retro_set_input_poll) (retro_input_poll_t), (*retro_set_input_state) (retro_input_state_t), (*retro_init) (void),
	     (*retro_run) (void), (*retro_unload_game) (void), (*retro_get_system_av_info) (struct retro_system_av_info *);
	bool (*retro_load_game) (const struct retro_game_info *), (*retro_serialize) (void *, size_t), (*retro_unserialize) (const void *, size_t);
	size_t (*retro_serialize_size) (void);
	SYM (retro_set_environment) SYM (retro_set_video_refresh) SYM (retro_set_audio_sample) SYM (retro_set_audio_sample_batch)
	SYM (retro_set_input_poll) SYM (retro_set_input_state) SYM (retro_init) SYM (retro_run) SYM (retro_unload_game) SYM (retro_get_system_av_info)
	SYM (retro_load_game) SYM (retro_serialize) SYM (retro_unserialize) SYM (retro_serialize_size)
	retro_set_environment (env); retro_set_video_refresh (video); retro_set_audio_sample (sample); retro_set_audio_sample_batch (audio);
	retro_set_input_poll (poll); retro_set_input_state (input); retro_init ();
	struct retro_game_info gi = { argv[3], NULL, 0, NULL };
	if (!retro_load_game (&gi)) { fprintf (stderr, "load failed\n"); return 2; }
	struct retro_system_av_info av; retro_get_system_av_info (&av);
#ifdef VXH_EGL
	if (hw_on) { if (!hw_setup (av.geometry.max_width, av.geometry.max_height)) { fprintf (stderr, "vxhost: no EGL context\n"); return 2; } hw.context_reset (); }
#endif
	fprintf (stderr, "picture %ux%u, %.0f Hz\n", av.geometry.base_width, av.geometry.base_height, av.timing.fps);
	/* (VXH_RUNAHEAD=1: each refresh as RetroArch's run-ahead of one frame, one instance - the frame run unseen, the state
	 *  saved, the next frame run and shown, the state put back) */
	static void *ra_buf; static size_t ra_sz; const int ra = getenv ("VXH_RUNAHEAD") != NULL;
	#define RUN() do { if (ra) { if (!ra_buf) { ra_sz = retro_serialize_size (); ra_buf = malloc (ra_sz ? ra_sz : 1); } \
		hidden = 1; retro_run (); hidden = 0; if (ra_sz && retro_serialize (ra_buf, ra_sz)) { retro_run (); retro_unserialize (ra_buf, ra_sz); } else retro_run (); } \
		else retro_run (); \
		{ static long rsf = -2; if (rsf == -2) rsf = getenv ("VXH_RESET") ? atol (getenv ("VXH_RESET")) : -1; \
		  if (fno == rsf) { void (*rr) (void) = (void (*) (void)) dlsym (h, "retro_reset"); if (rr) { rr (); fprintf (stderr, "vxhost: retro_reset at %ld\n", fno); } } } } while (0)
	if (!strcmp (argv[4], "bench")) {
		int n = argc > 5 ? atoi (argv[5]) : 1200, warm = 120; double t0 = 0, worst = 0;
		for (fno = 0; fno < n + warm; fno++) {
			double a = now (); RUN (); double d = now () - a; { static FILE *tf; static int ti = -1; if (ti < 0) { const char *z = getenv ("VXH_TRACE"); tf = z ? fopen (z, "w") : NULL; ti = 1; } if (tf) fprintf (tf, "%ld %.3f\n", fno, d * 1e3); }   /* (VXH_TRACE: each refresh's time) */
			if (fno == warm) t0 = a;
			if (fno >= warm && d > worst) worst = d; }
		double avg = (now () - t0) / n * 1e3;
		printf ("%.3f ms per frame on average, worst %.2f ms (%d frames; %.0f Hz needs %.2f ms)\n", avg, worst * 1e3, n, av.timing.fps, 1e3 / av.timing.fps);
		fflush (stdout); retro_unload_game ();   /* (VXPOC_PROF=1: the core's own breakdown - Android: in its log) */
	} else if (!strcmp (argv[4], "shot")) {
		int n = atoi (argv[5]);
		for (fno = 0; fno < n; fno++) { read_back = fno == n - 1; RUN (); }
		FILE *f = fopen (argv[6], "wb"); fprintf (f, "P6\n%u %u\n255\n", fw, fh);
		for (unsigned y = 0; y < fh; y++) for (unsigned x = 0; x < fw; x++) {
			uint32_t p = ((const uint32_t *) ((const char *) frame + y * fpitch))[x]; unsigned char c[3] = { p >> 16, p >> 8, p };
			fwrite (c, 1, 3, f); }
		fclose (f); printf ("%s %ux%u\n", argv[6], fw, fh);
	} else if (!strcmp (argv[4], "netsim")) {
		/* RetroArch's netplay, simulated: player 2's input arrives D frames late; until then the last one known is used, the
		 * frame run with it and the state saved before it; when the real input differs, the state of that frame is loaded and
		 * the frames since run again (unseen), as netplay does. At the end the state must be the very one of a straight run
		 * with the real inputs: netsim N D */
		const int N = argc > 5 ? atoi (argv[5]) : 900, D = argc > 6 ? atoi (argv[6]) : 4;
		size_t sz = retro_serialize_size (); if (!sz) { printf ("no save states for this game\n"); return 3; }
		unsigned *B[2]; int *X[2], *Y[2];
		for (int p_ = 0; p_ < 2; p_++) { B[p_] = calloc (N, sizeof (unsigned)); X[p_] = calloc (N, sizeof (int)); Y[p_] = calloc (N, sizeof (int)); }
		unsigned r = 12345;
		for (int p_ = 0; p_ < 2; p_++) { unsigned b = 0; int x = 0, y = 0;
			for (int f = 0; f < N; f++) { r = r * 1664525u + 1013904223u;   /* (held for a while, then changed: a player's hand) */
				if ((r >> 24) < 40) { b = (r >> 8) & ((1u << 0) | (1u << 1) | (1u << 8) | (1u << 9)); x = (int) ((r >> 4) % 3) * 32767 - 32767; y = (int) ((r >> 12) % 3) * 32767 - 32767; }
				B[p_][f] = b; X[p_][f] = x; Y[p_][f] = y; } }
		ns_on = 1; ns_ctx = 3;
		for (fno = 0; fno < 200; fno++) retro_run ();       /* (the game started: the same for both runs) */
		unsigned char *s0 = malloc (sz), *sa = malloc (sz), *sb = malloc (sz), **st = malloc (sizeof (char *) * (D + 2));
		const int RS = D + 2; for (int f = 0; f < RS; f++) st[f] = malloc (sz);   /* (a ring: a rollback goes back D frames at most - a state is ~19 MB) */
		retro_serialize (s0, sz); const long f0 = fno;
		#define SETIN(f, rb, rx, ry) do { ns_b[0] = B[0][f]; ns_x[0] = X[0][f]; ns_y[0] = Y[0][f]; ns_b[1] = (rb); ns_x[1] = (rx); ns_y[1] = (ry); } while (0)
		/* the straight run: the real inputs */
		for (int f = 0; f < N; f++) { SETIN (f, B[1][f], X[1][f], Y[1][f]); fno = f0 + f; retro_run (); }
		retro_serialize (sa, sz);
		/* netplay: predicted, rolled back */
		retro_unserialize (s0, sz);
		unsigned *ub = calloc (N, sizeof (unsigned)); int *ux = calloc (N, sizeof (int)), *uy = calloc (N, sizeof (int)), rollbacks = 0, replayed = 0;
		for (int t = 0; t < N; t++) {
			const int fa = t - D;                           /* (player 2's input of frame fa arrives now) */
			if (fa >= 0 && (ub[fa] != B[1][fa] || ux[fa] != X[1][fa] || uy[fa] != Y[1][fa])) {
				retro_unserialize (st[fa % RS], sz); rollbacks++; hidden = 1;
				for (int g = fa; g < t; g++) { const int k = g <= fa ? g : fa;
					ub[g] = B[1][k]; ux[g] = X[1][k]; uy[g] = Y[1][k];
					retro_serialize (st[g % RS], sz); SETIN (g, ub[g], ux[g], uy[g]); fno = f0 + g; retro_run (); replayed++; }
				hidden = 0; }
			const int k = fa >= 0 ? fa : -1;                /* (the guess: the last input known) */
			ub[t] = k >= 0 ? B[1][k] : 0; ux[t] = k >= 0 ? X[1][k] : 0; uy[t] = k >= 0 ? Y[1][k] : 0;
			retro_serialize (st[t % RS], sz); SETIN (t, ub[t], ux[t], uy[t]); fno = f0 + t; retro_run (); }
		{ int fa = N - D; if (fa < 0) fa = 0;                  /* (the last inputs arrive: the rest corrected) */
		  retro_unserialize (st[fa % RS], sz); hidden = 1;
		  for (int g = fa; g < N; g++) { SETIN (g, B[1][g], X[1][g], Y[1][g]); fno = f0 + g; retro_run (); replayed++; }
		  hidden = 0; }
		retro_serialize (sb, sz);
		size_t nd = 0; long first = -1; for (size_t i = 0; i < sz; i++) if (sa[i] != sb[i]) { if (first < 0) first = (long) i; nd++; }
		printf ("netsim %d frames, player 2 %d frames late: %d rollbacks, %d frames run again; the state %s (%zu bytes differ, first at %ld)\n",
		        N, D, rollbacks, replayed, nd ? "DIFFERS" : "the same", nd, first);
	} else if (!strcmp (argv[4], "state")) {
		/* save at 400, run 300 on (some refreshes hidden, as run-ahead does), save again; load the first and run the same
		 * 300: the second save and the sound must come out the same, byte for byte */
		for (fno = 0; fno < 400; fno++) retro_run ();
		size_t sz = retro_serialize_size (); if (!sz) { printf ("no save states for this game\n"); return 3; }
		unsigned char *s0 = malloc (sz), *s1 = malloc (sz), *s2 = malloc (sz);
		if (!retro_serialize (s0, sz)) { printf ("serialize failed\n"); return 3; }
		audio_hash = 0; audio_n = 0;
		for (int k = 0; k < 300; k++, fno++) { hidden = (k % 3) == 1; retro_run (); }
		hidden = 0; uint64_t ah1 = audio_hash, an1 = audio_n; retro_serialize (s1, sz);
		fno -= 300;
		if (!retro_unserialize (s0, sz)) { printf ("unserialize failed\n"); return 3; }
		audio_hash = 0; audio_n = 0;
		for (int k = 0; k < 300; k++, fno++) { hidden = (k % 3) == 1; retro_run (); }
		hidden = 0; retro_serialize (s2, sz);
		size_t diff = 0, first = (size_t) -1; for (size_t i = 0; i < sz; i++) if (s1[i] != s2[i]) { diff++; if (first == (size_t) -1) first = i; }
		printf ("state %zu bytes; after the same 300 refreshes: %s (%zu bytes differ, first at %zd); sound %s (%llu / %llu samples)\n",
			sz, diff ? "DIFFERENT" : "the same", diff, (ssize_t) first, ah1 == audio_hash && an1 == audio_n ? "the same" : "DIFFERENT",
			(unsigned long long) an1, (unsigned long long) audio_n);
		return diff || ah1 != audio_hash ? 4 : 0;
	}
	return 0;
}
