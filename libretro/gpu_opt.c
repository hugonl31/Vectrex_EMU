/* gpu_opt.c - Vectrex_EMU's libretro core: Optimize (the desktop's pctest.py), included by libretro_vx.c after fx_apply.
 * With the Quality profile on auto and the GPU drawing, once per device and screen rate (at the largest picture it is
 * for, the overlay on - see opt_check): a heavy scene drawn the way a busy game draws it (pctest.scene: circles of joined pieces with rests, raster
 * text, long lines, resting dots - ~2900 pieces, like Vectorblade's busiest frames) with every effect on (Ultra); if a
 * picture then needs more than 75 % of a refresh, each effect's cost is measured on its own and the costliest turned
 * off one by one until it fits - pctest.optimize's order and rules. A picture's time: many pictures drawn back to back
 * and waited for once (one wait per picture measured the GPU waking up, not its work). The result is kept in the saves
 * folder (vectrex_emu_optimized.txt), one line per case; measured in a game's first refreshes or by the own apps at their first start (vx_app_tune), never later in a game.
 * Copyright (c) 2026 Hugo Bokhorst. Part of Vectrex_EMU; see LICENSE.md. */

#ifndef OPT_FIT
#define OPT_FIT 0.75                            /* (pctest FIT: busy under this) */
#endif
static char opt_key[400], opt_key_seen[400];
static int opt_busy;

/* pctest.scene: the heavy frame, as the core's lines (emulator units, cycles) */
static int opt_scene (wline *L, int max)
{
	unsigned r = 7; int n = 0; double t = 0;
	#define RND() ((r = r * 1664525u + 1013904223u) >> 8) / 16777216.0
	#define PIECE(p1, p2, p3, p4, p5, p6) do { if (n < max) { wline *wl_ = &L[n++]; wl_->x0 = (p1); wl_->y0 = (p2); wl_->x1 = (p3); wl_->y1 = (p4); \
		wl_->cyc = (p6); wl_->t = t; wl_->b = (p5); wl_->rgb = 7; wl_->wq = 255; } t += (p6); } while (0)
	const double mx = ALG_MAX_X, my = ALG_MAX_Y;
	for (int i = 0; i < 60; i++) {                       /* circles of joined pieces, a rest where each starts */
		const double cx = (0.2 + 0.6 * RND ()) * mx, cy = (0.2 + 0.6 * RND ()) * my, rr = 600 + 2400 * RND ();
		static const int zz[3] = { 127, 95, 63 }; const int z = zz[(int) (RND () * 3) % 3];
		PIECE (cx + rr, cy, cx + rr, cy, z, 6);
		for (int k = 0; k < 24; k++) { const double a0 = 2 * M_PI * k / 24, a1 = 2 * M_PI * (k + 1) / 24, c = 2 * M_PI * rr / 24 / 80.0;
			PIECE (cx + rr * cos (a0), cy + rr * sin (a0), cx + rr * cos (a1), cy + rr * sin (a1), z, c > 1 ? c : 1); }
		t += 20; }
	for (int row = 0; row < 15; row++) {                 /* raster text: rows of short dashes */
		const double y = my * (0.1 + 0.05 * row); double x = mx * 0.15;
		for (int k = 0; k < 60; k++) { static const double ww[4] = { 140, 140, 280, 420 }; const double w = ww[(int) (RND () * 4) % 4];
			PIECE (x, y, x + w, y, 127, 2); x += w + 80; }
		t += 30; }
	for (int i = 0; i < 400; i++) {                      /* long lines */
		const double x0 = RND () * mx, y0 = RND () * my, x1 = RND () * mx, y1 = RND () * my;
		PIECE (x0, y0, x1, y1, 127, sqrt ((x1 - x0) * (x1 - x0) + (y1 - y0) * (y1 - y0)) / 80.0); t += 8; }
	for (int i = 0; i < 100; i++) {                      /* resting dots (shots, stars) */
		static const int zz[3] = { 127, 63, 31 }; const double x = RND () * mx, y = RND () * my;
		PIECE (x, y, x, y, zz[(int) (RND () * 3) % 3], 30); t += 8; }
	#undef PIECE
	#undef RND
	return n;
}
/* a picture's time (ms) with these switches: the lines built, the light, the glass and the case - pictures back to back */
static int opt_warm;                            /* (the GPU's clock up: once per measuring, not before each step) */
static double opt_time (const prow *v, wline *sc, int n, int frames)
{
	extern double vt_wall (void);
	fx_force = v; fx_apply (); bt_gpu = vx_beam && bt_gpu_ok ();   /* (the first picture: measured as it will be drawn) */
	const double dt = 1.0 / opt_rate; double t0 = 0;
	gpu_novideo = 1;
	double tw = vt_wall ();
	for (int f = -3; f < frames; f++) {
		if (f == -1 && opt_warm && vt_wall () - tw < 0.3) f = -2;   /* (warming up at least 0.3 s: the GPU's clock up first) */
		if (f == 0) { glFinish_ (); t0 = vt_wall (); opt_warm = 0; }
		memcpy (W1, sc, sizeof (wline) * n);
		const int np = build_lines (W1, n, 0, 0, 0);
		ph_frame (np, dt, opt_glow, opt_after, 0);
		gpu_present (NULL, FW, FH, FW, (float) CX0 / EW, 1.0f - (float) (CY0 + CH) / FH, (float) (CX0 + CW) / EW, 1.0f - (float) CY0 / FH, CW, CH, opt_rot);
	}
	glFinish_ ();
	double ms = (vt_wall () - t0) * 1e3 / frames;
	gpu_novideo = 0; fx_force = NULL;
	return fx_half ? ms / 2 : ms;
}
static void opt_file (char *o, size_t n)
{
	const char *sv = NULL; env_cb (RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY, &sv);
	if (sv && *sv) snprintf (o, n, "%s" SEP "vectrex_emu_optimized.txt", sv); else o[0] = 0;
}
static void opt_say (const char *m, unsigned frames)
{
	(void) frames; fprintf (stderr, "[vectrex_emu] %s\n", m);   /* (the log only: no message on screen - only what is needed shows) */
}
static const char *const OPT_OFF_NAMES[14] = { "dither", "bend smoothing", "case reflections", "parallax", "glass in room light",
	"light on the overlay", "light on the glass", "the second glow pass", "plastic glow", "scratches", "glass edge", "tube curve",
	"the phosphor at full size", "every second picture (half the pictures a second)" };
