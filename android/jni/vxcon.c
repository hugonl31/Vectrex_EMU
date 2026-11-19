/* vxcon.c - the Android app's game process without its screen: the core and the app's online steps (Online.java: host
 * or join, the game agreed by its MD5, prepare / load / go in order) in a console, for tests on a device nobody is
 * holding (Termux, run detached). No picture (the core's CPU path, not shown), no sound; a scripted player moves the
 * stick and presses the buttons. Logs each 10 s the lockstep's frame, the checks made and out of step - as the app.
 *   vxcon SYSDIR ROM SECONDS host lan|public|private [NAME]
 *   vxcon SYSDIR ROM SECONDS join ADDRESS|CODE [NAME]
 * Copyright (c) 2026 Hugo Bokhorst. Part of Vectrex_EMU; see LICENSE.md. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>
#include "libretro.h"

static char sysd[1024], saves[1024];
static double now (void) { struct timespec t; clock_gettime (CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec * 1e-9; }
static void say (const char *f, ...) { va_list a; va_start (a, f); time_t t = time (NULL); struct tm tm; localtime_r (&t, &tm);
	printf ("[%02d:%02d:%02d] ", tm.tm_hour, tm.tm_min, tm.tm_sec); vprintf (f, a); printf ("\n"); fflush (stdout); va_end (a); }
static const char *opt (const char *k)
{
	static const char *o[][2] = { { "vectrex_emu_size", "600 px" }, { "vectrex_emu_profile", "low" }, { "vectrex_emu_deck", "off" }, { "vectrex_emu_rotate", "off" },
		{ "vectrex_emu_renderer", "CPU only" }, { "vectrex_emu_rate", "60 Hz" }, { "vectrex_emu_threaded", "on" }, { "vectrex_emu_power_fx", "off" } };
	for (unsigned i = 0; i < sizeof o / sizeof o[0]; i++) if (!strcmp (o[i][0], k)) return o[i][1];
	return NULL;
}
static void clog (enum retro_log_level l, const char *f, ...) { (void) l; va_list a; va_start (a, f); vfprintf (stderr, f, a); va_end (a); }
static bool env (unsigned cmd, void *data)
{
	switch (cmd) {
	case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY: *(const char **) data = sysd; return true;
	case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY: *(const char **) data = saves; return true;
	case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT: return true;
	case RETRO_ENVIRONMENT_GET_VARIABLE: { struct retro_variable *v = data; v->value = opt (v->key); return v->value != NULL; }
	case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE: *(bool *) data = false; return true;
	case RETRO_ENVIRONMENT_GET_LOG_INTERFACE: ((struct retro_log_callback *) data)->log = clog; return true;
	case RETRO_ENVIRONMENT_SET_HW_RENDER: return false;              /* (no GPU here: the core's CPU path) */
	case RETRO_ENVIRONMENT_GET_CAN_DUPE: *(bool *) data = true; return true;
	case 52: *(unsigned *) data = 1; return true;
	case RETRO_ENVIRONMENT_GET_AUDIO_VIDEO_ENABLE: *(int *) data = 2; return true;   /* (sound only: no picture drawn) */
	default: return false;
	}
}
static void video (const void *d, unsigned w, unsigned h, size_t p) { (void) d; (void) w; (void) h; (void) p; }
static size_t abatch (const int16_t *d, size_t n) { (void) d; return n; }
static void asample (int16_t l, int16_t r) { (void) l; (void) r; }
static void ipoll (void) {}
static unsigned long fr; static unsigned btn; static int ax;
static int16_t istate (unsigned port, unsigned dev, unsigned idx, unsigned id)
{
	if (port) return 0;
	if ((dev & RETRO_DEVICE_MASK) == RETRO_DEVICE_JOYPAD) return id < 16 ? (btn >> id & 1) : 0;
	if ((dev & RETRO_DEVICE_MASK) == RETRO_DEVICE_ANALOG && idx == 0 && id == 0) return (int16_t) ax;
	return 0;
}
extern int vx_net_host (int, const char *, const char *, const char *, int, const char *, double), vx_net_join (const char *, const char *, const char *);
extern void vx_net_accept (int), vx_net_prepare (void), vx_net_go (void), vx_net_leave (void), vx_net_prepare_hot (void); extern int vx_net_go_hot (void);
extern const char *vx_net_status (void); extern void vx_app_load (const char *); extern int vx_net_md5_file (const char *, char *, long long *);
static int jint (const char *js, const char *k)   /* (the status' own number: a key of the same name inside the game's object is a word - skipped) */
{
	char p[64]; snprintf (p, sizeof p, "\"%s\": ", k); const size_t n = strlen (p);
	for (const char *q = strstr (js, p); q; q = strstr (q + 1, p)) if ((q[n] >= '0' && q[n] <= '9') || q[n] == '-') return atoi (q + n);
	return -999;
}
int main (int argc, char **argv)
{
	if (argc < 6) { fprintf (stderr, "vxcon SYSDIR ROM SECONDS host lan|public|private [NAME] | join WHERE [NAME]\n"); return 2; }
	snprintf (sysd, sizeof sysd, "%s", argv[1]); const char *rom = argv[2]; const double secs = atof (argv[3]); const int host = !strcmp (argv[4], "host");
	const char *where = argv[5], *name = argc > 6 ? argv[6] : "NOVA";
	snprintf (saves, sizeof saves, "%s/vxcon_saves", getenv ("HOME") ? getenv ("HOME") : "."); mkdir (saves, 0755);
	retro_set_environment (env); retro_set_video_refresh (video); retro_set_audio_sample (asample); retro_set_audio_sample_batch (abatch);
	retro_set_input_poll (ipoll); retro_set_input_state (istate); retro_init ();
	struct retro_game_info gi = { rom, NULL, 0, NULL }; if (!retro_load_game (&gi)) { say ("game %s: FAILED", rom); return 1; }
	say ("game %s loaded; %s %s as %s", rom, host ? "hosting" : "joining", where, name);
	char md5[33]; long long sz; vx_net_md5_file (rom, md5, &sz);
	for (int i = 0; i < 60; i++) retro_run ();
	if (host) vx_net_host (!strcmp (where, "public") ? 0 : !strcmp (where, "private") ? 1 : 2, name, "0123456789abcdef0123456789abcdef", rom, 0, "auto", 0.0);
	else vx_net_join (where, name, "fedcba9876543210fedcba9876543210");
	const double t0 = now (); double next = t0, lastlog = 0; int playing = 0, asked = 0, lastph = -1, hot = 0, lastf = -1, flog = 0; double lastfc = 0, maxgap = 0; char lastcode[16] = "";
	while (now () - t0 < secs) {
		const char *st = vx_net_status (); const int ph = jint (st, "phase"), role = jint (st, "role"), acc = jint (st, "accepted"), rdy = jint (st, "ready");
		if (ph != lastph) { say ("phase %d -> %d (role %d)", lastph, ph, role); lastph = ph; }
		{ const char *c = strstr (st, "\"code\": \""); if (c && c[9] != '"') { char code[16] = ""; sscanf (c + 9, "%15[^\"]", code); if (code[0] && strcmp (code, lastcode)) { snprintf (lastcode, sizeof lastcode, "%s", code); say ("code %s", code); } } }
		if (!playing && ph == 3) {
			if (role == 2 && !asked) { asked = 1; const char *m = strstr (st, "\"md5\": \""); int same = m && !strncmp (m + 8, md5, 32); vx_net_accept (same); say ("the host's game %s here", same ? "is" : "is NOT"); }
			if (acc == 1 && rdy == 1 && jint (st, "hot") == 1) {   /* (app to app: into the host's running game - its state follows; the host goes on) */
				if (role == 2 && !hot) { hot = 1; vx_net_prepare_hot (); vx_app_load (rom); say ("joining the host's running game"); }
				if (role == 1 && vx_net_go_hot ()) { playing = 1; say ("player 2 joins the running game"); } }
			else if (acc == 1 && rdy == 1) { vx_net_prepare (); vx_app_load (rom); vx_net_go (); playing = 1; say ("playing as player %d (both started anew)", role); } }
		if (!playing && hot && ph == 4) { playing = 1; say ("in the host's running game as player 2 (%d of %d state bytes came)", jint (st, "hot_got"), jint (st, "hot_total")); }
		{ const int f = jint (st, "frame"); const double t = now (); if (f != lastf) { if (playing && lastf >= 0 && t - lastfc > maxgap) maxgap = t - lastfc; lastf = f; lastfc = t; } }   /* (a stall: the longest wait for the next game frame) */
		if (playing && now () - lastlog >= 10) { const int f = jint (st, "frame");
			say ("frame %d (%.1f a second, longest stall %.0f ms), checked %d, out of step %d, delay %d, rtt %d ms%s", f, lastlog > 0 ? (f - flog) / (now () - lastlog) : 0.0, maxgap * 1000, jint (st, "checked"), jint (st, "desync"), jint (st, "delay"), jint (st, "rtt"), jint (st, "relay") == 1 ? ", relayed" : "");
			lastlog = now (); flog = f; maxgap = 0; }
		if (playing && jint (st, "left") == 1) { say ("the other player left"); break; }
		/* the scripted player: the stick back and forth, a button now and then */
		fr++; ax = (int) ((fr * 7) % 200) * 327 - 32700; btn = (fr % 50 < 3) ? 1u << RETRO_DEVICE_ID_JOYPAD_A : 0; if (fr % 97 < 4) btn |= 1u << RETRO_DEVICE_ID_JOYPAD_Y;
		retro_run ();
		next += 1.0 / 60; const double d = next - now (); if (d > 0) usleep ((useconds_t) (d * 1e6)); else if (d < -0.25) next = now ();
	}
	const char *st = vx_net_status (); say ("end: frame %d, checked %d, out of step %d", jint (st, "frame"), jint (st, "checked"), jint (st, "desync"));
	vx_net_leave (); usleep (300000); retro_unload_game (); retro_deinit (); return 0;
}
