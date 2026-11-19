/* deck.c - Vectrex_EMU's libretro core: the control deck below the picture (the desktop's strip: vectrex_emu.py
 * knob_geom, cart_geom, port2_state, port1_state, osd_state, strip_dx, slot_state, knob_click / knob_motion /
 * knob_release, port2_next; vecrender.py _knob, _cart, _port2, _osd, _meters) - from left to right the OSD, PAUSED
 * when paused, the P2 port, the cartridge, the P1 port, RESET and the volume knob, in the desktop's sizes (its units:
 * the picture's height). Display > Control deck; Display > Screen shape: the picture and the deck one block, black
 * bars to 4:3 or 16:9 (turned: 3:4 / 9:16). RetroArch's pointer (the mouse, or touch) works it as the desktop's mouse:
 * the P2 port only shows its device (the core options choose it), the cartridge the ROM browser, RESET (its upper half), the knob turned round (down to 0 switches off;
 * a click: +-5 %, switched off: on); the OSD opens nothing here. Drawn on the CPU when it changes, laid under the
 * picture on the GPU (gpu_present) or in the CPU's frame. Included by libretro_vx.c after browser.c.
 * Copyright (c) 2026 Hugo Bokhorst. Part of Vectrex_EMU; see LICENSE.md. */

#define DK_G 0x66aa88u
static size_t dk_n;   /* (dk_img, dk_w, dk_h, dk_gen: gpu_gl.c) */
static double dk_r, dk_cy, dk_kx, dk_rx, dk_cx, dk_p2x, dk_p1x, dk_ox, dk_ok_, dk_M;   /* (the deck's places: its own pixels) */
static int dk_p2 = 0, sp_ok;                     /* (port 2: 0 joystick, 1 VecVox, 2 VecVoice, 3 3D Imager, 4 light pen) */
static double dk_reset_until, dk_t_osd; static int dk_hover_label;
static char dk_osd[3][24]; static int dk_osd_col; static int dk_cpu = -1, dk_gpu = -1;
static struct { int on; double a, x, y, acc, vol0; int moved, up; } dk_drag;
static float vol_before_off = 1.0f;

/* (the layout, at the picture's size: vectrex_emu.py knob_geom & co in the sheet's pixels, the strip centred) */
static int dk_cols = 20, dk_thin; static double dk_ow;   /* (the OSD: its columns, thin meters, its width) */
static double dk_sl, dk_sr;                          /* (the strip's left and right ends: the modern deck's panels within them) */
static int dk_wmax (int cols) { int w = cols; static const int NL_[3] = { 9, 8, 9 };
	for (int i = 0; i < 3; i++) { const int L = NL_[i] + 1 + (int) strlen (dk_osd[i]); if (L > w) w = L; } return w; }
static void dk_place (void)
{
	const double ph = FH / 0.924, r = 0.0253 * ph, M = 0.0038 * ph, top = M, bot = DKH;   /* (deck rows: 0 at the sheet's bottom) */
	const double kx = EW - 2.4 * r, cx = EW / 2.0 + 1.8 * r, ks = 0.3 * r / 6.0;
	const double rx = kx - 3.4 * r, p2 = cx - 3.6 * r, gap = (rx - 0.95 * r) - (cx + 1.3 * r);
	/* strip_dx: the strip centred - the OSD always OSD_CHARS (20) wide at the knob's text size */
	const double sl = (p2 - 1.1 * r) - gap - 20 * 6.0 * ks, sr = kx + 2.4 * r, sdx = EW / 2.0 - (sl + sr) / 2.0 - CX0;
	/* osd_state: as high as the knob's labels; a low picture (k under OSD_KMIN 1.5): the desktop's low-window rule */
	const double mrows = 0.25 + 2 * 0.7; double k = fmin (ks, (bot - top) / ((3 + mrows) * 9.0)), ox; int cols = 20, thin = 0;
	if (k >= 1.5) ox = sl;
	else { const double right = (p2 - 1.1 * r) - gap, room = 20 * 6.0 * k, kfit = (bot - top) / ((3 + mrows) * 9.0);
		if (fmin (kfit, room / (17 * 6.0)) >= 1.5) k = 1.5;
		else { thin = 1; k = fmin (1.5, fmin (room / (17 * 6.0), (bot - top) / (3 * 9.0 + 4.0))); }
		cols = (int) (room / (6.0 * k)); cols = cols < 17 ? 17 : cols > 20 ? 20 : cols;
		ox = right - dk_wmax (cols) * 6.0 * k; }
	dk_r = r; dk_M = M; dk_cy = M + 1.8 * r; dk_kx = kx + sdx; dk_rx = rx + sdx; dk_cx = cx + sdx; dk_p2x = p2 + sdx;
	dk_p1x = ((cx + 1.3 * r) + (rx - 0.95 * r)) / 2.0 + sdx; dk_ox = ox + sdx; dk_ok_ = k; dk_cols = cols; dk_thin = thin;
	dk_ow = dk_wmax (cols) * 6.0 * k; dk_sl = fmax (0.0, sl + sdx); dk_sr = fmin ((double) dk_w, sr + sdx);
}
static void dk_circle (double x, double y, double rad, double w, unsigned c)
{ for (int i = 0; i < 72; i++) { const double a0 = i * 5.0 * M_PI / 180, a1 = (i + 1) * 5.0 * M_PI / 180;
	br_line ((float) (x + rad * sin (a0)), (float) (y - rad * cos (a0)), (float) (x + rad * sin (a1)), (float) (y - rad * cos (a1)), (float) w, c); } }
static void dk_icon (const float (*P)[2], const unsigned short (*L)[2], int nl, double ox, double oy, double s, double w, unsigned c)
{ for (int l = 0; l < nl; l++) for (int i = 0; i + 1 < L[l][1]; i++) { const float *a = P[L[l][0] + i], *b = P[L[l][0] + i + 1];
	br_line ((float) (ox + (a[0] - 50) * s), (float) (oy + (a[1] - 50) * s), (float) (ox + (b[0] - 50) * s), (float) (oy + (b[1] - 50) * s), (float) w, c); } }
static void dk_ctext (const char *t, double x, double ytop, double hgt, unsigned c)   /* (centred on x) */
{ const double k = hgt / 6.0; br_text (t, (float) (x - (strlen (t) * 6 - 2) * k / 2.0), (float) ytop, (float) k, c); }
#define NL(a) ((int) (sizeof a / sizeof a[0]))
static double dk_pop_until;
/* the slim deck: one low row - the emulation rate (or PAUSED), the ports, the cartridge, RESET, the knob; no labels,
 * each where the full deck has it */