static void opt_feature (prow *w, int k)            /* (pctest FEATURES: what each is when off) */
{
	switch (k) {
	case 0: w->dith = 0; break; case 1: w->sbend = 0; break; case 2: w->crefl = 0; break; case 3: w->par = 0; break;
	case 4: w->groom = 0; break; case 5: w->lov = 0; break; case 6: w->light = 0; break; case 7: w->passes = 1; break;
	case 8: w->aglow = 0; break; case 9: w->scr = 0; break; case 10: w->glass = 0; break; case 11: w->curve = 0; break;
	case 12: w->ps = 0.5f; break; default: w->half = 1; break; }
}
/* The cases (2026-11-24, the owner: "I don't think we should be testing benchmarks while the user is gaming"): one per
 * device and screen rate - not per picture size or overlay. Each measured with the overlay on (the heavier case: a
 * stand-in sheet when the game has none) at the largest picture it is for, so it holds with the overlay off, at any
 * smaller size and either way round: turning the overlay on or the device never measures. A rate without a case of its
 * own takes the next faster rate's (it holds at the slower one too). Measured only in the first refreshes after a
 * game's load - or by the own apps once at their first start, every rate, at the screen's largest picture
 * (vx_app_tune) - never in the middle of a game: then the nearest case meanwhile, measured at the next game's start.
 * The rule: the costliest effects off first; drawing every second picture only the last resort (it was the first:
 * it always saves most - the Nova at 120 Hz with an overlay drew 60 pictures a second for it). */
