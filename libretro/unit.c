/* unit.c - Vectrex_EMU's libretro core: the Vectrex unit (the desktop's Config > Unit and Circuit: vectrex_emu.py
 * UNIT_PARAMS, UNIT_PICTURE, UNIT_PRESETS, UNIT_SPREAD, unit_picture, unit_warp, knob_bias; vecrender.py unit_warp).
 * The analog parts go to the core (vt_set_analog, and as VT_ variables at a game's start); the picture's pots and the
 * yoke act on the lines after the integrators (picture_apply), the picture's shape before the tube's own (unit_wp),
 * the high voltage on the light and the spot, the brightness knob on the beam's drive. Included by libretro_vx.c before
 * the tube's map. Random and Cranky: unit number 1..10 - the same unit every time (the desktop: a new one each press).
 * Copyright (c) 2026 Hugo Bokhorst. Part of Vectrex_EMU; see LICENSE.md. */

#define KNOB_LO (-0.80)
#define KNOB_HI 0.32
#define HV_FACTORY 5.8
typedef struct {
	const char *key; int id; const char *env;          /* (vt_set_analog's id, -1: the frontend's; the variable at the start) */
	const char *label, *info; double lo, hi, step; const char *fmt;
	double owner, ideal; char tab;                     /* (u: Unit, c: Circuit) */
	char rk; double ra, rb; char ck; double ca, cb;     /* (UNIT_SPREAD: random, cranky - 'r' a range, 's' either way of the owner's) */
} unit_par;
static const unit_par UNIT_P[] = {
	/* Unit: the knob, the picture's pots and the yoke (UNIT_TAB_KEYS), then its shape (UNIT_SHAPE_KEYS) */
	{ "knob", 8, "VT_BRIGHT", "R509 Brightness", "The knob at the back.", 0.0, 1.0, 0.05, "%.2f", 0.8 / 1.12, 0.8 / 1.12, 'u', 0, 0, 0, 0, 0, 0 },
	{ "sizex", -1, NULL, "R401 X size", "The X deflection amplifier's input.", 60, 140, 2, "%.0f %%", 100, 100, 'u', 's', 0, 2.5, 's', 2.5, 6 },
	{ "sizey", -1, NULL, "R408 Y size", "The Y deflection amplifier's input.", 60, 140, 2, "%.0f %%", 100, 100, 'u', 's', 0, 2.5, 's', 2.5, 6 },
	{ "hv", -1, NULL, "R525 High voltage", NULL, 4.6, 7.0, 0.1, "%.2f kV", HV_FACTORY, HV_FACTORY, 'u', 's', 0, 0.12, 's', 0.12, 0.35 },
	{ "cenx", -1, NULL, "Centering X", "The yoke's magnets.", -10, 10, 0.5, "%+.1f %%", 0, 0, 'u', 's', 0, 1, 's', 1, 2.5 },
	{ "ceny", -1, NULL, "Centering Y", "The yoke's magnets.", -10, 10, 0.5, "%+.1f %%", 0, 0, 'u', 's', 0, 1, 's', 1, 2.5 },
	{ "rot", -1, NULL, "Yoke rotation", NULL, -5, 5, 0.25, "%+.2f deg", 0, 0, 'u', 's', 0, 0.5, 's', 0.5, 1.3 },
	{ "skew", -1, NULL, "Yoke skew", "The coils not quite at right angles.", -3, 3, 0.25, "%+.2f deg", 0, 0, 'u', 's', 0, 0.4, 's', 0.4, 1.2 },
	{ "keyx", -1, NULL, "Keystone X", "Bottom wider than the top: +.", -5, 5, 0.5, "%+.1f %%", 0, 0, 'u', 's', 0, 0.6, 's', 0.6, 2.5 },
	{ "keyy", -1, NULL, "Keystone Y", "Right taller than the left: +.", -5, 5, 0.5, "%+.1f %%", 0, 0, 'u', 's', 0, 0.6, 's', 0.6, 2.5 },
	{ "pinx", -1, NULL, "Pincushion X", "The sides bowed in: +, out: -.", -3, 3, 0.25, "%+.2f %%", 0, 0, 'u', 's', 0, 0.4, 's', 0.4, 1.2 },
	{ "piny", -1, NULL, "Pincushion Y", "Top and bottom bowed in: +, out: -.", -3, 3, 0.25, "%+.2f %%", 0, 0, 'u', 's', 0, 0.4, 's', 0.4, 1.2 },
	{ "linx", -1, NULL, "Linearity X", "The right side stretched: +.", -6, 6, 0.5, "%+.1f %%", 0, 0, 'u', 's', 0, 1, 's', 1, 3 },
	{ "liny", -1, NULL, "Linearity Y", "The bottom stretched: +.", -6, 6, 0.5, "%+.1f %%", 0, 0, 'u', 's', 0, 1, 's', 1, 3 },
	/* Circuit: the service trim pots, then its parts (the desktop's order) */
	{ "dacofs", 23, "VT_DACOFS", "R302 DAC offset", NULL, -74, 128, 2, "%+.1f steps", 0, 0, 'c', 0, 0, 0, 0, 0, 0 },
	{ "drift_x", 3, NULL, "R335 X rate offset", "The X integrator's drift.", -0.585, 0.585, 0.045, "%+.3f", 0, 0, 'c', 's', 0, 0.012, 's', 0.015, 0.04 },
	{ "drift_y", 4, NULL, "R333 Y rate offset", "The Y integrator's drift.", -0.585, 0.585, 0.045, "%+.3f", 0, 0, 'c', 's', 0, 0.012, 's', 0.015, 0.04 },
	{ "ring", -1, NULL, "R526 HV ringing", "0: minimum ringing.", -1, 1, 0.1, "%+.2f", 0, 0, 'c', 's', 0, 0.15, 's', 0.15, 0.45 },
	{ "zhold", 1, "VT_ZREFK", "Zero hold", "How well the zero reference charges.", 0.10, 1.00, 0.02, "%.2f", 0.38, 1.0, 'c', 'r', 0.30, 0.60, 'r', 0.20, 0.32 },
	{ "zcal", 2, "VT_CALSHIFT", "Zero calibration shift", NULL, 0, 6, 0.1, "%.1f", 2.9, 0, 'c', 'r', 2.0, 3.6, 'r', 3.4, 4.6 },
	{ "zofs", 7, "VT_ZOFS", "Zero offset", "The reference against the integrators.", -2, 2, 0.05, "%+.2f", 0, 0, 'c', 's', 0, 0.04, 's', 0.05, 0.10 },
	{ "ysettle", 5, "VT_YSHK", "Y hold settling", "1 = instant.", 0.20, 1.00, 0.02, "%.2f", 0.70, 1.0, 'c', 'r', 0.60, 0.90, 'r', 0.45, 0.62 },
	{ "beamoff", 6, "VT_BEAMOFF", "Beam switch-off delay", "How long the beam stays lit after a game blanks it: longer, and lines get tails.", 0, 3, 0.1, "%.1f cycles", 0.0, 0, 'c', 'r', 0.0, 0.4, 'r', 0.4, 1.0 },   /* (2026-11-23, as the desktop: its fraction acts in the beam engine again - the owner's 0 (his 0.5 acted as 0); the spreads round it) */
	{ "zerotx", 12, NULL, "Zeroing time X", "ZERO is an RC.", 0, 30, 0.5, "%.1f cycles", 4.5, 3.3, 'c', 'r', 3.8, 5.2, 'r', 5.2, 6.2 },
	{ "zeroty", 13, NULL, "Zeroing time Y", "ZERO is an RC.", 0, 30, 0.5, "%.1f cycles", 3.3, 3.3, 'c', 'r', 2.9, 3.8, 'r', 3.8, 4.6 },
	{ "ofsk", 14, "VT_OFSK", "Offset shift", "The zero reference.", 0, 300, 5, "%.0f", 166, 166, 'c', 'r', 158, 174, 'r', 150, 182 },
	{ "lagx", 9, "VT_DEFLAG", "Deflection lag X", NULL, 0, 20, 0.5, "%.1f cycles", 13.3, 13.3, 'c', 'r', 12.5, 13.8, 'r', 13.8, 15.0 },
	{ "lagy", 15, "VT_DEFLAGY", "Deflection lag Y", NULL, 0, 20, 0.5, "%.1f cycles", 13.3, 13.3, 'c', 'r', 12.5, 13.8, 'r', 13.8, 15.0 },
	{ "dac0", 16, NULL, "DAC bit 0 error", NULL, -0.03, 0.03, 0.001, "%+.4f", 0.0170, 0, 'c', 's', 0, 0.012, 's', 0.012, 0.025 },
	{ "dac1", 17, NULL, "DAC bit 1 error", NULL, -0.03, 0.03, 0.001, "%+.4f", 0.0069, 0, 'c', 's', 0, 0.008, 's', 0.008, 0.015 },
	{ "dac2", 18, NULL, "DAC bit 2 error", NULL, -0.03, 0.03, 0.001, "%+.4f", -0.0089, 0, 'c', 's', 0, 0.008, 's', 0.008, 0.012 },
	{ "dac3", 19, NULL, "DAC bit 3 error", NULL, -0.03, 0.03, 0.001, "%+.4f", -0.0014, 0, 'c', 's', 0, 0.004, 's', 0.004, 0.006 },
	{ "gap", 0, NULL, "Line gaps (stop delay)", "The integrators' stop delay.", 4, 10, 0.1, "%.1f cycles", 7, 7, 'c', 'r', 6.3, 7.7, 'r', 7.7, 9.3 },
	/* (the integrators' rate one way against the other, %: vecx.c rate_by - the owner's Y measured, 2026-11-23; random / cranky about 0.
	 *  Last: a random or cranky unit's other parts as before) */
	{ "xrate", 29, "VT_XRATE", "X rate right vs left", "Right faster than left: +.", -3, 3, 0.1, "%+.1f %%", 0, 0, 'c', 's', 0, 1.2, 's', 1.2, 2.5 },
	{ "yrate", 28, "VT_YRATE", "Y rate up vs down", "Up faster than down: +.", -3, 3, 0.1, "%+.1f %%", 2.1, 0, 'c', 's', 0, 1.2, 's', 1.2, 2.5 },   /* (2.1: with vecx.c RATE_LEAK 5800 - the owner's tube, the plain Full Grid's lower right, 2026-11-25) */
	{ NULL } };
