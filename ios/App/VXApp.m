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
#import <AudioToolbox/AudioToolbox.h>
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
@class VXGameVC; static __weak VXGameVC *vxa_vc; static int vxa_holding, vxa_nodraw;
static void vxa_net_tick (void);                  /* (online play: below) */
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
	const BOOL live = [kind isEqualToString:@"app-log"];
	[[NSURLSession.sharedSession dataTaskWithRequest:rq completionHandler:^(NSData *x, NSURLResponse *r, NSError *e) { (void) x;
		if (!live || (!e && ((NSHTTPURLResponse *) r).statusCode == 200)) return;   /* (no connection - on a phone's own network: kept, sent again) */
		NSString *back = [[NSString alloc] initWithData:d encoding:NSUTF8StringEncoding]; if (!back.length || !vxa_pend) return;
		@synchronized (vxa_pend) { if (vxa_pend.length < 2000000) [vxa_pend insertString:back atIndex:0]; } }] resume];
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
		if (!strcmp (d->key, "vectrex_emu_deck_look")) continue;   /* (the core's own deck's look: the app draws its own deck) */
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
/* the top bar and the full deck do not both fit (upright, the picture at its full width - 5 % less allowed): the deck
  * (slim) pops up by default. Fit: the iPhones, an iPad / iPad Air (1180 pt), an iPad mini, an 11-inch iPad Pro;
 * not: a 13-inch iPad Pro (4:3), a square screen - the Nova's 4:3 in the Android app alike */
int vxa_squarish (void)
{
	CGSize s = UIScreen.mainScreen.bounds.size; const double a = MAX (s.width, s.height), b = MIN (s.width, s.height);
	return b > 0 && a < b * 1.278 * 0.95 + 56 + 58 + 30;   /* (the picture 1.278 x its width high; the bar's row, the deck, the safe edges) */
}
/* the top bar alone does not fit either: it hides itself by default (three stages - both shown; the bar shown, the deck
 * popping up; both out of the way) */
int vxa_popdef (void)   /* (the slim deck does not fit with the bar either: it pops up by default - the four stages: bar + full deck,
                          bar + slim deck, bar + the deck popping up, both out of the way) */
{
	CGSize s = UIScreen.mainScreen.bounds.size; const double a = MAX (s.width, s.height), b = MIN (s.width, s.height);
	return b > 0 && a < b * 1.278 * 0.95 + 56 + 40 + 30;
}
int vxa_tight (void)
{
	CGSize s = UIScreen.mainScreen.bounds.size; const double a = MAX (s.width, s.height), b = MIN (s.width, s.height);
	return b > 0 && a < b * 1.278 * 0.95 + 56 + 30;
}
static void vxa_opts_defaults (void)
{
	for (int i = 0; i < vxa_nopt; i++) { NSString *sv = [[NSUserDefaults standardUserDefaults] stringForKey:[NSString stringWithFormat:@"opt.%s", vxa_opt[i].key]];
		if (sv) snprintf (vxa_opt[i].val, sizeof vxa_opt[i].val, "%s", sv.UTF8String); }
	/* (this app's own defaults: the control deck below the picture - its cartridge opens the ROM browser, RESET, the knob) */
	int dk = vxa_opt_find ("vectrex_emu_deck");
	{ NSUserDefaults *u = NSUserDefaults.standardUserDefaults;   /* (once, 1.7.6 (104), an iPad only: a screen that is not 4:3 - an iPad Air / iPad, 1.44 - had the
	                                              4:3 defaults (the bar hiding, the deck popping up): those taken back, the phone's layout fits) */
	  if (![u boolForKey:@"app.layout2"]) { [u setBool:YES forKey:@"app.layout2"];
		if (!vxa_squarish () && UIDevice.currentDevice.userInterfaceIdiom == UIUserInterfaceIdiomPad) { [u removeObjectForKey:@"app.topbar"]; [u removeObjectForKey:@"app.bar"]; [u removeObjectForKey:@"app.deckpop"];
			if (dk >= 0 && !strcmp (vxa_opt[dk].val, "pop-up")) { snprintf (vxa_opt[dk].val, sizeof vxa_opt[dk].val, "%s", "full"); [u removeObjectForKey:@"opt.vectrex_emu_deck"]; }
			vlog ("layout: this screen is not 4:3 - the top bar and the deck as a phone's"); } } }
	{ NSUserDefaults *u = NSUserDefaults.standardUserDefaults;   /* (once, 1.7.6 (108): the deck and the top bar to Auto - values kept from before it existed) */
	  if (![u boolForKey:@"app.auto1"]) { [u setBool:YES forKey:@"app.auto1"]; [u removeObjectForKey:@"opt.vectrex_emu_deck"]; [u removeObjectForKey:@"app.deckpop"];
		[u removeObjectForKey:@"app.topbar"]; [u removeObjectForKey:@"app.bar"]; if (dk >= 0) snprintf (vxa_opt[dk].val, sizeof vxa_opt[dk].val, "%s", "auto"); vlog ("deck and top bar: Auto (once)"); } }
	const char *dd = "auto";   /* (Auto: this screen's - full, slim, popping up; vxa_deck_style) */
	{ NSUserDefaults *u = NSUserDefaults.standardUserDefaults;   /* (the top bar's setting as Auto / Shown / Auto-hide: an older one carried over) */
	  if (![u stringForKey:@"app.bar"] && [u stringForKey:@"app.topbar"]) [u setObject:[[u stringForKey:@"app.topbar"] isEqualToString:@"auto"] ? @"hide" : @"always" forKey:@"app.bar"];
	  [u removeObjectForKey:@"app.topbar"]; }
	if (dk >= 0) snprintf (vxa_opt[dk].def, sizeof vxa_opt[dk].def, "%s", dd);   /* (the app's default - what Reset gives back) */
	if (dk >= 0 && ![[NSUserDefaults standardUserDefaults] stringForKey:@"opt.vectrex_emu_deck"]) snprintf (vxa_opt[dk].val, sizeof vxa_opt[dk].val, "%s", dd);
	/* (this device's own: the screen's refresh rate - 120 Hz on ProMotion, else 60) */
	int r = vxa_opt_find ("vectrex_emu_rate");
	if (r >= 0) snprintf (vxa_opt[r].def, sizeof vxa_opt[r].def, "%s", UIScreen.mainScreen.maximumFramesPerSecond >= 120 ? "120 Hz" : "60 Hz");   /* (this screen's: the default) */
	if (r >= 0 && ![[NSUserDefaults standardUserDefaults] stringForKey:@"opt.vectrex_emu_rate"])
		snprintf (vxa_opt[r].val, sizeof vxa_opt[r].val, "%s", UIScreen.mainScreen.maximumFramesPerSecond >= 120 ? "120 Hz" : "60 Hz");   /* (Metal shows the picture: ProMotion's 120 pictures a second) */
	vlog ("core options: %d (rate %s)", vxa_nopt, r >= 0 ? vxa_opt[r].val : "?");
}

/* ---------------------------------------------------------------- the core's environment */
static struct retro_hw_render_callback vxa_hw; static int vxa_hw_on;
static retro_keyboard_event_t vxa_kbcb;
static struct retro_system_av_info vxa_av;
static GLuint vxa_cfbo, vxa_cdepth; static unsigned vxa_cw, vxa_ch;      /* (the core's framebuffer - the slot it draws into now - and its size) */
static char vxa_sys[1100], vxa_saves[1100];
static int vxa_av_changed;
static void vxa_toast (NSString *s, double secs);
static uintptr_t vxa_get_fb (void);
static retro_proc_address_t vxa_get_proc (const char *s) { return (retro_proc_address_t) dlsym (RTLD_DEFAULT, s); }
static void vxa_corelog (enum retro_log_level l, const char *fmt, ...)
{
	char b[1500]; va_list a; va_start (a, fmt); vsnprintf (b, sizeof b, fmt, a); va_end (a); (void) l; fputs (b, stderr);
}
/* the core's framebuffer: three IOSurfaces taking turns - the core draws into one with OpenGL ES while Metal shows the
 * one finished at the refresh before (no copy). One surface made them take turns: glFinish each refresh, OpenGL and Metal
 * never at once - "starts smooth, then choppy" (builds 47-59). A picture is shown one refresh after it is drawn. Half
 * floats: room above white for HDR (8-bit BGRA where the device will not) */
