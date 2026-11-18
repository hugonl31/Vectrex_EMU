/* VXApp.m - Vectrex_EMU for iPhone and iPad, step 1 (the plan: the mockup canvas): our own app around the core, no
 * RetroArch. It hosts the libretro core compiled into it as RetroArch did - the same environment calls, the picture by
 * OpenGL ES 3 (the core draws into our framebuffer, we copy it to the screen 1:1), the sound through a ring buffer, the
 * controllers and the keyboard (Game Controller framework), touch (the core's control deck and ROM browser as a pointer,
 * a stick and buttons 1-4 drawn here while nothing else is connected). The test builds' content - games, overlays, BIOS,
 * UVMC2, PiTrex - comes as one pack from the project's server at the first start (tools/ios_pack.py), so the app stays
 * small; everything that happens is logged into the core's log, which the core sends live (dbgsend.c).
 * Copyright (c) 2026 Hugo Bokhorst. Part of Vectrex_EMU; see LICENSE.md. */
#define GLES_SILENCE_DEPRECATION 1
#import <UIKit/UIKit.h>
#import <OpenGLES/EAGL.h>
#import <OpenGLES/ES3/gl.h>
#import <OpenGLES/ES3/glext.h>
#import <QuartzCore/QuartzCore.h>
#import <AVFoundation/AVFoundation.h>
#import <GameController/GameController.h>
#include <dlfcn.h>
#include <sys/utsname.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <mach/mach.h>
#include <zlib.h>
#include "libretro.h"
#include "vx_token.h"

/* ---------------------------------------------------------------- the log: the app's own, sent live from the first
 * second (the core's sender starts only with a game - build 39 crashed before, unseen), kept in Documents/applog.txt; a
 * crash writes its call stack there and the next start sends it */