#define NUNIT (sizeof UNIT_P / sizeof UNIT_P[0] - 1)
static double unit_v[NUNIT], unit_sent[NUNIT]; static int unit_known;
/* (what the lines see: unit_picture, unit_warp, knob_bias) */
static double up_sx = 1, up_sy = 1, up_a, up_tx, up_ty, up_sk, up_light = 1, up_width = 1, up_kb; static int up_xf;
static double uw[6]; static int uw_on;

static int unit_idx (const char *k) { for (int i = 0; i < (int) NUNIT; i++) if (!strcmp (UNIT_P[i].key, k)) return i; return -1; }
static double knob_bias (double v) { double b = KNOB_LO + (KNOB_HI - KNOB_LO) * (v < 0 ? 0 : v > 1 ? 1 : v); return fabs (b) < 1e-3 ? 0.0 : b; }
/* (an option's values: auto, then lo..hi by step, as the desktop's slider shows them) */
static size_t unit_vals (const unit_par *p, char *o, size_t n)
{
	size_t u = (size_t) snprintf (o, n, "auto");
	const int m = (int) floor ((p->hi - p->lo) / p->step + 0.5);
	for (int i = 0; i <= m && i < 120 && u < n; i++) { double v = p->lo + i * p->step; if (fabs (v) < 1e-9) v = 0;
		u += (size_t) snprintf (o + u, n - u, "|"); u += (size_t) snprintf (o + u, n - u, p->fmt, v); }
	return u;
}
/* the unit: the preset (owner, ideal, random N, cranky N), the options set by hand on top */
static void unit_make (const char *preset, const char *(*get) (const char *))
{
	int rnd = 0, crank = 0, num = 1;
	if (!strncmp (preset, "random", 6)) { rnd = 1; num = atoi (preset + 6); } else if (!strncmp (preset, "cranky", 6)) { crank = 1; num = atoi (preset + 6); }
	unsigned s = 2166136261u ^ (unsigned) (num * 7919 + crank * 104729);
	#define URND() ((s = s * 1664525u + 1013904223u) >> 8) / 16777216.0
	for (int i = 0; i < (int) NUNIT; i++) {
		const unit_par *p = &UNIT_P[i]; double v = !strcmp (preset, "ideal") ? p->ideal : p->owner;
		if ((rnd || crank) && (crank ? p->ck : p->rk)) {      /* (vectrex_emu.py random_unit) */
			const char k = crank ? p->ck : p->rk; const double a = crank ? p->ca : p->ra, b = crank ? p->cb : p->rb;
			if (k == 'r') v = a + (b - a) * URND ();
			else { const double c = !strncmp (p->key, "dac", 3) || strstr (p->key, "rate") ? 0.0 : p->owner; v = c + (URND () < 0.5 ? -1 : 1) * (a + (b - a) * URND ()); }
			v = v < p->lo ? p->lo : v > p->hi ? p->hi : v; }
		char key[64]; snprintf (key, sizeof key, "vectrex_emu_unit_%s", p->key);
		const char *o = get (key); if (o && o[0] && strcmp (o, "auto")) v = atof (o);
		unit_v[i] = v; }
	#undef URND
	{ const int lx = unit_idx ("lagx"), ly = unit_idx ("lagy");   /* (2026-11-22, the measured timing: lag 13.3 in three stages - a saved 6 / 6, the default
	                                                 before, is moved over as the desktop does) */
	  if (unit_v[lx] == 6.0 && unit_v[ly] == 6.0) unit_v[lx] = unit_v[ly] = 13.3; }
	/* unit_picture */
	double hv = unit_v[unit_idx ("hv")] * (1.0 - 0.08 * unit_v[unit_idx ("ring")] * unit_v[unit_idx ("ring")]);
	const double stiff = sqrt (HV_FACTORY / (hv > 1 ? hv : 1));
	up_sx = unit_v[unit_idx ("sizex")] / 100.0 * stiff; up_sy = unit_v[unit_idx ("sizey")] / 100.0 * stiff;
	up_a = unit_v[unit_idx ("rot")] * M_PI / 180; up_tx = unit_v[unit_idx ("cenx")] / 100.0 * ALG_MAX_Y; up_ty = -unit_v[unit_idx ("ceny")] / 100.0 * ALG_MAX_Y;
	up_sk = tan (unit_v[unit_idx ("skew")] * M_PI / 180); up_light = (hv - 0.8) / (HV_FACTORY - 0.8); up_width = stiff;
	up_xf = !(fabs (up_sx - 1) < 1e-9 && fabs (up_sy - 1) < 1e-9 && up_a == 0 && up_tx == 0 && up_ty == 0 && up_sk == 0);
	static const char *const W[6] = { "keyx", "keyy", "pinx", "piny", "linx", "liny" }; uw_on = 0;
	for (int k = 0; k < 6; k++) { uw[k] = unit_v[unit_idx (W[k])] / 100.0; uw_on |= uw[k] != 0; }
	up_kb = knob_bias (unit_v[unit_idx ("knob")]);
	unit_known = 1;
}
/* (the core: the analog parts at a game's start - add-ons keep the standard zero hold) */
static void unit_env (int addon)
{
	static char e[NUNIT + 3][96]; int n = 0;
	(void) addon; for (int i = 0; i < (int) NUNIT; i++) { const unit_par *p = &UNIT_P[i]; if (!p->env) continue;   /* (the UVMC2 / PiTrex too: the console's zero
	                                                 hold, as the desktop since 2026-11-13 - the zero test cartridge drew sharp at 0-8 on the owner's Vectrex) */
		snprintf (e[n], sizeof e[n], "%s=%.4f", p->env, p->id == 8 ? knob_bias (unit_v[i]) : unit_v[i]); vx_putenv (e[n]); n++; }
	snprintf (e[n], sizeof e[n], "VT_HWOFS=%.4f,%.4f", unit_v[unit_idx ("drift_x")], unit_v[unit_idx ("drift_y")]); vx_putenv (e[n]); n++;
	snprintf (e[n], sizeof e[n], "VT_ZEROTAU=%.3f,%.3f", unit_v[unit_idx ("zerotx")], unit_v[unit_idx ("zeroty")]); vx_putenv (e[n]); n++;
	snprintf (e[n], sizeof e[n], "VT_DACBITS=%.5f,%.5f,%.5f,%.5f", unit_v[unit_idx ("dac0")], unit_v[unit_idx ("dac1")], unit_v[unit_idx ("dac2")], unit_v[unit_idx ("dac3")]);
	vx_putenv (e[n]); n++;
	for (int i = 0; i < (int) NUNIT; i++) unit_sent[i] = unit_v[i];
}
/* (a game's start: every analog part to the core at once - the core reads its VT_ variables only once, at the first game: a
 *  UVMC2 game first (the standard zero hold, an add-on's) left every 6809 game after it at that, and Last Human's calibration
 *  screen drew slanted boxes and an open octagon at its defaults (the iPhone 2026-11-20). Every game the unit's, the UVMC2 /
 *  PiTrex too (the desktop's rule since 2026-11-13) */