#include <stdatomic.h>
#define VXA_NS 3
static CVPixelBufferRef vxa_pbs[VXA_NS]; static CVOpenGLESTextureCacheRef vxa_gcache; static CVOpenGLESTextureRef vxa_gtexs[VXA_NS];
static CVMetalTextureCacheRef vxa_mcache; static CVMetalTextureRef vxa_mtexs[VXA_NS]; static id<MTLDevice> vxa_mdev; static int vxa_half;
static GLuint vxa_fbos[VXA_NS]; static GLsync vxa_fence[VXA_NS]; static atomic_int vxa_busy[VXA_NS];   /* (busy: Metal still reading it) */
static unsigned vxa_sw[VXA_NS], vxa_sh[VXA_NS];          /* (each slot's picture size) */
static int vxa_pending = -1; static unsigned vxa_slot_seq[VXA_NS];   /* (a picture not finished at its refresh: kept, shown later in the same refresh) */
static int vxa_draw, vxa_newest = -1; static unsigned vxa_seq;   /* (vxa_seq: pictures finished, counted) */                    /* (the slot the core draws into; the last one finished) */
static uintptr_t vxa_get_fb (void) { return vxa_fbos[vxa_draw]; }
static void vxa_pick_draw (void)                         /* (a slot that is not the newest picture and not being shown) */
{
	for (int tries = 0; tries < 400; tries++) {
		for (int k = 1; k <= VXA_NS; k++) { const int s2 = (vxa_draw + k) % VXA_NS;
			if (s2 != vxa_newest && s2 != vxa_pending && !atomic_load (&vxa_busy[s2])) { vxa_draw = s2; vxa_cfbo = vxa_fbos[s2]; return; } }
		if (vxa_pending >= 0 && vxa_pending != vxa_newest && !atomic_load (&vxa_busy[vxa_pending])) {   /* (no other: the late one given up) */
			vxa_draw = vxa_pending; vxa_pending = -1; vxa_cfbo = vxa_fbos[vxa_draw]; return; }
		usleep (100); }                                  /* (all shown or about to: a moment - rare) */
	vxa_draw = (vxa_newest + 1) % VXA_NS; vxa_cfbo = vxa_fbos[vxa_draw];
}
static void vxa_core_fb (unsigned w, unsigned h)              /* (the core's framebuffers at least w x h) */
{
	if (w <= vxa_cw && h <= vxa_ch && vxa_fbos[0]) return;
	if (w < vxa_cw) w = vxa_cw; if (h < vxa_ch) h = vxa_ch;
	for (int k = 0; k < 2000; k++) { int b = 0; for (int i = 0; i < VXA_NS; i++) b |= atomic_load (&vxa_busy[i]); if (!b) break; usleep (100); }   /* (Metal done with them) */
	glFinish ();
	if (!vxa_fbos[0]) glGenFramebuffers (VXA_NS, vxa_fbos);
	if (!vxa_gcache) CVOpenGLESTextureCacheCreate (kCFAllocatorDefault, NULL, [EAGLContext currentContext], NULL, &vxa_gcache);
	if (!vxa_mcache && vxa_mdev) CVMetalTextureCacheCreate (kCFAllocatorDefault, NULL, vxa_mdev, NULL, &vxa_mcache);
	NSDictionary *at = @{ (id) kCVPixelBufferIOSurfacePropertiesKey: @{}, (id) kCVPixelBufferOpenGLESCompatibilityKey: @YES, (id) kCVPixelBufferMetalCompatibilityKey: @YES };
	int half = 1, ok = 1;
	for (int i = 0; i < VXA_NS; i++) {
		if (vxa_gtexs[i]) { CFRelease (vxa_gtexs[i]); vxa_gtexs[i] = NULL; } if (vxa_mtexs[i]) { CFRelease (vxa_mtexs[i]); vxa_mtexs[i] = NULL; }
		if (vxa_pbs[i]) { CVPixelBufferRelease (vxa_pbs[i]); vxa_pbs[i] = NULL; } if (vxa_fence[i]) { glDeleteSync (vxa_fence[i]); vxa_fence[i] = NULL; }
		for (int hf = half; hf >= 0 && !vxa_gtexs[i]; hf--) {
			if (vxa_pbs[i]) { CVPixelBufferRelease (vxa_pbs[i]); vxa_pbs[i] = NULL; }
			if (CVPixelBufferCreate (kCFAllocatorDefault, w, h, hf ? kCVPixelFormatType_64RGBAHalf : kCVPixelFormatType_32BGRA, (__bridge CFDictionaryRef) at, &vxa_pbs[i]) != kCVReturnSuccess) continue;
			CVReturn r = hf ? CVOpenGLESTextureCacheCreateTextureFromImage (kCFAllocatorDefault, vxa_gcache, vxa_pbs[i], NULL, GL_TEXTURE_2D, GL_RGBA16F, (GLsizei) w, (GLsizei) h, GL_RGBA, GL_HALF_FLOAT, 0, &vxa_gtexs[i])
			                : CVOpenGLESTextureCacheCreateTextureFromImage (kCFAllocatorDefault, vxa_gcache, vxa_pbs[i], NULL, GL_TEXTURE_2D, GL_RGBA, (GLsizei) w, (GLsizei) h, GL_BGRA_EXT, GL_UNSIGNED_BYTE, 0, &vxa_gtexs[i]);
			if (r != kCVReturnSuccess) { vxa_gtexs[i] = NULL; continue; }
			half = hf;                                   /* (the first's format: all three the same) */
			if (vxa_mcache) CVMetalTextureCacheCreateTextureFromImage (kCFAllocatorDefault, vxa_mcache, vxa_pbs[i], NULL, hf ? MTLPixelFormatRGBA16Float : MTLPixelFormatBGRA8Unorm, w, h, 0, &vxa_mtexs[i]); }
		const GLuint tx = vxa_gtexs[i] ? CVOpenGLESTextureGetName (vxa_gtexs[i]) : 0;
		glBindTexture (GL_TEXTURE_2D, tx);
		glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glBindFramebuffer (GL_FRAMEBUFFER, vxa_fbos[i]); glFramebufferTexture2D (GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tx, 0);
		if (vxa_hw.depth || vxa_hw.stencil) {
			if (!vxa_cdepth) { glGenRenderbuffers (1, &vxa_cdepth); }
			if (i == 0) { glBindRenderbuffer (GL_RENDERBUFFER, vxa_cdepth); glRenderbufferStorage (GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, (GLsizei) w, (GLsizei) h); }
			glFramebufferRenderbuffer (GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, vxa_cdepth); }
		ok &= glCheckFramebufferStatus (GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE && vxa_mtexs[i] != NULL;
		vxa_sw[i] = vxa_sh[i] = 0; atomic_store (&vxa_busy[i], 0); }
	vxa_half = half; vxa_newest = -1; vxa_pending = -1; vxa_draw = 0; vxa_cfbo = vxa_fbos[0];
	vxa_cw = w; vxa_ch = h; vlog ("core framebuffers: %d of %u x %u, %s, shared with Metal: %s", VXA_NS, w, h, vxa_half ? "half floats (HDR room)" : "8-bit", ok ? "yes" : "NOT ALL");
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
	case RETRO_ENVIRONMENT_GET_AUDIO_VIDEO_ENABLE: *(int *) data = vxa_nodraw ? 2 : 3; return true;   /* (a catch-up run: sound, no picture) */
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
uint32_t vxa_pad_buttons (void)                   /* (the first controller's RetroPad buttons, the player's mapping - the library's) */
{
	NSArray<GCController *> *cs = GCController.controllers; if (!cs.count) return 0;
	vxa_pad p; vxa_read_pad (cs[0], &p); return p.btn;
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
#define VXA_DECKH 58                            /* (the native control deck's height: full; slim 40) */
#define VXA_SLIMH 40
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
	CAShapeLayer *_padTog; CATextLayer *_padTogT; CGRect _padTogR; int _padDir;   /* (stick / D-pad: the switch beside it; the D-pad's arms pressed) */
	UILabel *_status; UIProgressView *_prog; UIButton *_menuBtn; UIView *_bar, *_welcome, *_deck; int _firstPack;
	UIButton *_popTab; double _popUntil;           /* (the pop-up deck: its tab, shown until) */
	NSArray<NSDictionary *> *_parts; NSMutableArray<NSString *> *_qIds; BOOL _qWifi; long long _qTotal, _qDone;
	long _runs, _presents, _late, _ticks; double _stat_t; unsigned _shownSeq; long _notReady, _lateShown;
	double _tRun, _tFin, _tDraw, _tMax, _tLinkDur;               /* (where a refresh's time goes: the stat line - "starts smooth, then choppy") */
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
	_padTog = [CAShapeLayer layer]; _padTog.fillColor = _stickBase.fillColor; _padTog.strokeColor = _stickBase.strokeColor; _padTog.lineWidth = 1.5; [_ctl.layer addSublayer:_padTog];
	_padTogT = [CATextLayer layer]; _padTogT.fontSize = 9.5; _padTogT.alignmentMode = kCAAlignmentCenter; _padTogT.contentsScale = UIScreen.mainScreen.scale;
	_padTogT.font = (__bridge CFTypeRef) [UIFont systemFontOfSize:9.5 weight:UIFontWeightBold]; _padTogT.foregroundColor = g.CGColor; [_ctl.layer addSublayer:_padTogT];
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
	_popTab = [UIButton buttonWithType:UIButtonTypeCustom]; _popTab.hidden = YES; _popTab.accessibilityLabel = @"Show the control deck";
	{ UIView *t = [[UIView alloc] initWithFrame:CGRectMake (10, 0, 44, 14)]; t.userInteractionEnabled = NO; t.backgroundColor = VX_PANEL; t.layer.borderWidth = 1; t.layer.borderColor = VX_EDGE.CGColor;
	  t.layer.cornerRadius = 7; t.layer.maskedCorners = kCALayerMinXMaxYCorner | kCALayerMaxXMaxYCorner; t.tag = 5;
	  UIImageView *a = [[UIImageView alloc] initWithImage:[UIImage systemImageNamed:@"chevron.up" withConfiguration:[UIImageSymbolConfiguration configurationWithPointSize:9 weight:UIImageSymbolWeightBold]]];
	  a.tintColor = VX_DIM; a.center = CGPointMake (22, 7); [t addSubview:a]; [_popTab addSubview:t]; }
	[_popTab addTarget:self action:@selector(popOpen) forControlEvents:UIControlEventTouchUpInside]; [self.view addSubview:_popTab];
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
	dispatch_after (dispatch_time (DISPATCH_TIME_NOW, (int64_t) (2 * NSEC_PER_SEC)), dispatch_get_main_queue (), ^{
		VXGameVC *s = ws; if (s) vlog ("downloads: %s", s.presentedViewController ? "shown" : "NOT shown"); });
}
- (void) askUpdate:(NSArray<NSString *> *)need
{
	long long sz = 0; NSMutableArray *names = [NSMutableArray array];
	for (NSDictionary *p in _parts) if ([need containsObject:p[@"id"]]) { sz += [p[@"size"] longLongValue]; [names addObject:p[@"name"]]; }
	vlog ("parts: new versions - %s (%.1f MB)", [names componentsJoinedByString:@", "].UTF8String, sz / 1e6);
	/* (the new versions of the parts here, and a needed one missing: fetched at once on the start screen, the progress
	 *  shown - the game only after them, the owner: no playing until all downloads are done. No offer to wait on - on the
	 *  iPad it never showed on the start screen and the start waited for good) */
	[self fetchParts:need wifi:NO];
}
- (void) fetchParts:(NSArray<NSString *> *)ids wifi:(BOOL)wifi
{
	if (!ids.count) { [self startCore]; return; }
	long long total = 0; for (NSDictionary *p in _parts) if ([ids containsObject:p[@"id"]]) total += [p[@"size"] longLongValue];
	_qIds = [ids mutableCopy]; _qWifi = wifi; _qTotal = total; _qDone = 0; _prog.hidden = NO; _prog.progress = 0;
	if (_link && !_welcome.superview) {                    /* (playing: the start screen over the game again, the game waits until it is done) */
		_welcome.alpha = 1; _welcome.frame = self.view.bounds; [self.view addSubview:_welcome]; vxa_hold (1); }
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
		if (!_link) { _firstPack = 1; [self startCore]; } else [self closeWelcome];
		return; }
	NSString *pid = _qIds.firstObject; NSDictionary *part = nil; for (NSDictionary *p in _parts) if ([p[@"id"] isEqualToString:pid]) part = p;
	if (!part) { [_qIds removeObjectAtIndex:0]; [self nextPart]; return; }
	long long psz = [part[@"size"] longLongValue];
	[VXPack.shared fetchPart:pid version:part[@"version"] wifiOnly:_qWifi progress:^(int64_t got, int64_t tot) { (void) tot;
			long long all = self->_qDone + got; self->_prog.progress = self->_qTotal > 0 ? (float) ((double) all / self->_qTotal) : 0;
			self->_status.text = [NSString stringWithFormat:@"%@\n%.1f of %.1f MB  -  %.0f %%\nIt goes on in the background: use other apps meanwhile.", part[@"name"], all / 1e6, self->_qTotal / 1e6,
				self->_qTotal > 0 ? 100.0 * all / self->_qTotal : 0];
}
		unpacking:^{ self->_status.text = [NSString stringWithFormat:@"%@\nUnpacking...", part[@"name"]]; }
		done:^(BOOL ok) {
			if (!ok) { self->_prog.hidden = YES; self->_status.text = @"The games could not be fetched. Close the app and try again."; if ([VXPack installed].count) {   /* (the files here still play: on with them after a moment) */
					self->_status.text = @"The download failed - no connection to the project's server. On with the files here; try again later in Settings > Downloads.";
					dispatch_after (dispatch_time (DISPATCH_TIME_NOW, (int64_t) (4 * NSEC_PER_SEC)), dispatch_get_main_queue (), ^{ self->_qIds = nil; if (self->_link) [self closeWelcome]; else [self startCore]; }); }
				return; }
			self->_qDone += psz; [self->_qIds removeObject:pid]; [self nextPart]; }];
}
- (void) closeWelcome                             /* (a download while playing done: the game on again) */
{
	[UIView animateWithDuration:0.4 animations:^{ self->_welcome.alpha = 0; } completion:^(BOOL f) { (void) f; [self->_welcome removeFromSuperview]; }];
	vxa_hold (0);
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
	[NSUserDefaults.standardUserDefaults removeObjectForKey:@"resk.land"]; [NSUserDefaults.standardUserDefaults removeObjectForKey:@"resk.up"];   /* (a smaller drawing tried in a test: gone) */
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
	__weak VXGameVC *wn = self; dispatch_after (dispatch_time (DISPATCH_TIME_NOW, (int64_t) (3 * NSEC_PER_SEC)), dispatch_get_main_queue (), ^{ VXGameVC *g = wn; if (g) vxa_name_first (g); });   /* (the player name) */
}
- (void) present { [self presentSlot:vxa_newest late:NO]; }
- (void) presentSlot:(int)sl late:(BOOL)late
{
	if (!_have) return;
	if (sl < 0 || !vxa_mtexs[sl] || !_pipe) return;
	if (vxa_slot_seq[sl] == _shownSeq || (int) (vxa_slot_seq[sl] - _shownSeq) < 0) return;   /* (nothing new; older than the one shown) */
	double tf0 = CACurrentMediaTime ();
	if (vxa_fence[sl]) {                                /* (its drawing done - normally long since. Not done at the refresh's start: kept,
	                                                       tried again after the emulator's step in the same refresh (tick) - dropped, it was
	                                                       10-25 pictures a second on an M3 iPad: the stutter, the owner 2026-11-19. A long
	                                                       wait here spun the processor: 120-144 % on the iPad, it heated up) */
		if (glClientWaitSync (vxa_fence[sl], GL_SYNC_FLUSH_COMMANDS_BIT, 2000000) == GL_TIMEOUT_EXPIRED) {
			_tFin += CACurrentMediaTime () - tf0; if (late) vxa_pending = -1; else { vxa_pending = sl; _notReady++; } return; }
		glDeleteSync (vxa_fence[sl]); vxa_fence[sl] = NULL; }
	_tFin += CACurrentMediaTime () - tf0; if (late) _lateShown++;
	if (vxa_pending == sl) vxa_pending = -1;
	_fw = vxa_sw[sl]; _fh = vxa_sh[sl]; _shownSeq = vxa_slot_seq[sl];
	/* (fit the picture, its own pixels if it fits: upright always at the same place - under the top row with the menu
	 *  button, the deck right below it when on, 150 pt kept for the touch controls; sideways in the middle) */
	double sc = _gl.contentScaleFactor; UIEdgeInsets s = self.view.safeAreaInsets; CGSize v = self.view.bounds.size;
	const int land = v.width > v.height, ipad = UIDevice.currentDevice.userInterfaceIdiom == UIUserInterfaceIdiomPad;
	const double row = land || [self barAuto] ? 0 : VXA_TOPROW;   /* (the quick bar's row: kept, or over the picture when it hides itself) */
	const int dmode = [self deckMode]; const double deckh = dmode == 1 ? VXA_DECKH : dmode == 2 ? VXA_SLIMH : 0;   /* (the native deck below the picture, upright; the pop-up: over it) */
	const double keep = (land || ipad || _ctl.hidden ? 0 : 150) + deckh;   /* (iPhone upright with the touch controls: room below for them; iPad: over the picture's foot) */
	double aw = (v.width - s.left - s.right) * sc, ah = (v.height - s.top - s.bottom - row - keep) * sc, k = fmin (1.0, fmin (aw / _fw, ah / _fh));
	double w = _fw * k, h = _fh * k, x = s.left * sc + (aw - w) / 2, top = land ? s.top * sc + (ah - h) / 2 : (s.top + row) * sc;
	{ extern int vx_app_pich; const double fit = fmin (ah, aw * _fh / _fw), r = fit / _fh;   /* (the core draws the picture at the size there is room for: 1:1) */
	  if (fabs (r - 1) > 0.01) { int want = (int) lround ((vx_app_pich > 0 ? vx_app_pich : (int) _fh) * r);
		if (want != vx_app_pich) { vlog ("picture: room for %.0f px high, %u now - asking the core for %d", fit, _fh, want); vx_app_pich = want; } } }
	@autoreleasepool {
		double td0 = CACurrentMediaTime ();
		id<CAMetalDrawable> d = [(CAMetalLayer *) _gl.layer nextDrawable]; _tDraw += CACurrentMediaTime () - td0; if (!d) return;
		MTLRenderPassDescriptor *rp = [MTLRenderPassDescriptor renderPassDescriptor]; rp.colorAttachments[0].texture = d.texture;
		rp.colorAttachments[0].loadAction = MTLLoadActionClear; rp.colorAttachments[0].clearColor = MTLClearColorMake (0, 0, 0, 1); rp.colorAttachments[0].storeAction = MTLStoreActionStore;
		id<MTLCommandBuffer> cb = [_queue commandBuffer]; id<MTLRenderCommandEncoder> en = [cb renderCommandEncoderWithDescriptor:rp];
		float rect[4] = { (float) (x / _dw * 2 - 1), (float) (1 - (top + h) / _dh * 2), (float) ((x + w) / _dw * 2 - 1), (float) (1 - top / _dh * 2) };   /* (NDC: x0 y0 x1 y1) */
		float uv[4] = { (float) _fw / vxa_cw, (float) _fh / vxa_ch, vxa_hw.bottom_left_origin ? 0.f : 1.f, 0 };
		float edr = 1; if (@available (iOS 16.0, *)) edr = (float) UIScreen.mainScreen.currentEDRHeadroom;
		float cfg[4] = { edr, vxa_half ? 1.f : 0.f, 0, 0 };
		[en setRenderPipelineState:_pipe]; [en setVertexBytes:rect length:sizeof rect atIndex:0]; [en setVertexBytes:uv length:sizeof uv atIndex:1];
		[en setFragmentBytes:cfg length:sizeof cfg atIndex:0]; [en setFragmentTexture:CVMetalTextureGetTexture (vxa_mtexs[sl]) atIndex:0];
		[en setFragmentSamplerState:(k == 1.0 ? _nearest : _linear) atIndex:0];
		[en drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4]; [en endEncoding];
		atomic_store (&vxa_busy[sl], 1); [cb addCompletedHandler:^(id<MTLCommandBuffer> b) { (void) b; atomic_store (&vxa_busy[sl], 0); }];
		[cb presentDrawable:d]; [cb commit]; _presents++; }
	CGRect pic = CGRectMake (x / sc, top / sc, w / sc, h / sc);
	[self placeDeck:pic mode:dmode];
	if (!CGRectEqualToRect (pic, _pic)) { _pic = pic; vlog ("picture %u x %u px at %.0f,%.0f %.0f x %.0f pt (%s)", _fw, _fh, pic.origin.x, pic.origin.y, pic.size.width, pic.size.height, k == 1.0 ? "1:1" : "scaled"); [self layoutControls]; }
}
- (void) gotFrame:(unsigned)w :(unsigned)h { (void) w; (void) h; _have = 1; }
- (void) tick:(CADisplayLink *)l
{
	double now = CACurrentMediaTime(), dt = _last ? now - _last : 0; _last = now; _ticks++;
	if (dt > 1.5 / (vxa_av.timing.fps > 0 ? vxa_av.timing.fps : 60)) _late++;
	if (dt > 0.25) dt = 0.25;
	_acc += dt * vxa_av.timing.fps; int n = (int) _acc; _acc -= n;
	double buf = vxa_ring_fill () / (vxa_av.timing.sample_rate > 0 ? vxa_av.timing.sample_rate : 44100);   /* (the sound decides: never far ahead, never dry) */
	const int onl = vxa_net_playing ();                     /* (VXApp.h: vxa_net_on is declared further down) */
	if (buf > 0.12 && n > 0) n--; else if (buf < 0.03 && n < 3 && !onl) n++;
	if (n > (onl ? 2 : 4)) n = onl ? 2 : 4;   /* (online: the lockstep waits for the other side - runs to fill the dry sound
	                                                           drew pictures nobody saw: 115 runs, 0 pictures a second, the owner 2026-11-19) */
	vxa_net_tick ();                               /* (online: the game agreed, its news - also while a screen is over the game) */
	if (vxa_holding) [self padBack];               /* (a screen over the game: the Vectrex's button 3 goes back) */
	if (vxa_holding || _welcome.superview) return; /* (a screen over the game, or a download on the start screen: it waits - no playing while downloading) */
	[EAGLContext setCurrentContext:_ctx];
	[self present];                                  /* (the picture finished at the last refresh: shown while the core draws the next) */
	vxa_pick_draw ();
	/* (behind: the runs before the last emulate only - drawing each one's picture as well, that no one sees, kept the GPU
	 *  behind for good: 3 runs a tick, ~20 pictures a second until a screen was opened) */
	const double tr0 = CACurrentMediaTime ();
	for (int i = 0; i < n; i++) { vxa_nodraw = i < n - 1; vxa_input_poll (); retro_run (); _runs++; }
	_tRun += CACurrentMediaTime () - tr0; _tLinkDur += l.targetTimestamp - l.timestamp;
	vxa_nodraw = 0;
	if (vxa_pending >= 0) [self presentSlot:vxa_pending late:YES];   /* (the one not ready at the start: now, in this refresh still) */
	if (vx_app_browser_wanted ()) { vlog ("the core's ROM browser asked for: the library instead"); [VXUI showLibraryFrom:self]; }
	{ const double tt = CACurrentMediaTime () - now; if (tt > _tMax) _tMax = tt; }
	if (now - _stat_t >= 5) {
		double t = now - _stat_t; _stat_t = now;
		if (_ticks) { const CGRect pr = _pic; const int barOver = !_bar.hidden && CGRectIntersectsRect (_bar.frame, pr), deckOver = !_deck.hidden && CGRectIntersectsRect (CGRectInset (_deck.frame, 0, 1), pr);
			vlog ("time a refresh: emulator %.1f ms, GL wait %.1f ms (%ld not ready, %ld shown late), Metal wait %.1f ms, longest %.1f ms; display %.1f Hz%s; over the picture: bar %d, deck %d",
			_tRun * 1000 / _ticks, _tFin * 1000 / _ticks, _notReady, _lateShown, _tDraw * 1000 / _ticks, _tMax * 1000, _tLinkDur > 0 ? _ticks / _tLinkDur : 0,
			NSProcessInfo.processInfo.lowPowerModeEnabled ? ", Low Power Mode" : "", barOver, deckOver); }
		_notReady = 0; _lateShown = 0;
		_tRun = _tFin = _tDraw = _tMax = _tLinkDur = 0;
		vlog ("stat: %.0f runs/s, %.0f pictures/s, %ld late of %ld ticks, sound %.0f ms, %u dry, %u dropped, %.0f MB, heat %ld, controllers %lu, keyboard %d",
			_runs / t, _presents / t, _late, _ticks, buf * 1000, vxa_under, vxa_drop, vxa_mem_mb (), (long) NSProcessInfo.processInfo.thermalState,
			(unsigned long) GCController.controllers.count, GCKeyboard.coalescedKeyboard != nil);
		{ extern int vx_net_playing_now (void), vx_net_diag (char *, int); if (vx_net_playing_now ()) { char d[200]; vx_net_diag (d, sizeof d); vlog ("online: %s", d); } }   /* (the lockstep's figures: the delay, ahead, waits) */
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
- (BOOL) deckShown { const int m = [self deckMode]; return m == 1 || m == 2; }   /* (room kept for it below the picture) */
- (int) deckMode                               /* (the setting Control deck, upright: 1 full, 2 slim, 3 popping up (full or slim), 0 off) */
{
	CGSize v = self.view.bounds.size; if (v.width > v.height || !_link) return 0;
	extern int vxa_deck_style (void), vxa_deck_pop (void); const int st = vxa_deck_style ();
	return st && vxa_deck_pop () ? 3 : st;
}
/* the deck in its place: full / slim below the picture (only when it moves - setting it each picture laid the view out
 * again and again); pop-up: the slim deck over the picture's foot for a while after its tab or a touch on it, while paused */
- (void) placeDeck:(CGRect)pic mode:(int)m
{
	CGSize v = self.view.bounds.size; UIEdgeInsets s = self.view.safeAreaInsets; VXDeck *dk = (VXDeck *) _deck;
	if (m == 0) { if (!_deck.hidden) _deck.hidden = YES; if (!_popTab.hidden) _popTab.hidden = YES; return; }
	extern int vxa_deck_style (void); const int want = m == 3 ? vxa_deck_style () : m; if (dk.mode != want) dk.mode = want;
	const double h = want == 1 ? VXA_DECKH : VXA_SLIMH;
	/* (popping up: below the picture where there is room for it (an iPhone upright), else over the picture's foot) */
	const BOOL below = m != 3 || v.height - s.bottom - CGRectGetMaxY (pic) >= h + 8;
	CGRect df = CGRectMake (s.left, below ? CGRectGetMaxY (pic) : CGRectGetMaxY (pic) - h, v.width - s.left - s.right, h);
	if (!CGRectEqualToRect (df, _deck.frame)) _deck.frame = df;
	BOOL show = YES;
	if (m == 3) { show = CACurrentMediaTime () < _popUntil || vx_app_paused () || vxa_holding; [_deck.superview bringSubviewToFront:_deck]; }
	if (_deck.hidden == show) { _deck.hidden = !show; if (show) [dk tick]; }
	const BOOL tab = NO; if (_popTab.hidden != !tab) _popTab.hidden = !tab;   /* (no tab: a tap on the picture's foot opens the pop-up deck) */
	if (tab) { CGRect tf = CGRectMake (CGRectGetMidX (pic) - 32, CGRectGetMaxY (pic) - 14, 64, 26); if (!CGRectEqualToRect (tf, _popTab.frame)) { _popTab.frame = tf; [_popTab viewWithTag:5].frame = CGRectMake (10, 12, 44, 14); } }
}
/* the Vectrex's button 3 (B on a modern pad; the player's own button, Settings > Controls) - back, on every screen over the
 * game: a step back in a settings list, else the screen closed (the owner: one button for back everywhere) */
- (void) padBack
{
	static uint32_t prev; const uint32_t b = vxa_pad_buttons (), down = b & ~prev; prev = b;
	if (!(down >> RETRO_DEVICE_ID_JOYPAD_B & 1)) return;
	UIViewController *top = self; while (top.presentedViewController && !top.presentedViewController.isBeingDismissed) top = top.presentedViewController;
	if (top == self) return;
	if ([top isKindOfClass:UINavigationController.class] && ((UINavigationController *) top).viewControllers.count > 1) { [(UINavigationController *) top popViewControllerAnimated:YES]; ulog (@"pad: back"); return; }
	if ([top isKindOfClass:UIAlertController.class]) return;   /* (a question: answered by tapping) */
	ulog (@"pad: close"); __weak VXGameVC *ws = self;
	[top dismissViewControllerAnimated:YES completion:^{ VXGameVC *g = ws; if (g && !g.presentedViewController) vxa_hold (0); }];   /* (nothing over the game any more: on) */
}
- (void) popOpen { _popUntil = CACurrentMediaTime () + 4; ulog (@"deck: pop-up opened"); [self placeDeck:_pic mode:[self deckMode]]; }
- (void) deckPoked { if ([self deckMode] == 3) _popUntil = CACurrentMediaTime () + 4; }
/* the quick bar hiding itself (Settings > App: the default on a 4:3 screen - an iPad, where its row would cost the picture its width): over
 * the picture, it fades 3 s after it was last shown; a tap at the top of the screen brings it back */
- (BOOL) barAuto
{
	extern int vxa_bar_hides (void); return vxa_bar_hides () != 0;
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
	[self layoutControls];   /* (the touch controls shown again: laid out while in the background they stayed hidden until something else changed) */
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
	else { double top = CGRectGetMaxY (pic) + ([self deckMode] == 1 ? VXA_DECKH : [self deckMode] == 2 ? VXA_SLIMH : 0), bot = v.height - s.bottom, need = fmax (120, 140 * tsize);
		if (bot - top < need) { bot = CGRectGetMaxY (pic) - 4; top = bot - need; }   /* (no room below the deck: over the picture's foot, above the deck - never on it) */
		zl = CGRectMake (s.left, top, v.width / 2, bot - top); zr = CGRectMake (v.width / 2, top, v.width / 2 - s.right, zl.size.height); }
	if ([ud boolForKey:@"touch.lefty"]) { CGRect t = zl; zl = zr; zr = t; }   /* (left-handed: the stick on the right) */
	_stickZone = zl;
	{ const BOOL left = CGRectGetMidX (zl) < v.width / 2; const double tw = 54, th = 26;   /* (the stick / D-pad switch: the zone's outer bottom corner) */
	  _padTogR = CGRectMake (left ? CGRectGetMinX (zl) + 12 : CGRectGetMaxX (zl) - 12 - tw, CGRectGetMaxY (zl) - 8 - th, tw, th);   /* (a pill: the word fits) */
	  _padTog.path = [UIBezierPath bezierPathWithRoundedRect:_padTogR cornerRadius:th / 2].CGPath;
	  _padTogT.string = [self dpad] ? @"STICK" : @"D-PAD"; _padTogT.frame = CGRectMake (_padTogR.origin.x, CGRectGetMidY (_padTogR) - 6.5, _padTogR.size.width, 13); }
	if ([self dpad]) _stickC = CGPointMake (CGRectGetMidX (zl), CGRectGetMidY (zl) + 10);   /* (the D-pad: in one place, always) */
	double r = fmin (fmin (zr.size.width / 4.6, zr.size.height / 4.4), 38 * tsize), gap = r * 0.35, cx = CGRectGetMidX (zr), cy = CGRectGetMidY (zr);
	for (int i = 0; i < 4; i++) {
		double bx = cx + (i % 2 ? r + gap / 2 : -r - gap / 2), by = cy + (i < 2 ? -r - gap / 2 : r + gap / 2);
		_btnR[i] = CGRectMake (bx - r, by - r, 2 * r, 2 * r);
		_btn[i].path = [UIBezierPath bezierPathWithOvalInRect:_btnR[i]].CGPath;
		_btnT[i].frame = CGRectMake (bx - r, by - 14, 2 * r, 28); }
	if ([self dpad]) [self drawPad];
	else if (!_stickT) { _stickC = CGPointMake (CGRectGetMidX (zl), CGRectGetMidY (zl)); [self drawStick:_stickC]; }
}
- (BOOL) dpad { return [NSUserDefaults.standardUserDefaults boolForKey:@"touch.dpad"]; }   /* (Settings > Controls, or the switch beside it) */
/* the D-pad: a cross in one place; up / down / left / right clearly - a diagonal only near the exact corner (30 degrees of
 * each 90), nothing in the middle (games such as Clean Sweep: the stick's way missed a turn now and then) */
- (void) drawPad
{
	const double R = _stickR > 0 ? _stickR : 60, w = R * 0.33; const CGPoint c = _stickC;
	UIBezierPath *p = [UIBezierPath bezierPath];
	[p moveToPoint:CGPointMake (c.x - w, c.y - R)]; [p addLineToPoint:CGPointMake (c.x + w, c.y - R)]; [p addLineToPoint:CGPointMake (c.x + w, c.y - w)];
	[p addLineToPoint:CGPointMake (c.x + R, c.y - w)]; [p addLineToPoint:CGPointMake (c.x + R, c.y + w)]; [p addLineToPoint:CGPointMake (c.x + w, c.y + w)];
	[p addLineToPoint:CGPointMake (c.x + w, c.y + R)]; [p addLineToPoint:CGPointMake (c.x - w, c.y + R)]; [p addLineToPoint:CGPointMake (c.x - w, c.y + w)];
	[p addLineToPoint:CGPointMake (c.x - R, c.y + w)]; [p addLineToPoint:CGPointMake (c.x - R, c.y - w)]; [p addLineToPoint:CGPointMake (c.x - w, c.y - w)]; [p closePath];
	_stickBase.path = p.CGPath;
	UIBezierPath *k = [UIBezierPath bezierPath]; const double ins = w * 1.1, r2 = R - 4, h = w - 4;   /* (the arms pressed, lit) */
	if (_padDir & 1) [k appendPath:[UIBezierPath bezierPathWithRoundedRect:CGRectMake (c.x - h, c.y - r2, 2 * h, r2 - ins) cornerRadius:4]];
	if (_padDir & 2) [k appendPath:[UIBezierPath bezierPathWithRoundedRect:CGRectMake (c.x - h, c.y + ins, 2 * h, r2 - ins) cornerRadius:4]];
	if (_padDir & 4) [k appendPath:[UIBezierPath bezierPathWithRoundedRect:CGRectMake (c.x - r2, c.y - h, r2 - ins, 2 * h) cornerRadius:4]];
	if (_padDir & 8) [k appendPath:[UIBezierPath bezierPathWithRoundedRect:CGRectMake (c.x + ins, c.y - h, r2 - ins, 2 * h) cornerRadius:4]];
	_stickKnob.path = k.CGPath;
}
- (void) stickTo:(CGPoint)p   /* (the stick or the D-pad under the thumb: port 1's directions) */
{
	const double R = _stickR > 0 ? _stickR : 60; double dx = p.x - _stickC.x, dy = p.y - _stickC.y, ax, ay; uint32_t b = vxa_tpad.btn & ~(0xFu << RETRO_DEVICE_ID_JOYPAD_UP);
	if ([self dpad]) {
		int dir = 0; if (hypot (dx, dy) > R * 0.22) { double a = atan2 (dy, dx) * 180 / M_PI; if (a < 0) a += 360;
			const double m = fmod (a, 90);   /* (within 15 degrees of a diagonal: both; else the nearest of the four) */
			if (m > 30 && m < 60) { dir = (dy < 0 ? 1 : 2) | (dx < 0 ? 4 : 8); }
			else { const int q = (int) floor ((a + 45) / 90) % 4; dir = q == 0 ? 8 : q == 1 ? 2 : q == 2 ? 4 : 1; } }
		if (dir != _padDir) { _padDir = dir; [self drawPad]; if (dir) [[[UIImpactFeedbackGenerator alloc] initWithStyle:UIImpactFeedbackStyleLight] impactOccurred]; }
		ax = (dir & 8) ? 1 : (dir & 4) ? -1 : 0; ay = (dir & 2) ? 1 : (dir & 1) ? -1 : 0;
	} else {
		const double d = hypot (dx, dy); if (d > R) { dx *= R / d; dy *= R / d; }
		[self drawStick:CGPointMake (_stickC.x + dx, _stickC.y + dy)]; ax = dx / R; ay = dy / R; }
	if (ay < -0.4) b |= 1u << RETRO_DEVICE_ID_JOYPAD_UP; if (ay > 0.4) b |= 1u << RETRO_DEVICE_ID_JOYPAD_DOWN;
	if (ax < -0.4) b |= 1u << RETRO_DEVICE_ID_JOYPAD_LEFT; if (ax > 0.4) b |= 1u << RETRO_DEVICE_ID_JOYPAD_RIGHT;
	vxa_tpad.btn = b; vxa_tpad.ax[0][0] = (int16_t) (ax * 32767); vxa_tpad.ax[0][1] = (int16_t) (ay * 32767);
}
- (void) drawStick:(CGPoint)knob
{
	double R = _stickR > 0 ? _stickR : 60; _stickBase.path = [UIBezierPath bezierPathWithOvalInRect:CGRectMake (_stickC.x - R, _stickC.y - R, 2 * R, 2 * R)].CGPath;
	double kr = R * 0.43; _stickKnob.path = [UIBezierPath bezierPathWithOvalInRect:CGRectMake (knob.x - kr, knob.y - kr, 2 * kr, 2 * kr)].CGPath;
}
- (void) touchesBegan:(NSSet<UITouch *> *)ts withEvent:(UIEvent *)e
{
	if ([self barAuto] && _bar.alpha < 0.5) for (UITouch *t in ts) if ([t locationInView:self.view].y < self.view.safeAreaInsets.top + 70) { vlog ("top tapped: the quick bar"); [self showBar]; return; }
	if ([self deckMode] == 3 && _deck.hidden && _pic.size.width > 0) for (UITouch *t in ts) { CGPoint p = [t locationInView:self.view];   /* (the pop-up deck: a tap on the picture's foot) */
		/* (the picture's foot or the room under it - never a touch for the stick or a button: on the iPad they lie over
		 *  the picture's foot, and a press on 3 / 4 opened the deck instead) */
		BOOL onCtl = NO; if (!_ctl.hidden) { for (int i = 0; i < 4; i++) if (CGRectContainsPoint (CGRectInset (_btnR[i], -8, -8), p)) onCtl = YES; if (CGRectContainsPoint (_stickZone, p)) onCtl = YES; }
		if (!onCtl && p.x >= CGRectGetMinX (_pic) && p.x <= CGRectGetMaxX (_pic) && p.y >= CGRectGetMaxY (_pic) - 70 && p.y <= CGRectGetMaxY (_pic) + 80) { [self popOpen]; return; } }
	for (UITouch *t in ts) {
		CGPoint p = [t locationInView:self.view]; NSString *what = @"nothing";
		if (!_ctl.hidden) {
			int hit = -1; for (int i = 0; i < 4; i++) if (CGRectContainsPoint (CGRectInset (_btnR[i], -8, -8), p)) hit = i;
			if (hit >= 0 && !_btnTouch[hit]) { _btnTouch[hit] = t; static const int id4[4] = { RETRO_DEVICE_ID_JOYPAD_Y, RETRO_DEVICE_ID_JOYPAD_X, RETRO_DEVICE_ID_JOYPAD_B, RETRO_DEVICE_ID_JOYPAD_A };
				vxa_tpad.btn |= 1u << id4[hit]; _btn[hit].fillColor = [UIColor colorWithRed:0x24 / 255.0 green:0x44 / 255.0 blue:0x3a / 255.0 alpha:0.9].CGColor;
				UIImpactFeedbackGenerator *h = [[UIImpactFeedbackGenerator alloc] initWithStyle:UIImpactFeedbackStyleLight]; [h impactOccurred]; what = [NSString stringWithFormat:@"button %d", hit + 1]; }
			else if (CGRectContainsPoint (CGRectInset (_padTogR, -6, -6), p)) {   /* (the stick / D-pad switch) */
				const BOOL d = ![self dpad]; [NSUserDefaults.standardUserDefaults setBool:d forKey:@"touch.dpad"]; _padDir = 0;
				[[[UIImpactFeedbackGenerator alloc] initWithStyle:UIImpactFeedbackStyleMedium] impactOccurred]; [self layoutControls]; what = d ? @"switched to the D-pad" : @"switched to the stick"; }
			else if (!_stickT && CGRectContainsPoint (_stickZone, p)) { _stickT = t; if (![self dpad]) { _stickC = p; [self drawStick:p]; } else [self stickTo:p]; what = [self dpad] ? @"D-pad" : @"stick"; }
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
		if (t == _stickT) [self stickTo:p];
	}
}
- (void) touchesEnded:(NSSet<UITouch *> *)ts withEvent:(UIEvent *)e
{
	for (UITouch *t in ts) {
		CGPoint p = [t locationInView:self.view];
		if (t == _ptrT) { _ptrT = nil; [self pointerAt:p on:0]; }
		if (t == _stickT) { _stickT = nil; _padDir = 0; vxa_tpad.btn &= ~(0xFu << RETRO_DEVICE_ID_JOYPAD_UP); vxa_tpad.ax[0][0] = vxa_tpad.ax[0][1] = 0; [self layoutControls]; }
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
	if (d == RETRO_HW_FRAME_BUFFER_VALID) {
		if (vxa_fence[vxa_draw]) glDeleteSync (vxa_fence[vxa_draw]);
		vxa_fence[vxa_draw] = glFenceSync (GL_SYNC_GPU_COMMANDS_COMPLETE, 0); glFlush ();   /* (sent to the GPU now - not waited for) */
		vxa_sw[vxa_draw] = w; vxa_sh[vxa_draw] = h; vxa_newest = vxa_draw; vxa_seq++; vxa_slot_seq[vxa_draw] = vxa_seq; vxa_pick_draw ();
		[vxa_vc gotFrame:w :h]; return; }
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
/* the control deck (Settings > App): Auto (the default: what fits this screen - full, slim, slim popping up), Off, Full,
 * Slim, and whether a full / slim one pops up (an app setting of its own; the old value "pop-up": slim, popping up) */
static const char *vxa_deck_val (void) { int i = vxa_opt_find ("vectrex_emu_deck"); return i >= 0 ? vxa_opt[i].val : "auto"; }
int vxa_deck_sel (void) { const char *d = vxa_deck_val (); return !strcmp (d, "off") ? 1 : !strcmp (d, "full") ? 2 : !strcmp (d, "slim") || !strcmp (d, "pop-up") ? 3 : 0; }   /* (0 Auto, 1 Off, 2 Full, 3 Slim) */
int vxa_deck_style (void)                     /* (0 off, 1 full, 2 slim) */
{
	const int s = vxa_deck_sel (); return s == 0 ? (vxa_squarish () ? 2 : 1) : s == 1 ? 0 : s == 2 ? 1 : 2;
}
int vxa_deck_pop (void)
{
	const int s = vxa_deck_sel (); if (s == 0) return vxa_popdef () != 0; if (!strcmp (vxa_deck_val (), "pop-up")) return 1;
	return [NSUserDefaults.standardUserDefaults boolForKey:@"app.deckpop"];
}
void vxa_deck_set (int sel, int pop)
{
	static const char *const V[4] = { "auto", "off", "full", "slim" }; if (sel < 0 || sel > 3) sel = 0;
	vxa_oset (vxa_opt_find ("vectrex_emu_deck"), V[sel]); [NSUserDefaults.standardUserDefaults setBool:pop != 0 forKey:@"app.deckpop"]; vlog ("deck %s%s", V[sel], sel >= 2 && pop ? ", pop-up" : "");
}
/* the top bar (Settings > App): Auto (the default: shown where it fits, else hiding itself), Shown, Auto-hide */
int vxa_bar_sel (void) { NSString *m = [NSUserDefaults.standardUserDefaults stringForKey:@"app.bar"]; return [m isEqualToString:@"always"] ? 1 : [m isEqualToString:@"hide"] ? 2 : 0; }
int vxa_bar_hides (void) { const int s = vxa_bar_sel (); return s == 2 || (s == 0 && vxa_tight ()); }
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
static int vxa_net_pick (NSString *path);
void vxa_load (NSString *path)
{
	if (vxa_net_pick (path)) return;                  /* (online: the host's pick goes to both, player 2's to none) */
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
int vxa_is_held (void) { return vxa_holding; }
void vxa_toast_ext (NSString *s) { extern void vxa_ntoast_c (NSString *s); vxa_ntoast_c (s); }
void vxa_hold (int on)
{
	if (on != vxa_holding) { vxa_holding = on; vlog ("game %s", on ? "held (a screen over it)" : "running again");
		extern int vxa_net_playing (void); if (vxa_net_playing ()) vx_net_pause (on); }   /* (online: a menu open pauses both, as the desktop's P) */
}

/* ---------------------------------------------------------------- online play (the core's netvx.c; VXOnline.m the screen) */
#include <ifaddrs.h>
#include <arpa/inet.h>
NSString *vxa_net_ip (void)                          /* (this phone's address on its Wi-Fi: what player 2 enters) */
{
	struct ifaddrs *a = NULL, *i; NSString *out = @"?";
	if (getifaddrs (&a) == 0) for (i = a; i; i = i->ifa_next)
		if (i->ifa_addr && i->ifa_addr->sa_family == AF_INET && !strcmp (i->ifa_name, "en0")) { char b[64]; inet_ntop (AF_INET, &((struct sockaddr_in *) i->ifa_addr)->sin_addr, b, sizeof b); out = @(b); break; }
	if (a) freeifaddrs (a);
	return out;
}
/* the nearby host: the address .1 of the Wi-Fi this phone joined - an ad-hoc network's host is its router (an Android
 * phone's Wi-Fi Direct group 192.168.49.1, an iPhone's hotspot 172.20.10.1, a Windows PC's 192.168.137.1, Linux 10.42.0.1) */
NSString *vxa_net_gateway (void)                     /* (nil: not on an ad-hoc network - a hotel's .1 is its router, not a player) */
{
	NSString *ip = vxa_net_ip (); NSRange d = [ip rangeOfString:@"." options:NSBackwardsSearch];
	if ([ip isEqualToString:@"?"] || d.location == NSNotFound) return nil;
	for (NSString *p in @[ @"172.20.10.", @"192.168.49.", @"192.168.137.", @"10.42.0." ]) if ([ip hasPrefix:p]) return [p stringByAppendingString:@"1"];
	return nil;
}
int vxa_hotspot_on (void)                             /* (this iPhone's Personal Hotspot up: its own network 172.20.10.1) */
{
	struct ifaddrs *a = NULL, *i; int on = 0;
	if (getifaddrs (&a) == 0) for (i = a; i; i = i->ifa_next)
		if (i->ifa_addr && i->ifa_addr->sa_family == AF_INET && !strncmp (i->ifa_name, "bridge", 6)) { char b[64]; inet_ntop (AF_INET, &((struct sockaddr_in *) i->ifa_addr)->sin_addr, b, sizeof b); if (!strncmp (b, "172.20.10.", 10)) on = 1; }
	if (a) freeifaddrs (a);
	return on;
}
static NSString *vxa_net_rom, *vxa_net_join_rom;    /* (the game this phone hosts / joins) */
static int vxa_net_on;                               /* (1 while a game for two runs) */
static NSMutableDictionary<NSString *, NSString *> *vxa_md5s;   /* (path -> its MD5: the library, for the host's game) */
void vxa_net_hosting (NSString *rom) { vxa_net_rom = rom; }
int vxa_net_playing (void) { return vxa_net_on; }
static void vxa_ntoast (NSString *s);
/* online, a game picked in the library: the host's starts on both sides (vx_net_regame), player 2 can not */
static int vxa_net_pick (NSString *path)
{
	if (!vxa_net_on) return 0;
	NSDictionary *st = [NSJSONSerialization JSONObjectWithData:[@(vx_net_status ()) dataUsingEncoding:NSUTF8StringEncoding] options:0 error:nil];
	if ([st[@"role"] intValue] != 1) { vxa_ntoast (@"ONLINE: ONLY THE HOST CHANGES THE GAME"); vlog ("online: player 2 picked %s - only the host changes the game", path.lastPathComponent.UTF8String); return 1; }
	int ski = vxa_opt_find ("vectrex_emu_skip_intro"); const int skip = ski >= 0 && !strcmp (vxa_opt[ski].val, "on");
	if (vx_net_regame (path.fileSystemRepresentation, skip, "auto", 0.0)) { vxa_ntoast ([NSString stringWithFormat:@"ONLINE: STARTING %@ FOR BOTH...", path.lastPathComponent.stringByDeletingPathExtension.uppercaseString]);
		vlog ("online: the host picked %s - offered to player 2", path.lastPathComponent.UTF8String); }
	return 1;
}
static void vxa_ntoast (NSString *s);
void vxa_ntoast_c (NSString *s) { vxa_ntoast (s); }
static void vxa_ntoast (NSString *s)
{
	UIViewController *vc = vxa_vc; if (!vc) return; UIView *v = vc.view.window ?: vc.view;   /* (the window: over any open screen - a chat line came while the
	                                                     Online screen was open, under it, unseen) */
	UILabel *l = [UILabel new]; l.text = [NSString stringWithFormat:@"   %@   ", s]; l.font = [UIFont systemFontOfSize:14 weight:UIFontWeightMedium]; l.textColor = VX_HI;
	l.backgroundColor = [VX_PANEL colorWithAlphaComponent:0.95]; l.layer.cornerRadius = 10; l.layer.masksToBounds = YES; l.layer.borderWidth = 1; l.layer.borderColor = VX_EDGE2.CGColor;
	l.numberOfLines = 2; l.textAlignment = NSTextAlignmentCenter;
	CGSize sz = [l sizeThatFits:CGSizeMake (v.bounds.size.width - 40, 80)]; sz.width = fmin (sz.width, v.bounds.size.width - 40);
	static double nextY; static double nextT; const double now = CACurrentMediaTime (); if (now > nextT) nextY = 0;   /* (several at once: stacked) */
	l.frame = CGRectMake ((v.bounds.size.width - sz.width) / 2, v.safeAreaInsets.top + 64 + nextY, sz.width, fmax (34, sz.height + 8)); nextY += l.frame.size.height + 6; nextT = now + 3.5;
	[v addSubview:l]; [UIView animateWithDuration:0.5 delay:3.5 options:0 animations:^{ l.alpha = 0; } completion:^(BOOL d) { (void) d; [l removeFromSuperview]; }];
}
void vxa_net_chat_in (NSArray *lines)
{
	int n = 0; for (NSString *t in lines) if ([t isKindOfClass:NSString.class]) { vlog ("online chat in: %s", t.UTF8String); vxa_ntoast ([NSString stringWithFormat:@"💬 %@", t]); n++; }
	if (n) { AudioServicesPlaySystemSound (1003); [[UINotificationFeedbackGenerator new] notificationOccurred:UINotificationFeedbackTypeSuccess]; }   /* (a line in:
	                                                     heard and felt - the game goes on) */
}
/* (the other player paused: a line that stays until they are back) */
static UILabel *vxa_ban_l;
static void vxa_banner (NSString *s)
{
	if (!s) { [vxa_ban_l removeFromSuperview]; vxa_ban_l = nil; return; }
	UIViewController *vc = vxa_vc; if (!vc) return; UIView *v = vc.view;
	if (!vxa_ban_l) { UILabel *l = [UILabel new]; l.font = [UIFont systemFontOfSize:15 weight:UIFontWeightSemibold]; l.textColor = VX_HI;
		l.backgroundColor = [VX_PANEL colorWithAlphaComponent:0.95]; l.layer.cornerRadius = 10; l.layer.masksToBounds = YES; l.layer.borderWidth = 1; l.layer.borderColor = VX_EDGE2.CGColor;
		l.numberOfLines = 2; l.textAlignment = NSTextAlignmentCenter; vxa_ban_l = l; }
	if (![vxa_ban_l.text isEqualToString:s]) vxa_ban_l.text = s;
	CGSize sz = [vxa_ban_l sizeThatFits:CGSizeMake (v.bounds.size.width - 40, 80)]; sz.width = fmin (sz.width + 28, v.bounds.size.width - 40);
	vxa_ban_l.frame = CGRectMake ((v.bounds.size.width - sz.width) / 2, v.safeAreaInsets.top + 20, sz.width, fmax (40, sz.height + 14));
	if (vxa_ban_l.superview != v) [v addSubview:vxa_ban_l]; else [v bringSubviewToFront:vxa_ban_l];
}
/* (the chat over the running game: a line typed and sent - no menu, no pause) */
void vxa_chat_prompt (void)
{
	UIViewController *vc = vxa_vc; if (!vc || vc.presentedViewController) return;
	UIAlertController *a = [UIAlertController alertControllerWithTitle:@"Chat" message:nil preferredStyle:UIAlertControllerStyleAlert];
	[a addTextFieldWithConfigurationHandler:^(UITextField *f) { f.placeholder = @"A line to the other player"; f.autocapitalizationType = UITextAutocapitalizationTypeSentences; }];
	[a addAction:[UIAlertAction actionWithTitle:@"Cancel" style:UIAlertActionStyleCancel handler:nil]];
	[a addAction:[UIAlertAction actionWithTitle:@"Send" style:UIAlertActionStyleDefault handler:^(UIAlertAction *x) { NSString *v = a.textFields.firstObject.text;
		if (v.length) { extern void vx_net_chat (const char *); vx_net_chat (v.UTF8String); vlog ("online chat out: %s", v.UTF8String); } }]];
	[vc presentViewController:a animated:YES completion:nil];
}
static NSString *vxa_find_md5 (NSString *md5)          /* (the host's game in this phone's library: by its MD5, as the desktop's find_rom) */
{
	if (!vxa_md5s) vxa_md5s = [NSMutableDictionary dictionary];
	for (NSString *top in @[ @"roms", @"system/vectrex_emu/pitrex" ]) {     /* (the games; the PiTrex's own image lives on its card) */
		NSString *root = [vxa_docs () stringByAppendingPathComponent:top];
		NSDirectoryEnumerator *e = [NSFileManager.defaultManager enumeratorAtPath:root];
		for (NSString *rel in e) { NSString *ext = rel.pathExtension.lowercaseString;
			if (![@[ @"bin", @"vec", @"gam", @"rom", @"um2", @"img", @"pit" ] containsObject:ext]) continue;
			if ([top hasPrefix:@"system"] && rel.pathComponents.count > 1) continue;   /* (the card's own folders: not the image) */
			NSString *p = [root stringByAppendingPathComponent:rel], *m = vxa_md5s[p];
			if (!m) { char h[33]; long long n; if (!vx_net_md5_file (p.fileSystemRepresentation, h, &n)) continue; m = @(h); vxa_md5s[p] = m; }
			if ([m isEqualToString:md5]) return p; } }
	return nil;
}
/* each moment: what the online game asks of the app - both start the agreed game, its news shown */
static void vxa_net_tick (void)
{
	static double last; const double now = CACurrentMediaTime (); if (now - last < 0.15) return; last = now;
	NSDictionary *st = [NSJSONSerialization JSONObjectWithData:[@(vx_net_status ()) dataUsingEncoding:NSUTF8StringEncoding] options:0 error:nil];
	if (![st isKindOfClass:NSDictionary.class]) return;
	const int ph = [st[@"phase"] intValue], role = [st[@"role"] intValue], acc = [st[@"accepted"] intValue];
	static int asked, said_pause, said_desync, said_lost, hot_started; static double hot_said; int just = 0;   /* (just: started in this pass - ph is still the phase before) */
	NSString *peer = [st[@"peer"] isKindOfClass:NSString.class] && [st[@"peer"] length] ? [st[@"peer"] uppercaseString] : (role == 1 ? @"PLAYER 2" : @"THE HOST");
	if (ph < 3) { asked = 0; hot_started = 0; vxa_net_join_rom = nil; }
	/* player 2, joining the host's running game: its state arriving, then in it (the core switches to playing itself) */
	if (role == 2 && hot_started && !vxa_net_on) {
		if (ph == 4) { vxa_net_on = 1; said_pause = said_desync = said_lost = 0; vlog ("online: in the host's running game as player 2");
			vxa_ntoast ([NSString stringWithFormat:@"ONLINE: IN %@'S GAME - YOU ARE PLAYER 2%@", peer, [st[@"relay"] intValue] ? @" (RELAYED)" : @""]); }
		else if (now - hot_said > 1.0 && [st[@"hot_total"] intValue] > 0) { hot_said = now;
			vxa_ntoast ([NSString stringWithFormat:@"ONLINE: JOINING %@'S GAME... %d %%", peer, 100 * [st[@"hot_got"] intValue] / MAX (1, [st[@"hot_total"] intValue])]); } }
	if (ph == 3 && !vxa_net_on) {
		if (role == 1 && acc == 1 && [st[@"ready"] intValue] && vxa_net_rom.length) {   /* (the host: player 2 has the game (and its card compared))  */
			if ([st[@"hot"] intValue] && vx_net_go_hot ()) {   /* (app to app, no SD card: player 2 plugged into the running game - no reset) */
				vxa_net_on = 1; just = 1; said_pause = said_desync = said_lost = 0; vlog ("online: player 2 joins the running game as player 2");
				vxa_ntoast ([NSString stringWithFormat:@"ONLINE: %@ JOINED AS PLAYER 2%@ - YOUR GAME GOES ON", peer, [st[@"relay"] intValue] ? @" (RELAYED)" : @""]); }
			else {                                       /* (the desktop, or a UVMC2 / PiTrex: both start anew) */
				vx_net_prepare (); vxa_load (vxa_net_rom); vx_net_go (); vxa_net_on = 1; just = 1; said_pause = said_desync = said_lost = 0;
				vlog ("online: playing %s as player 1 (both started anew)", vxa_net_rom.lastPathComponent.UTF8String);
				vxa_ntoast ([NSString stringWithFormat:@"ONLINE: %@ JOINED%@ - BOTH START ANEW, YOU ARE PLAYER 1", peer, [st[@"relay"] intValue] ? @" (RELAYED)" : @""]); } }
		else if (role == 2 && !asked) { asked = 1;                               /* (the joiner: the host's game in the library?) */
			NSDictionary *g = st[@"game"]; NSString *md5 = [g isKindOfClass:NSDictionary.class] ? g[@"md5"] : nil;
			NSString *name = [g isKindOfClass:NSDictionary.class] ? [g[@"name"] stringByDeletingPathExtension] : @"?";
			{ NSString *p = md5 ? vxa_find_md5 (md5) : nil;
				if (!p) { vx_net_accept (0); vxa_ntoast ([NSString stringWithFormat:@"ONLINE: YOU DO NOT HAVE %@", name.uppercaseString]); vlog ("online: no ROM with md5 %s", md5.UTF8String); }
				else { vx_net_accept (1); vxa_net_join_rom = p; vlog ("online: the host's game here: %s - waiting for the start", p.lastPathComponent.UTF8String); } } }
		else if (role == 2 && acc == 1 && [st[@"ready"] intValue] && vxa_net_join_rom.length && [st[@"hot"] intValue]) {   /* (into the host's running game: its state follows) */
			if (!hot_started) { hot_started = 1; hot_said = 0; vx_net_prepare_hot (); vxa_load (vxa_net_join_rom);
				vlog ("online: joining the host's running game %s", vxa_net_join_rom.lastPathComponent.UTF8String); vxa_ntoast ([NSString stringWithFormat:@"ONLINE: JOINING %@'S GAME...", peer]); } }
		else if (role == 2 && acc == 1 && [st[@"ready"] intValue] && vxa_net_join_rom.length) {   /* (both agreed, the card too: start) */
			vx_net_prepare (); vxa_load (vxa_net_join_rom); vx_net_go (); vxa_net_on = 1; just = 1; said_pause = said_desync = said_lost = 0;
			vlog ("online: playing %s as player 2", vxa_net_join_rom.lastPathComponent.UTF8String);
			vxa_ntoast ([NSString stringWithFormat:@"ONLINE: CONNECTED%@ - YOU ARE PLAYER 2%@", [st[@"relay"] intValue] ? @" (RELAYED)" : @"",
				[st[@"stream"] intValue] ? @" - STREAMED BY THE HOST" : [st[@"card"] intValue] ? [NSString stringWithFormat:@" - UVMC2 %@", [st[@"uvm2"] uppercaseString]] : @""]); } }   /* (streamed: a card game run by the host, its lines shown here - netvx.c) */
	if (vxa_net_on) {
		static int seen_rg = 0, seen_reset = 0;
		{ NSString *m = st[@"regame_msg"]; if ([m isKindOfClass:NSString.class] && m.length) { vxa_ntoast ([@"ONLINE: " stringByAppendingString:m.uppercaseString]); vlog ("online: %s", m.UTF8String); } }
		{ NSDictionary *g = st[@"regame_offer"];                 /* (player 2: the host's next game - here?) */
		  if ([g isKindOfClass:NSDictionary.class]) { NSString *p = [g[@"md5"] isKindOfClass:NSString.class] ? vxa_find_md5 (g[@"md5"]) : nil;
			vx_net_regame_answer (p.length ? p.fileSystemRepresentation : NULL);
			if (!p.length) vxa_ntoast ([NSString stringWithFormat:@"ONLINE: YOU DO NOT HAVE %@", [g[@"name"] description].stringByDeletingPathExtension.uppercaseString]);
			vlog ("online: the host's next game %s: %s", [[g[@"name"] description] UTF8String], p.length ? p.lastPathComponent.UTF8String : "not here"); } }
		{ const int n = [st[@"regame_n"] intValue]; if (n != seen_rg) { seen_rg = n; NSString *gn = [st[@"game"] isKindOfClass:NSDictionary.class] ? [st[@"game"][@"name"] description] : @"";
			vxa_ntoast ([NSString stringWithFormat:@"ONLINE: NOW PLAYING %@", gn.stringByDeletingPathExtension.uppercaseString]); vlog ("online: both switched to %s", gn.UTF8String); } }
		{ const int n = [st[@"reset_n"] intValue]; if (n != seen_reset) { seen_reset = n; const int me = [st[@"role"] intValue];
			NSString *who = [st[@"reset_by"] intValue] == me ? @"YOU" : ([st[@"peer"] isKindOfClass:NSString.class] && [st[@"peer"] length] ? [st[@"peer"] uppercaseString] : @"THE OTHER PLAYER");
			vxa_ntoast ([NSString stringWithFormat:@"RESET (%@)", who]); vlog ("online: reset by %s", who.UTF8String); } }
		NSArray *chat = st[@"chat"]; if ([chat isKindOfClass:NSArray.class] && chat.count) vxa_net_chat_in (chat);
		const int pp = [st[@"peer_paused"] intValue] && [st[@"paused"] intValue];
		{ const int pw = [st[@"peer_why"] intValue];   /* (the other player's own pause: 1 a menu, 2 another app - the line stays until they are back) */
		  if (pw) { vxa_banner ([NSString stringWithFormat:@"PAUSED BY %@ - %@", peer, pw == 2 ? @"IN ANOTHER APP" : @"IN THE MENU"]);
			if (said_pause != pw) { said_pause = pw; vlog ("online: paused by the other player (%s)", pw == 2 ? "another app" : "the menu"); } }
		  else if (said_pause) { said_pause = 0; vxa_banner (nil); vxa_ntoast ([NSString stringWithFormat:@"%@ IS BACK", peer]); vlog ("online: the other player is back"); } }
		if ([st[@"desync"] intValue] >= 0 && !said_desync) { said_desync = 1; vxa_ntoast (@"ONLINE: OUT OF STEP - THE TWO GAMES DIFFER"); }
		/* (only Leave ends a game: the other's Leave ends it here too; a gap in the connection is waited out) */
		if ([st[@"left"] intValue] && !said_lost) { said_lost = 1; vx_net_leave (); vxa_net_on = 0;
			if (role == 1) { extern int vxa_net_rehost (void); const int again = vxa_net_rehost ();   /* (the host: its game goes on, open for the next player) */
				vxa_ntoast ([NSString stringWithFormat:@"ONLINE: %@ LEFT - %@", peer, again ? @"YOUR GAME GOES ON, HOSTED AGAIN" : @"YOUR GAME GOES ON"]); vlog ("online: the other player left - hosted again: %d", again); }
			else { vxa_ntoast ([NSString stringWithFormat:@"ONLINE: %@ LEFT - YOUR GAME GOES ON", peer]); vlog ("online: the host left"); } }
		else if (!said_lost) { static int gap; const int l = [st[@"lost"] intValue];
			if (l && !gap) { gap = 1; if ([st[@"peer_why"] intValue] != 2) vxa_ntoast (@"ONLINE: NO CONNECTION - WAITING FOR THE OTHER PLAYER (ONLINE > LEAVE TO STOP)"); vlog ("online: no connection - waiting"); }
			else if (!l && gap) { gap = 0; vxa_ntoast (@"ONLINE: CONNECTED AGAIN"); vlog ("online: connected again"); } }
		if (ph != 4 && !said_lost && !just) vxa_net_on = 0; }
	if (!vxa_net_on && vxa_ban_l) vxa_banner (nil);   /* (not online any more: the pause line away) */   /* (not in the pass that started it: ph was read before - it switched every game's
	                                                     "online" off at once: no game change for the host, the offline catch-up on - 2026-11-20) */
}

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
static UIBackgroundTaskIdentifier vxa_bgt; static int vxa_bgt_on;   /* (UIBackgroundTaskInvalid is no constant: a flag beside it) */
- (void) applicationWillResignActive:(UIApplication *)a { [vxa_vc setRunning:0]; if (vxa_net_playing ()) vx_net_pause (2); }   /* (online: the other side sees
	                                                     "in another app" - a call, Control Center, the app switcher alike) */
- (void) applicationDidEnterBackground:(UIApplication *)a
{
	if (!vxa_net_playing () || vxa_bgt_on) return;   /* (online: iOS's extra half minute - the connection kept, the pause told) */
	vxa_bgt_on = 1; vxa_bgt = [a beginBackgroundTaskWithName:@"online" expirationHandler:^{ vlog ("online: iOS stops the app in the background now"); if (vxa_bgt_on) { vxa_bgt_on = 0; [a endBackgroundTask:vxa_bgt]; } }];
	vlog ("online: in the background - the connection kept for a while");
}
- (void) applicationDidBecomeActive:(UIApplication *)a
{
	[vxa_vc setRunning:1]; if (vxa_net_playing ()) vx_net_pause (vxa_holding ? 1 : 0);
	if (vxa_bgt_on) { vxa_bgt_on = 0; [a endBackgroundTask:vxa_bgt]; }
}
- (void) applicationDidReceiveMemoryWarning:(UIApplication *)a { vlog ("memory warning: %.0f MB in use", vxa_mem_mb ()); }
- (void) applicationWillTerminate:(UIApplication *)a { vlog ("the app ends"); retro_unload_game (); retro_deinit (); }
@end
