/* dbgsend.c - Vectrex_EMU's libretro core: the owner's private iOS test build sends what testing needs to the project's
 * server (tools/vxreport.py, POST /ioslog on the Linode, a secret token - only in a build made with VX_LOG_TOKEN):
 *   - the core's log, its new lines every 10 s; the previous session's log at a game's start (after a crash: its end)
 *   - RetroArch's own logs (logs/, one file a run), their new lines every 10 s
 *   - a status every 15 s: the build, the screen, the picture, the frame rates, EMU RATE, CPU, the game, the menu driver
 *   - RetroArch's screenshots (screenshots/), each once, as they appear
 *   - iOS's crash and hang reports (MetricKit), on the start after them
 * Each send on a thread of its own (the game never waits for the network); what was sent is noted in the saves folder
 * (vectrex_emu_sent.txt) so nothing goes twice. Included by libretro_vx.c after pxfetch.c.
 * Copyright (c) 2026 Hugo Bokhorst. Part of Vectrex_EMU; see LICENSE.md. */
#if VX_IOS && defined(VX_LOG_TOKEN)
#include <dirent.h>
#include <strings.h>
#define DBG_HOST "172.233.60.40"
#define DBG_PORT "7746"
static char dbg_sess[12], dbg_build[16], dbg_log[1100], dbg_root[1100], dbg_saves[1100];
static long dbg_logoff;