static void dk_draw_slim (void)
{
	dk_place ();
	const double r = 0.36 * dk_h, cy = dk_h / 2.0, w = fmax (1.0, r / 30.0); dk_r = r; dk_cy = cy;
	{ const double k = fmax (1.0, dk_h * 0.30 / 6.0), xr = dk_ox + dk_ow; char t[32]; unsigned c = DK_G;
	  if (vx_paused || br_open) { snprintf (t, sizeof t, "PAUSED"); c = 0xed9933u; } else { snprintf (t, sizeof t, "EMU %s", dk_osd[2]); c = (unsigned) dk_osd_col; }
	  br_text (t, (float) (xr - (strlen (t) * 6.0 - 1.0) * k), (float) (cy - 3 * k), (float) k, c); }
	for (int pt = 2; pt >= (net_on () ? 1 : 2); pt--) {   /* (P1: only online, as the desktop's port1_state - netplay) */ const int kind = pt == 2 ? dk_p2 : 0; const double cx = pt == 2 ? dk_p2x : dk_p1x, s = 2.0 * r / 100.0 * (kind == 2 ? 1.35 : kind == 1 ? 1.2 : 1.1);
		const double oy = cy, ox = cx + (kind == 2 ? 10 * s : 0);
		if (kind == 2) dk_icon (PORT2_ICON_P, PORT2_ICON_L, NL (PORT2_ICON_L), ox, oy, s, w, DK_G);
		else if (kind == 1) dk_icon (VOICE_ICON_P, VOICE_ICON_L, NL (VOICE_ICON_L), ox, oy, s, w, DK_G);
		else if (kind == 3) dk_icon (IMAGER_ICON_P, IMAGER_ICON_L, NL (IMAGER_ICON_L), ox, oy, s, w, DK_G);
		else if (kind == 4) dk_icon (LPEN_ICON_P, LPEN_ICON_L, NL (LPEN_ICON_L), ox, oy, s, w, DK_G);
		else dk_icon (JOY_ICON_P, JOY_ICON_L, NL (JOY_ICON_L), ox, oy, s, w, DK_G); }
	{ const double s = 2.0 * r / 100.0;
	  if (!ej_out) dk_icon (CART_ICON_P, CART_ICON_L, NL (CART_ICON_L), dk_cx, cy, s, w, DK_G);
	  else for (int l = 0; l < NL (LOAD_LINES); l++) for (int i = 0; i + 1 < LOAD_LINES[l][1]; i++) { const double s2 = 2.0 * r / 160.0, top = cy - r;
		const float *a = LOAD_PTS[LOAD_LINES[l][0] + i], *b = LOAD_PTS[LOAD_LINES[l][0] + i + 1];
		br_line ((float) (dk_cx + (a[0] - 50) * s2), (float) (top + a[1] * s2), (float) (dk_cx + (b[0] - 50) * s2), (float) (top + b[1] * s2), (float) w, DK_G); }
	  if (br_open) br_rect ((float) (dk_cx - 52 * s), (float) (cy - 56 * s), (float) (dk_cx + 52 * s), (float) (cy + 56 * s), DK_G, (float) w); }
	dk_circle (dk_rx, cy, 0.95 * r, w, DK_G); dk_circle (dk_rx, cy, (vt_wall () < dk_reset_until ? 0.68 : 0.78) * r, w, DK_G);
	{ const double cx = dk_kx, ang = (-150.0 + 300.0 * (pw_on ? vx_vol : 0.0)) * M_PI / 180;
	  dk_circle (cx, cy, r, w, DK_G); dk_circle (cx, cy, 0.6 * r, w, DK_G);
	  br_line ((float) (cx + 0.6 * r * sin (ang)), (float) (cy - 0.6 * r * cos (ang)), (float) (cx + 0.95 * r * sin (ang)), (float) (cy - 0.95 * r * cos (ang)), (float) (w * 1.6), DK_G);
	  for (int i = 0; i < 20; i++) { const double a0 = (50 + i * 4.0) * M_PI / 180, a1 = (54 + i * 4.0) * M_PI / 180, r0 = 1.25 * r + (0.03 + 0.17 * i / 20.0) * r;
		br_line ((float) (cx + 1.25 * r * sin (a0)), (float) (cy - 1.25 * r * cos (a0)), (float) (cx + 1.25 * r * sin (a1)), (float) (cy - 1.25 * r * cos (a1)), (float) w, DK_G);
		br_line ((float) (cx + r0 * sin (a0)), (float) (cy - r0 * cos (a0)), (float) (cx + (r0 + 0.0085 * r) * sin (a1)), (float) (cy - (r0 + 0.0085 * r) * cos (a1)), (float) w, DK_G); }
	  if (dk_hover_label) { char t[16]; if (pw_on) snprintf (t, sizeof t, "%d%%", (int) (vx_vol * 100 + 0.5)); else snprintf (t, sizeof t, "OFF");
		const double k = fmax (1.0, dk_h * 0.22 / 6.0); br_text (t, (float) (cx + 1.6 * r), (float) (cy - 3 * k), (float) k, DK_G); } }
}
/* the modern deck (the own iOS app's look, chosen in Control deck look - the default on Android and iOS): rounded panels
 * of one width each whatever they show - the figures (PAUSED, OFF), port 2's device, the cartridge (OUT), RESET, the
 * volume (the knob; slim: a bar). Full: icons above their words, CPU / GPU under the figures; slim and the pop-up: one
 * low row, icons beside their words. The touch zones follow the panels (dk_p2x, dk_cx, dk_rx, dk_kx) */