#define OPT_VER "b3"                            /* (a new rule or new costs: every case measured anew, once) */
int vx_app_tunes;                               /* (the own apps test the device themselves, asked first (vx_app_tune): the core never measures on its own) */
static long opt_px_seen; static int opt_quiet, opt_pending;   /* (opt_quiet: refreshes left in which measuring may run) */
static const char *opt_gpu (void)
{
	if (!glGetString_) glGetString_ = (pf_glGetString) hwr.get_proc_address ("glGetString");
	const char *gr = glGetString_ ? (const char *) glGetString_ (0x1F01) : NULL; return gr ? gr : "";
}
static void opt_keyof (char *o, size_t n, int rate)
{
	snprintf (o, n, OPT_VER " %s %d Hz", opt_gpu (), rate);
	for (char *q = o; *q; q++) if (*q == '|' || *q == '\n' || *q == '\r') *q = ' ';
}
/* the file's case for this device at this rate (or the next faster one measured) for a picture of px pixels: 1 found */
static int opt_lookup (int rate, long px, prow *out, int *rate_got, long *px_got)
{
	char fn[1900]; opt_file (fn, sizeof fn); FILE *f = fn[0] ? fopen (fn, "rb") : NULL; if (!f) return 0;
	char pre[400]; snprintf (pre, sizeof pre, OPT_VER " %s ", opt_gpu ()); for (char *q = pre; *q; q++) if (*q == '|' || *q == '\n' || *q == '\r') *q = ' ';
	const size_t pl = strlen (pre); char line[1024]; int best = 0, bestr = 0; long bestpx = 0; prow bv;
	while (fgets (line, sizeof line, f)) {
		if (strncmp (line, pre, pl)) continue;
		int r = 0; long mpx = 0; char *b = strchr (line, '|'); if (!b) continue;
		if (sscanf (line + pl, "%d Hz %ld px", &r, &mpx) != 2 || r < rate || mpx * 10 < px * 9) continue;   /* (slower, or a smaller picture: not this one's) */
		prow v; int x[14];
		if (sscanf (b + 1, "%d %d %d %d %d %d %d %d %d %d %d %d %d %d %f", &x[0], &x[1], &x[2], &x[3], &x[4], &x[5], &x[6], &x[7], &x[8],
		            &x[9], &x[10], &x[11], &x[12], &x[13], &v.ps) != 15) continue;
		v.passes = x[0]; v.curve = x[1]; v.glass = x[2]; v.groom = x[3]; v.light = x[4]; v.lov = x[5]; v.aglow = x[6]; v.scr = x[7];
		v.par = x[8]; v.crefl = x[9]; v.sbend = x[10]; v.dith = x[11]; v.half = x[12]; v.lh = x[13];
		if (!best || r < bestr || (r == bestr && mpx < bestpx)) { best = 1; bestr = r; bestpx = mpx; bv = v; } }   /* (the nearest: its own rate, the smallest picture that holds) */
	fclose (f);
	if (best) { *out = bv; if (rate_got) *rate_got = bestr; if (px_got) *px_got = bestpx; }
	return best;
}
static void opt_store (int rate, long px, const prow *v)
{
	char fn[1900]; opt_file (fn, sizeof fn); if (!fn[0]) return;
	char key[400]; opt_keyof (key, sizeof key, rate); const size_t kl = strlen (key);
	char *all = NULL; size_t al = 0; FILE *r = fopen (fn, "rb");
	if (r) { fseek (r, 0, SEEK_END); long L = ftell (r); fseek (r, 0, SEEK_SET); all = malloc ((size_t) L + 1); al = all ? fread (all, 1, (size_t) L, r) : 0; if (all) all[al] = 0; fclose (r); }
	FILE *w = fopen (fn, "wb");
	if (w) {
		for (char *p = all, *e; p && *p; p = e) { e = strchr (p, '\n'); e = e ? e + 1 : p + strlen (p);
			if (strncmp (p, OPT_VER " ", 3)) continue;           /* (the old rule's cases: gone - measured anew) */
			if (!(strncmp (p, key, kl) == 0 && p[kl] == ' ')) fwrite (p, 1, (size_t) (e - p), w); }   /* (this rate's old case replaced) */
		fprintf (w, "%s %ld px|%d %d %d %d %d %d %d %d %d %d %d %d %d %d %.2f\n", key, px, v->passes, v->curve, v->glass, v->groom, v->light, v->lov, v->aglow,
		         v->scr, v->par, v->crefl, v->sbend, v->dith, v->half, v->lh, v->ps);
		fclose (w); }
	free (all);
}
/* (the apps keep the player in the loop: the step running now, and what the last measurement left off) */
static char opt_status[200], opt_last_off[600]; static double opt_last_share; static int opt_last_fit;
/* this rate measured now, at this picture, with an overlay on -> its switches */
static prow opt_measure (void)
{
	opt_busy = 1;
	snprintf (opt_status, sizeof opt_status, "%d Hz: every effect on", opt_rate);
	opt_say ("Vectrex_EMU: measuring this device for the best quality (once)...", 120);
	/* (the overlay on: the game's own, else a stand-in sheet - a filter's colour, see-through, the size of a real one) */
	unsigned char *ov0 = ov; const int ovw0 = ovw, ovh0 = ovh, on0 = ov_on; unsigned char *stand = NULL;
	if (!ov) { ovw = 552; ovh = 707; stand = malloc ((size_t) ovw * ovh * 4);
		if (stand) for (size_t i = 0; i < (size_t) ovw * ovh; i++) { stand[i * 4] = 60; stand[i * 4 + 1] = 140; stand[i * 4 + 2] = 230; stand[i * 4 + 3] = (i / ovw) % 160 < 120 ? 140 : 255; }
		ov = stand; }
	if (ov) { ov_on = 1; ov_made = 0; ov_make (); ov_tk[0] = -1; ov_tabs (); }
	static wline sc[NW]; const int n = opt_scene (sc, NW / 3);
	const double frame = 1000.0 / opt_rate, budget = OPT_FIT * frame;
	prow v = FX_PROFILES[0];
	opt_warm = 1;
	const double before = opt_time (&v, sc, n, 24); double now = before;
	char off[600] = "";
	if (now > budget) {
		struct { double gain; int k; } g[14]; int ng = 0;
		for (int k = 0; k < 14; k++) { prow w = v; opt_feature (&w, k); if (!memcmp (&w, &v, sizeof w)) continue;
			snprintf (opt_status, sizeof opt_status, "%d Hz: too slow with every effect - each effect's cost (%d of 14)", opt_rate, k + 1);
			const double t = opt_time (&w, sc, n, 12), gain = before - t;
			if (gain > 0.02 * frame) { g[ng].gain = gain; g[ng].k = k; ng++; } }
		for (int i = 0; i < ng; i++) for (int j = i + 1; j < ng; j++) {   /* (the costliest first - every second picture last, whatever it saves) */
			const int hi = g[i].k == 13, hj = g[j].k == 13;
			if (hi > hj || (hi == hj && g[j].gain > g[i].gain)) { double a = g[i].gain; int b = g[i].k; g[i] = g[j]; g[j].gain = a; g[j].k = b; } }
		for (int i = 0; i < ng && now > budget; i++) { prow w = v; opt_feature (&w, g[i].k);
			snprintf (opt_status, sizeof opt_status, "%d Hz: trying without %s", opt_rate, OPT_OFF_NAMES[g[i].k]);
			const double t = opt_time (&w, sc, n, 16);
			if (now - t < 0.02 * frame) continue;              /* (no real saving once the others are off: stays on) */
			v = w; now = t; snprintf (off + strlen (off), sizeof off - strlen (off), "%s%s", off[0] ? ", " : "", OPT_OFF_NAMES[g[i].k]); }
	}
	/* (the game's own overlay back, as it was) */
	ov = ov0; ovw = ovw0; ovh = ovh0; ov_on = on0; ov_made = 0; ov_tk[0] = -1; free (stand);
	ph_made = 0;                                   /* (the phosphor dark again: the scene's light gone) */
	opt_busy = 0;
	snprintf (opt_last_off, sizeof opt_last_off, "%s", off); opt_last_share = now / frame; opt_last_fit = now <= budget;
	char m[900]; snprintf (m, sizeof m, "Vectrex_EMU: %s (%.1f ms a picture, %.0f %% of a refresh)%s%s", off[0] ? "best this device runs at full speed" : "every effect on",
		now, 100 * now / frame, off[0] ? " - off: " : "", off);
	opt_say (m, 300);
	fprintf (stderr, "[vectrex_emu] Optimize: %s %d Hz, %d x %d, overlay on - every effect %.2f ms, now %.2f ms\n", opt_gpu (), opt_rate, FW, FH, before, now);
	return v;
}
/* (each refresh, before its picture: this case's switches - from the file, else measured if the game just started) */
static void opt_check (void)
{
	if (opt_quiet > 0) opt_quiet--;
	if (opt_profile || !gpu_ok || !ph_ok || stereo || opt_busy || vx_nobuild) { if (opt_profile) opt_row_ok = 0; return; }
	const long px = (long) FW * FH; char key[400]; opt_keyof (key, sizeof key, opt_rate);
	if (!strcmp (key, opt_key_seen) && px <= opt_px_seen && !(opt_pending && opt_quiet)) return;
	snprintf (opt_key_seen, sizeof opt_key_seen, "%s", key); snprintf (opt_key, sizeof opt_key, "%s", key); opt_px_seen = px;
	prow v; int rg = 0; long pg = 0;
	if (opt_lookup (opt_rate, px, &v, &rg, &pg)) { opt_row = v; opt_row_ok = 1; opt_pending = 0;
		fprintf (stderr, "[vectrex_emu] Optimize: %s - as measured before (%d Hz, %ld px)\n", key, rg, pg); fx_apply (); return; }
	if (!opt_quiet || vx_app_tunes) {              /* (in the middle of a game, or the apps: not now - the nearest case; measured at the next start / the app's test) */
		if (opt_lookup (opt_rate, 0, &v, &rg, &pg) || opt_lookup (0, 0, &v, &rg, &pg)) { opt_row = v; opt_row_ok = 1; }
		else { opt_row = FX_PROFILES[1]; opt_row_ok = 1; }   /* (none at all: High meanwhile) */
		if (!opt_pending) fprintf (stderr, "[vectrex_emu] Optimize: %s at %ld px not measured - measured at the next game's start\n", key, px);
		opt_pending = 1; fx_apply (); return; }
	v = opt_measure (); opt_store (opt_rate, px, &v); opt_row = v; opt_row_ok = 1; opt_pending = 0; fx_apply ();
}