#include <execinfo.h>
#include <signal.h>
#include <fcntl.h>
#include <unistd.h>
static NSMutableArray<NSString *> *vxa_early; static int vxa_loaded;
static NSMutableString *vxa_pend; static int vxa_lfd = -1; static char vxa_sess[8];
static void vxa_post (NSString *kind, NSData *d)
{
#ifdef VX_LOG_TOKEN
	if (!d.length) return;
	NSMutableURLRequest *rq = [NSMutableURLRequest requestWithURL:[NSURL URLWithString:@"http://172.233.60.40:7746/ioslog"]];
	rq.HTTPMethod = @"POST"; rq.HTTPBody = d; rq.timeoutInterval = 15;
	[rq setValue:@VX_LOG_TOKEN forHTTPHeaderField:@"X-Vectrex-Token"]; [rq setValue:[NSString stringWithFormat:@"%s-%@", vxa_sess, kind] forHTTPHeaderField:@"X-Vectrex-Kind"];
	[rq setValue:@"txt" forHTTPHeaderField:@"X-Vectrex-Ext"];
	[[NSURLSession.sharedSession dataTaskWithRequest:rq] resume];
#else
	(void) kind; (void) d;
#endif
}
static void vxa_flush_log (void)
{
	NSString *s; @synchronized (vxa_pend) { s = [vxa_pend copy]; [vxa_pend setString:@""]; }
	if (s.length) vxa_post (@"app-log", [s dataUsingEncoding:NSUTF8StringEncoding]);
}
static void vxa_crash_sig (int sig)
{
	if (vxa_lfd >= 0) {
		char m[64]; int n = snprintf (m, sizeof m, "\n*** CRASH: signal %d - the call stack:\n", sig); write (vxa_lfd, m, (size_t) n);
		void *bt[64]; int k = backtrace (bt, 64); backtrace_symbols_fd (bt, k, vxa_lfd); fsync (vxa_lfd); }
	signal (sig, SIG_DFL); raise (sig);
}
static void vxa_crash_exc (NSException *e)
{
	if (vxa_lfd >= 0) { NSString *s = [NSString stringWithFormat:@"\n*** CRASH: %@: %@\n%@\n", e.name, e.reason, [e.callStackSymbols componentsJoinedByString:@"\n"]];
		const char *c = s.UTF8String; write (vxa_lfd, c, strlen (c)); fsync (vxa_lfd); }
}
static void vxa_log_start (void)
{
	vxa_pend = [NSMutableString new]; snprintf (vxa_sess, sizeof vxa_sess, "a%04x", arc4random_uniform (0x10000));
	NSString *docs = NSSearchPathForDirectoriesInDomains (NSDocumentDirectory, NSUserDomainMask, YES).firstObject;
	NSString *p = [docs stringByAppendingPathComponent:@"applog.txt"];
	NSData *prev = [NSData dataWithContentsOfFile:p]; if (prev.length) vxa_post (@"app-previous-log", prev);   /* (the last run's, its crash with it) */
	vxa_lfd = open (p.fileSystemRepresentation, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	NSSetUncaughtExceptionHandler (vxa_crash_exc);
	int sigs[] = { SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT, SIGTRAP }; for (int i = 0; i < 6; i++) signal (sigs[i], vxa_crash_sig);
	[NSTimer scheduledTimerWithTimeInterval:1.0 repeats:YES block:^(NSTimer *t) { (void) t; vxa_flush_log (); }];
}
static void vlog (const char *fmt, ...) __attribute__ ((format (printf, 1, 2)));
static void vlog (const char *fmt, ...)
{
	char b[1200]; va_list a; va_start (a, fmt); vsnprintf (b, sizeof b, fmt, a); va_end (a);
	struct timeval tv; gettimeofday (&tv, NULL); struct tm tm; localtime_r (&tv.tv_sec, &tm);
	char line[1300]; snprintf (line, sizeof line, "[app %02d:%02d:%02d.%03d] %s\n", tm.tm_hour, tm.tm_min, tm.tm_sec, (int) (tv.tv_usec / 1000), b);
	if (vxa_lfd >= 0) write (vxa_lfd, line, strlen (line));
	if (vxa_pend) @synchronized (vxa_pend) { [vxa_pend appendString:@(line)]; }
	(void) vxa_early; (void) vxa_loaded;
}
static double vxa_mem_mb (void)
{
	task_vm_info_data_t i; mach_msg_type_number_t n = TASK_VM_INFO_COUNT;
	return task_info (mach_task_self (), TASK_VM_INFO, (task_info_t) &i, &n) == KERN_SUCCESS ? i.phys_footprint / 1048576.0 : -1;
}

/* ---------------------------------------------------------------- the core's settings (its option list, v0 style) */
#define VXA_NOPT 256
static struct { char key[64]; char val[160]; char def[160]; char vals[1200]; char desc[160]; } vxa_opt[VXA_NOPT]; static int vxa_nopt, vxa_opt_dirty;
static int vxa_opt_find (const char *k) { for (int i = 0; i < vxa_nopt; i++) if (!strcmp (vxa_opt[i].key, k)) return i; return -1; }
static void vxa_opt_store (int i)
{
	[[NSUserDefaults standardUserDefaults] setObject:@(vxa_opt[i].val) forKey:[NSString stringWithFormat:@"opt.%s", vxa_opt[i].key]];
}
static void vxa_opts_set (const struct retro_variable *v)
{
	vxa_nopt = 0;
	for (; v && v->key && vxa_nopt < VXA_NOPT; v++) {
		const char *s = v->value ? strstr (v->value, "; ") : NULL; if (!s) continue; s += 2;
		int i = vxa_nopt++; snprintf (vxa_opt[i].key, sizeof vxa_opt[i].key, "%s", v->key); snprintf (vxa_opt[i].vals, sizeof vxa_opt[i].vals, "%s", s); snprintf (vxa_opt[i].desc, sizeof vxa_opt[i].desc, "%.*s", (int) (s - 2 - v->value), v->value);
		const char *bar = strchr (s, '|'); size_t n = bar ? (size_t) (bar - s) : strlen (s); if (n >= sizeof vxa_opt[i].def) n = sizeof vxa_opt[i].def - 1;
		memcpy (vxa_opt[i].def, s, n); vxa_opt[i].def[n] = 0; snprintf (vxa_opt[i].val, sizeof vxa_opt[i].val, "%s", vxa_opt[i].def);
		NSString *sv = [[NSUserDefaults standardUserDefaults] stringForKey:[NSString stringWithFormat:@"opt.%s", v->key]];
		if (sv) snprintf (vxa_opt[i].val, sizeof vxa_opt[i].val, "%s", sv.UTF8String);
	}
	/* (this app's own defaults: the control deck below the picture - its cartridge opens the ROM browser, RESET, the knob) */
	int dk = vxa_opt_find ("vectrex_emu_deck");
	if (dk >= 0 && ![[NSUserDefaults standardUserDefaults] stringForKey:@"opt.vectrex_emu_deck"]) snprintf (vxa_opt[dk].val, sizeof vxa_opt[dk].val, "full");
	/* (this device's own: the screen's refresh rate - 120 Hz on ProMotion, else 60) */
	int r = vxa_opt_find ("vectrex_emu_rate");
	if (r >= 0 && ![[NSUserDefaults standardUserDefaults] stringForKey:@"opt.vectrex_emu_rate"])
		snprintf (vxa_opt[r].val, sizeof vxa_opt[r].val, "%s", UIScreen.mainScreen.maximumFramesPerSecond >= 120 ? "120 Hz" : "60 Hz");
	vlog ("core options: %d (rate %s)", vxa_nopt, r >= 0 ? vxa_opt[r].val : "?");
}

/* ---------------------------------------------------------------- the core's environment */
static struct retro_hw_render_callback vxa_hw; static int vxa_hw_on;
static retro_keyboard_event_t vxa_kbcb;
static struct retro_system_av_info vxa_av;
static GLuint vxa_cfbo, vxa_ctex, vxa_cdepth; static unsigned vxa_cw, vxa_ch;      /* (the core's framebuffer and its size) */
static char vxa_sys[1100], vxa_saves[1100];
static int vxa_av_changed;
static void vxa_toast (NSString *s, double secs);
static uintptr_t vxa_get_fb (void) { return vxa_cfbo; }
static retro_proc_address_t vxa_get_proc (const char *s) { return (retro_proc_address_t) dlsym (RTLD_DEFAULT, s); }
static void vxa_corelog (enum retro_log_level l, const char *fmt, ...)
{
	char b[1500]; va_list a; va_start (a, fmt); vsnprintf (b, sizeof b, fmt, a); va_end (a); (void) l; fputs (b, stderr);
}
static void vxa_core_fb (unsigned w, unsigned h)              /* (the core's framebuffer at least w x h) */
{
	if (w <= vxa_cw && h <= vxa_ch && vxa_cfbo) return;
	if (w < vxa_cw) w = vxa_cw; if (h < vxa_ch) h = vxa_ch;
	if (!vxa_cfbo) { glGenFramebuffers (1, &vxa_cfbo); glGenTextures (1, &vxa_ctex); }
	glBindTexture (GL_TEXTURE_2D, vxa_ctex); glTexImage2D (GL_TEXTURE_2D, 0, GL_RGBA8, (GLsizei) w, (GLsizei) h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
	glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glBindFramebuffer (GL_FRAMEBUFFER, vxa_cfbo); glFramebufferTexture2D (GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, vxa_ctex, 0);
	if (vxa_hw.depth || vxa_hw.stencil) {
		if (!vxa_cdepth) glGenRenderbuffers (1, &vxa_cdepth);
		glBindRenderbuffer (GL_RENDERBUFFER, vxa_cdepth); glRenderbufferStorage (GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, (GLsizei) w, (GLsizei) h);
		glFramebufferRenderbuffer (GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, vxa_cdepth); }
	GLenum st = glCheckFramebufferStatus (GL_FRAMEBUFFER);
	vxa_cw = w; vxa_ch = h; vlog ("core framebuffer %u x %u%s", w, h, st == GL_FRAMEBUFFER_COMPLETE ? "" : " - NOT COMPLETE");
}
static bool vxa_env (unsigned cmd, void *data)
{
	switch (cmd) {
	case RETRO_ENVIRONMENT_GET_CAN_DUPE: *(bool *) data = true; return true;
	case RETRO_ENVIRONMENT_SET_MESSAGE: { const struct retro_message *m = data; vlog ("core message: %s", m->msg);
		NSString *s = @(m->msg); double t = m->frames / 60.0; dispatch_async (dispatch_get_main_queue (), ^{ vxa_toast (s, t); }); return true; }
	case 60: { const char *m = *(const char **) data; unsigned ms = ((const unsigned *) ((const char **) data + 1))[0];   /* (SET_MESSAGE_EXT) */
		vlog ("core message: %s", m); NSString *s = @(m); dispatch_async (dispatch_get_main_queue (), ^{ vxa_toast (s, ms / 1000.0); }); return true; }
	case RETRO_ENVIRONMENT_SET_PERFORMANCE_LEVEL: return true;
	case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY: *(const char **) data = vxa_sys; return true;
	case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY: *(const char **) data = vxa_saves; return true;
	case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT: vlog ("pixel format %d", *(const int *) data); return *(const int *) data == RETRO_PIXEL_FORMAT_XRGB8888;
	case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS: case RETRO_ENVIRONMENT_SET_CONTROLLER_INFO: case RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME: return true;
	case RETRO_ENVIRONMENT_SET_KEYBOARD_CALLBACK: vxa_kbcb = ((const struct retro_keyboard_callback *) data)->callback; vlog ("keyboard callback set"); return true;
	case RETRO_ENVIRONMENT_SET_HW_RENDER: {
		struct retro_hw_render_callback *c = data;
		vlog ("hw render: context %d, version %u.%u, depth %d stencil %d bottom-left %d", c->context_type, c->version_major, c->version_minor, c->depth, c->stencil, c->bottom_left_origin);
		if (c->context_type != RETRO_HW_CONTEXT_OPENGLES3 && !(c->context_type == RETRO_HW_CONTEXT_OPENGLES_VERSION && c->version_major == 3)) return false;
		c->get_current_framebuffer = vxa_get_fb; c->get_proc_address = vxa_get_proc; vxa_hw = *c; vxa_hw_on = 1; return true; }
	case 56: *(unsigned *) data = RETRO_HW_CONTEXT_OPENGLES3; return true;   /* (GET_PREFERRED_HW_RENDER) */
	case RETRO_ENVIRONMENT_SET_VARIABLES: vxa_opts_set (data); return true;
	case RETRO_ENVIRONMENT_GET_VARIABLE: { struct retro_variable *v = data; int i = vxa_opt_find (v->key); v->value = i >= 0 ? vxa_opt[i].val : NULL; return i >= 0; }
	case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE: *(bool *) data = vxa_opt_dirty; vxa_opt_dirty = 0; return true;
	case 70: { const struct retro_variable *v = data; if (!v || !v->key) return true;   /* (SET_VARIABLE: the core sets one itself) */
		int i = vxa_opt_find (v->key); if (i < 0 || !v->value) return false;
		snprintf (vxa_opt[i].val, sizeof vxa_opt[i].val, "%s", v->value); vxa_opt_store (i); vlog ("core set option %s = %s", v->key, v->value); return true; }
	case RETRO_ENVIRONMENT_GET_LOG_INTERFACE: ((struct retro_log_callback *) data)->log = vxa_corelog; return true;
	case RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO: vxa_av = *(const struct retro_system_av_info *) data; vxa_av_changed = 1;
		vlog ("av info: %u x %u (max %u x %u), %.2f Hz", vxa_av.geometry.base_width, vxa_av.geometry.base_height, vxa_av.geometry.max_width, vxa_av.geometry.max_height, vxa_av.timing.fps);
		if (vxa_cfbo) vxa_core_fb (vxa_av.geometry.max_width, vxa_av.geometry.max_height); return true;
	case RETRO_ENVIRONMENT_SET_GEOMETRY: vxa_av.geometry = *(const struct retro_game_geometry *) data;
		vlog ("geometry: %u x %u", vxa_av.geometry.base_width, vxa_av.geometry.base_height); return true;
	case RETRO_ENVIRONMENT_GET_LANGUAGE: *(unsigned *) data = RETRO_LANGUAGE_ENGLISH; return true;
	case RETRO_ENVIRONMENT_GET_AUDIO_VIDEO_ENABLE: *(int *) data = 3; return true;
	default: return false;
	}
}

/* ---------------------------------------------------------------- sound: the core's samples through a ring buffer */
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
static AVAudioEngine *vxa_eng;
static void vxa_audio_start (double rate)
{
	AVAudioSession *s = AVAudioSession.sharedInstance; NSError *e = nil;
	[s setCategory:AVAudioSessionCategoryAmbient error:&e]; [s setPreferredIOBufferDuration:0.005 error:nil]; [s setActive:YES error:nil];
	AVAudioFormat *f = [[AVAudioFormat alloc] initWithCommonFormat:AVAudioPCMFormatFloat32 sampleRate:rate channels:2 interleaved:NO];
	AVAudioSourceNode *src = [[AVAudioSourceNode alloc] initWithFormat:f renderBlock:^OSStatus (BOOL *silent, const AudioTimeStamp *ts, AVAudioFrameCount n, AudioBufferList *abl) {
		(void) ts; float *L = abl->mBuffers[0].mData, *R = abl->mNumberBuffers > 1 ? abl->mBuffers[1].mData : NULL; uint32_t rd = vxa_rd, have = __atomic_load_n (&vxa_wr, __ATOMIC_ACQUIRE) - rd;
		for (AVAudioFrameCount i = 0; i < n; i++) {
			if (have) { L[i] = vxa_ring[(rd % VXA_RING) * 2] / 32768.0f; if (R) R[i] = vxa_ring[(rd % VXA_RING) * 2 + 1] / 32768.0f; rd++; have--; }
			else { L[i] = 0; if (R) R[i] = 0; vxa_under++; } }
		__atomic_store_n (&vxa_rd, rd, __ATOMIC_RELEASE); *silent = NO; return noErr; }];
	vxa_eng = [AVAudioEngine new]; [vxa_eng attachNode:src]; [vxa_eng connect:src to:vxa_eng.mainMixerNode format:f];
	BOOL ok = [vxa_eng startAndReturnError:&e];
	vlog ("sound: %.0f Hz -> %.0f Hz output, buffer %.1f ms: %s", rate, s.sampleRate, s.IOBufferDuration * 1000, ok ? "running" : e.localizedDescription.UTF8String);
}

/* ---------------------------------------------------------------- input: controllers, keyboard, touch */
typedef struct { uint32_t btn; int16_t ax[2][2]; } vxa_pad;
static vxa_pad vxa_pads[2], vxa_tpad;                   /* (the controllers, ports 1 and 2; the touch controls, port 1) */
static uint8_t vxa_kb[RETROK_LAST];
static struct { int on; double x, y; } vxa_ptr;          /* (a finger on the picture: the core's pointer, -1..1) */
static const char *const vxa_bname[16] = { "B(3)", "Y(1)", "Select", "Start", "Up", "Down", "Left", "Right", "A(4)", "X(2)", "L1", "R1", "L2", "R2", "L3", "R3" };
static void vxa_read_pad (GCController *c, vxa_pad *p)
{
	GCExtendedGamepad *g = c.extendedGamepad; memset (p, 0, sizeof *p); if (!g) return;
	float lx = g.leftThumbstick.xAxis.value, ly = g.leftThumbstick.yAxis.value;
	uint32_t b = 0;
#define VB(cond, id) if (cond) b |= 1u << (id)
	VB (g.buttonA.pressed, RETRO_DEVICE_ID_JOYPAD_B); VB (g.buttonX.pressed, RETRO_DEVICE_ID_JOYPAD_Y);
	VB (g.buttonB.pressed, RETRO_DEVICE_ID_JOYPAD_A); VB (g.buttonY.pressed, RETRO_DEVICE_ID_JOYPAD_X);
	VB (g.buttonOptions.pressed, RETRO_DEVICE_ID_JOYPAD_SELECT); VB (g.buttonMenu.pressed, RETRO_DEVICE_ID_JOYPAD_START);
	VB (g.leftShoulder.pressed, RETRO_DEVICE_ID_JOYPAD_L); VB (g.rightShoulder.pressed, RETRO_DEVICE_ID_JOYPAD_R);
	VB (g.leftTrigger.pressed, RETRO_DEVICE_ID_JOYPAD_L2); VB (g.rightTrigger.pressed, RETRO_DEVICE_ID_JOYPAD_R2);
	VB (g.dpad.up.pressed || ly > 0.5f, RETRO_DEVICE_ID_JOYPAD_UP); VB (g.dpad.down.pressed || ly < -0.5f, RETRO_DEVICE_ID_JOYPAD_DOWN);
	VB (g.dpad.left.pressed || lx < -0.5f, RETRO_DEVICE_ID_JOYPAD_LEFT); VB (g.dpad.right.pressed || lx > 0.5f, RETRO_DEVICE_ID_JOYPAD_RIGHT);
#undef VB
	p->btn = b; p->ax[0][0] = (int16_t) (lx * 32767); p->ax[0][1] = (int16_t) (-ly * 32767);
	p->ax[1][0] = (int16_t) (g.rightThumbstick.xAxis.value * 32767); p->ax[1][1] = (int16_t) (-g.rightThumbstick.yAxis.value * 32767);
}
static void vxa_input_poll (void)
{
	NSArray<GCController *> *cs = GCController.controllers; static uint32_t prev[2];
	for (int port = 0; port < 2; port++) {
		vxa_pad p; memset (&p, 0, sizeof p); if ((NSUInteger) port < cs.count) vxa_read_pad (cs[port], &p);
		if (port == 0) { p.btn |= vxa_tpad.btn; if (vxa_tpad.ax[0][0] || vxa_tpad.ax[0][1]) { p.ax[0][0] = vxa_tpad.ax[0][0]; p.ax[0][1] = vxa_tpad.ax[0][1]; } }
		uint32_t ch = p.btn ^ prev[port];
		for (int i = 0; i < 16; i++) if (ch >> i & 1) vlog ("in p%d %s %s%s", port + 1, vxa_bname[i], p.btn >> i & 1 ? "down" : "up", port == 0 && (vxa_tpad.btn >> i & 1) ? " (touch)" : "");
		prev[port] = p.btn; vxa_pads[port] = p; }
}
static int16_t vxa_input_state (unsigned port, unsigned device, unsigned index, unsigned id)
{
	switch (device & RETRO_DEVICE_MASK) {
	case RETRO_DEVICE_JOYPAD: return port < 2 && id < 16 ? (vxa_pads[port].btn >> id & 1) : 0;
	case RETRO_DEVICE_ANALOG: return port < 2 && index < 2 && id < 2 ? vxa_pads[port].ax[index][id] : 0;
	case RETRO_DEVICE_KEYBOARD: return id < RETROK_LAST ? vxa_kb[id] : 0;
	case RETRO_DEVICE_POINTER:
		if (port || index) return 0;
		if (id == RETRO_DEVICE_ID_POINTER_PRESSED) return (int16_t) vxa_ptr.on;
		if (id == RETRO_DEVICE_ID_POINTER_X) return (int16_t) (vxa_ptr.x * 32767);
		if (id == RETRO_DEVICE_ID_POINTER_Y) return (int16_t) (vxa_ptr.y * 32767);
		return 0;
	default: return 0;
	}
}
static unsigned vxa_hid_to_retrok (GCKeyCode c, uint32_t *ch)
{
	*ch = 0; long u = (long) c;
	if (u >= 0x04 && u <= 0x1D) { *ch = 'a' + (uint32_t) (u - 0x04); return RETROK_a + (unsigned) (u - 0x04); }
	if (u >= 0x1E && u <= 0x26) { *ch = '1' + (uint32_t) (u - 0x1E); return RETROK_1 + (unsigned) (u - 0x1E); }
	if (u >= 0x3A && u <= 0x45) return RETROK_F1 + (unsigned) (u - 0x3A);
	switch (u) {
	case 0x27: *ch = '0'; return RETROK_0;  case 0x28: *ch = 13; return RETROK_RETURN; case 0x29: return RETROK_ESCAPE;
	case 0x2A: *ch = 8; return RETROK_BACKSPACE; case 0x2B: *ch = 9; return RETROK_TAB; case 0x2C: *ch = ' '; return RETROK_SPACE;
	case 0x2D: *ch = '-'; return RETROK_MINUS; case 0x2E: *ch = '='; return RETROK_EQUALS; case 0x35: *ch = '`'; return RETROK_BACKQUOTE;
	case 0x36: *ch = ','; return RETROK_COMMA; case 0x37: *ch = '.'; return RETROK_PERIOD; case 0x38: *ch = '/'; return RETROK_SLASH;
	case 0x4B: return RETROK_PAGEUP; case 0x4E: return RETROK_PAGEDOWN; case 0x4F: return RETROK_RIGHT; case 0x50: return RETROK_LEFT;
	case 0x51: return RETROK_DOWN; case 0x52: return RETROK_UP; case 0xE0: return RETROK_LCTRL; case 0xE1: return RETROK_LSHIFT;
	case 0xE2: return RETROK_LALT; case 0xE3: return RETROK_LSUPER; case 0xE4: return RETROK_RCTRL; case 0xE5: return RETROK_RSHIFT;
	case 0xE6: return RETROK_RALT; case 0xE7: return RETROK_RSUPER;
	default: return RETROK_UNKNOWN;
	}
}
static void vxa_key (GCKeyCode code, BOOL down)
{
	uint32_t ch; unsigned k = vxa_hid_to_retrok (code, &ch);
	vlog ("in key 0x%02lx -> %u %s", (long) code, k, down ? "down" : "up");
	if (k == RETROK_UNKNOWN || k >= RETROK_LAST) return;
	vxa_kb[k] = down ? 1 : 0;
	uint16_t mods = (vxa_kb[RETROK_LSHIFT] || vxa_kb[RETROK_RSHIFT] ? RETROKMOD_SHIFT : 0) | (vxa_kb[RETROK_LCTRL] || vxa_kb[RETROK_RCTRL] ? RETROKMOD_CTRL : 0);
	if (vxa_kbcb) vxa_kbcb (down, k, down ? ch : 0, mods);
}

/* ---------------------------------------------------------------- the content pack: .tgz streamed into Documents */
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
		} else {                                                /* (a pax header or a GNU long name: the next entry's name) */
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
static int vxa_untgz (const char *tgz, const char *root, int *files)    /* (0: whole) */
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

/* ---------------------------------------------------------------- the view: OpenGL ES, the touch controls, messages */
@interface VXGLView : UIView
@end
@implementation VXGLView
+ (Class) layerClass { return [CAEAGLLayer class]; }
@end

@interface VXGameVC : UIViewController
- (void) gotFrame:(unsigned)w :(unsigned)h;
- (void) setRunning:(int)on;
@end
void vxa_set_callbacks (void);
static __weak VXGameVC *vxa_vc; static UILabel *vxa_toast_l;
static void vxa_toast (NSString *s, double secs)
{
	if (!vxa_toast_l) return;
	vxa_toast_l.text = [NSString stringWithFormat:@"  %@  ", s]; [vxa_toast_l sizeToFit];
	CGRect b = vxa_toast_l.superview.bounds; UIEdgeInsets sa = vxa_toast_l.superview.safeAreaInsets;
	vxa_toast_l.frame = CGRectMake ((b.size.width - vxa_toast_l.frame.size.width - 8) / 2, sa.top + 8, vxa_toast_l.frame.size.width + 8, 30);
	vxa_toast_l.alpha = 1; [vxa_toast_l.layer removeAllAnimations];
	[UIView animateWithDuration:0.4 delay:(secs > 0.5 ? secs : 2) options:0 animations:^{ vxa_toast_l.alpha = 0; } completion:nil];
}

@implementation VXGameVC {
	VXGLView *_gl; EAGLContext *_ctx; GLuint _dfbo, _drb; GLint _dw, _dh;
	CADisplayLink *_link; double _last, _acc; int _started, _running;
	unsigned _fw, _fh; int _have;                                   /* (the last picture from the core) */
	CGRect _pic;                                                   /* (where it is on the screen, in points) */
	UIView *_ctl; CAShapeLayer *_stickBase, *_stickKnob, *_btn[4]; CATextLayer *_btnT[4]; CGRect _btnR[4];
	UITouch *_stickT, *_ptrT, *_btnTouch[4]; CGPoint _stickC; CGRect _stickZone;
	UILabel *_status; UIProgressView *_prog; UIButton *_menuBtn;
	long _runs, _presents, _late, _ticks; double _stat_t;
}
- (BOOL) prefersStatusBarHidden { return YES; }
- (BOOL) prefersHomeIndicatorAutoHidden { return YES; }
- (UIRectEdge) preferredScreenEdgesDeferringSystemGestures { return UIRectEdgeAll; }
- (void) viewDidLoad
{
	[super viewDidLoad]; vxa_vc = self; self.view.backgroundColor = UIColor.blackColor; self.view.multipleTouchEnabled = YES;
	_gl = [[VXGLView alloc] initWithFrame:self.view.bounds]; _gl.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
	_gl.userInteractionEnabled = NO; _gl.contentScaleFactor = UIScreen.mainScreen.nativeScale;
	CAEAGLLayer *l = (CAEAGLLayer *) _gl.layer; l.opaque = YES;
	l.drawableProperties = @{ kEAGLDrawablePropertyRetainedBacking: @NO, kEAGLDrawablePropertyColorFormat: kEAGLColorFormatRGBA8 };
	[self.view addSubview:_gl];
	_ctl = [[UIView alloc] initWithFrame:self.view.bounds]; _ctl.autoresizingMask = _gl.autoresizingMask; _ctl.userInteractionEnabled = NO; [self.view addSubview:_ctl];
	UIColor *g = [UIColor colorWithRed:0x66 / 255.0 green:0xaa / 255.0 blue:0x88 / 255.0 alpha:1];
	_stickBase = [CAShapeLayer layer]; _stickBase.fillColor = [UIColor colorWithRed:0.06 green:0.11 blue:0.09 alpha:0.6].CGColor; _stickBase.strokeColor = [UIColor colorWithRed:0x2f / 255.0 green:0x55 / 255.0 blue:0x44 / 255.0 alpha:1].CGColor; _stickBase.lineWidth = 1.5;
	_stickKnob = [CAShapeLayer layer]; _stickKnob.fillColor = [UIColor colorWithRed:0x16 / 255.0 green:0x30 / 255.0 blue:0x2a / 255.0 alpha:1].CGColor; _stickKnob.strokeColor = g.CGColor; _stickKnob.lineWidth = 1.5;
	[_ctl.layer addSublayer:_stickBase]; [_ctl.layer addSublayer:_stickKnob];
	for (int i = 0; i < 4; i++) {
		_btn[i] = [CAShapeLayer layer]; _btn[i].fillColor = _stickBase.fillColor; _btn[i].strokeColor = g.CGColor; _btn[i].lineWidth = 1.5; [_ctl.layer addSublayer:_btn[i]];
		_btnT[i] = [CATextLayer layer]; _btnT[i].string = [NSString stringWithFormat:@"%d", i + 1]; _btnT[i].fontSize = 22; _btnT[i].alignmentMode = kCAAlignmentCenter;
		_btnT[i].foregroundColor = [UIColor colorWithRed:0x88 / 255.0 green:0xcc / 255.0 blue:0xbb / 255.0 alpha:1].CGColor; _btnT[i].contentsScale = UIScreen.mainScreen.scale; [_ctl.layer addSublayer:_btnT[i]]; }
	vxa_toast_l = [UILabel new]; vxa_toast_l.font = [UIFont systemFontOfSize:14 weight:UIFontWeightMedium]; vxa_toast_l.textColor = [UIColor colorWithRed:0x88 / 255.0 green:0xcc / 255.0 blue:0xbb / 255.0 alpha:1];
	vxa_toast_l.backgroundColor = [UIColor colorWithRed:0x0b / 255.0 green:0x15 / 255.0 blue:0x11 / 255.0 alpha:0.92]; vxa_toast_l.layer.cornerRadius = 8; vxa_toast_l.layer.masksToBounds = YES; vxa_toast_l.alpha = 0;
	[self.view addSubview:vxa_toast_l];
	_menuBtn = [UIButton buttonWithType:UIButtonTypeSystem];
	[_menuBtn setImage:[UIImage systemImageNamed:@"line.3.horizontal"] forState:UIControlStateNormal]; _menuBtn.tintColor = [UIColor colorWithRed:0x88 / 255.0 green:0xcc / 255.0 blue:0xbb / 255.0 alpha:1];
	_menuBtn.backgroundColor = [UIColor colorWithRed:0x0b / 255.0 green:0x15 / 255.0 blue:0x11 / 255.0 alpha:0.75]; _menuBtn.layer.cornerRadius = 22;
	_menuBtn.layer.borderWidth = 1; _menuBtn.layer.borderColor = [UIColor colorWithRed:0x2f / 255.0 green:0x55 / 255.0 blue:0x44 / 255.0 alpha:1].CGColor;
	_menuBtn.accessibilityLabel = @"Menu"; _menuBtn.showsMenuAsPrimaryAction = YES; _menuBtn.hidden = YES;
	__weak VXGameVC *ws = self;
	_menuBtn.menu = [UIMenu menuWithChildren:@[ [UIDeferredMenuElement elementWithUncachedProvider:^(void (^done) (NSArray<UIMenuElement *> *)) { done (@[ [ws buildMenu] ]); }] ]];
	[self.view addSubview:_menuBtn];
	_status = [UILabel new]; _status.textColor = g; _status.font = [UIFont systemFontOfSize:15]; _status.numberOfLines = 0; _status.textAlignment = NSTextAlignmentCenter; [self.view addSubview:_status];
	_prog = [UIProgressView new]; _prog.progressTintColor = g; _prog.trackTintColor = [UIColor colorWithRed:0x16 / 255.0 green:0x30 / 255.0 blue:0x2a / 255.0 alpha:1]; _prog.hidden = YES; [self.view addSubview:_prog];
	NSNotificationCenter *nc = NSNotificationCenter.defaultCenter;
	for (NSString *n in @[ GCControllerDidConnectNotification, GCControllerDidDisconnectNotification, GCKeyboardDidConnectNotification, GCKeyboardDidDisconnectNotification ])
		[nc addObserver:self selector:@selector(devicesChanged:) name:n object:nil];
	[nc addObserver:self selector:@selector(thermal) name:NSProcessInfoThermalStateDidChangeNotification object:nil];
}
- (void) viewDidLayoutSubviews
{
	[super viewDidLayoutSubviews];
	CGRect b = self.view.bounds; { UIEdgeInsets s0 = self.view.safeAreaInsets; _menuBtn.frame = CGRectMake (b.size.width - s0.right - 56, s0.top + 8, 44, 44); }
	_status.frame = CGRectMake (24, b.size.height / 2 - 60, b.size.width - 48, 80); _prog.frame = CGRectMake (40, b.size.height / 2 + 30, b.size.width - 80, 4);
	if (_ctx) { [EAGLContext setCurrentContext:_ctx]; [self makeDisplayBuffer]; }
	[self layoutControls];
	UIEdgeInsets s = self.view.safeAreaInsets;
	vlog ("layout: view %.0f x %.0f pt, safe %.0f %.0f %.0f %.0f, scale %.2f, %s", b.size.width, b.size.height, s.top, s.left, s.bottom, s.right, UIScreen.mainScreen.nativeScale, b.size.width > b.size.height ? "landscape" : "portrait");
}
- (void) viewDidAppear:(BOOL)a
{
	[super viewDidAppear:a]; if (_started) return; _started = 1;
	struct utsname u; uname (&u); UIScreen *sc = UIScreen.mainScreen;
	vlog ("Vectrex_EMU %s (%s) on %s, iOS %s; screen %.0f x %.0f pt, %.0f x %.0f px, scale %.2f, up to %ld Hz; memory %.0f MB",
		[[NSBundle.mainBundle objectForInfoDictionaryKey:@"CFBundleShortVersionString"] UTF8String], [[NSBundle.mainBundle objectForInfoDictionaryKey:@"CFBundleVersion"] UTF8String],
		u.machine, UIDevice.currentDevice.systemVersion.UTF8String, sc.bounds.size.width, sc.bounds.size.height, sc.nativeBounds.size.width, sc.nativeBounds.size.height,
		sc.nativeScale, (long) sc.maximumFramesPerSecond, NSProcessInfo.processInfo.physicalMemory / 1048576.0);
	NSString *docs = NSSearchPathForDirectoriesInDomains (NSDocumentDirectory, NSUserDomainMask, YES).firstObject;
	snprintf (vxa_sys, sizeof vxa_sys, "%s/system", docs.UTF8String); snprintf (vxa_saves, sizeof vxa_saves, "%s/saves", docs.UTF8String);
	vxa_mkdirs (vxa_sys, 1); vxa_mkdirs (vxa_saves, 1);
	[self checkPack];
}
/* the content pack: its version from the server; a new one (or none here yet) is fetched and unpacked */
- (void) checkPack
{
#ifdef VX_LOG_TOKEN
	NSString *docs = NSSearchPathForDirectoriesInDomains (NSDocumentDirectory, NSUserDomainMask, YES).firstObject;
	NSString *have = [[NSString stringWithContentsOfFile:[docs stringByAppendingPathComponent:@"system/vectrex_emu/pack_version.txt"] encoding:NSUTF8StringEncoding error:nil]
		stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceAndNewlineCharacterSet];
	NSMutableURLRequest *rq = [NSMutableURLRequest requestWithURL:[NSURL URLWithString:@"http://172.233.60.40:7746/iosextra/packver"]];
	[rq setValue:@VX_LOG_TOKEN forHTTPHeaderField:@"X-Vectrex-Token"]; rq.timeoutInterval = 8;
	_status.text = @"Looking for games...";
	[[NSURLSession.sharedSession dataTaskWithRequest:rq completionHandler:^(NSData *d, NSURLResponse *r, NSError *e) {
		NSString *want = d && ((NSHTTPURLResponse *) r).statusCode == 200 ? [[[NSString alloc] initWithData:d encoding:NSUTF8StringEncoding] stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceAndNewlineCharacterSet] : nil;
		vlog ("pack: here %s, server %s%s", have ? have.UTF8String : "none", want ? want.UTF8String : "unreachable", e ? [@" - " stringByAppendingString:e.localizedDescription].UTF8String : "");
		dispatch_async (dispatch_get_main_queue (), ^{
			if (want && ![want isEqualToString:have ?: @""]) [self fetchPack:want];
			else if (have) [self startCore];
			else self->_status.text = @"The games could not be fetched: no connection to the project's server. Close the app and try again."; });
	}] resume];
#else
	vlog ("pack: no token in this build - the content must be in the app's folder already");
	[self startCore];
#endif
}
- (void) fetchPack:(NSString *)ver
{
#ifdef VX_LOG_TOKEN
	_status.text = @"Fetching the games, overlays, UVMC2 and PiTrex\n(once for each new set)"; _prog.hidden = NO; _prog.progress = 0;
	NSMutableURLRequest *rq = [NSMutableURLRequest requestWithURL:[NSURL URLWithString:@"http://172.233.60.40:7746/iosextra/pack"]];
	[rq setValue:@VX_LOG_TOKEN forHTTPHeaderField:@"X-Vectrex-Token"]; rq.timeoutInterval = 60;
	double t0 = CACurrentMediaTime(); vlog ("pack %s: fetching", ver.UTF8String);
	NSURLSessionDownloadTask *t = [NSURLSession.sharedSession downloadTaskWithRequest:rq completionHandler:^(NSURL *loc, NSURLResponse *r, NSError *e) {
		if (!loc || ((NSHTTPURLResponse *) r).statusCode != 200) {
			vlog ("pack: fetch failed (%ld) %s", (long) ((NSHTTPURLResponse *) r).statusCode, e.localizedDescription.UTF8String);
			dispatch_async (dispatch_get_main_queue (), ^{ self->_prog.hidden = YES; self->_status.text = @"The games could not be fetched. Close the app and try again."; }); return; }
		NSString *tmp = [NSTemporaryDirectory () stringByAppendingPathComponent:@"pack.tgz"]; [NSFileManager.defaultManager removeItemAtPath:tmp error:nil];
		[NSFileManager.defaultManager moveItemAtURL:loc toURL:[NSURL fileURLWithPath:tmp] error:nil];
		double t1 = CACurrentMediaTime(); long long sz = [[NSFileManager.defaultManager attributesOfItemAtPath:tmp error:nil] fileSize];
		vlog ("pack: %.1f MB in %.1f s; unpacking", sz / 1e6, t1 - t0);
		dispatch_async (dispatch_get_main_queue (), ^{ self->_status.text = @"Unpacking..."; });
		NSString *docs = NSSearchPathForDirectoriesInDomains (NSDocumentDirectory, NSUserDomainMask, YES).firstObject; int files = 0;
		int ok = vxa_untgz (tmp.fileSystemRepresentation, docs.fileSystemRepresentation, &files);
		[NSFileManager.defaultManager removeItemAtPath:tmp error:nil];
		vlog ("pack %s: %d files unpacked in %.1f s%s", ver.UTF8String, files, CACurrentMediaTime() - t1, ok ? " - NOT WHOLE" : "");
		dispatch_async (dispatch_get_main_queue (), ^{ self->_prog.hidden = YES; [self startCore]; });
	}];
	[t resume];
	[NSTimer scheduledTimerWithTimeInterval:0.25 repeats:YES block:^(NSTimer *tm) {   /* (the progress bar; every 10 % in the log) */
		double p = t.countOfBytesExpectedToReceive > 0 ? (double) t.countOfBytesReceived / t.countOfBytesExpectedToReceive : 0;
		self->_prog.progress = (float) p; static int lastp = -1; int pc = (int) (p * 10);
		if (pc != lastp) { lastp = pc; vlog ("pack: %d %% (%.1f MB)", pc * 10, t.countOfBytesReceived / 1e6); }
		if (t.state != NSURLSessionTaskStateRunning) [tm invalidate]; }];
#endif
}
- (void) makeDisplayBuffer
{
	if (!_dfbo) { glGenFramebuffers (1, &_dfbo); glGenRenderbuffers (1, &_drb); }
	glBindRenderbuffer (GL_RENDERBUFFER, _drb); [_ctx renderbufferStorage:GL_RENDERBUFFER fromDrawable:(CAEAGLLayer *) _gl.layer];
	glBindFramebuffer (GL_FRAMEBUFFER, _dfbo); glFramebufferRenderbuffer (GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, _drb);
	glGetRenderbufferParameteriv (GL_RENDERBUFFER, GL_RENDERBUFFER_WIDTH, &_dw); glGetRenderbufferParameteriv (GL_RENDERBUFFER, GL_RENDERBUFFER_HEIGHT, &_dh);
	vlog ("display buffer %d x %d px", _dw, _dh);
}
- (void) startCore
{
	_status.text = @"";
	_ctx = [[EAGLContext alloc] initWithAPI:kEAGLRenderingAPIOpenGLES3]; [EAGLContext setCurrentContext:_ctx]; [self makeDisplayBuffer];
	vlog ("GL: %s / %s / %s", glGetString (GL_VENDOR), glGetString (GL_RENDERER), glGetString (GL_VERSION));
	retro_set_environment (vxa_env); vxa_set_callbacks ();
	retro_init ();
	NSString *game = [NSUserDefaults.standardUserDefaults stringForKey:@"lastGame"];
	NSString *docs = NSSearchPathForDirectoriesInDomains (NSDocumentDirectory, NSUserDomainMask, YES).firstObject;
	if (!game || ![NSFileManager.defaultManager fileExistsAtPath:[docs stringByAppendingPathComponent:game]]) game = @"roms/Mine Storm.bin";
	NSString *path = [docs stringByAppendingPathComponent:game];
	struct retro_game_info gi = { path.fileSystemRepresentation, NULL, 0, NULL };
	BOOL ok = retro_load_game (&gi);
	vxa_loaded = 1; for (NSString *s in vxa_early) fputs (s.UTF8String, stderr); [vxa_early removeAllObjects];   /* (the core's log exists now) */
	vlog ("game %s: %s", game.UTF8String, ok ? "loaded" : "FAILED");
	if (!ok) { _status.text = [NSString stringWithFormat:@"%@ could not be started.", game.lastPathComponent]; return; }
	retro_get_system_av_info (&vxa_av);
	vlog ("av: %u x %u (max %u x %u), aspect %.3f, %.2f Hz, sound %.0f Hz", vxa_av.geometry.base_width, vxa_av.geometry.base_height, vxa_av.geometry.max_width, vxa_av.geometry.max_height, vxa_av.geometry.aspect_ratio, vxa_av.timing.fps, vxa_av.timing.sample_rate);
	if (vxa_hw_on) { vxa_core_fb (vxa_av.geometry.max_width, vxa_av.geometry.max_height); if (vxa_hw.context_reset) vxa_hw.context_reset (); vlog ("GL context handed to the core"); }
	vxa_audio_start (vxa_av.timing.sample_rate);
	_link = [CADisplayLink displayLinkWithTarget:self selector:@selector(tick:)];
	if (@available (iOS 15.0, *)) { float f = (float) vxa_av.timing.fps; _link.preferredFrameRateRange = CAFrameRateRangeMake (f, f, f); }
	[_link addToRunLoop:NSRunLoop.mainRunLoop forMode:NSRunLoopCommonModes]; _running = 1; _menuBtn.hidden = NO;
	[self devicesChanged:nil]; _stat_t = CACurrentMediaTime();
}
- (void) present
{
	if (!_have) return;
	glBindFramebuffer (GL_READ_FRAMEBUFFER, vxa_cfbo); glBindFramebuffer (GL_DRAW_FRAMEBUFFER, _dfbo);
	glDisable (GL_SCISSOR_TEST); glColorMask (1, 1, 1, 1); glViewport (0, 0, _dw, _dh); glClearColor (0, 0, 0, 1); glClear (GL_COLOR_BUFFER_BIT);
	/* (fit the picture, its own pixels if it fits: upright at the top of the safe area, sideways in the middle) */
	double sc = _gl.contentScaleFactor; UIEdgeInsets s = self.view.safeAreaInsets; CGSize v = self.view.bounds.size;
	double aw = (v.width - s.left - s.right) * sc, ah = (v.height - s.top - s.bottom) * sc, k = fmin (1.0, fmin (aw / _fw, ah / _fh));
	double w = _fw * k, h = _fh * k, x = s.left * sc + (aw - w) / 2, top = v.width > v.height ? s.top * sc + (ah - h) / 2 : s.top * sc;
	int y0 = (int) (_dh - top - h + 0.5);                      /* (GL: from the bottom) */
	int sy0 = 0, sy1 = (int) _fh; if (!vxa_hw.bottom_left_origin) { sy0 = (int) _fh; sy1 = 0; }
	glBlitFramebuffer (0, sy0, (GLint) _fw, sy1, (GLint) x, y0, (GLint) (x + w), (GLint) (y0 + h), GL_COLOR_BUFFER_BIT, k == 1.0 ? GL_NEAREST : GL_LINEAR);
	glBindRenderbuffer (GL_RENDERBUFFER, _drb); [_ctx presentRenderbuffer:GL_RENDERBUFFER]; _presents++;
	CGRect pic = CGRectMake (x / sc, top / sc, w / sc, h / sc);
	if (!CGRectEqualToRect (pic, _pic)) { _pic = pic; vlog ("picture %u x %u px at %.0f,%.0f %.0f x %.0f pt (%s)", _fw, _fh, pic.origin.x, pic.origin.y, pic.size.width, pic.size.height, k == 1.0 ? "1:1" : "scaled"); [self layoutControls]; }
}
- (void) gotFrame:(unsigned)w :(unsigned)h { _fw = w; _fh = h; _have = 1; }
- (void) tick:(CADisplayLink *)l
{
	double now = CACurrentMediaTime(), dt = _last ? now - _last : 0; _last = now; _ticks++;
	if (dt > 1.5 / (vxa_av.timing.fps > 0 ? vxa_av.timing.fps : 60)) _late++;
	if (dt > 0.25) dt = 0.25;
	_acc += dt * vxa_av.timing.fps; int n = (int) _acc; _acc -= n;
	double buf = vxa_ring_fill () / (vxa_av.timing.sample_rate > 0 ? vxa_av.timing.sample_rate : 44100);   /* (the sound decides: never far ahead, never dry) */
	if (buf > 0.12 && n > 0) n--; else if (buf < 0.03 && n < 3) n++;
	if (n > 4) n = 4;
	[EAGLContext setCurrentContext:_ctx];
	for (int i = 0; i < n; i++) { vxa_input_poll (); retro_run (); _runs++; }
	if (n) [self present];
	if (now - _stat_t >= 5) {
		double t = now - _stat_t; _stat_t = now;
		vlog ("stat: %.0f runs/s, %.0f pictures/s, %ld late of %ld ticks, sound %.0f ms, %u dry, %u dropped, %.0f MB, heat %ld, controllers %lu, keyboard %d",
			_runs / t, _presents / t, _late, _ticks, buf * 1000, vxa_under, vxa_drop, vxa_mem_mb (), (long) NSProcessInfo.processInfo.thermalState,
			(unsigned long) GCController.controllers.count, GCKeyboard.coalescedKeyboard != nil);
		_runs = _presents = _late = _ticks = 0; vxa_under = vxa_drop = 0; }
}
/* the menu (step 1's stopgap until the native screens of step 2): the PC's keys for the actions the core knows, every
 * core setting as a pull-down with its value ticked */
static void vxa_press (unsigned k, int ctrl)
{
	vlog ("menu: key %u%s", k, ctrl ? " with Ctrl" : "");
	if (ctrl) { vxa_kb[RETROK_LCTRL] = 1; if (vxa_kbcb) vxa_kbcb (true, RETROK_LCTRL, 0, RETROKMOD_CTRL); }
	vxa_kb[k] = 1; if (vxa_kbcb) vxa_kbcb (true, k, k < 128 ? k : 0, ctrl ? RETROKMOD_CTRL : 0);
	dispatch_after (dispatch_time (DISPATCH_TIME_NOW, (int64_t) (0.12 * NSEC_PER_SEC)), dispatch_get_main_queue (), ^{
		vxa_kb[k] = 0; if (vxa_kbcb) vxa_kbcb (false, k, 0, ctrl ? RETROKMOD_CTRL : 0);
		if (ctrl) { vxa_kb[RETROK_LCTRL] = 0; if (vxa_kbcb) vxa_kbcb (false, RETROK_LCTRL, 0, 0); } });
}
static void vxa_set_opt (int i, NSString *v)
{
	snprintf (vxa_opt[i].val, sizeof vxa_opt[i].val, "%s", v.UTF8String); vxa_opt_store (i); vxa_opt_dirty = 1;
	vlog ("menu: %s = %s", vxa_opt[i].key, vxa_opt[i].val);
}
- (UIMenu *) optionMenu:(int)i title:(NSString *)title
{
	NSMutableArray *acts = [NSMutableArray array];
	for (NSString *v in [@(vxa_opt[i].vals) componentsSeparatedByString:@"|"]) {
		UIAction *a = [UIAction actionWithTitle:v image:nil identifier:nil handler:^(UIAction *x) { (void) x; vxa_set_opt (i, v); }];
		if (!strcmp (vxa_opt[i].val, v.UTF8String)) a.state = UIMenuElementStateOn;
		[acts addObject:a]; }
	UIMenu *m = [UIMenu menuWithTitle:title children:acts];
	if (@available (iOS 15.0, *)) m.subtitle = @(vxa_opt[i].val);
	return m;
}
- (UIMenu *) buildMenu
{
	vlog ("menu opened");
	UIAction *(^act) (NSString *, NSString *, void (^) (void)) = ^UIAction * (NSString *t, NSString *img, void (^f) (void)) {
		return [UIAction actionWithTitle:t image:[UIImage systemImageNamed:img] identifier:nil handler:^(UIAction *x) { (void) x; f (); }]; };
	NSMutableArray *top = [NSMutableArray arrayWithArray:@[
		act (@"Games", @"square.grid.2x2", ^{ vxa_press (RETROK_l, 0); }),
		act (@"RESET", @"arrow.counterclockwise", ^{ vxa_press (RETROK_r, 1); }),
		act (@"Eject / insert cartridge", @"eject", ^{ vxa_press (RETROK_e, 1); }),
		act (@"Pause", @"pause", ^{ vxa_press (RETROK_p, 0); }),
		act (@"Overlay on / off", @"square.on.square", ^{ vxa_press (RETROK_o, 0); }) ]];
	int dk = vxa_opt_find ("vectrex_emu_deck"); if (dk >= 0) [top addObject:[self optionMenu:dk title:@"Control deck"]];
	/* (every setting, grouped by the part before the colon of its name: "Overlay: plastic glow" under Overlay) */
	NSMutableDictionary<NSString *, NSMutableArray *> *groups = [NSMutableDictionary dictionary]; NSMutableArray *order = [NSMutableArray array];
	for (int i = 0; i < vxa_nopt; i++) {
		NSString *d = @(vxa_opt[i].desc); NSRange c = [d rangeOfString:@": "];
		NSString *g = c.location != NSNotFound ? [d substringToIndex:c.location] : @"General", *t = c.location != NSNotFound ? [d substringFromIndex:c.location + 2] : d;
		if (!groups[g]) { groups[g] = [NSMutableArray array]; [order addObject:g]; }
		[groups[g] addObject:[self optionMenu:i title:t]]; }
	NSMutableArray *all = [NSMutableArray array];
	for (NSString *g in order) [all addObject:[UIMenu menuWithTitle:g children:groups[g]]];
	[top addObject:[UIMenu menuWithTitle:@"All settings" image:[UIImage systemImageNamed:@"slider.horizontal.3"] identifier:nil options:0 children:all]];
	return [UIMenu menuWithTitle:@"Vectrex_EMU" children:top];
}
- (void) setRunning:(int)on
{
	if (!_link || on == _running) return; _running = on; _link.paused = !on; _last = 0;
	if (on) [vxa_eng startAndReturnError:nil]; else [vxa_eng pause];
	vlog ("%s", on ? "resumed" : "paused (the app in the background)");
}
- (void) thermal { vlog ("heat: %ld", (long) NSProcessInfo.processInfo.thermalState); }
- (void) devicesChanged:(NSNotification *)n
{
	for (GCController *c in GCController.controllers) if (c.extendedGamepad) c.extendedGamepad.valueChangedHandler = nil;
	GCKeyboard *kb = GCKeyboard.coalescedKeyboard;
	if (kb) kb.keyboardInput.keyChangedHandler = ^(GCKeyboardInput *i, GCControllerButtonInput *b, GCKeyCode code, BOOL down) { (void) i; (void) b; vxa_key (code, down); };
	NSMutableString *s = [NSMutableString string];
	for (GCController *c in GCController.controllers) [s appendFormat:@"%@ (%@); ", c.vendorName ?: @"?", c.productCategory ?: @"?"];
	vlog ("devices%s: controllers %s keyboard %s", n ? [@" " stringByAppendingString:n.name].UTF8String : "", s.length ? s.UTF8String : "none; ", kb ? "yes" : "no");
	[self layoutControls];
}
/* the touch controls: a stick where the thumb lands (left), buttons 1-4 as the Vectrex controller (right); only while no
 * controller and no keyboard is connected; below the picture upright, beside it sideways */
- (void) layoutControls
{
	BOOL show = GCController.controllers.count == 0 && GCKeyboard.coalescedKeyboard == nil && _running;
	_ctl.hidden = !show; if (!show) return;
	CGSize v = self.view.bounds.size; UIEdgeInsets s = self.view.safeAreaInsets; CGRect pic = _pic;
	CGRect zl, zr;
	if (v.width > v.height) { zl = CGRectMake (s.left, s.top, CGRectGetMinX (pic) - s.left, v.height - s.top - s.bottom); zr = CGRectMake (CGRectGetMaxX (pic), s.top, v.width - s.right - CGRectGetMaxX (pic), zl.size.height); }
	else { double top = CGRectGetMaxY (pic) + 60; if (v.height - s.bottom - top < 150) top = v.height - s.bottom - 150;   /* (room for the deck's row; too little: over the picture's foot) */
		zl = CGRectMake (s.left, top, v.width / 2, v.height - s.bottom - top); zr = CGRectMake (v.width / 2, top, v.width / 2 - s.right, zl.size.height); }
	_stickZone = zl;
	double r = fmin (zr.size.width / 4.6, 38), gap = r * 0.35, cx = CGRectGetMidX (zr), cy = CGRectGetMidY (zr);
	for (int i = 0; i < 4; i++) {
		double bx = cx + (i % 2 ? r + gap / 2 : -r - gap / 2), by = cy + (i < 2 ? -r - gap / 2 : r + gap / 2);
		_btnR[i] = CGRectMake (bx - r, by - r, 2 * r, 2 * r);
		_btn[i].path = [UIBezierPath bezierPathWithOvalInRect:_btnR[i]].CGPath;
		_btnT[i].frame = CGRectMake (bx - r, by - 14, 2 * r, 28); }
	if (!_stickT) { _stickC = CGPointMake (CGRectGetMidX (zl), CGRectGetMidY (zl)); [self drawStick:_stickC]; }
}
- (void) drawStick:(CGPoint)knob
{
	double R = 60; _stickBase.path = [UIBezierPath bezierPathWithOvalInRect:CGRectMake (_stickC.x - R, _stickC.y - R, 2 * R, 2 * R)].CGPath;
	_stickKnob.path = [UIBezierPath bezierPathWithOvalInRect:CGRectMake (knob.x - 26, knob.y - 26, 52, 52)].CGPath;
}
- (void) touchesBegan:(NSSet<UITouch *> *)ts withEvent:(UIEvent *)e
{
	for (UITouch *t in ts) {
		CGPoint p = [t locationInView:self.view]; NSString *what = @"nothing";
		if (!_ctl.hidden) {
			int hit = -1; for (int i = 0; i < 4; i++) if (CGRectContainsPoint (CGRectInset (_btnR[i], -8, -8), p)) hit = i;
			if (hit >= 0 && !_btnTouch[hit]) { _btnTouch[hit] = t; static const int id4[4] = { RETRO_DEVICE_ID_JOYPAD_Y, RETRO_DEVICE_ID_JOYPAD_X, RETRO_DEVICE_ID_JOYPAD_B, RETRO_DEVICE_ID_JOYPAD_A };
				vxa_tpad.btn |= 1u << id4[hit]; _btn[hit].fillColor = [UIColor colorWithRed:0x24 / 255.0 green:0x44 / 255.0 blue:0x3a / 255.0 alpha:0.9].CGColor;
				UIImpactFeedbackGenerator *h = [[UIImpactFeedbackGenerator alloc] initWithStyle:UIImpactFeedbackStyleLight]; [h impactOccurred]; what = [NSString stringWithFormat:@"button %d", hit + 1]; }
			else if (!_stickT && CGRectContainsPoint (_stickZone, p)) { _stickT = t; _stickC = p; [self drawStick:p]; what = @"stick"; }
		}
		if ([what isEqualToString:@"nothing"] && !_ptrT && CGRectContainsPoint (_pic, p)) { _ptrT = t; [self pointerAt:p on:1]; what = @"picture"; }
		vlog ("touch down %.0f,%.0f: %s", p.x, p.y, what.UTF8String);
	}
}
- (void) pointerAt:(CGPoint)p on:(int)on
{
	vxa_ptr.on = on; vxa_ptr.x = (p.x - _pic.origin.x) / _pic.size.width * 2 - 1; vxa_ptr.y = (p.y - _pic.origin.y) / _pic.size.height * 2 - 1;
	if (vxa_ptr.x < -1) vxa_ptr.x = -1; if (vxa_ptr.x > 1) vxa_ptr.x = 1; if (vxa_ptr.y < -1) vxa_ptr.y = -1; if (vxa_ptr.y > 1) vxa_ptr.y = 1;
}
- (void) touchesMoved:(NSSet<UITouch *> *)ts withEvent:(UIEvent *)e
{
	for (UITouch *t in ts) {
		CGPoint p = [t locationInView:self.view];
		if (t == _ptrT) [self pointerAt:p on:1];
		if (t == _stickT) {
			double dx = p.x - _stickC.x, dy = p.y - _stickC.y, R = 60, d = hypot (dx, dy); if (d > R) { dx *= R / d; dy *= R / d; }
			[self drawStick:CGPointMake (_stickC.x + dx, _stickC.y + dy)];
			double ax = dx / R, ay = dy / R; uint32_t b = vxa_tpad.btn & ~(0xFu << RETRO_DEVICE_ID_JOYPAD_UP);
			if (ay < -0.4) b |= 1u << RETRO_DEVICE_ID_JOYPAD_UP; if (ay > 0.4) b |= 1u << RETRO_DEVICE_ID_JOYPAD_DOWN;
			if (ax < -0.4) b |= 1u << RETRO_DEVICE_ID_JOYPAD_LEFT; if (ax > 0.4) b |= 1u << RETRO_DEVICE_ID_JOYPAD_RIGHT;
			vxa_tpad.btn = b; vxa_tpad.ax[0][0] = (int16_t) (ax * 32767); vxa_tpad.ax[0][1] = (int16_t) (ay * 32767); }
	}
}
- (void) touchesEnded:(NSSet<UITouch *> *)ts withEvent:(UIEvent *)e
{
	for (UITouch *t in ts) {
		CGPoint p = [t locationInView:self.view];
		if (t == _ptrT) { _ptrT = nil; [self pointerAt:p on:0]; }
		if (t == _stickT) { _stickT = nil; vxa_tpad.btn &= ~(0xFu << RETRO_DEVICE_ID_JOYPAD_UP); vxa_tpad.ax[0][0] = vxa_tpad.ax[0][1] = 0; [self layoutControls]; }
		for (int i = 0; i < 4; i++) if (t == _btnTouch[i]) { static const int id4[4] = { RETRO_DEVICE_ID_JOYPAD_Y, RETRO_DEVICE_ID_JOYPAD_X, RETRO_DEVICE_ID_JOYPAD_B, RETRO_DEVICE_ID_JOYPAD_A };
			_btnTouch[i] = nil; vxa_tpad.btn &= ~(1u << id4[i]); _btn[i].fillColor = _stickBase.fillColor; }
		vlog ("touch up %.0f,%.0f", p.x, p.y);
	}
}
- (void) touchesCancelled:(NSSet<UITouch *> *)ts withEvent:(UIEvent *)e { [self touchesEnded:ts withEvent:e]; }
@end

/* ---------------------------------------------------------------- the core's other callbacks */
static void vxa_video (const void *d, unsigned w, unsigned h, size_t pitch)
{
	(void) pitch;
	if (d == RETRO_HW_FRAME_BUFFER_VALID) { [vxa_vc gotFrame:w :h]; return; }
	if (!d) return;                                    /* (the same picture again) */
	static int told; if (!told) { told = 1; vlog ("a picture without the GPU (%u x %u) - not shown in this step", w, h); }
}
static void vxa_poll (void) {}
void vxa_set_callbacks (void)
{
	retro_set_video_refresh (vxa_video); retro_set_audio_sample (vxa_audio_sample); retro_set_audio_sample_batch (vxa_audio_batch);
	retro_set_input_poll (vxa_poll); retro_set_input_state (vxa_input_state);
}

/* ---------------------------------------------------------------- the app */
@interface VXAppDelegate : UIResponder <UIApplicationDelegate>
@property (strong, nonatomic) UIWindow *window;
@end
@implementation VXAppDelegate
- (BOOL) application:(UIApplication *)a didFinishLaunchingWithOptions:(NSDictionary *)o
{
	vxa_log_start (); vlog ("app start");
	self.window = [[UIWindow alloc] initWithFrame:UIScreen.mainScreen.bounds]; self.window.rootViewController = [VXGameVC new];
	self.window.backgroundColor = UIColor.blackColor; [self.window makeKeyAndVisible]; return YES;
}
- (void) applicationWillResignActive:(UIApplication *)a { [vxa_vc setRunning:0]; }
- (void) applicationDidBecomeActive:(UIApplication *)a { [vxa_vc setRunning:1]; }
- (void) applicationDidReceiveMemoryWarning:(UIApplication *)a { vlog ("memory warning: %.0f MB in use", vxa_mem_mb ()); }
- (void) applicationWillTerminate:(UIApplication *)a { vlog ("the app ends"); retro_unload_game (); retro_deinit (); }
@end