#define DKM_PANEL 0x0b1511u
#define DKM_EDGE 0x1d3329u
#define DKM_TRACK 0x16302au
#define DKM_HI 0x88ccbbu
#define DKM_DIM 0x7f978bu
#define DKM_OFF 0x4a6658u
#define DKM_OR 0xed9933u
static double dkm_bx0, dkm_bx1;                 /* (slim: the volume bar's track, for the finger) */
static void dkm_panel (double x0, double y0, double x1, double y1, double rad, unsigned fill, unsigned edge, double lw)
{
	if (x1 - x0 < 2 * rad) rad = (x1 - x0) / 2; if (y1 - y0 < 2 * rad) rad = (y1 - y0) / 2;
	for (double y = y0; y < y1; y += 1.0) {          /* (filled a row at a time: the corners rounded) */
		const double dy = y + 0.5 < y0 + rad ? y0 + rad - (y + 0.5) : y + 0.5 > y1 - rad ? (y + 0.5) - (y1 - rad) : 0;
		const double in = dy > 0 ? rad - sqrt (fmax (0.0, rad * rad - dy * dy)) : 0;
		br_fill ((float) (x0 + in), (float) y, (float) (x1 - in), (float) fmin (y + 1.0, y1), fill, 1); }
	br_line ((float) (x0 + rad), (float) y0, (float) (x1 - rad), (float) y0, (float) lw, edge); br_line ((float) (x0 + rad), (float) y1, (float) (x1 - rad), (float) y1, (float) lw, edge);
	br_line ((float) x0, (float) (y0 + rad), (float) x0, (float) (y1 - rad), (float) lw, edge); br_line ((float) x1, (float) (y0 + rad), (float) x1, (float) (y1 - rad), (float) lw, edge);
	const double cx[4] = { x0 + rad, x1 - rad, x1 - rad, x0 + rad }, cy[4] = { y0 + rad, y0 + rad, y1 - rad, y1 - rad }, a0[4] = { 180, 270, 0, 90 };   /* (each corner its own quarter, y down: top left 180-270 - the quarter beside it curled the corners outwards, the Nova 2026-11-19) */
	for (int c = 0; c < 4; c++) for (int i = 0; i < 9; i++) { const double p = (a0[c] + i * 10) * M_PI / 180, q = (a0[c] + (i + 1) * 10) * M_PI / 180;
		br_line ((float) (cx[c] + rad * cos (p)), (float) (cy[c] + rad * sin (p)), (float) (cx[c] + rad * cos (q)), (float) (cy[c] + rad * sin (q)), (float) lw, edge); }
}
static double dkm_textw (const char *t, double k) { return (strlen (t) * 6.0 - 1.0) * k; }
static void dkm_ctext (const char *t, double cx, double cy, double k, unsigned c) { br_text (t, (float) (cx - dkm_textw (t, k) / 2), (float) (cy - 3 * k), (float) k, c); }
static void dkm_arc (double cx, double cy, double rad, double d0, double d1, double w, unsigned c)   /* (degrees, clockwise from up) */
{
	const int n = (int) fmax (2, fabs (d1 - d0) / 6);
	for (int i = 0; i < n; i++) { const double p = (d0 + (d1 - d0) * i / n) * M_PI / 180, q = (d0 + (d1 - d0) * (i + 1) / n) * M_PI / 180;
		br_line ((float) (cx + rad * sin (p)), (float) (cy - rad * cos (p)), (float) (cx + rad * sin (q)), (float) (cy - rad * cos (q)), (float) w, c); }
}
/* (a panel's icon - the classic deck's vector pictures - and its word: full above, slim beside) */
static void dkm_dev_icon (int kind, double cx, double cy, double h, double w, unsigned c)
{
	const double s = h / 100.0 * (kind == 2 ? 1.35 : kind == 1 ? 1.2 : 1.1), ox = cx + (kind == 2 ? 10 * s : 0);
	if (kind == 2) dk_icon (PORT2_ICON_P, PORT2_ICON_L, NL (PORT2_ICON_L), ox, cy, s, w, c);
	else if (kind == 1) dk_icon (VOICE_ICON_P, VOICE_ICON_L, NL (VOICE_ICON_L), ox, cy, s, w, c);
	else if (kind == 3) dk_icon (IMAGER_ICON_P, IMAGER_ICON_L, NL (IMAGER_ICON_L), ox, cy, s, w, c);
	else if (kind == 4) dk_icon (LPEN_ICON_P, LPEN_ICON_L, NL (LPEN_ICON_L), ox, cy, s, w, c);
	else dk_icon (JOY_ICON_P, JOY_ICON_L, NL (JOY_ICON_L), ox, cy, s, w, c);
}
static void dkm_reset_icon (double cx, double cy, double h, double w, unsigned c)   /* (the circular arrow) */
{
	const double r = h * 0.36; dkm_arc (cx, cy, r, -60, 240, w, c);
	const double p = -60 * M_PI / 180, tx = cx + r * sin (p), ty = cy - r * cos (p);
	br_line ((float) tx, (float) ty, (float) (tx + 0.42 * r), (float) (ty - 0.12 * r), (float) w, c); br_line ((float) tx, (float) ty, (float) (tx + 0.10 * r), (float) (ty + 0.42 * r), (float) w, c);
}
static void dk_draw_modern (int slim)
{
	dk_place ();
	const double L0 = dk_sr > dk_sl + 40 ? dk_sl : 0, W = (dk_sr > dk_sl + 40 ? dk_sr - dk_sl : dk_w) * 0.975, H = dk_h, m = fmax (3.0, 0.018 * W), gap = fmax (2.0, (slim ? 0.011 : 0.014) * W), ph = H * (slim ? 0.78 : 0.80), y0 = (H - ph) / 2, y1 = y0 + ph, cy = (y0 + y1) / 2;
	const double lw = fmax (1.0, H / 70.0), rad = fmin (ph * 0.22, 0.024 * W), iw = fmax (1.2, ph / (slim ? 26.0 : 34.0)), mm = L0 + m;
	const int paused = vx_paused || br_open, on = pw_on;
	double sw, bw, x;
	if (!slim) { sw = floor ((W - 2 * m) * 0.33); const double kw = ph * 1.09; bw = floor ((W - 2 * m - sw - kw - 4 * gap) / 3); }
	else { sw = fmin (0.29 * (W - 2 * m), 4.3 * ph); bw = 1.93 * ph; }
	/* the figures: FPS HZ EMU (EMU coloured), CPU / GPU (full); PAUSED; switched off: -- */
	dkm_panel (mm, y0, mm + sw, y1, rad, DKM_PANEL, DKM_EDGE, lw);
	{ const double k = fmin ((slim ? 0.30 : 0.22) * ph / 6.0, 0.88 * sw / (19 * 6.0)), ty = slim ? cy : y0 + 0.32 * ph;
	  if (paused && on) dkm_ctext ("PAUSED", mm + sw / 2, cy, fmin (0.30 * ph / 6.0, 0.8 * sw / 36.0), DKM_OR);
	  else if (!on) dkm_ctext ("-- FPS  -- HZ  --", mm + sw / 2, ty, k, DKM_OFF);
	  else { char a[48]; snprintf (a, sizeof a, "%s  %s  ", dk_osd[0], dk_osd[1]); const double tw = dkm_textw (a, k) + 6 * k + dkm_textw (dk_osd[2], k);
		const float xe = br_text (a, (float) (mm + (sw - tw) / 2), (float) (ty - 3 * k), (float) k, DKM_HI); br_text (dk_osd[2], xe, (float) (ty - 3 * k), (float) k, (unsigned) dk_osd_col); }
	  if (!slim && !(paused && on)) { const double k2 = fmin (0.13 * ph / 6.0, 0.7 * k), by = y0 + 0.70 * ph, lx = mm + 0.04 * sw, lw2 = dkm_textw ("CPU", k2);
		const double tw = (sw - 0.08 * sw - 2 * lw2 - 3 * 4 * k2) / 2; const int pc[2] = { on ? dk_cpu : -1, on ? dk_gpu : -1 };
		for (int i = 0; i < 2; i++) { const double bx = lx + i * (lw2 + 2 * 4 * k2 + tw); br_text (i ? "GPU" : "CPU", (float) bx, (float) (by - 3 * k2), (float) k2, DKM_DIM);
			const double tx0 = bx + lw2 + 4 * k2, th = fmax (2.0, 0.06 * ph); br_fill ((float) tx0, (float) (by - th / 2), (float) (tx0 + tw), (float) (by + th / 2), DKM_TRACK, 1);
			if (pc[i] > 0) br_fill ((float) tx0, (float) (by - th / 2), (float) (tx0 + tw * fmin (1.0, pc[i] / 100.0)), (float) (by + th / 2), DK_G, 1); } } }
	x = mm + sw + gap;
	/* port 2's device (online in RetroArch: its own netplay - the port shown as P1 / P2 by the desktop's rule: P2 PORT) */
	{ static const char *const WF[5] = { "P2", "VECVOX", "VECVOICE", "3D IMAGER", "LIGHT PEN" }, *const WS[5] = { "P2", "VOX", "VOICE", "3D", "PEN" };
	  const int k5 = dk_p2 >= 0 && dk_p2 <= 4 ? dk_p2 : 0; const char *wd = slim ? WS[k5] : WF[k5];
	  dkm_panel (x, y0, x + bw, y1, rad, DKM_PANEL, DKM_EDGE, lw); dk_p2x = x + bw / 2;
	  if (!slim) { dkm_dev_icon (k5, x + bw / 2, y0 + 0.36 * ph, 0.40 * ph, iw, DKM_HI); dkm_ctext (wd, x + bw / 2, y0 + 0.78 * ph, fmin (0.15 * ph / 6.0, 0.86 * bw / (strlen (wd) * 6.0)), DKM_HI); }
	  else { const double k = fmin (0.28 * ph / 6.0, 0.55 * bw / (strlen (wd) * 6.0)), tw = dkm_textw (wd, k), ih = 0.62 * ph, gx = x + (bw - ih - 0.12 * ph - tw) / 2;
		dkm_dev_icon (k5, gx + ih / 2, cy, ih, iw, DKM_HI); br_text (wd, (float) (gx + ih + 0.12 * ph), (float) (cy - 3 * k), (float) k, DKM_HI); }
	  x += bw + gap; }
	/* the cartridge: in, or OUT (dim) */
	{ const unsigned c = ej_out ? DKM_OFF : DKM_HI; const char *wd = ej_out ? "OUT" : "CART";
	  dkm_panel (x, y0, x + bw, y1, rad, DKM_PANEL, br_open ? 0x2f5544u : DKM_EDGE, lw); dk_cx = x + bw / 2;
	  if (!slim) { dk_icon (CART_ICON_P, CART_ICON_L, NL (CART_ICON_L), x + bw / 2, y0 + 0.36 * ph, 0.40 * ph / 100.0, iw, c); dkm_ctext (wd, x + bw / 2, y0 + 0.78 * ph, fmin (0.15 * ph / 6.0, 0.86 * bw / 24.0), c); }
	  else { const double k = fmin (0.28 * ph / 6.0, 0.55 * bw / 24.0), tw = dkm_textw (wd, k), ih = 0.62 * ph, gx = x + (bw - ih - 0.12 * ph - tw) / 2;
		dk_icon (CART_ICON_P, CART_ICON_L, NL (CART_ICON_L), gx + ih / 2, cy, ih / 100.0, iw, c); br_text (wd, (float) (gx + ih + 0.12 * ph), (float) (cy - 3 * k), (float) k, c); }
	  x += bw + gap; }
	/* RESET: the circular arrow and the word (slim: the word) - pressed: brighter */
	{ const unsigned c = vt_wall () < dk_reset_until ? 0xbbffeeu : DKM_HI;
	  dkm_panel (x, y0, x + bw, y1, rad, DKM_PANEL, DKM_EDGE, lw); dk_rx = x + bw / 2;
	  if (!slim) { dkm_reset_icon (x + bw / 2, y0 + 0.36 * ph, 0.40 * ph, iw, c); dkm_ctext ("RESET", x + bw / 2, y0 + 0.78 * ph, fmin (0.15 * ph / 6.0, 0.86 * bw / 30.0), c); }
	  else dkm_ctext ("RESET", x + bw / 2, cy, fmin (0.28 * ph / 6.0, 0.72 * bw / 30.0), c);
	  x += bw + gap; }
	/* the volume: the knob (full) - its arc from 135 degrees, the cap, the value inside; slim: a bar. Off: OFF */
	{ const double x1 = L0 + W - m; dkm_panel (x, y0, x1, y1, rad, DKM_PANEL, DKM_EDGE, lw); dk_kx = (x + x1) / 2; dk_cy = cy; dk_r = ph * 0.5;
	  const double v = on ? vx_vol : 0.0;
	  if (!slim) { const double r = ph * 0.38, kcx = (x + x1) / 2; dkm_arc (kcx, cy, r, -135, 135, fmax (2.0, ph / 16.0), DKM_TRACK);
		if (v > 0.002) dkm_arc (kcx, cy, r, -135, -135 + 270 * v, fmax (2.0, ph / 16.0), DK_G);
		dk_circle (kcx, cy, 0.52 * r, lw, on ? DKM_HI : DKM_OR); char t[8]; if (on) snprintf (t, sizeof t, "%d", (int) (v * 100 + 0.5)); else snprintf (t, sizeof t, "OFF");
		dkm_ctext (t, kcx, cy, fmin (0.11 * ph / 6.0, 0.8 * r / (strlen (t) * 6.0)), on ? DKM_HI : DKM_OR); dkm_bx0 = dkm_bx1 = 0; }
	  else { const double ih = 0.48 * ph, ix = x + 0.10 * ph, k = fmin (0.26 * ph / 6.0, 0.18 * (x1 - x) / 18.0); char t[8];
		if (on) snprintf (t, sizeof t, "%d", (int) (v * 100 + 0.5)); else snprintf (t, sizeof t, "OFF");
		const unsigned c = on ? DKM_HI : DKM_OR;
		/* (the speaker - or the power sign switched off) */
		if (on) { const double sx = ix, sy = cy; br_fill ((float) sx, (float) (sy - 0.16 * ih), (float) (sx + 0.22 * ih), (float) (sy + 0.16 * ih), c, 1);
			br_line ((float) (sx + 0.22 * ih), (float) (sy - 0.16 * ih), (float) (sx + 0.52 * ih), (float) (sy - 0.42 * ih), (float) iw, c);
			br_line ((float) (sx + 0.52 * ih), (float) (sy - 0.42 * ih), (float) (sx + 0.52 * ih), (float) (sy + 0.42 * ih), (float) iw, c);
			br_line ((float) (sx + 0.52 * ih), (float) (sy + 0.42 * ih), (float) (sx + 0.22 * ih), (float) (sy + 0.16 * ih), (float) iw, c);
			dkm_arc (sx + 0.5 * ih, sy, 0.30 * ih, 40, 140, iw, c); dkm_arc (sx + 0.5 * ih, sy, 0.52 * ih, 40, 140, iw, c); }
		else { dkm_arc (ix + 0.45 * ih, cy, 0.38 * ih, 30, 330, iw, c); br_line ((float) (ix + 0.45 * ih), (float) (cy - 0.5 * ih), (float) (ix + 0.45 * ih), (float) (cy - 0.05 * ih), (float) iw, c); }
		const double tx1 = x1 - 0.12 * ph - dkm_textw ("100", k), bx0 = ix + ih + 0.18 * ph, bx1 = tx1 - 0.18 * ph, th = fmax (2.0, 0.14 * ph);
		br_fill ((float) bx0, (float) (cy - th / 2), (float) bx1, (float) (cy + th / 2), DKM_TRACK, 1);
		if (v > 0) br_fill ((float) bx0, (float) (cy - th / 2), (float) (bx0 + (bx1 - bx0) * v), (float) (cy + th / 2), DK_G, 1);
		br_text (t, (float) (x1 - 0.12 * ph - dkm_textw (t, k)), (float) (cy - 3 * k), (float) k, c); dkm_bx0 = bx0; dkm_bx1 = bx1; } }
}
/* (the pop-up's tab, modern: under the picture's foot, in the middle - tap it, or the picture's bottom) */
static void dk_draw_modern_tab (void)
{
	const double tw = fmax (24.0, 0.10 * dk_w), th = fmax (10.0, 0.26 * dk_h), x0 = (dk_w - tw) / 2, y0 = dk_h - th;
	dkm_panel (x0, y0, x0 + tw, dk_h + th, th * 0.5, DKM_PANEL, DKM_EDGE, fmax (1.0, dk_h / 70.0));
	const double a = th * 0.28, cx = dk_w / 2.0, cy = y0 + th * 0.45;
	br_line ((float) (cx - a), (float) (cy + 0.4 * a), (float) cx, (float) (cy - 0.4 * a), (float) fmax (1.0, a / 4), DKM_DIM);
	br_line ((float) cx, (float) (cy - 0.4 * a), (float) (cx + a), (float) (cy + 0.4 * a), (float) fmax (1.0, a / 4), DKM_DIM);
}
/* the pop-up deck's arrow (hidden: bottom left, tap it - or the picture's bottom - for the deck) */
static void dk_draw_arrow (void)
{
	const double a = 0.16 * dk_h, x = dk_ox + a, y = dk_h - 1.6 * a;
	br_fill ((float) (x - 1.3 * a), (float) (y - 1.1 * a), (float) (x + 1.3 * a), (float) (y + 1.1 * a), 0x000000u, 0.55f);
	br_line ((float) (x - a), (float) (y + 0.5 * a), (float) x, (float) (y - 0.5 * a), (float) fmax (1.0, a / 6), DK_G);
	br_line ((float) x, (float) (y - 0.5 * a), (float) (x + a), (float) (y + 0.5 * a), (float) fmax (1.0, a / 6), DK_G);
}
static void dk_draw (void)
{
	dk_w = CW; dk_h = DKH; if (dk_w < 1 || dk_h < 1) return;
	const size_t n = (size_t) dk_w * dk_h;
	if (n != dk_n) { free (dk_img); dk_img = malloc (n * 4); dk_n = n; }
	if (!dk_img) return;
	memset (dk_img, 0, n * 4); dr_img = dk_img; dr_w = dk_w; dr_h = dk_h; dk_gen++;
	if (opt_deck_look) {                              /* (the modern look: slim and the pop-up one low row, full the app's deck) */
		if (dk_pop && !dk_shown) { dk_place (); dk_draw_modern_tab (); return; }
		dk_draw_modern (opt_deck == 2 || dk_pop); return; }
	if (opt_deck == 2) { dk_draw_slim (); return; }
	dk_place ();
	if (dk_pop && !dk_shown) { dk_draw_arrow (); return; }
	if (dk_pop) br_fill (0, 0, (float) dk_w, (float) dk_h, 0x000000u, 0.82f);   /* (over the picture: on a dark panel) */
	const double r = dk_r, cy = dk_cy, w = r / 45.0 > 1.2 ? r / 45.0 : 1.2, lh = 0.30 * r;
	/* the OSD (vecrender.py _osd, _meters): the frame rate, the screen's, the emulation's; the CPU meter */
	{ const double k = dk_ok_, x = dk_ox, y = dk_M; static const char *const N[3] = { "INTERNAL:", "MONITOR:", "EMU RATE:" };
	  for (int i = 0; i < 3; i++) { char row[40]; const int vl = (int) strlen (dk_osd[i]), pad = dk_wmax (dk_cols) - vl; snprintf (row, sizeof row, "%-*s", pad, N[i]);
		const float xe = br_text (row, (float) x, (float) (y + i * 9.0 * k), (float) k, DK_G);
		br_text (dk_osd[i], xe, (float) (y + i * 9.0 * k), (float) k, i == 2 ? (unsigned) dk_osd_col : DK_G); }
	  const double km = k * 0.7, my = y + (3 + 0.25) * 9.0 * k, xe = x + dk_ow - k, bw = (xe - x) - 9 * 6.0 * km;
	  if (dk_thin) for (int i = 0; i < 2; i++) {        /* (_meters_thin: two thin lines under the text) */
		const double ty = y + 3 * 9.0 * k + (0.5 + i * 2.0) * k, th = fmax (1.0, k); const int pct = i == 0 ? dk_cpu : dk_gpu;
		br_fill ((float) x, (float) ty, (float) xe, (float) (ty + th), DK_G, 0.30f);
		if (pct >= 0) br_fill ((float) x, (float) ty, (float) (x + (xe - x) * fmin (1.0, pct / 100.0)), (float) (ty + th), pct < 90 ? DK_G : pct <= 100 ? 0xed9933u : 0xed5454u, 1); }
	  else for (int i = 0; i < 2 && bw > 12 * km; i++) { const double ty = my + i * 9.0 * km; const int pct = i == 0 ? dk_cpu : dk_gpu;
		br_text (i ? "GPU" : "CPU", (float) x, (float) ty, (float) km, DK_G);
		const double bx0 = x + 4 * 6.0 * km; br_rect ((float) bx0, (float) ty, (float) (bx0 + bw), (float) (ty + 6 * km), DK_G, (float) fmax (1.0, km * 0.42));
		const unsigned c = pct < 90 ? DK_G : pct <= 100 ? 0xed9933u : 0xed5454u;
		if (pct > 0) br_fill ((float) (bx0 + km), (float) (ty + km), (float) (bx0 + km + (bw - 2 * km) * fmin (1.0, pct / 100.0)), (float) (ty + 5 * km), c, 0.75f);
		char fig[8]; if (pct < 0) snprintf (fig, sizeof fig, "--"); else snprintf (fig, sizeof fig, "%d%%", pct > 999 ? 999 : pct);
		br_text (fig, (float) (xe - (strlen (fig) * 6.0 - 1.0) * km), (float) ty, (float) km, pct < 0 ? DK_G : c); } }
	/* PAUSED: between the OSD and the P2 port (slot_state) */
	if (vx_paused || br_open) { const double left = dk_ox + dk_ow, right = dk_p2x - 1.1 * r, room = right - left;   /* (the ROM browser open: paused too) */
		if (room > 0) { static const char *const R2 = "PRESS PAUSE TO RESUME";   /* (the browser open: PAUSED alone) */
			double kp = 0.80 * room / (6.0 * 6), kn = fmin (0.42 * kp, 0.80 * room / (6.0 * strlen (R2))), hgt = 11.0 * kp + 6.0 * kn;
			const double fb = 0, wb = dk_h;   /* (the frame's bottom edge: the sheet's) */ if (hgt > 0.8 * (wb - fb)) { const double f = 0.8 * (wb - fb) / hgt; kp *= f; kn *= f; hgt *= f; }
			const double y0 = (fb + wb) / 2.0 - hgt / 2.0, cx = (left + right) / 2.0;
			dk_ctext ("PAUSED", cx, y0, 6 * kp, 0xed9933u); if (!br_open) dk_ctext (R2, cx, y0 + 11.0 * kp, 6 * kn, DK_G); } }
	/* the ports (_port2): port 2's device, port 1's joystick */
	for (int pt = 2; pt >= (net_on () ? 1 : 2); pt--) {   /* (P1: only online, as the desktop's port1_state - netplay) */ const int kind = pt == 2 ? dk_p2 : 0; const double cx = pt == 2 ? dk_p2x : dk_p1x;
		double s = 2.0 * r / 100.0 * (kind == 2 ? 1.35 : kind == 1 ? 1.2 : 1.1); const double oy = kind == 2 ? cy : cy - 0.12 * r, ox = cx + (kind == 2 ? 10 * s : 0);
		if (kind == 2) dk_icon (PORT2_ICON_P, PORT2_ICON_L, NL (PORT2_ICON_L), ox, oy, s, w, DK_G);
		else if (kind == 1) dk_icon (VOICE_ICON_P, VOICE_ICON_L, NL (VOICE_ICON_L), ox, oy, s, w, DK_G);
		else if (kind == 3) dk_icon (IMAGER_ICON_P, IMAGER_ICON_L, NL (IMAGER_ICON_L), ox, oy, s, w, DK_G);
		else if (kind == 4) dk_icon (LPEN_ICON_P, LPEN_ICON_L, NL (LPEN_ICON_L), ox, oy, s, w, DK_G);
		else dk_icon (JOY_ICON_P, JOY_ICON_L, NL (JOY_ICON_L), ox, oy, s, w, DK_G);
		const char *t = pt == 2 ? "P2 PORT" : "P1 PORT"; const double l2 = fmin (lh, 2.6 * r * 6.0 / (strlen (t) * 6 - 2));
		dk_ctext (t, cx, cy - 1.5 * r - 0.30 * r, l2, DK_G); }
	/* the cartridge (_cart): in the slot, or out (the load symbol) */
	{ const double cx = dk_cx, s = 2.0 * r / 100.0;
	  if (!ej_out) dk_icon (CART_ICON_P, CART_ICON_L, NL (CART_ICON_L), cx, cy, s, w, DK_G);
	  else { const double s2 = 2.0 * r / 160.0, top = cy - r;
		for (int l = 0; l < NL (LOAD_LINES); l++) for (int i = 0; i + 1 < LOAD_LINES[l][1]; i++) { const float *a = LOAD_PTS[LOAD_LINES[l][0] + i], *b = LOAD_PTS[LOAD_LINES[l][0] + i + 1];
			const double dy = l >= 3 ? 0.0 : 0.0, sh = l >= 3 ? -60 * s2 + 60 * s2 : 0.0; (void) dy;
			br_line ((float) (cx + (a[0] - 50) * s2), (float) (top + a[1] * s2 + sh), (float) (cx + (b[0] - 50) * s2), (float) (top + b[1] * s2 + sh), (float) w, DK_G); } }
	  if (br_open) br_rect ((float) (cx - 52 * s), (float) (cy - 56 * s), (float) (cx + 52 * s), (float) (cy + 56 * s), DK_G, (float) w);
	  dk_ctext ("CARTRIDGE", cx, cy - 1.5 * r - lh, lh, DK_G); }
	/* the knob and RESET (_knob) */
	{ const double cx = dk_kx, vol = pw_on ? vx_vol : 0.0;
	  #define AT(x, y, rad, deg, ox, oy) do { const double a_ = (deg) * M_PI / 180; ox = (x) + (rad) * sin (a_); oy = (y) - (rad) * cos (a_); } while (0)
	  dk_circle (cx, cy, r, w, DK_G); dk_circle (cx, cy, 0.6 * r, w, DK_G);
	  for (int i = 0; i < 48; i++) { double x, y; AT (cx, cy, 0.645 * r, i * 7.5, x, y); br_fill ((float) (x - 0.6), (float) (y - 0.6), (float) (x + 0.6), (float) (y + 0.6), DK_G, 0.8f); }
	  const double ang = -150.0 + 300.0 * vol; double tx, ty, bx, by, px, py; AT (cx, cy, 0.93 * r, ang, tx, ty); AT (cx, cy, 0.76 * r, ang, bx, by); AT (0.0, 0.0, 0.08 * r, ang + 90.0, px, py);
	  br_line ((float) tx, (float) ty, (float) (bx + px), (float) (by + py), (float) w, DK_G); br_line ((float) (bx + px), (float) (by + py), (float) (bx - px), (float) (by - py), (float) w, DK_G);
	  br_line ((float) (bx - px), (float) (by - py), (float) tx, (float) ty, (float) w, DK_G);
	  char lab[16]; if (dk_hover_label) { if (pw_on) snprintf (lab, sizeof lab, "VOL %d%%", (int) (vx_vol * 100 + 0.5)); else snprintf (lab, sizeof lab, "OFF"); } else snprintf (lab, sizeof lab, "OFF/ON");
	  dk_ctext (lab, cx, cy - 1.5 * r - lh, lh, DK_G);
	  double ix[21], iy[21], ox_[21], oy_[21];
	  for (int i = 0; i < 21; i++) { AT (cx, cy, 1.25 * r, 50 + i * 4.0, ix[i], iy[i]); AT (cx, cy, 1.25 * r + (0.03 + 0.17 * i / 20.0) * r, 50 + i * 4.0, ox_[i], oy_[i]); }
	  for (int i = 0; i < 20; i++) { br_line ((float) ix[i], (float) iy[i], (float) ix[i + 1], (float) iy[i + 1], (float) w, DK_G); br_line ((float) ox_[i], (float) oy_[i], (float) ox_[i + 1], (float) oy_[i + 1], (float) w, DK_G); }
	  br_line ((float) ix[0], (float) iy[0], (float) ox_[0], (float) oy_[0], (float) w, DK_G); br_line ((float) ix[20], (float) iy[20], (float) ox_[20], (float) oy_[20], (float) w, DK_G);
	  dk_ctext ("VOL", cx + 1.6 * r + (3 * 6 - 2) * lh / 12.0, cy - lh / 2.0, lh, DK_G);
	  dk_circle (dk_rx, cy, 0.95 * r, w, DK_G); dk_circle (dk_rx, cy, (vt_wall () < dk_reset_until ? 0.68 : 0.78) * r, w, DK_G);
	  dk_ctext ("RESET", dk_rx, cy - 1.5 * r - lh, lh, DK_G);
	  #undef AT
	}
}
/* (the CPU's frame: the block in the output - bars black, the picture, the deck under it) */
static void dk_compose (uint32_t *o, const uint32_t *pic, size_t pitch)
{
	if (TW != CW || TH != CH) memset (o, 0, (size_t) TW * TH * 4);
	for (int y = 0; y < CH; y++) memcpy (o + (size_t) (BY + y) * TW + BX, pic + (size_t) y * pitch, (size_t) CW * 4);
	if (!dk_img) return;
	for (int y = 0; y < dk_h && BY + dk_y + y < TH; y++) for (int x = 0; x < dk_w && x < CW; x++) {   /* (premultiplied, over) */
		const uint32_t sv = dk_img[(size_t) y * dk_w + x]; const unsigned a = sv >> 24; if (!a) continue;
		uint32_t *d = o + (size_t) (BY + dk_y + y) * TW + BX + x; const unsigned ia = 255 - a, dv = *d;
		const unsigned rr = ((sv >> 16) & 255) + (((dv >> 16) & 255) * ia + 127) / 255, gg = ((sv >> 8) & 255) + (((dv >> 8) & 255) * ia + 127) / 255, bb = (sv & 255) + ((dv & 255) * ia + 127) / 255;
		*d = (rr > 255 ? 255u : rr) << 16 | (gg > 255 ? 255u : gg) << 8 | (bb > 255 ? 255u : bb); }
}
/* (each refresh: the OSD's figures once a second; the deck drawn anew when anything on it changed) */
static double dk_t0, dk_busy, dk_run0; static long dk_f0, dk_r0; static unsigned long long dk_e0; static long dk_runs;
/* (EMU RATE over the last DK_RW seconds, updated each second: one second's figure wobbled 98..101 % with the moment
 *  RetroArch calls the core (a few ms either way) - and 100 % only while no refresh came late: a missed refresh is the
 *  system not keeping up, and shows at once) */
