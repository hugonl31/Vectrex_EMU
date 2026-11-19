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
#import <Metal/Metal.h>
#import <CoreVideo/CoreVideo.h>
#include <dlfcn.h>
#include <sys/utsname.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <mach/mach.h>
#include <zlib.h>
#include "libretro.h"
#include "vx_token.h"
#import "VXApp.h"
#import <Network/Network.h>
static nw_path_monitor_t vxa_mon; static volatile int vxa_expensive;   /* (mobile data, a personal hotspot: costs the user) */
int vxa_net_expensive (void) { return vxa_expensive; }
static void vxa_net_watch (void)
{
	if (vxa_mon) return; vxa_mon = nw_path_monitor_create ();
	nw_path_monitor_set_update_handler (vxa_mon, ^(nw_path_t p) { int e = nw_path_is_expensive (p) || nw_path_uses_interface_type (p, nw_interface_type_cellular);
		if (e != vxa_expensive) { vxa_expensive = e; } });
	nw_path_monitor_set_queue (vxa_mon, dispatch_get_main_queue ()); nw_path_monitor_start (vxa_mon);
}
@class VXGameVC; static __weak VXGameVC *vxa_vc; static int vxa_holding;
extern const char *vx_app_game (void); extern void vx_app_load (const char *path); extern int vx_app_browser_wanted (void);
void vxa_hdr_update (void); float vxa_hdr_headroom (void);

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
static struct { char key[64]; char val[160]; char def[160]; char vals[1200]; char desc[160]; char cat[32]; char info[600]; } vxa_opt[VXA_NOPT]; static int vxa_nopt, vxa_opt_dirty;
static struct { char key[32]; char desc[64]; char info[200]; } vxa_cat[16]; static int vxa_ncat;
/* (libretro.h's core options v2 - not in this repo's older header: the same layout, as the core declares it) */
struct vxa_ov { const char *value, *label; }; struct vxa_oc { const char *key, *desc, *info; };
struct vxa_od { const char *key, *desc, *desc_categorized, *info, *info_categorized, *category_key; struct vxa_ov values[128]; const char *default_value; };
struct vxa_o2 { struct vxa_oc *categories; struct vxa_od *definitions; };
static int vxa_opt_find (const char *k) { for (int i = 0; i < vxa_nopt; i++) if (!strcmp (vxa_opt[i].key, k)) return i; return -1; }
static void vxa_opt_store (int i)
{
	[[NSUserDefaults standardUserDefaults] setObject:@(vxa_opt[i].val) forKey:[NSString stringWithFormat:@"opt.%s", vxa_opt[i].key]];
}
static void vxa_opts_defaults (void);
static void vxa_opts_v2 (const struct vxa_o2 *o)   /* (the definitions with their submenus and help texts) */
{
	vxa_nopt = 0; vxa_ncat = 0;
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
	vxa_opts_defaults ();
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
	vxa_opts_defaults ();
}
static void vxa_opts_defaults (void)
{
	for (int i = 0; i < vxa_nopt; i++) { NSString *sv = [[NSUserDefaults standardUserDefaults] stringForKey:[NSString stringWithFormat:@"opt.%s", vxa_opt[i].key]];
		if (sv) snprintf (vxa_opt[i].val, sizeof vxa_opt[i].val, "%s", sv.UTF8String); }
	/* (this app's own defaults: the control deck below the picture - its cartridge opens the ROM browser, RESET, the knob) */
	int dk = vxa_opt_find ("vectrex_emu_deck");
	if (dk >= 0 && ![[NSUserDefaults standardUserDefaults] stringForKey:@"opt.vectrex_emu_deck"]) snprintf (vxa_opt[dk].val, sizeof vxa_opt[dk].val, "full");
	/* (this device's own: the screen's refresh rate - 120 Hz on ProMotion, else 60) */
	int r = vxa_opt_find ("vectrex_emu_rate");
	if (r >= 0 && ![[NSUserDefaults standardUserDefaults] stringForKey:@"opt.vectrex_emu_rate"])
		snprintf (vxa_opt[r].val, sizeof vxa_opt[r].val, "%s", UIScreen.mainScreen.maximumFramesPerSecond >= 120 ? "120 Hz" : "60 Hz");   /* (Metal shows the picture: ProMotion's 120 pictures a second) */
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
/* the core's framebuffer: one IOSurface the core draws into with OpenGL ES and the app shows with Metal (no copy) - half
 * floats: room above white for HDR (8-bit BGRA where the device will not) */
static CVPixelBufferRef vxa_pb; static CVOpenGLESTextureCacheRef vxa_gcache; static CVOpenGLESTextureRef vxa_gtex;
static CVMetalTextureCacheRef vxa_mcache; static CVMetalTextureRef vxa_mtex; static id<MTLDevice> vxa_mdev; static int vxa_half;
static void vxa_core_fb (unsigned w, unsigned h)              /* (the core's framebuffer at least w x h) */
{
	if (w <= vxa_cw && h <= vxa_ch && vxa_cfbo) return;
	if (w < vxa_cw) w = vxa_cw; if (h < vxa_ch) h = vxa_ch;
	if (!vxa_cfbo) glGenFramebuffers (1, &vxa_cfbo);
	if (!vxa_gcache) CVOpenGLESTextureCacheCreate (kCFAllocatorDefault, NULL, [EAGLContext currentContext], NULL, &vxa_gcache);
	if (!vxa_mcache && vxa_mdev) CVMetalTextureCacheCreate (kCFAllocatorDefault, NULL, vxa_mdev, NULL, &vxa_mcache);
	if (vxa_gtex) { CFRelease (vxa_gtex); vxa_gtex = NULL; } if (vxa_mtex) { CFRelease (vxa_mtex); vxa_mtex = NULL; } if (vxa_pb) { CVPixelBufferRelease (vxa_pb); vxa_pb = NULL; }
	NSDictionary *at = @{ (id) kCVPixelBufferIOSurfacePropertiesKey: @{}, (id) kCVPixelBufferOpenGLESCompatibilityKey: @YES, (id) kCVPixelBufferMetalCompatibilityKey: @YES };
	for (int half = 1; half >= 0 && !vxa_gtex; half--) {
		if (vxa_pb) { CVPixelBufferRelease (vxa_pb); vxa_pb = NULL; }
		if (CVPixelBufferCreate (kCFAllocatorDefault, w, h, half ? kCVPixelFormatType_64RGBAHalf : kCVPixelFormatType_32BGRA, (__bridge CFDictionaryRef) at, &vxa_pb) != kCVReturnSuccess) continue;
		CVReturn r = half ? CVOpenGLESTextureCacheCreateTextureFromImage (kCFAllocatorDefault, vxa_gcache, vxa_pb, NULL, GL_TEXTURE_2D, GL_RGBA16F, (GLsizei) w, (GLsizei) h, GL_RGBA, GL_HALF_FLOAT, 0, &vxa_gtex)
		                  : CVOpenGLESTextureCacheCreateTextureFromImage (kCFAllocatorDefault, vxa_gcache, vxa_pb, NULL, GL_TEXTURE_2D, GL_RGBA, (GLsizei) w, (GLsizei) h, GL_BGRA_EXT, GL_UNSIGNED_BYTE, 0, &vxa_gtex);
		if (r != kCVReturnSuccess) { vxa_gtex = NULL; continue; }
		vxa_half = half; if (vxa_mcache) CVMetalTextureCacheCreateTextureFromImage (kCFAllocatorDefault, vxa_mcache, vxa_pb, NULL, half ? MTLPixelFormatRGBA16Float : MTLPixelFormatBGRA8Unorm, w, h, 0, &vxa_mtex); }
	vxa_ctex = vxa_gtex ? CVOpenGLESTextureGetName (vxa_gtex) : 0;
	glBindTexture (GL_TEXTURE_2D, vxa_ctex);
	glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glBindFramebuffer (GL_FRAMEBUFFER, vxa_cfbo); glFramebufferTexture2D (GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, vxa_ctex, 0);
	if (vxa_hw.depth || vxa_hw.stencil) {
		if (!vxa_cdepth) glGenRenderbuffers (1, &vxa_cdepth);
		glBindRenderbuffer (GL_RENDERBUFFER, vxa_cdepth); glRenderbufferStorage (GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, (GLsizei) w, (GLsizei) h);
		glFramebufferRenderbuffer (GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, vxa_cdepth); }
	GLenum st = glCheckFramebufferStatus (GL_FRAMEBUFFER);
	vxa_cw = w; vxa_ch = h; vlog ("core framebuffer %u x %u, %s, shared with Metal: %s%s", w, h, vxa_half ? "half floats (HDR room)" : "8-bit", vxa_mtex ? "yes" : "NO",
		st == GL_FRAMEBUFFER_COMPLETE ? "" : " - NOT COMPLETE");
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
	case 52: *(unsigned *) data = 2; return true;           /* (GET_CORE_OPTIONS_VERSION: v2 - the submenus and help texts) */
	case 67: vxa_opts_v2 (data); return true;                /* (SET_CORE_OPTIONS_V2) */
	case 68: return true;                                    /* (SET_CORE_OPTIONS_UPDATE_DISPLAY_CALLBACK) */
	case RETRO_ENVIRONMENT_GET_VARIABLE: { struct retro_variable *v = data; int i = vxa_opt_find (v->key);
		if (i >= 0 && !strcmp (v->key, "vectrex_emu_deck")) { v->value = "off"; return true; }   /* (the app draws its own deck: the core's stays off) */
		v->value = i >= 0 ? vxa_opt[i].val : NULL; return i >= 0; }
	case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE: *(bool *) data = vxa_opt_dirty; vxa_opt_dirty = 0; return true;
	case 70: { const struct retro_variable *v = data; if (!v || !v->key) return true;   /* (SET_VARIABLE: the core sets one itself) */
		int i = vxa_opt_find (v->key); if (i < 0 || !v->value) return false;
		snprintf (vxa_opt[i].val, sizeof vxa_opt[i].val, "%s", v->value); vxa_opt_store (i); vlog ("core set option %s = %s", v->key, v->value); return true; }
	case RETRO_ENVIRONMENT_GET_LOG_INTERFACE: ((struct retro_log_callback *) data)->log = vxa_corelog; return true;
	case RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO: vxa_av = *(const struct retro_system_av_info *) data; vxa_av_changed = 1;
		vlog ("av info: %u x %u (max %u x %u), %.2f Hz (memory %.0f MB)", vxa_av.geometry.base_width, vxa_av.geometry.base_height, vxa_av.geometry.max_width, vxa_av.geometry.max_height, vxa_av.timing.fps, vxa_mem_mb ());
		if (vxa_cfbo) vxa_core_fb (vxa_av.geometry.max_width, vxa_av.geometry.max_height); return true;
	case RETRO_ENVIRONMENT_SET_GEOMETRY: vxa_av.geometry = *(const struct retro_game_geometry *) data;
		vlog ("geometry: %u x %u (memory %.0f MB)", vxa_av.geometry.base_width, vxa_av.geometry.base_height, vxa_mem_mb ()); return true;
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
	{ /* (Vectrex buttons 1-4 - the core's RetroPad Y X B A - on the buttons the player chose; the default as the
	   *  Vectrex controller's row on a modern pad's diamond: 1 left, 2 top, 3 bottom, 4 right) */
	  const BOOL phys[8] = { g.buttonA.pressed, g.buttonB.pressed, g.buttonX.pressed, g.buttonY.pressed, g.leftShoulder.pressed, g.rightShoulder.pressed, g.leftTrigger.pressed, g.rightTrigger.pressed };
	  static const int vid[4] = { RETRO_DEVICE_ID_JOYPAD_Y, RETRO_DEVICE_ID_JOYPAD_X, RETRO_DEVICE_ID_JOYPAD_B, RETRO_DEVICE_ID_JOYPAD_A }, def[4] = { 2, 3, 0, 1 };
	  for (int k = 0; k < 4; k++) { NSNumber *m = [NSUserDefaults.standardUserDefaults objectForKey:[NSString stringWithFormat:@"map.%d", k + 1]];
		int src = m ? m.intValue : def[k]; if (src >= 0 && src < 8 && phys[src]) b |= 1u << vid[k]; } }
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
		if (port == 0 && (p.btn >> RETRO_DEVICE_ID_JOYPAD_SELECT & 1) && (ch >> RETRO_DEVICE_ID_JOYPAD_X & 1) && (p.btn >> RETRO_DEVICE_ID_JOYPAD_X & 1)) {   /* (Select + X: the game menu) */
			vlog ("Select + X: the game menu"); dispatch_async (dispatch_get_main_queue (), ^{ if (vxa_vc) [VXUI showGameMenuFrom:vxa_vc]; }); }
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
	if ((long) code == 0x10 && !vxa_kb[RETROK_LCTRL] && !vxa_kb[RETROK_RCTRL]) {   /* (M: the game menu, as RetroArch's menu key was) */
		if (down) { vlog ("M: the game menu"); dispatch_async (dispatch_get_main_queue (), ^{ if (vxa_vc) [VXUI showGameMenuFrom:vxa_vc]; }); } return; }
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

/* ---------------------------------------------------------------- the content pack, fetched by iOS's background transfer
 * service: it goes on while the user is in another app (or the app is suspended); iOS wakes the app to unpack it and a
 * notification says the games are ready */
#import <UserNotifications/UserNotifications.h>
@interface VXPack : NSObject <NSURLSessionDownloadDelegate>
@property (nonatomic, copy) void (^progress) (int64_t, int64_t); @property (nonatomic, copy) void (^unpacking) (void); @property (nonatomic, copy) void (^done) (BOOL); @property (nonatomic, copy) void (^bgDone) (void);
@property (nonatomic, strong) NSURLSession *wifiSession;
@property (nonatomic, copy) NSString *ver; @property (nonatomic, strong) NSURLSession *session;
+ (instancetype) shared;
@end
@implementation VXPack { int _lastpc; double _t0; }
+ (instancetype) shared { static VXPack *p; static dispatch_once_t o; dispatch_once (&o, ^{ p = [VXPack new]; }); return p; }
- (NSURLSession *) session
{
	if (!_session) {
		NSURLSessionConfiguration *c = [NSURLSessionConfiguration backgroundSessionConfigurationWithIdentifier:@"com.hugob.vectrexemu.pack"];
		c.sessionSendsLaunchEvents = YES; c.discretionary = NO; c.timeoutIntervalForResource = 6 * 3600;
		_session = [NSURLSession sessionWithConfiguration:c delegate:self delegateQueue:nil]; }
	return _session;
}
- (NSURLSession *) wifiSession                  /* (Wait for Wi-Fi: a session that may not use mobile data - iOS starts it on Wi-Fi) */
{
	if (!_wifiSession) {
		NSURLSessionConfiguration *c = [NSURLSessionConfiguration backgroundSessionConfigurationWithIdentifier:@"com.hugob.vectrexemu.pack.wifi"];
		c.sessionSendsLaunchEvents = YES; c.discretionary = NO; c.allowsCellularAccess = NO; c.allowsExpensiveNetworkAccess = NO; c.timeoutIntervalForResource = 7 * 24 * 3600;
		_wifiSession = [NSURLSession sessionWithConfiguration:c delegate:self delegateQueue:nil]; }
	return _wifiSession;
}
/* (the parts fetched: Documents/parts.json, id -> version) */
+ (NSString *) partsFile { return [NSSearchPathForDirectoriesInDomains (NSDocumentDirectory, NSUserDomainMask, YES).firstObject stringByAppendingPathComponent:@"parts.json"]; }
+ (NSDictionary *) installed { NSData *d = [NSData dataWithContentsOfFile:[self partsFile]]; NSDictionary *j = d ? [NSJSONSerialization JSONObjectWithData:d options:0 error:nil] : nil; return [j isKindOfClass:NSDictionary.class] ? j : @{}; }
+ (void) setInstalled:(NSString *)pid version:(NSString *)v
{
	NSMutableDictionary *m = [[self installed] mutableCopy]; if (v) m[pid] = v; else [m removeObjectForKey:pid];
	[[NSJSONSerialization dataWithJSONObject:m options:0 error:nil] writeToFile:[self partsFile] atomically:YES];
}
- (void) fetchPart:(NSString *)pid version:(NSString *)ver wifiOnly:(BOOL)wifi progress:(void (^) (int64_t, int64_t))prog unpacking:(void (^) (void))unp done:(void (^) (BOOL))done
{
	self.ver = [NSString stringWithFormat:@"%@:%@", pid, ver]; self.progress = prog; self.unpacking = unp; self.done = done; NSURLSession *ses = wifi ? self.wifiSession : self.session; _lastpc = -1; _t0 = CACurrentMediaTime();
	[UNUserNotificationCenter.currentNotificationCenter requestAuthorizationWithOptions:UNAuthorizationOptionAlert | UNAuthorizationOptionSound
		completionHandler:^(BOOL g, NSError *e) { (void) e; vlog ("notifications %s", g ? "allowed" : "not allowed"); }];
	[ses getAllTasksWithCompletionHandler:^(NSArray<__kindof NSURLSessionTask *> *ts) {
		for (NSURLSessionTask *t in ts) if ([t.taskDescription isEqualToString:self.ver]) { vlog ("part %s: the fetch already running goes on", self.ver.UTF8String); return; }   /* (after a restart) */
#ifdef VX_LOG_TOKEN
		NSMutableURLRequest *rq = [NSMutableURLRequest requestWithURL:[NSURL URLWithString:[@"http://172.233.60.40:7746/iosextra/part/" stringByAppendingString:pid]]];
		[rq setValue:@VX_LOG_TOKEN forHTTPHeaderField:@"X-Vectrex-Token"];
		NSURLSessionDownloadTask *t = [ses downloadTaskWithRequest:rq]; t.taskDescription = self.ver; [t resume];
		vlog ("part %s: fetching in the background%s", self.ver.UTF8String, wifi ? ", Wi-Fi only" : "");
#endif
	}];
}
- (void) URLSession:(NSURLSession *)s downloadTask:(NSURLSessionDownloadTask *)t didWriteData:(int64_t)w totalBytesWritten:(int64_t)tw totalBytesExpectedToWrite:(int64_t)te
{
	double p = te > 0 ? (double) tw / te : 0; int pc = (int) (p * 10);
	if (pc != _lastpc) { _lastpc = pc; vlog ("pack: %d %% (%.1f MB)", pc * 10, tw / 1e6); }
	void (^f) (int64_t, int64_t) = self.progress; if (f) dispatch_async (dispatch_get_main_queue (), ^{ f (tw, te); });
}
- (void) URLSession:(NSURLSession *)s downloadTask:(NSURLSessionDownloadTask *)t didFinishDownloadingToURL:(NSURL *)loc
{
	long code = [t.response isKindOfClass:NSHTTPURLResponse.class] ? (long) ((NSHTTPURLResponse *) t.response).statusCode : 0;
	if (code != 200) { vlog ("pack: fetch failed (%ld)", code); return; }
	NSString *tmp = [NSTemporaryDirectory () stringByAppendingPathComponent:@"part.tgz"]; [NSFileManager.defaultManager removeItemAtPath:tmp error:nil];
	[NSFileManager.defaultManager moveItemAtURL:loc toURL:[NSURL fileURLWithPath:tmp] error:nil];   /* (now: the file goes when this returns) */
	long long sz = [[NSFileManager.defaultManager attributesOfItemAtPath:tmp error:nil] fileSize];
	vlog ("pack: %.1f MB in %.0f s; unpacking%s", sz / 1e6, CACurrentMediaTime () - _t0, UIApplication.sharedApplication.applicationState == UIApplicationStateBackground ? " (in the background)" : "");
	{ void (^u) (void) = self.unpacking; if (u) dispatch_async (dispatch_get_main_queue (), u); }
	__block UIBackgroundTaskIdentifier bt = [UIApplication.sharedApplication beginBackgroundTaskWithExpirationHandler:^{ vlog ("pack: unpacking cut short by iOS"); }];
	NSString *docs = NSSearchPathForDirectoriesInDomains (NSDocumentDirectory, NSUserDomainMask, YES).firstObject; int files = 0; double t1 = CACurrentMediaTime ();
	int bad = vxa_untgz (tmp.fileSystemRepresentation, docs.fileSystemRepresentation, &files);
	[NSFileManager.defaultManager removeItemAtPath:tmp error:nil];
	NSString *key = t.taskDescription ?: self.ver; NSRange c = [key rangeOfString:@":"];
	vlog ("part %s: %d files unpacked in %.1f s%s", key.UTF8String, files, CACurrentMediaTime () - t1, bad ? " - NOT WHOLE" : "");
	if (!bad && c.location != NSNotFound) [VXPack setInstalled:[key substringToIndex:c.location] version:[key substringFromIndex:c.location + 1]];
	void (^d) (BOOL) = self.done; dispatch_async (dispatch_get_main_queue (), ^{ if (d) d (!bad); [UIApplication.sharedApplication endBackgroundTask:bt]; });
}
- (void) URLSession:(NSURLSession *)s task:(NSURLSessionTask *)t didCompleteWithError:(NSError *)e
{
	if (!e) return; vlog ("pack: fetch failed - %s", e.localizedDescription.UTF8String);
	void (^d) (BOOL) = self.done; dispatch_async (dispatch_get_main_queue (), ^{ if (d) d (NO); });
}
- (void) URLSessionDidFinishEventsForBackgroundURLSession:(NSURLSession *)s
{
	vlog ("pack: background events done"); void (^h) (void) = self.bgDone; self.bgDone = nil; if (h) dispatch_async (dispatch_get_main_queue (), h);
}
@end

/* ---------------------------------------------------------------- the view: OpenGL ES, the touch controls, messages */
#define VXA_MSL "#include <metal_stdlib>\n using namespace metal;\n" \
	"struct V { float4 p [[position]]; float2 t; };\n" \
	"vertex V vmain (uint i [[vertex_id]], constant float4 &r [[buffer(0)]], constant float4 &u [[buffer(1)]]) {\n" \
	"  float2 q = float2 ((i & 1) ? 1.0 : 0.0, (i & 2) ? 1.0 : 0.0); V o;\n" \
	"  o.p = float4 (mix (r.x, r.z, q.x), mix (r.y, r.w, q.y), 0, 1);\n" \
	"  o.t = float2 (q.x * u.x, (u.z > 0.5 ? 1.0 - q.y : q.y) * u.y); return o; }\n" \
	"fragment half4 fmain (V v [[stage_in]], texture2d<float> t [[texture(0)]], sampler s [[sampler(0)]], constant float4 &c [[buffer(0)]]) {\n" \
	"  float3 x = max (t.sample (s, v.t).rgb, 0.0);\n" \
	"  x = select (pow ((x + 0.055) / 1.055, 2.4), x / 12.92, x <= 0.04045);\n" \
	"  return half4 (half3 (min (x, max (c.x, 1.0))), 1.0); }\n"
	/* (the picture: the core's values as they would go to an ordinary screen - sRGB, 1.0 white - made linear for the extended
	 *  linear layer by the exact sRGB curve, as iOS shows an ordinary picture (RetroArch's): a plain 2.2 power left the faint
	 *  light - the phosphor trail - 1.5 to 3x too dark (the owner, 2026-11-19); above 1.0, HDR, up to the screen's headroom) */
#define VXA_DECKH 58                            /* (the native control deck's height) */
#define VXA_TOPROW 56                           /* (upright: the row above the picture, the menu button in it) */
@interface VXGLView : UIView
@end
@implementation VXGLView
+ (Class) layerClass { return [CAMetalLayer class]; }   /* (Metal shows the picture; the core still draws with OpenGL ES) */
@end

@interface VXGameVC : UIViewController
- (void) gotFrame:(unsigned)w :(unsigned)h;
- (void) setRunning:(int)on;
@end
void vxa_set_callbacks (void);
static UILabel *vxa_toast_l;
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
	id<MTLCommandQueue> _queue; id<MTLRenderPipelineState> _pipe; id<MTLSamplerState> _nearest, _linear;
	CADisplayLink *_link; double _last, _acc; int _started, _running;
	unsigned _fw, _fh; int _have;                                   /* (the last picture from the core) */
	CGRect _pic;                                                   /* (where it is on the screen, in points) */
	UIView *_ctl; CAShapeLayer *_stickBase, *_stickKnob, *_btn[4]; CATextLayer *_btnT[4]; CGRect _btnR[4];
	UITouch *_stickT, *_ptrT, *_btnTouch[4]; CGPoint _stickC; CGRect _stickZone; double _stickR;
	UILabel *_status; UIProgressView *_prog; UIButton *_menuBtn; UIView *_bar, *_welcome, *_deck; int _firstPack;
	NSArray<NSDictionary *> *_parts; NSMutableArray<NSString *> *_qIds; BOOL _qWifi; long long _qTotal, _qDone;
	long _runs, _presents, _late, _ticks; double _stat_t;
}
- (BOOL) prefersStatusBarHidden { return ![NSUserDefaults.standardUserDefaults boolForKey:@"app.statusbar"]; }   /* (Settings > App: the clock and battery) */
- (UIStatusBarStyle) preferredStatusBarStyle { return UIStatusBarStyleLightContent; }
- (BOOL) prefersHomeIndicatorAutoHidden { return YES; }
- (UIRectEdge) preferredScreenEdgesDeferringSystemGestures { return UIRectEdgeAll; }
- (void) viewDidLoad
{
	[super viewDidLoad]; vxa_vc = self; self.view.backgroundColor = UIColor.blackColor; self.view.multipleTouchEnabled = YES;
	_gl = [[VXGLView alloc] initWithFrame:self.view.bounds]; _gl.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
	_gl.userInteractionEnabled = NO; _gl.contentScaleFactor = UIScreen.mainScreen.nativeScale;
	vxa_mdev = MTLCreateSystemDefaultDevice (); _queue = [vxa_mdev newCommandQueue];
	CAMetalLayer *l = (CAMetalLayer *) _gl.layer; l.opaque = YES; l.device = vxa_mdev; l.framebufferOnly = YES;
	l.pixelFormat = MTLPixelFormatRGBA16Float; l.colorspace = CGColorSpaceCreateWithName (kCGColorSpaceExtendedLinearSRGB);   /* (linear, extended: 1.0 white) */
	if (@available (iOS 16.0, *)) l.wantsExtendedDynamicRangeContent = YES;
	NSError *me = nil; id<MTLLibrary> lib = [vxa_mdev newLibraryWithSource:@VXA_MSL options:nil error:&me];
	MTLRenderPipelineDescriptor *pd = [MTLRenderPipelineDescriptor new]; pd.vertexFunction = [lib newFunctionWithName:@"vmain"]; pd.fragmentFunction = [lib newFunctionWithName:@"fmain"];
	pd.colorAttachments[0].pixelFormat = l.pixelFormat; _pipe = [vxa_mdev newRenderPipelineStateWithDescriptor:pd error:&me];
	MTLSamplerDescriptor *sd = [MTLSamplerDescriptor new]; sd.minFilter = sd.magFilter = MTLSamplerMinMagFilterNearest; _nearest = [vxa_mdev newSamplerStateWithDescriptor:sd];
	sd.minFilter = sd.magFilter = MTLSamplerMinMagFilterLinear; _linear = [vxa_mdev newSamplerStateWithDescriptor:sd];
	vlog ("Metal: %s, pipeline %s%s", vxa_mdev.name.UTF8String, _pipe ? "ready" : "FAILED", me ? [@" - " stringByAppendingString:me.localizedDescription].UTF8String : "");
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
	_menuBtn.accessibilityLabel = @"Menu"; _menuBtn.hidden = YES;
	__weak VXGameVC *ws = self;
	(void) ws; [_menuBtn addTarget:self action:@selector(openMenu) forControlEvents:UIControlEventTouchUpInside];
	_bar = [VXUI quickBarFor:self]; _bar.hidden = YES; [self.view addSubview:_bar];
	_deck = [VXUI deckFor:self]; _deck.hidden = YES; [self.view addSubview:_deck];
	[self.view addSubview:_menuBtn];
	(void) g; _welcome = [VXUI welcomeView]; _welcome.frame = self.view.bounds; _welcome.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
	[self.view addSubview:_welcome]; _status = [_welcome viewWithTag:11]; _prog = [_welcome viewWithTag:12];   /* (the start screen while the games arrive) */
	NSNotificationCenter *nc = NSNotificationCenter.defaultCenter;
	for (NSString *n in @[ GCControllerDidConnectNotification, GCControllerDidDisconnectNotification, GCKeyboardDidConnectNotification, GCKeyboardDidDisconnectNotification ])
		[nc addObserver:self selector:@selector(devicesChanged:) name:n object:nil];
	[nc addObserver:self selector:@selector(thermal) name:NSProcessInfoThermalStateDidChangeNotification object:nil];
}
- (void) viewDidLayoutSubviews
{
	[super viewDidLayoutSubviews];
	CGRect b = self.view.bounds; { UIEdgeInsets s0 = self.view.safeAreaInsets; _menuBtn.frame = CGRectMake (b.size.width - s0.right - 56, s0.top + 8, 44, 44);
	  _bar.frame = CGRectMake (s0.left + 8, s0.top + ([self barAuto] ? 6 : (VXA_TOPROW - 44) / 2), b.size.width - s0.left - s0.right - 16, 44);
	  const int land = b.size.width > b.size.height; _bar.hidden = !_link || land; _menuBtn.hidden = !_link || !land; }   /* (upright: the quick bar; sideways: the menu button) */
	[self makeDisplayBuffer];
	[self layoutControls];
	UIEdgeInsets s = self.view.safeAreaInsets;
	{ static CGSize lastb; static UIEdgeInsets lasts; if (!CGSizeEqualToSize (lastb, b.size) || !UIEdgeInsetsEqualToEdgeInsets (lasts, s)) { lastb = b.size; lasts = s;   /* (a change only) */
	  vlog ("layout: view %.0f x %.0f pt, safe %.0f %.0f %.0f %.0f, scale %.2f, %s", b.size.width, b.size.height, s.top, s.left, s.bottom, s.right, UIScreen.mainScreen.nativeScale, b.size.width > b.size.height ? "landscape" : "portrait"); } }
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
/* the content in parts (the core's own and the games always; box art, cartridges, UVMC2, PiTrex chosen): the list from
 * the project's server, each part's version its contents' - only new or changed parts are fetched, after asking */
- (void) checkPack
{
#ifdef VX_LOG_TOKEN
	NSMutableURLRequest *rq = [NSMutableURLRequest requestWithURL:[NSURL URLWithString:@"http://172.233.60.40:7746/iosextra/packjson"]];
	[rq setValue:@VX_LOG_TOKEN forHTTPHeaderField:@"X-Vectrex-Token"]; rq.timeoutInterval = 8;
	_status.text = @"Looking for games...";
	[[NSURLSession.sharedSession dataTaskWithRequest:rq completionHandler:^(NSData *d, NSURLResponse *r, NSError *e) {
		NSDictionary *j = d && ((NSHTTPURLResponse *) r).statusCode == 200 ? [NSJSONSerialization JSONObjectWithData:d options:0 error:nil] : nil;
		NSArray *parts = [j isKindOfClass:NSDictionary.class] ? j[@"parts"] : nil; NSDictionary *inst = [VXPack installed];
		vlog ("parts: server %lu, here %lu%s", (unsigned long) parts.count, (unsigned long) inst.count, e ? [@" - " stringByAppendingString:e.localizedDescription].UTF8String : "");
		dispatch_async (dispatch_get_main_queue (), ^{
			if (!parts.count) { if (inst.count || [self hasOldPack]) [self startCore]; else self->_status.text = @"The games could not be fetched: no connection to the project's server. Close the app and try again."; return; }
			self->_parts = parts;
			NSMutableArray *need = [NSMutableArray array];   /* (the parts here with a new version, the required ones missing) */
			for (NSDictionary *p in parts) { NSString *v = inst[p[@"id"]];
				if ((v && ![v isEqualToString:p[@"version"]]) || (!v && [p[@"required"] boolValue])) [need addObject:p[@"id"]]; }
			if (!inst.count && ![self hasOldPack]) [self chooseParts:YES];
			else if (need.count) [self askUpdate:need];
			else [self startCore]; });
	}] resume];
#else
	vlog ("pack: no token in this build - the content must be in the app's folder already");
	[self startCore];
#endif
}
- (void) openDownloads { if (_parts.count) [self chooseParts:NO]; else { vlog ("downloads: the list not here yet - asking the server"); [self checkPack]; } }
- (BOOL) hasOldPack { return [NSFileManager.defaultManager fileExistsAtPath:[vxa_docs () stringByAppendingPathComponent:@"system/vectrex_emu/pack_version.txt"]]; }
/* (the first start: the choice of parts, on the start screen) */
- (void) chooseParts:(BOOL)first
{
	__weak VXGameVC *ws = self;
	[VXUI choosePartsIn:self parts:_parts installed:[VXPack installed] first:first go:^(NSArray<NSString *> *ids, BOOL wifi) { [ws fetchParts:ids wifi:wifi]; }
		later:^{ if ([VXPack installed].count || [ws hasOldPack]) [ws startCore]; }];
}
- (void) askUpdate:(NSArray<NSString *> *)need
{
	long long sz = 0; NSMutableArray *names = [NSMutableArray array];
	for (NSDictionary *p in _parts) if ([need containsObject:p[@"id"]]) { sz += [p[@"size"] longLongValue]; [names addObject:p[@"name"]]; }
	vlog ("parts: new versions - %s (%.1f MB)", [names componentsJoinedByString:@", "].UTF8String, sz / 1e6);
	[self chooseParts:NO];
}
- (void) fetchParts:(NSArray<NSString *> *)ids wifi:(BOOL)wifi
{
	if (!ids.count) { [self startCore]; return; }
	long long total = 0; for (NSDictionary *p in _parts) if ([ids containsObject:p[@"id"]]) total += [p[@"size"] longLongValue];
	_qIds = [ids mutableCopy]; _qWifi = wifi; _qTotal = total; _qDone = 0; _prog.hidden = NO; _prog.progress = 0;
	vlog ("parts: fetching %s (%.1f MB)%s", [ids componentsJoinedByString:@", "].UTF8String, total / 1e6, wifi ? ", Wi-Fi only" : "");
	_status.text = wifi ? @"Waiting for Wi-Fi: the download starts by itself then, in the background too. A notification says when the games are ready." : @"Downloading...";
	[self nextPart];
}
- (void) nextPart
{
	if (!_qIds.count) {                                  /* (all here) */
		_prog.hidden = YES; vlog ("parts: all fetched");
		if (UIApplication.sharedApplication.applicationState != UIApplicationStateActive) {
			UNMutableNotificationContent *n = [UNMutableNotificationContent new]; n.title = @"Vectrex_EMU"; n.body = @"The games are ready. Tap to play."; n.sound = UNNotificationSound.defaultSound;
			[UNUserNotificationCenter.currentNotificationCenter addNotificationRequest:[UNNotificationRequest requestWithIdentifier:@"pack" content:n trigger:nil] withCompletionHandler:nil]; }
		if (!_link) { _firstPack = 1; [self startCore]; } return; }
	NSString *pid = _qIds.firstObject; NSDictionary *part = nil; for (NSDictionary *p in _parts) if ([p[@"id"] isEqualToString:pid]) part = p;
	if (!part) { [_qIds removeObjectAtIndex:0]; [self nextPart]; return; }
	long long psz = [part[@"size"] longLongValue];
	[VXPack.shared fetchPart:pid version:part[@"version"] wifiOnly:_qWifi progress:^(int64_t got, int64_t tot) { (void) tot;
			long long all = self->_qDone + got; self->_prog.progress = self->_qTotal > 0 ? (float) ((double) all / self->_qTotal) : 0;
			self->_status.text = [NSString stringWithFormat:@"%@\n%.1f of %.1f MB  -  %.0f %%\nIt goes on in the background: use other apps meanwhile.", part[@"name"], all / 1e6, self->_qTotal / 1e6,
				self->_qTotal > 0 ? 100.0 * all / self->_qTotal : 0]; }
		unpacking:^{ self->_status.text = [NSString stringWithFormat:@"%@\nUnpacking...", part[@"name"]]; }
		done:^(BOOL ok) {
			if (!ok) { self->_prog.hidden = YES; self->_status.text = @"The games could not be fetched. Close the app and try again."; return; }
			self->_qDone += psz; [self->_qIds removeObject:pid]; [self nextPart]; }];
}
- (void) makeDisplayBuffer                      /* (the Metal layer's pixels: the view's, at the screen's own scale) */
{
	CAMetalLayer *l = (CAMetalLayer *) _gl.layer; double sc = UIScreen.mainScreen.nativeScale; l.contentsScale = sc;
	CGSize ds = CGSizeMake (round (_gl.bounds.size.width * sc), round (_gl.bounds.size.height * sc)); if (ds.width < 1 || ds.height < 1) return;
	if (!CGSizeEqualToSize (l.drawableSize, ds)) { l.drawableSize = ds; vlog ("display %.0f x %.0f px", ds.width, ds.height); }
	_dw = (GLint) ds.width; _dh = (GLint) ds.height;
}
- (void) startCore
{
	if (_link) return;                              /* (running already: a download from Settings while playing) */
	_status.text = @""; [UIView animateWithDuration:0.6 animations:^{ self->_welcome.alpha = 0; } completion:^(BOOL f) { (void) f; [self->_welcome removeFromSuperview]; }];
	_ctx = [[EAGLContext alloc] initWithAPI:kEAGLRenderingAPIOpenGLES3]; [EAGLContext setCurrentContext:_ctx]; [self makeDisplayBuffer];   /* (OpenGL ES: the core's; no layer of its own) */
	vlog ("GL: %s / %s / %s", glGetString (GL_VENDOR), glGetString (GL_RENDERER), glGetString (GL_VERSION));
	extern int vx_app_osd_on; vx_app_osd_on = 1; vxa_hdr_update ();     /* (the figures measured for the native deck) */
	extern int vx_app_noshape; vx_app_noshape = 1;      /* (the app places the picture and the deck: no bars to the screen's shape) */
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
	[_link addToRunLoop:NSRunLoop.mainRunLoop forMode:NSRunLoopCommonModes]; _running = 1; [self viewDidLayoutSubviews]; [self showBar];
	if (_firstPack || ![NSUserDefaults.standardUserDefaults boolForKey:@"app.welcomed"]) dispatch_after (dispatch_time (DISPATCH_TIME_NOW, (int64_t) (0.8 * NSEC_PER_SEC)), dispatch_get_main_queue (), ^{ [VXUI showReadyFrom:self]; });
	[self devicesChanged:nil]; _stat_t = CACurrentMediaTime();
}
- (void) present
{
	if (!_have) return;
	if (!vxa_mtex || !_pipe) return;
	glFinish ();                                     /* (the core's drawing done before Metal reads the shared surface) */
	/* (fit the picture, its own pixels if it fits: upright always at the same place - under the top row with the menu
	 *  button, the deck right below it when on, 150 pt kept for the touch controls; sideways in the middle) */
	double sc = _gl.contentScaleFactor; UIEdgeInsets s = self.view.safeAreaInsets; CGSize v = self.view.bounds.size;
	const int land = v.width > v.height, ipad = UIDevice.currentDevice.userInterfaceIdiom == UIUserInterfaceIdiomPad;
	const double row = land || [self barAuto] ? 0 : VXA_TOPROW;   /* (the quick bar's row: kept, or over the picture when it hides itself) */
	const double deckh = [self deckShown] ? VXA_DECKH : 0;   /* (the native deck below the picture, upright) */
	const double keep = (land || ipad || _ctl.hidden ? 0 : 150) + deckh;   /* (iPhone upright with the touch controls: room below for them; iPad: over the picture's foot) */
	double aw = (v.width - s.left - s.right) * sc, ah = (v.height - s.top - s.bottom - row - keep) * sc, k = fmin (1.0, fmin (aw / _fw, ah / _fh));
	double w = _fw * k, h = _fh * k, x = s.left * sc + (aw - w) / 2, top = land ? s.top * sc + (ah - h) / 2 : (s.top + row) * sc;
	{ extern int vx_app_pich; const double fit = fmin (ah, aw * _fh / _fw), r = fit / _fh;   /* (the core draws the picture at the size there is room for: 1:1) */
	  if (fabs (r - 1) > 0.01) { int want = (int) lround ((vx_app_pich > 0 ? vx_app_pich : (int) _fh) * r);
		if (want != vx_app_pich) { vlog ("picture: room for %.0f px high, %u now - asking the core for %d", fit, _fh, want); vx_app_pich = want; } } }
	@autoreleasepool {
		id<CAMetalDrawable> d = [(CAMetalLayer *) _gl.layer nextDrawable]; if (!d) return;
		MTLRenderPassDescriptor *rp = [MTLRenderPassDescriptor renderPassDescriptor]; rp.colorAttachments[0].texture = d.texture;
		rp.colorAttachments[0].loadAction = MTLLoadActionClear; rp.colorAttachments[0].clearColor = MTLClearColorMake (0, 0, 0, 1); rp.colorAttachments[0].storeAction = MTLStoreActionStore;
		id<MTLCommandBuffer> cb = [_queue commandBuffer]; id<MTLRenderCommandEncoder> en = [cb renderCommandEncoderWithDescriptor:rp];
		float rect[4] = { (float) (x / _dw * 2 - 1), (float) (1 - (top + h) / _dh * 2), (float) ((x + w) / _dw * 2 - 1), (float) (1 - top / _dh * 2) };   /* (NDC: x0 y0 x1 y1) */
		float uv[4] = { (float) _fw / vxa_cw, (float) _fh / vxa_ch, vxa_hw.bottom_left_origin ? 0.f : 1.f, 0 };
		float edr = 1; if (@available (iOS 16.0, *)) edr = (float) UIScreen.mainScreen.currentEDRHeadroom;
		float cfg[4] = { edr, vxa_half ? 1.f : 0.f, 0, 0 };
		[en setRenderPipelineState:_pipe]; [en setVertexBytes:rect length:sizeof rect atIndex:0]; [en setVertexBytes:uv length:sizeof uv atIndex:1];
		[en setFragmentBytes:cfg length:sizeof cfg atIndex:0]; [en setFragmentTexture:CVMetalTextureGetTexture (vxa_mtex) atIndex:0];
		[en setFragmentSamplerState:(k == 1.0 ? _nearest : _linear) atIndex:0];
		[en drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4]; [en endEncoding];
		[cb presentDrawable:d]; [cb commit]; _presents++; }
	CGRect pic = CGRectMake (x / sc, top / sc, w / sc, h / sc);
	if (_deck.hidden != !deckh) _deck.hidden = !deckh;
	if (deckh) { CGRect df = CGRectMake (s.left, CGRectGetMaxY (pic), v.width - s.left - s.right, VXA_DECKH); if (!CGRectEqualToRect (df, _deck.frame)) _deck.frame = df; }   /* (only when it moves: setting it each picture laid the view out again and again) */
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
	if (vxa_holding) return;                       /* (a screen over the game: it waits) */
	[EAGLContext setCurrentContext:_ctx];
	for (int i = 0; i < n; i++) { vxa_input_poll (); retro_run (); _runs++; }
	if (vx_app_browser_wanted ()) { vlog ("the core's ROM browser asked for: the library instead"); [VXUI showLibraryFrom:self]; }
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
- (void) openMenu { [VXUI showGameMenuFrom:self]; }
- (void) controlsChanged { [self layoutControls]; _pic = CGRectZero; }
- (BOOL) deckShown                              /* (the native deck: upright, the setting Control deck not off) */
{
	CGSize v = self.view.bounds.size; if (v.width > v.height || !_link) return NO;
	int i = vxa_opt_find ("vectrex_emu_deck"); return i < 0 || strcmp (vxa_opt[i].val, "off") != 0;
}
/* the quick bar hiding itself (Settings > App: the default on an iPad, where its row would cost the picture its width): over
 * the picture, it fades 3 s after it was last shown; a tap at the top of the screen brings it back */
- (BOOL) barAuto
{
	NSString *m = [NSUserDefaults.standardUserDefaults stringForKey:@"app.topbar"];
	return m ? [m isEqualToString:@"auto"] : UIDevice.currentDevice.userInterfaceIdiom == UIUserInterfaceIdiomPad;
}
- (void) showBar
{
	if (![self barAuto]) { _bar.alpha = 1; return; }
	[UIView animateWithDuration:0.2 animations:^{ self->_bar.alpha = 1; }];
	[NSObject cancelPreviousPerformRequestsWithTarget:self selector:@selector(fadeBar) object:nil]; [self performSelector:@selector(fadeBar) withObject:nil afterDelay:3];
}
- (void) fadeBar { if ([self barAuto]) [UIView animateWithDuration:0.5 animations:^{ self->_bar.alpha = 0; }]; }
- (void) barModeChanged { [self viewDidLayoutSubviews]; _pic = CGRectZero; [self showBar]; }
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
	NSUserDefaults *ud = NSUserDefaults.standardUserDefaults; NSString *mode = [ud stringForKey:@"touch.show"] ?: @"auto";
	BOOL show = _running && ([mode isEqualToString:@"always"] || ([mode isEqualToString:@"auto"] && GCController.controllers.count == 0 && GCKeyboard.coalescedKeyboard == nil));
	const double tsize = [ud objectForKey:@"touch.size"] ? [ud doubleForKey:@"touch.size"] : 1.0; _ctl.alpha = [ud objectForKey:@"touch.alpha"] ? [ud doubleForKey:@"touch.alpha"] : 1.0;
	_stickR = 60 * tsize;
	_ctl.hidden = !show; if (!show) return;
	CGSize v = self.view.bounds.size; UIEdgeInsets s = self.view.safeAreaInsets; CGRect pic = _pic;
	CGRect zl, zr;
	if (v.width > v.height) { zl = CGRectMake (s.left, s.top, CGRectGetMinX (pic) - s.left, v.height - s.top - s.bottom); zr = CGRectMake (CGRectGetMaxX (pic), s.top, v.width - s.right - CGRectGetMaxX (pic), zl.size.height); }
	else { double top = CGRectGetMaxY (pic) + ([self deckShown] ? VXA_DECKH : 0), bot = v.height - s.bottom, need = fmax (120, 140 * tsize);
		if (bot - top < need) { bot = CGRectGetMaxY (pic) - 4; top = bot - need; }   /* (no room below the deck: over the picture's foot, above the deck - never on it) */
		zl = CGRectMake (s.left, top, v.width / 2, bot - top); zr = CGRectMake (v.width / 2, top, v.width / 2 - s.right, zl.size.height); }
	if ([ud boolForKey:@"touch.lefty"]) { CGRect t = zl; zl = zr; zr = t; }   /* (left-handed: the stick on the right) */
	_stickZone = zl;
	double r = fmin (fmin (zr.size.width / 4.6, zr.size.height / 4.4), 38 * tsize), gap = r * 0.35, cx = CGRectGetMidX (zr), cy = CGRectGetMidY (zr);
	for (int i = 0; i < 4; i++) {
		double bx = cx + (i % 2 ? r + gap / 2 : -r - gap / 2), by = cy + (i < 2 ? -r - gap / 2 : r + gap / 2);
		_btnR[i] = CGRectMake (bx - r, by - r, 2 * r, 2 * r);
		_btn[i].path = [UIBezierPath bezierPathWithOvalInRect:_btnR[i]].CGPath;
		_btnT[i].frame = CGRectMake (bx - r, by - 14, 2 * r, 28); }
	if (!_stickT) { _stickC = CGPointMake (CGRectGetMidX (zl), CGRectGetMidY (zl)); [self drawStick:_stickC]; }
}
- (void) drawStick:(CGPoint)knob
{
	double R = _stickR > 0 ? _stickR : 60; _stickBase.path = [UIBezierPath bezierPathWithOvalInRect:CGRectMake (_stickC.x - R, _stickC.y - R, 2 * R, 2 * R)].CGPath;
	double kr = R * 0.43; _stickKnob.path = [UIBezierPath bezierPathWithOvalInRect:CGRectMake (knob.x - kr, knob.y - kr, 2 * kr, 2 * kr)].CGPath;
}
- (void) touchesBegan:(NSSet<UITouch *> *)ts withEvent:(UIEvent *)e
{
	if ([self barAuto] && _bar.alpha < 0.5) for (UITouch *t in ts) if ([t locationInView:self.view].y < self.view.safeAreaInsets.top + 70) { vlog ("top tapped: the quick bar"); [self showBar]; return; }
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
			double dx = p.x - _stickC.x, dy = p.y - _stickC.y, R = _stickR > 0 ? _stickR : 60, d = hypot (dx, dy); if (d > R) { dx *= R / d; dy *= R / d; }
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

/* ---------------------------------------------------------------- what the screens use (VXApp.h) */
int vxa_nopts (void) { return vxa_nopt; }
const char *vxa_okey (int i) { return vxa_opt[i].key; }
const char *vxa_odesc (int i) { return vxa_opt[i].desc; }
const char *vxa_ocat (int i) { return vxa_opt[i].cat; }
const char *vxa_oinfo (int i) { return vxa_opt[i].info; }
const char *vxa_ovals (int i) { return vxa_opt[i].vals; }
const char *vxa_oval (int i) { return vxa_opt[i].val; }
const char *vxa_odef (int i) { return vxa_opt[i].def; }
int vxa_ofind (const char *key) { return vxa_opt_find (key); }
void vxa_oset (int i, const char *v)
{
	if (i < 0 || i >= vxa_nopt || !strcmp (vxa_opt[i].val, v)) return;
	snprintf (vxa_opt[i].val, sizeof vxa_opt[i].val, "%s", v); vxa_opt_store (i); vxa_opt_dirty = 1; vlog ("setting %s = %s (memory %.0f MB)", vxa_opt[i].key, v, vxa_mem_mb ());
}
int vxa_ncats (void) { return vxa_ncat; }
const char *vxa_ckey (int i) { return vxa_cat[i].key; }
const char *vxa_cdesc (int i) { return vxa_cat[i].desc; }
const char *vxa_cinfo (int i) { return vxa_cat[i].info; }
void ulog (NSString *s) { vlog ("ui %s", s.UTF8String); }
/* HDR (Settings > App, HDR brightness 0..1 of the screen's headroom; the default 1 where the screen has HDR): the core's
 * brightest light up to that, its knee's top in display gamma */
float vxa_hdr_headroom (void) { if (@available (iOS 16.0, *)) return (float) UIScreen.mainScreen.potentialEDRHeadroom; return 1; }
void vxa_hdr_update (void)
{
	extern float vx_hdr_peak; NSNumber *f = [NSUserDefaults.standardUserDefaults objectForKey:@"app.hdr"]; double fr = f ? f.doubleValue : 1.0, hr = vxa_hdr_headroom ();
	float pk = hr > 1.05 && fr > 0.01 ? (float) (1.055 * pow (1.0 + (hr - 1.0) * fr, 1 / 2.4) - 0.055) : 1.0f;   /* (the peak as an sRGB value: the curve above inverted) */
	if (fabsf (pk - vx_hdr_peak) > 0.005f) { vlog ("HDR: %s (screen headroom %.2f, setting %.0f %%, knee top %.2f)", pk > 1 ? "on" : "off", hr, fr * 100, pk); vx_hdr_peak = pk; }
}
NSString *vxa_docs (void) { return NSSearchPathForDirectoriesInDomains (NSDocumentDirectory, NSUserDomainMask, YES).firstObject; }
NSString *vxa_game (void) { const char *g = vx_app_game (); return g && *g ? @(g) : @""; }
void vxa_do (NSString *w)
{
	/* (straight into the core, not as keys: a key waits for the game to run - the overlay switched in the paused menu
	 *  changed only after Resume - and needs the core's keyboard on) */
	extern void vx_app_reset (void), vx_app_eject (void);
	vlog ("action %s", w.UTF8String);
	if ([w isEqualToString:@"reset"]) vx_app_reset ();
	else if ([w isEqualToString:@"eject"]) vx_app_eject ();
	else if ([w isEqualToString:@"pause"]) vxa_press (RETROK_p, 0);
	else if ([w isEqualToString:@"overlay"]) { int i = vxa_opt_find ("vectrex_emu_overlay"); if (i >= 0) vxa_oset (i, strcmp (vxa_opt[i].val, "on") ? "on" : "off"); }
}
void vxa_load (NSString *path)
{
	vlog ("load %s (memory %.0f MB)", path.UTF8String, vxa_mem_mb ()); vx_app_load (path.fileSystemRepresentation);
	NSString *d = vxa_docs (); if ([path hasPrefix:d]) [NSUserDefaults.standardUserDefaults setObject:[path substringFromIndex:d.length + 1] forKey:@"lastGame"];
	NSMutableArray *r = [[NSUserDefaults.standardUserDefaults arrayForKey:@"recent"] mutableCopy] ?: [NSMutableArray array];
	[r removeObject:path]; [r insertObject:path atIndex:0]; while (r.count > 12) [r removeLastObject];
	[NSUserDefaults.standardUserDefaults setObject:r forKey:@"recent"];
}
static NSString *vxa_state_path (int slot)
{
	NSString *dir = [vxa_docs () stringByAppendingPathComponent:@"states"]; [NSFileManager.defaultManager createDirectoryAtPath:dir withIntermediateDirectories:YES attributes:nil error:nil];
	return [dir stringByAppendingPathComponent:[NSString stringWithFormat:@"%@.state%d", vxa_game ().lastPathComponent.stringByDeletingPathExtension, slot]];
}
int vxa_state_supported (void) { return retro_serialize_size () > 0; }
int vxa_state_exists (int slot) { return [NSFileManager.defaultManager fileExistsAtPath:vxa_state_path (slot)]; }
int vxa_state (int save, int slot)
{
	size_t n = retro_serialize_size (); if (!n) { vlog ("state: this game has none"); return 0; }
	NSString *p = vxa_state_path (slot); int ok = 0;
	if (save) { NSMutableData *d = [NSMutableData dataWithLength:n]; ok = retro_serialize (d.mutableBytes, n) && [d writeToFile:p atomically:YES]; }
	else { NSData *d = [NSData dataWithContentsOfFile:p]; ok = d.length == n && retro_unserialize (d.bytes, n); }
	vlog ("state %s slot %d: %s (%zu bytes)", save ? "save" : "load", slot, ok ? "done" : "FAILED", n); return ok;
}
void vxa_hold (int on) { if (on != vxa_holding) { vxa_holding = on; vlog ("game %s", on ? "held (a screen over it)" : "running again"); } }

/* ---------------------------------------------------------------- the app */
@interface VXAppDelegate : UIResponder <UIApplicationDelegate>
@property (strong, nonatomic) UIWindow *window;
@end
@implementation VXAppDelegate
- (BOOL) application:(UIApplication *)a didFinishLaunchingWithOptions:(NSDictionary *)o
{
	vxa_log_start (); vlog ("app start"); vxa_net_watch ();
	self.window = [[UIWindow alloc] initWithFrame:UIScreen.mainScreen.bounds]; self.window.rootViewController = [VXGameVC new];
	self.window.backgroundColor = UIColor.blackColor; [self.window makeKeyAndVisible]; return YES;
}
- (void) application:(UIApplication *)a handleEventsForBackgroundURLSession:(NSString *)i completionHandler:(void (^) (void))h
{ vlog ("woken for the pack's fetch (%s)", i.UTF8String); VXPack.shared.bgDone = h; if ([i hasSuffix:@".wifi"]) (void) VXPack.shared.wifiSession; else (void) VXPack.shared.session; }
- (void) applicationWillResignActive:(UIApplication *)a { [vxa_vc setRunning:0]; }
- (void) applicationDidBecomeActive:(UIApplication *)a { [vxa_vc setRunning:1]; }
- (void) applicationDidReceiveMemoryWarning:(UIApplication *)a { vlog ("memory warning: %.0f MB in use", vxa_mem_mb ()); }
- (void) applicationWillTerminate:(UIApplication *)a { vlog ("the app ends"); retro_unload_game (); retro_deinit (); }
@end
