/* gpu_opt.c - Vectrex_EMU's libretro core: Optimize (the desktop's pctest.py), included by libretro_vx.c after fx_apply.
 * With the Quality profile on auto and the GPU drawing, once per device, picture size, screen rate and overlay (on or
 * off): a heavy scene drawn the way a busy game draws it (pctest.scene: circles of joined pieces with rests, raster
 * text, long lines, resting dots - ~2900 pieces, like Vectorblade's busiest frames) with every effect on (Ultra); if a
 * picture then needs more than 75 % of a refresh, each effect's cost is measured on its own and the costliest turned
 * off one by one until it fits - pctest.optimize's order and rules. A picture's time: many pictures drawn back to back
 * and waited for once (one wait per picture measured the GPU waking up, not its work). The result is kept in the saves
 * folder (vectrex_emu_optimized.txt), one line per case; the game pauses about two seconds the first time.
 * Copyright (c) 2026 Hugo Bokhorst. Part of Vectrex_EMU; see LICENSE.md. */

#define OPT_FIT 0.75                            /* (pctest FIT: busy under this) */
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
static double opt_time (const prow *v, wline *sc, int n, int frames)
{
	extern double vt_wall (void);
	fx_force = v; fx_apply (); bt_gpu = vx_beam && bt_gpu_ok ();   /* (the first picture: measured as it will be drawn) */
	const double dt = 1.0 / opt_rate; double t0 = 0;
	gpu_novideo = 1;
	double tw = vt_wall ();
	for (int f = -3; f < frames; f++) {
		if (f == -1 && vt_wall () - tw < 0.3) f = -2;   /* (warming up at least 0.3 s: the GPU's clock up first) */
		if (f == 0) { glFinish_ (); t0 = vt_wall (); }
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
	"the phosphor at full size", "60 pictures a second" };
static void opt_feature (prow *w, int k)            /* (pctest FEATURES: what each is when off) */
{
	switch (k) {
	case 0: w->dith = 0; break; case 1: w->sbend = 0; break; case 2: w->crefl = 0; break; case 3: w->par = 0; break;
	case 4: w->groom = 0; break; case 5: w->lov = 0; break; case 6: w->light = 0; break; case 7: w->passes = 1; break;
	case 8: w->aglow = 0; break; case 9: w->scr = 0; break; case 10: w->glass = 0; break; case 11: w->curve = 0; break;
	case 12: w->ps = 0.5f; break; default: w->half = 1; break; }
}
/* (each refresh, before its picture: measured once per case - its result from the file, else now) */
static void opt_check (void)
{
	if (opt_profile || !gpu_ok || !ph_ok || stereo || opt_busy || vx_nobuild) { if (opt_profile) opt_row_ok = 0; return; }
	if (!glGetString_) glGetString_ = (pf_glGetString) hwr.get_proc_address ("glGetString");
	const char *gr = glGetString_ ? (const char *) glGetString_ (0x1F01) : "";
	snprintf (opt_key, sizeof opt_key, "b2 %s %dx%d %d Hz%s", gr ? gr : "", FW, FH, opt_rate, ov && ov_on && !sheet_file ? " overlay" : "");   /* (b2: the beam on the phosphor, 2026-11-22 - its costs are other ones: measured anew) */
	for (char *q = opt_key; *q; q++) if (*q == '|' || *q == '\n' || *q == '\r') *q = ' ';
	if (!strcmp (opt_key, opt_key_seen)) return;
	snprintf (opt_key_seen, sizeof opt_key_seen, "%s", opt_key);
	/* the file: this case's line */
	char fn[1900]; opt_file (fn, sizeof fn);
	FILE *f = fn[0] ? fopen (fn, "rb") : NULL; char line[1024];
	if (f) { while (fgets (line, sizeof line, f)) { char *b = strchr (line, '|'); if (!b) continue; *b = 0;
			if (strcmp (line, opt_key)) continue;
			prow v; int x[14];
			if (sscanf (b + 1, "%d %d %d %d %d %d %d %d %d %d %d %d %d %d %f", &x[0], &x[1], &x[2], &x[3], &x[4], &x[5], &x[6], &x[7], &x[8],
			            &x[9], &x[10], &x[11], &x[12], &x[13], &v.ps) == 15) {
				v.passes = x[0]; v.curve = x[1]; v.glass = x[2]; v.groom = x[3]; v.light = x[4]; v.lov = x[5]; v.aglow = x[6]; v.scr = x[7];
				v.par = x[8]; v.crefl = x[9]; v.sbend = x[10]; v.dith = x[11]; v.half = x[12]; v.lh = x[13];
				opt_row = v; opt_row_ok = 1; fclose (f); fprintf (stderr, "[vectrex_emu] Optimize: %s - as measured before\n", opt_key); return; } }
		fclose (f); }
	/* measured now (pctest.optimize) */
	opt_busy = 1;
	opt_say ("Vectrex_EMU: measuring this device for the best quality (once)...", 120);
	static wline sc[NW]; const int n = opt_scene (sc, NW / 3);
	const double frame = 1000.0 / opt_rate, budget = OPT_FIT * frame;
	prow v = FX_PROFILES[0];
	const double before = opt_time (&v, sc, n, 24); double now = before;
	char off[600] = "";
	if (now > budget) {
		struct { double gain; int k; } g[14]; int ng = 0;
		for (int k = 0; k < 14; k++) { prow w = v; opt_feature (&w, k); if (!memcmp (&w, &v, sizeof w)) continue;
			const double t = opt_time (&w, sc, n, 12), gain = before - t;
			if (gain > 0.02 * frame) { g[ng].gain = gain; g[ng].k = k; ng++; } }
		for (int i = 0; i < ng; i++) for (int j = i + 1; j < ng; j++) if (g[j].gain > g[i].gain) { double a = g[i].gain; int b = g[i].k; g[i] = g[j]; g[j].gain = a; g[j].k = b; }
		for (int i = 0; i < ng && now > budget; i++) { prow w = v; opt_feature (&w, g[i].k);
			const double t = opt_time (&w, sc, n, 16);
			if (now - t < 0.02 * frame) continue;              /* (no real saving once the others are off: stays on) */
			v = w; now = t; snprintf (off + strlen (off), sizeof off - strlen (off), "%s%s", off[0] ? ", " : "", OPT_OFF_NAMES[g[i].k]); }
	}
	opt_row = v; opt_row_ok = 1; ph_made = 0;               /* (the phosphor dark again: the scene's light gone) */
	opt_busy = 0; fx_apply ();
	char m[900]; snprintf (m, sizeof m, "Vectrex_EMU: %s (%.1f ms a picture, %.0f %% of a refresh)%s%s", off[0] ? "best this device runs at full speed" : "every effect on",
		now, 100 * now / frame, off[0] ? " - off: " : "", off);
	opt_say (m, 300);
	fprintf (stderr, "[vectrex_emu] Optimize: %s - every effect %.2f ms, now %.2f ms\n", opt_key, before, now);
	if (fn[0]) {                                           /* (the file: this case's line replaced) */
		char *all = NULL; size_t al = 0; FILE *r = fopen (fn, "rb");
		if (r) { fseek (r, 0, SEEK_END); long L = ftell (r); fseek (r, 0, SEEK_SET); all = malloc ((size_t) L + 1); al = all ? fread (all, 1, (size_t) L, r) : 0; if (all) all[al] = 0; fclose (r); }
		FILE *w = fopen (fn, "wb");
		if (w) {
			for (char *p = all, *e; p && *p; p = e) { e = strchr (p, '\n'); e = e ? e + 1 : p + strlen (p);
				size_t kl = strlen (opt_key); if (!(strncmp (p, opt_key, kl) == 0 && p[kl] == '|')) fwrite (p, 1, (size_t) (e - p), w); }
			fprintf (w, "%s|%d %d %d %d %d %d %d %d %d %d %d %d %d %d %.2f\n", opt_key, v.passes, v.curve, v.glass, v.groom, v.light, v.lov, v.aglow,
			         v.scr, v.par, v.crefl, v.sbend, v.dith, v.half, v.lh, v.ps);
			fclose (w); }
		free (all); }
}