static void unit_force (int addon)
{
	extern void vt_set_analog (int id, double v);
	for (int i = 0; i < (int) NUNIT; i++) { const unit_par *p = &UNIT_P[i]; if (p->id < 0) continue;
		vt_set_analog (p->id, p->id == 8 ? knob_bias (unit_v[i]) : unit_v[i]); unit_sent[i] = unit_v[i]; }
}
/* (while running: the parts changed since sent, to the core) */
static void unit_live (int addon)
{
	extern void vt_set_analog (int id, double v);
	for (int i = 0; i < (int) NUNIT; i++) { const unit_par *p = &UNIT_P[i];
		(void) addon; if (p->id < 0 || unit_v[i] == unit_sent[i]) continue;
		vt_set_analog (p->id, p->id == 8 ? knob_bias (unit_v[i]) : unit_v[i]); unit_sent[i] = unit_v[i]; }
}
static void picture_apply (double *x, double *y)       /* (vectrex_emu.py picture_apply: emulator units) */
{
	const double cx = ALG_MAX_X / 2.0, cy = ALG_MAX_Y / 2.0; double u = (*x - cx) * up_sx, v = (*y - cy) * up_sy;
	u -= up_sk * v; const double c = cos (up_a), s = sin (up_a);
	*x = cx + c * u - s * v + up_tx; *y = cy + s * u + c * v + up_ty;
}
static void picture_undo (double *x, double *y)
{
	const double cx = ALG_MAX_X / 2.0, cy = ALG_MAX_Y / 2.0; double u = *x - cx - up_tx, v = *y - cy - up_ty;
	const double c = cos (up_a), s = sin (up_a), uu = c * u + s * v, vv = -s * u + c * v;
	*x = cx + (uu + up_sk * vv) / up_sx; *y = cy + vv / up_sy;
}
static void unit_wp (double *u, double *v)              /* (vecrender.py unit_warp: half heights round the centre, v down) */
{
	const double UW = (double) ALG_MAX_X / ALG_MAX_Y, a = *u / UW, b = *v;
	const double U = *u * (1.0 + 0.5 * uw[0] * b) * (1.0 + uw[2] * b * b) + 0.5 * uw[4] * UW * a * a;
	const double V = *v * (1.0 + 0.5 * uw[1] * a) * (1.0 + uw[3] * a * a) + 0.5 * uw[5] * b * b;
	*u = U; *v = V;
}