#define DK_RW 4
static double dk_rw_e[DK_RW], dk_rw_t[DK_RW]; static int dk_rw_m[DK_RW], dk_rw_n, dk_rw_i, dk_miss; static double dk_lastshown;
int vx_app_osd_on;                              /* (set by the own iOS app: the figures measured with the drawn deck off) */
static void dk_rate_reset (void) { dk_rw_n = 0; dk_rw_i = 0; dk_miss = 0; dk_lastshown = 0; }
static void dk_tick (double busy)
{
	if (!dk_on && !dk_pop && !vx_app_osd_on) return;   /* (the own iOS app: its native deck reads the figures) */
	(void) busy;
	{ int av = 3; if (!env_cb (RETRO_ENVIRONMENT_GET_AUDIO_VIDEO_ENABLE, &av) || (av & 1)) { dk_runs++;
		const double w = vt_wall (), gap = dk_lastshown > 0 ? w - dk_lastshown : 0;
		if (gap > 0.3) { dk_t0 = w; dk_f0 = aq_frames; dk_r0 = dk_runs; dk_e0 = run_t; dk_busy = 0; dk_rate_reset (); }   /* (a stop - a game
		                                          loading, RetroArch's menu: not the emulation's speed - the rate measured anew
		                                          from here; it showed below 100 % for seconds after Last Human's start) */
		else if (gap > 1.5 / opt_rate) dk_miss++;
		dk_lastshown = w; } }   /* (MONITOR: the refreshes
	                                              shown - run-ahead's hidden runs not counted: 120 Hz on a 60 Hz screen) */
	{ static int nw = -1; const int n = net_on (); if (n != nw) { nw = n; dk_dirty_set = 1; } }
	{ static int hw = -1; const int h = vx_paused || br_open;   /* (paused, or the ROM browser open: EMU RATE 0%, not red - at once) */
	  if (h != hw) { hw = h; dk_dirty_set = 1; if (h && pw_on) { snprintf (dk_osd[2], 24, "0%%"); dk_osd_col = DK_G; }
		if (dk_t0 != 0) { dk_t0 = vt_wall (); dk_f0 = aq_frames; dk_r0 = dk_runs; dk_e0 = run_t; dk_busy = 0; } dk_rate_reset (); } }   /* (the rate measured anew: the stop not in it) */
	if (dk_pop) { const int want = vx_paused || dk_drag.on || vt_wall () < dk_pop_until;   /* (the pop-up: a while after a touch, while paused) */
		if (want != dk_shown) { dk_shown = want; dk_dirty_set = 1; } }
	{ static clock_t c0; const clock_t c = clock (); if (c0) dk_busy += (double) (c - c0) / CLOCKS_PER_SEC; c0 = c; }   /* (the processor: all the core's threads) */
	const double now = vt_wall ();
	if (dk_t0 == 0) { dk_t0 = now; dk_f0 = aq_frames; dk_r0 = dk_runs; dk_e0 = run_t; snprintf (dk_osd[0], 24, "-- FPS"); snprintf (dk_osd[1], 24, "-- HZ"); snprintf (dk_osd[2], 24, "--"); dk_osd_col = DK_G; }
	if (now - dk_t0 >= 1.0) { const double dt = now - dk_t0;
		snprintf (dk_osd[0], 24, "%d FPS", (int) ((aq_frames - dk_f0) / dt + 0.5)); snprintf (dk_osd[1], 24, "%d HZ", (int) ((dk_runs - dk_r0) / dt + 0.5));
		dk_rw_e[dk_rw_i] = (double) (run_t - dk_e0) / 1.5e6; dk_rw_t[dk_rw_i] = dt; dk_rw_m[dk_rw_i] = dk_miss; dk_miss = 0;
		dk_rw_i = (dk_rw_i + 1) % DK_RW; if (dk_rw_n < DK_RW) dk_rw_n++;
		double se = 0, st = 0; int sm = 0; for (int i = 0; i < dk_rw_n; i++) { se += dk_rw_e[i]; st += dk_rw_t[i]; sm += dk_rw_m[i]; }
		double sp = se / st * 100.0;
		if (!sm && sp >= 97 && sp < 103) sp = 100;              /* (every refresh on time: keeping up - the rest is the timing's wobble) */
		if (!pw_on) { snprintf (dk_osd[2], 24, "--"); dk_osd_col = DK_G; dk_cpu = dk_gpu = -1; }
		else if (vx_paused || br_open) { snprintf (dk_osd[2], 24, "0%%"); dk_osd_col = DK_G; dk_cpu = dk_gpu = -1; }   /* (stopped on purpose: not red) */
		else { snprintf (dk_osd[2], 24, "%d%%", sp >= 99.5 ? 100 : (int) sp); dk_osd_col = sp >= 97 ? DK_G : sp >= 80 ? 0xed9933u : 0xed5454u;
			dk_cpu = (int) (100.0 * dk_busy / dt + 0.5);
			dk_gpu = gq_ok > 0 && gq_n ? (int) (gq_sum_ms / 10.0 / dt + 0.5) : -1; }   /* (the GPU's busy share of the second: its time per picture, summed) */
		dk_t0 = now; dk_f0 = aq_frames; dk_r0 = dk_runs; dk_e0 = run_t; dk_busy = 0; gq_sum_ms = 0; gq_n = 0; dk_dirty_set = 1; }
	if ((dk_on || dk_pop) && (dk_dirty_set || vt_wall () < dk_reset_until + 0.05)) { dk_draw (); dk_dirty_set = 0; }
}
static void turn_volume (double d)
{
	if (!pw_on) { if (d <= 0 || net_on ()) return; vx_vol = 0; pw_switch (1, 30); }
	vx_vol += (float) d; if (vx_vol > 1) vx_vol = 1;
	if (net_on () && vx_vol < 0.01f) vx_vol = 0.01f;   /* (netplay: not switched off - that would be one side only) */
	if (vx_vol <= 0.0005f) { vx_vol = 0; vol_before_off = 0.5f; pw_switch (0, 0); }   /* (down to 0: switched off, as the Vectrex) */
	dk_dirty_set = 1;
}
static void power_up (void) { turn_volume (vol_before_off > 0 ? vol_before_off : 0.5); }
/* (the own iOS app's native deck: the figures, the knob, the switch) */
void vx_app_osd (char *fps, char *hz, char *emu, int *cpu, int *gpu)
{ snprintf (fps, 24, "%s", dk_osd[0]); snprintf (hz, 24, "%s", dk_osd[1]); snprintf (emu, 24, "%s", dk_osd[2]); *cpu = dk_cpu; *gpu = dk_gpu; }
double vx_app_volume (void) { return pw_on ? vx_vol : 0; }
void vx_app_turn (double d) { turn_volume (d); }
void vx_app_power (void) { if (netvx_playing ()) { fprintf (stderr, "[vectrex_emu] power: not during an online game\n"); return; } if (pw_on) { vol_before_off = vx_vol > 0.01f ? vx_vol : 0.5f; vx_vol = 0; pw_switch (0, 0); dk_dirty_set = 1; } else power_up (); }
/* RetroArch's pointer (the mouse, touch) on the deck - and, switched off, on the picture: on */
static int dk_prev_press;
static void dk_pointer (void)
{
	const int pr = input_cb (0, RETRO_DEVICE_POINTER, 0, RETRO_DEVICE_ID_POINTER_PRESSED) != 0;
	if ((!dk_on && !dk_pop) || br_open || br_ate) { dk_prev_press = pr; dk_drag.on = 0; br_ate = 0; return; }   /* (a touch that closes the
	                                                    browser - on the cartridge, as the desktop's click - does not open it again) */
	double fx = (input_cb (0, RETRO_DEVICE_POINTER, 0, RETRO_DEVICE_ID_POINTER_X) + 32768) / 65536.0, fy = (input_cb (0, RETRO_DEVICE_POINTER, 0, RETRO_DEVICE_ID_POINTER_Y) + 32768) / 65536.0;
	{ const double ax = fx, ay = fy; if (opt_rot == 1) { fx = 1 - ay; fy = ax; } else if (opt_rot == 2) { fx = 1 - ax; fy = 1 - ay; } else if (opt_rot == 3) { fx = ay; fy = 1 - ax; } }
	const double x = fx * TW - BX, y = fy * TH - BY - dk_y, r = dk_r;   /* (the deck's own pixels: y 0 at its top) */
	{ static double t_press = -1;                       /* (debounced: a touch screen's bounce is no second press - none within 0.25 s) */
	  if (pr && !dk_prev_press) { if (vt_wall () - t_press < 0.25) { dk_prev_press = pr; return; } t_press = vt_wall (); } }
	if (dk_pop && pr && !dk_prev_press) {               /* (the pop-up: a touch on the picture's bottom shows it, elsewhere hides it) */
		if (!dk_shown) { if (y >= 0) { dk_pop_until = vt_wall () + 5.0; dk_shown = 1; dk_dirty_set = 1; dk_prev_press = pr; return; } }
		else if (y < 0) { dk_pop_until = 0; dk_prev_press = pr; return; }
		else dk_pop_until = vt_wall () + 5.0; }
	if (dk_pop && !dk_shown) { dk_prev_press = pr; return; }
	/* (a finger: the piece nearest across the deck - each up to halfway to its neighbours, the deck's whole height; the
	 *  desktop's circles were too small to hit on a handheld's screen, the owner 2026-11-15) */
	int zone = 0;
	if (y >= 0 && y <= dk_h) { const double X[5] = { dk_p2x, dk_cx, net_on () ? dk_p1x : -1e9, dk_rx, dk_kx }; double best = 1e9;   /* (P1: online only) */
		for (int i = 0; i < 5; i++) { const double d = fabs (x - X[i]); if (d < best) { best = d; zone = i + 1; } }
		if (best > 2.2 * r) zone = 0; }
	if (net_on () && pr && !dk_prev_press && zone != 5) { dk_prev_press = pr; return; }   /* (netplay: only the volume - the rest changes the
	                                                    emulation on one side) */
	/* (port 2: only a symbol - its device is chosen in the core options; RESET: only the button's upper half, the side of
	 *  its word, away from the screen's edge - a thumb slipping off the right stick switched port 2 and reset Last Human,
	 *  the Nova 2026-11-18) */
	{ static int held, done; static double ht;   /* (RESET: held 0.2 s there; moved off before then: nothing) */
	  const int on = zone == 4 && (opt_deck_look ? y >= 0 && y <= dk_h : y <= dk_cy + 0.4 * r && fabs (x - dk_rx) <= 1.3 * r);   /* (the button's upper half up to its word, wider; modern: its panel) */
	  if (pr && !dk_prev_press && on) { held = 1; ht = vt_wall (); done = 0; }
	  else if (!pr || !on) held = 0;
	  else if (held && !done && vt_wall () - ht >= 0.2) { extern void vecx_reset_button (void); done = 1; if (netvx_playing ()) ls_reset_req = 1; else { emu_wait_ext (); vecx_reset_button (); } dk_reset_until = vt_wall () + 0.18; dk_dirty_set = 1; } }
	if (pr && !dk_prev_press) {
		if (zone == 1 || zone == 4) { }
		else if (zone == 2) { if (vt_wall () - br_t_toggle >= 0.3) { br_show (1); dk_dirty_set = 1; } }
		else if (zone == 3) { }                         /* (P1: for online play, later) */
		else if (zone == 5 && dkm_bx1 > dkm_bx0) {   /* (the modern slim deck's bar: the volume where the finger is) */
			dk_drag.on = 1; dk_drag.moved = 1; dk_drag.a = -1e9; dk_hover_label = 1;
			if (pw_on) turn_volume (fmax (0.01, fmin (1.0, (x - dkm_bx0) / (dkm_bx1 - dkm_bx0))) - vx_vol); else power_up (); dk_dirty_set = 1; }
		else if (zone == 5) { dk_drag.on = 1; dk_drag.moved = 0; dk_drag.x = x; dk_drag.y = y; dk_drag.up = x >= dk_kx; dk_drag.acc = 0; dk_drag.vol0 = vx_vol;
			dk_drag.a = hypot (x - dk_kx, y - dk_cy) < 0.3 * r ? 1e9 : atan2 (x - dk_kx, -(y - dk_cy)) * 180 / M_PI; dk_hover_label = 1; dk_dirty_set = 1; }
		else if (y < 0 && !pw_on && !net_on ()) power_up ();
		else if (vx_paused && x > dk_ox + dk_ow && x < dk_p2x - 1.1 * r && y >= 0) { vx_paused = 0; dk_dirty_set = 1; }
	} else if (pr && dk_drag.on && dk_drag.a == -1e9) {   /* (the bar: followed) */
		if (pw_on && dkm_bx1 > dkm_bx0) { turn_volume (fmax (0.01, fmin (1.0, (x - dkm_bx0) / (dkm_bx1 - dkm_bx0))) - vx_vol); dk_dirty_set = 1; }
	} else if (pr && dk_drag.on) {                   /* (turned round its centre, clockwise louder, one to one with its marker) */
		if (!dk_drag.moved && hypot (x - dk_drag.x, y - dk_drag.y) >= fmax (3.0, 0.12 * r)) dk_drag.moved = 1;
		const double a = hypot (x - dk_kx, y - dk_cy) < 0.3 * r ? 1e9 : atan2 (x - dk_kx, -(y - dk_cy)) * 180 / M_PI;
		if (a < 1e8 && dk_drag.a < 1e8) { const double da = fmod (a - dk_drag.a + 540.0, 360.0) - 180.0; dk_drag.acc += da / 300.0;
			if (fabs (dk_drag.acc) >= 0.005) { const double st = dk_drag.acc; dk_drag.acc = 0; turn_volume (st); } }
		dk_drag.a = a;
	} else if (!pr && dk_drag.on) {                  /* (let go: not turned - a click: +-5 %, switched off: on) */
		dk_drag.on = 0; dk_hover_label = 0; dk_dirty_set = 1;
		if (!dk_drag.moved) { if (vx_vol != dk_drag.vol0 && pw_on) vx_vol = (float) dk_drag.vol0;
			if (!pw_on) power_up (); else turn_volume (dk_drag.up ? 0.05 : -0.05); }
	}
	dk_prev_press = pr;
}