typedef struct { char kind[64], ext[8]; unsigned char *d; size_t n; } dbg_job;
static void *dbg_thr (void *a)
{
	dbg_job *j = a;
	struct addrinfo hints, *ai = NULL; memset (&hints, 0, sizeof hints); hints.ai_socktype = SOCK_STREAM;
	if (!getaddrinfo (DBG_HOST, DBG_PORT, &hints, &ai) && ai) {
		int s = socket (ai->ai_family, ai->ai_socktype, ai->ai_protocol);
		if (s >= 0) { struct timeval tv = { 20, 0 }; setsockopt (s, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv); setsockopt (s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
#ifdef SO_NOSIGPIPE
			{ int one = 1; setsockopt (s, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one); }
#endif
			if (!connect (s, ai->ai_addr, ai->ai_addrlen)) {
				char h[600]; const int hn = snprintf (h, sizeof h, "POST /ioslog HTTP/1.0\r\nHost: " DBG_HOST "\r\nUser-Agent: Vectrex_EMU-iOS\r\n"
					"X-Vectrex-Token: %s\r\nX-Vectrex-Kind: %s-%s\r\nX-Vectrex-Ext: %s\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n",
					VX_LOG_TOKEN, dbg_sess, j->kind, j->ext, j->n);
				if (send (s, h, (size_t) hn, 0) == hn) { size_t o = 0; while (o < j->n) { const ssize_t w = send (s, j->d + o, j->n - o, 0); if (w <= 0) break; o += (size_t) w; }
					char r[64]; (void) recv (s, r, sizeof r, 0); } }
			close (s); }
		freeaddrinfo (ai); }
	free (j->d); free (j);
	return NULL;
}
static void dbg_post (const char *kind, const char *ext, const void *d, size_t n)   /* (a copy; sent on a thread) */
{
	if (!n || n > 15u << 20) return;
	dbg_job *j = calloc (1, sizeof *j); if (!j) return;
	j->d = malloc (n); if (!j->d) { free (j); return; }
	memcpy (j->d, d, n); j->n = n; snprintf (j->kind, sizeof j->kind, "%s", kind); snprintf (j->ext, sizeof j->ext, "%s", ext);
	pthread_t t; if (pthread_create (&t, NULL, dbg_thr, j)) { free (j->d); free (j); } else pthread_detach (t);
}
static unsigned char *dbg_read (const char *p, long from, size_t *n)   /* (a file from an offset; *n its length) */
{
	FILE *f = fopen (p, "rb"); *n = 0; if (!f) return NULL;
	fseek (f, 0, SEEK_END); const long z = ftell (f); if (z <= from) { fclose (f); return NULL; }
	unsigned char *b = malloc ((size_t) (z - from)); if (!b) { fclose (f); return NULL; }
	fseek (f, from, SEEK_SET); *n = fread (b, 1, (size_t) (z - from), f); fclose (f); return b;
}
/* (what was sent: name and size, a line each - the logs' growth, the screenshots once) */
static char *dbg_sent; static size_t dbg_sentn;
static long dbg_sent_size (const char *name)
{
	if (!dbg_sent) return -1; const size_t ln = strlen (name);
	for (char *l = dbg_sent; l && *l; l = strchr (l, '\n') ? strchr (l, '\n') + 1 : NULL)
		if (!strncmp (l, name, ln) && l[ln] == '\t') return atol (l + ln + 1);
	return -1;
}
static void dbg_sent_set (const char *name, long size)
{
	char line[600]; snprintf (line, sizeof line, "%s\t%ld\n", name, size);
	const size_t ln = strlen (name); char *l = dbg_sent;
	for (; l && *l; l = strchr (l, '\n') ? strchr (l, '\n') + 1 : NULL)
		if (!strncmp (l, name, ln) && l[ln] == '\t') { char *e = strchr (l, '\n'); const size_t rest = e ? dbg_sentn - (size_t) (e + 1 - dbg_sent) : 0;
			memmove (l, e ? e + 1 : l + strlen (l), rest + 1); dbg_sentn -= (size_t) ((e ? e + 1 : l + strlen (l)) - l); break; }
	char *nb = realloc (dbg_sent, dbg_sentn + strlen (line) + 1); if (!nb) return; dbg_sent = nb;
	memcpy (dbg_sent + dbg_sentn, line, strlen (line) + 1); dbg_sentn += strlen (line);
	char p[1200]; snprintf (p, sizeof p, "%s/vectrex_emu_sent.txt", dbg_saves); FILE *f = fopen (p, "w"); if (f) { fputs (dbg_sent, f); fclose (f); }
}
/* (a folder's files with these endings: the new ones sent whole, a grown one's new part - logs - or once - shots) */
static void dbg_scan (const char *sub, const char *ending, const char *kind, int grow)
{
	char d[1200]; snprintf (d, sizeof d, "%s/%s", dbg_root, sub); DIR *dd = opendir (d); if (!dd) return;
	struct dirent *e; int sent = 0;
	while ((e = readdir (dd)) && sent < 3) {
		const char *f = e->d_name; const size_t fl = strlen (f), el = strlen (ending);
		if (e->d_type == DT_DIR && f[0] != '.' && !strchr (sub, '/')) {   /* (one level down: screenshots sorted into folders) */
			char s2[600]; snprintf (s2, sizeof s2, "%s/%s", sub, f); dbg_scan (s2, ending, kind, grow); continue; }
		if (f[0] == '.' || fl < el || strcasecmp (f + fl - el, ending)) continue;
		char p[1500], key[600]; snprintf (p, sizeof p, "%s/%s", d, f); snprintf (key, sizeof key, "%s/%s", sub, f);
		struct stat st; if (stat (p, &st)) continue;
		const long had = dbg_sent_size (key);
		if (had >= (long) st.st_size || (!grow && had >= 0)) continue;
		size_t n; unsigned char *b = dbg_read (p, grow && had > 0 ? had : 0, &n);
		if (b) { char k[64]; snprintf (k, sizeof k, "%s", kind); dbg_post (k, grow ? "txt" : "png", b, n); free (b); sent++; }
		dbg_sent_set (key, (long) st.st_size); }
	closedir (dd);
}
/* (iOS's crash and hang reports: MetricKit, a subscriber made at run time - JSON each, sent) */
#include <objc/runtime.h>
#include <objc/message.h>
static void dbg_payloads (id arr, const char *kind)
{
	if (!arr) return;
	const unsigned long n = ((unsigned long (*) (id, SEL)) objc_msgSend) (arr, sel_registerName ("count"));
	for (unsigned long i = 0; i < n && i < 20; i++) {
		id p = ((id (*) (id, SEL, unsigned long)) objc_msgSend) (arr, sel_registerName ("objectAtIndex:"), i);
		id js = p ? ((id (*) (id, SEL)) objc_msgSend) (p, sel_registerName ("JSONRepresentation")) : NULL; if (!js) continue;
		const void *b = ((const void *(*) (id, SEL)) objc_msgSend) (js, sel_registerName ("bytes"));
		const unsigned long ln = ((unsigned long (*) (id, SEL)) objc_msgSend) (js, sel_registerName ("length"));
		if (b && ln) dbg_post (kind, "json", b, ln); }
}
static void dbg_diag (id self, SEL cmd, id payloads) { (void) self; (void) cmd; dbg_payloads (payloads, "diagnostics"); }
static void dbg_metrickit (void)
{
	Class mm = objc_getClass ("MXMetricManager"); if (!mm) return;
	Class c = objc_allocateClassPair (objc_getClass ("NSObject"), "VXDiagSubscriber", 0); if (!c) return;
	class_addMethod (c, sel_registerName ("didReceiveDiagnosticPayloads:"), (IMP) dbg_diag, "v@:@");
	Protocol *pr = objc_getProtocol ("MXMetricManagerSubscriber"); if (pr) class_addProtocol (c, pr);
	objc_registerClassPair (c);
	id o = ((id (*) (id, SEL)) objc_msgSend) ((id) c, sel_registerName ("new"));
	id m = ((id (*) (id, SEL)) objc_msgSend) ((id) mm, sel_registerName ("sharedManager"));
	if (m && o) { ((void (*) (id, SEL, id)) objc_msgSend) (m, sel_registerName ("addSubscriber:"), o);
		SEL past = sel_registerName ("pastDiagnosticPayloads");   /* (iOS 14: those not yet delivered) */
		if (((BOOL (*) (id, SEL, SEL)) objc_msgSend) (m, sel_registerName ("respondsToSelector:"), past))
			dbg_payloads (((id (*) (id, SEL)) objc_msgSend) (m, past), "diagnostics-past"); }
}
static void dbg_bundle_version (void)
{
	id b = ((id (*) (id, SEL)) objc_msgSend) ((id) objc_getClass ("NSBundle"), sel_registerName ("mainBundle"));
	id k = ((id (*) (id, SEL, const char *)) objc_msgSend) ((id) objc_getClass ("NSString"), sel_registerName ("stringWithUTF8String:"), "CFBundleVersion");
	id v = b && k ? ((id (*) (id, SEL, id)) objc_msgSend) (b, sel_registerName ("objectForInfoDictionaryKey:"), k) : NULL;
	const char *s = v ? ((const char *(*) (id, SEL)) objc_msgSend) (v, sel_registerName ("UTF8String")) : NULL;
	snprintf (dbg_build, sizeof dbg_build, "%s", s ? s : "?");
}
/* at a game's start, before the log is written anew: the previous session's log; the first time: MetricKit */
static void dbg_start (const char *sys)
{
	static int once; if (!sys || !*sys) return;
	snprintf (dbg_log, sizeof dbg_log, "%s/vectrex_emu/vectrex_emu.log", sys);
	snprintf (dbg_root, sizeof dbg_root, "%s", sys); { char *s = strrchr (dbg_root, '/'); if (s) *s = 0; }   /* (RetroArch's folder) */
	{ const char *sv = NULL; env_cb (RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY, &sv); snprintf (dbg_saves, sizeof dbg_saves, "%s", sv && *sv ? sv : dbg_root); }
	if (!once) { once = 1; snprintf (dbg_sess, sizeof dbg_sess, "s%04x", (unsigned) (VX_TICKS () & 0xffff)); dbg_bundle_version ();
		char p[1200]; snprintf (p, sizeof p, "%s/vectrex_emu_sent.txt", dbg_saves); size_t n; unsigned char *b = dbg_read (p, 0, &n);
		if (b) { dbg_sent = realloc (b, n + 1); if (dbg_sent) { dbg_sent[n] = 0; dbg_sentn = n; } }
		dbg_metrickit (); }
	size_t n; unsigned char *b = dbg_read (dbg_log, 0, &n);   /* (before this game writes it anew) */
	if (b) { dbg_post ("previous-core-log", "txt", b, n); free (b); }
	dbg_logoff = 0;
}
/* each refresh: every 10 s the logs' new lines and new screenshots, every 15 s the status */
static void dbg_tick (void)
{
	static double t10, t15; const double now = vt_wall (); if (!dbg_root[0]) return;
	if (now - t10 >= 10.0) { t10 = now;
		fflush (stderr); size_t n; unsigned char *b = dbg_read (dbg_log, dbg_logoff, &n);
		if (b) { dbg_post ("core-log", "txt", b, n); dbg_logoff += (long) n; free (b); }
		dbg_scan ("logs", ".log", "retroarch-log", 1); dbg_scan ("screenshots", ".png", "screenshot", 0); }
	if (now - t15 >= 15.0) { t15 = now;
		char s[1400]; const int sn = snprintf (s, sizeof s,
			"build %s  core 1.1\nscreen %d x %d  picture %d x %d  frame %d x %d  rot %d  deck %d\n%s | %s | %s  CPU %d%%  GPU %d%%\n"
			"paused %d  power %d  overlay %d  imager %d  stereo %d  game %s\n",
			dbg_build, scr_w, scr_h, CW, CH, OW, OH, opt_rot, opt_deck, dk_osd[0], dk_osd[1], dk_osd[2], dk_cpu, dk_gpu,
			vx_paused, pw_on, ov_on, opt_imager, stereo, cur_game);
		if (sn > 0) dbg_post ("status", "txt", s, (size_t) sn); }
}
#else
static void dbg_start (const char *sys) { (void) sys; }
static void dbg_tick (void) { }
#endif
