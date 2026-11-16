/* browser.c - Vectrex_EMU's libretro core: the ROM browser (the desktop's: vectrex_emu.py scan_roms / browser_pad,
 * vecrender.py _browser) over the picture - its dark panel, the title band, the list with the chosen row as a bar, the
 * scroll buttons, LOAD with the cartridge going into the slot, the keys in a foot band, in the desktop's stroke font.
 * Opened with its button (Ports > ROM browser button: L3 by default; "ROM browser" in Quick Menu > Controls). The stick or d-pad chooses
 * (held: it repeats), left / right turns a page, button 4 opens a folder or loads the game, button 1 goes
 * up a folder - where none is left it closes, as its button does. Touch: a row chooses, the chosen one again loads; the
 * buttons as drawn; outside the panel closes. The game runs on without input meanwhile. It starts in the folder the
 * game was loaded from. Drawn on the CPU into an image of the sheet's size (only when it changes), laid over the picture
 * on the GPU (gpu_phos.c) or into the CPU's frame. Included by libretro_vx.c before retro_run.
 * Copyright (c) 2026 Hugo Bokhorst. Part of Vectrex_EMU; see LICENSE.md. */
#include "hud_font.h"
#ifdef _WIN32
#include <windows.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#endif

typedef struct { char name[200]; char path[1100]; char kind; } br_ent;   /* (kind: u BACK, d folder, r ROM, m UVMC2, p PiTrex) */
static br_ent *br_e; static int br_n, br_cap, br_sel, br_top, br_open, br_dirty = 1, br_hold, br_prev_ok;
static char br_dir[1100], br_game[1100];
static unsigned br_prevb; static double br_t_next; static int br_dprev, br_prevtouch, br_ate;   /* (br_ate: a touch used here - the deck ignores it) */
static double br_t_toggle = -1;                   /* (opened / closed then: a touch toggles it again only 0.3 s later) */
static uint32_t *br_img; static size_t br_imgn; static int br_bx0, br_by0, br_bx1, br_by1, br_shown, br_gen;
static double br_hold_t;
#define BR_NVIS 14
static int br_cmp (const void *a, const void *b)
{
	const br_ent *x = a, *y = b; int kx = x->kind == 'u' ? 0 : x->kind == 'd' ? 1 : 2, ky = y->kind == 'u' ? 0 : y->kind == 'd' ? 1 : 2;
	if (kx != ky) return kx - ky;
	return vx_stricmp (x->name, y->name);
}
static void br_add (const char *name, const char *path, char kind)
{
	if (br_n == br_cap) { br_cap = br_cap ? 2 * br_cap : 256; br_e = realloc (br_e, sizeof (br_ent) * br_cap); }
	br_ent *e = &br_e[br_n++]; snprintf (e->name, sizeof e->name, "%s", name); snprintf (e->path, sizeof e->path, "%s", path); e->kind = kind;
}
static char br_kind (const char *f)              /* (a ROM's kind by its extension, 0: not one) */
{
	{ size_t L = strlen (f); if (L > 7 && !vx_stricmp (f + L - 7, "_sd.img")) return 0; }   /* (an SD card's image: not a game) */
	const char *e = strrchr (f, '.'); if (!e) return 0;
	static const char *const R[] = { ".bin", ".vec", ".gam", ".rom", ".v4e", NULL };
	for (int i = 0; R[i]; i++) if (!vx_stricmp (e, R[i])) return 'r';
	if (!vx_stricmp (e, ".um2")) return 'm';
	if (!vx_stricmp (e, ".img") || !vx_stricmp (e, ".pit")) return 'p';
	return 0;
}
static void br_parent (const char *d, char *o, size_t n)
{
	snprintf (o, n, "%s", d); size_t L = strlen (o); while (L > 1 && (o[L - 1] == '/' || o[L - 1] == '\\')) o[--L] = 0;
	char *s = strrchr (o, '/'), *t = strrchr (o, '\\'); if (t > s) s = t;
	if (!s) { o[0] = 0; return; }
	if (s == o) s[1] = 0; else if (s == o + 2 && o[1] == ':') s[1] = 0; else *s = 0;
}
/* (one folder: BACK, its folders, its ROMs - each by name; not the overlays folder, nor hidden files) */
static void br_scan (const char *d, const char *select)
{
	br_n = 0; snprintf (br_dir, sizeof br_dir, "%s", d);
	char up[1100]; br_parent (d, up, sizeof up);
	if (up[0] && strcmp (up, d)) br_add ("BACK", up, 'u');
#ifdef _WIN32
	char pat[1200]; snprintf (pat, sizeof pat, "%s\\*", d); WIN32_FIND_DATAA fd; HANDLE h = FindFirstFileA (pat, &fd);
	if (h != INVALID_HANDLE_VALUE) { do { const char *f = fd.cFileName; if (f[0] == '.') continue;
		char p[1200]; snprintf (p, sizeof p, "%s\\%s", d, f);
		if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) { if (vx_stricmp (f, "overlays")) br_add (f, p, 'd'); }
		else { char k = br_kind (f); if (k) { char nm[200]; snprintf (nm, sizeof nm, "%s", f); char *x = strrchr (nm, '.'); if (x) *x = 0; br_add (nm, p, k); } }
	  } while (FindNextFileA (h, &fd)); FindClose (h); }
#else
	DIR *dd = opendir (d);
	if (dd) { struct dirent *de; while ((de = readdir (dd))) { const char *f = de->d_name; if (f[0] == '.') continue;
		char p[1200]; snprintf (p, sizeof p, "%s%s%s", d, d[strlen (d) - 1] == '/' ? "" : "/", f); struct stat st;
		if (stat (p, &st)) continue;
		if (S_ISDIR (st.st_mode)) { if (vx_stricmp (f, "overlays")) br_add (f, p, 'd'); }
		else { char k = br_kind (f); if (k) { char nm[200]; snprintf (nm, sizeof nm, "%s", f); char *x = strrchr (nm, '.'); if (x) *x = 0; br_add (nm, p, k); } } }
	  closedir (dd); }
#endif
	if (br_n > 1) qsort (br_e, (size_t) br_n, sizeof (br_ent), br_cmp);
	br_sel = br_n > 1 && br_e[0].kind == 'u' ? 1 : 0; br_top = 0;
	if (select) for (int i = 0; i < br_n; i++) if (!strcmp (br_e[i].path, select)) br_sel = i;
	if (br_sel >= BR_NVIS) br_top = br_sel - 3;
	br_dirty = 1;
}
static void br_move (int d)
{
	if (!br_n) return;
	br_sel += d; if (br_sel < 0) br_sel = 0; if (br_sel > br_n - 1) br_sel = br_n - 1;
	if (br_sel < br_top) br_top = br_sel; if (br_sel >= br_top + BR_NVIS) br_top = br_sel - BR_NVIS + 1;
	br_dirty = 1;
}
static void br_show (int on)
{
	br_open = on; br_dirty = 1; br_prev_ok = 0; br_t_toggle = vt_wall ();
	if (on) { char d[1100], cur[1100]; snprintf (cur, sizeof cur, "%s", br_game); br_parent (cur, d, sizeof d);
		br_scan (br_dir[0] ? br_dir : d[0] ? d : ".", br_dir[0] ? NULL : cur); }
}
static int game_switch (const char *path); static void eject_toggle (void); static char cur_game[1100]; static int ej_out;
static void br_activate (void)                     /* (button 4 / the plug: a folder opened, a game loaded) */
{
	if (!br_n) return;
	br_ent *e = &br_e[br_sel];
	if (e->kind == 'u') { char was[1100]; snprintf (was, sizeof was, "%s", br_dir); br_scan (e->path, was); return; }
	if (e->kind == 'd') { br_scan (e->path, NULL); return; }
	char p[1100]; snprintf (p, sizeof p, "%s", e->path);
	br_show (0); br_hold = 1; br_hold_t = vt_wall ();
	game_switch (p);                                  /* (at the next refresh's start: libretro_vx.c) */
}
static void br_back (void)                         /* (button 1: up a folder; none left: closed) */
{
	if (br_n && br_e[0].kind == 'u') { char was[1100]; snprintf (was, sizeof was, "%s", br_dir); br_scan (br_e[0].path, was); }
	else { br_show (0); br_hold = 1; br_hold_t = vt_wall (); }
}

/* ---- the drawing: the desktop's geometry (vectrex_emu.py browser_geom) in the sheet's pixels */
typedef struct { float x0, y0, x1, y1, lst[4], plug[4], up[4], dn[4], cl[4], rh, k; } br_geo;
static br_geo br_g;
static void br_geom (void)
{
	const double ph = FH / 0.924, pw = ph * ALG_MAX_X / ALG_MAX_Y, pl = EW / 2.0 - pw / 2, pt = FH / 2.0 - ph / 2;
	br_geo g; g.x0 = (float) (pl + 0.1 * pw); g.x1 = (float) (pl + 0.9 * pw); g.y0 = (float) (pt + 0.12 * ph); g.y1 = (float) (pt + 0.82 * ph);
	if (g.x0 < CX0 + 2) g.x0 = (float) (CX0 + 2); if (g.x1 > CX0 + CW - 3) g.x1 = (float) (CX0 + CW - 3);
	if (g.y0 < CY0 + 2) g.y0 = (float) (CY0 + 2); if (g.y1 > CY0 + CH - 3) g.y1 = (float) (CY0 + CH - 3);
	const float rh = (g.y1 - g.y0) / (BR_NVIS + 4.6f), b = 1.2f * rh; g.rh = rh; g.k = rh / 13.5f;
	g.lst[0] = g.x0 + 0.6f * rh; g.lst[1] = g.y0 + 1.6f * rh; g.lst[2] = g.x1 - 0.6f * rh; g.lst[3] = g.lst[1] + BR_NVIS * rh;
	g.plug[0] = g.x1 - 2.9f * rh; g.plug[1] = g.lst[3] + 0.55f * rh; g.plug[2] = g.x1 - 1.0f * rh; g.plug[3] = g.lst[3] + 1.7f * rh;
	g.up[0] = g.lst[0]; g.up[1] = g.lst[3] + 0.5f * rh; g.up[2] = g.lst[0] + b; g.up[3] = g.up[1] + b;
	g.dn[0] = g.lst[0] + 1.4f * b; g.dn[1] = g.up[1]; g.dn[2] = g.lst[0] + 2.4f * b; g.dn[3] = g.up[3];
	g.cl[0] = g.x1 - 0.6f * rh - b; g.cl[1] = g.y0 + 0.3f * rh; g.cl[2] = g.x1 - 0.6f * rh; g.cl[3] = g.cl[1] + b;
	br_g = g;
}
static uint32_t *dr_img; static int dr_w, dr_h;    /* (what br_over draws into: the browser's image, the deck's) */
static void br_over (int x, int y, unsigned rgb, float a)   /* (premultiplied ARGB, the colour over what is there) */
{
	if (x < 0 || y < 0 || x >= dr_w || y >= dr_h || a <= 0) return;
	if (a > 1) a = 1;
	uint32_t *p = dr_img + (size_t) y * dr_w + x, d = *p; const float ia = 1 - a;
	const unsigned r = (unsigned) (((rgb >> 16) & 255) * a + ((d >> 16) & 255) * ia + 0.5f), g = (unsigned) (((rgb >> 8) & 255) * a + ((d >> 8) & 255) * ia + 0.5f),
	               b = (unsigned) ((rgb & 255) * a + (d & 255) * ia + 0.5f), A = (unsigned) (255 * a + (d >> 24) * ia + 0.5f);
	*p = (A > 255 ? 255u : A) << 24 | (r > 255 ? 255u : r) << 16 | (g > 255 ? 255u : g) << 8 | (b > 255 ? 255u : b);
}
static void br_fill (float x0, float y0, float x1, float y1, unsigned c, float a)
{
	for (int y = (int) floorf (y0); y < (int) ceilf (y1); y++) { const float cy = fminf (y + 1.0f, y1) - fmaxf ((float) y, y0);
		for (int x = (int) floorf (x0); x < (int) ceilf (x1); x++) { const float cx = fminf (x + 1.0f, x1) - fmaxf ((float) x, x0); br_over (x, y, c, a * cx * cy); } }
}
static void br_line (float ax, float ay, float bx, float by, float w, unsigned c)   /* (a line w px wide, its edges soft) */
{
	const float r = w / 2 + 1; int x0 = (int) floorf (fminf (ax, bx) - r), x1 = (int) ceilf (fmaxf (ax, bx) + r), y0 = (int) floorf (fminf (ay, by) - r), y1 = (int) ceilf (fmaxf (ay, by) + r);
	const float dx = bx - ax, dy = by - ay, L2 = dx * dx + dy * dy;
	for (int y = y0; y <= y1; y++) for (int x = x0; x <= x1; x++) {
		const float px = x + 0.5f, py = y + 0.5f; float t = L2 > 0 ? ((px - ax) * dx + (py - ay) * dy) / L2 : 0; t = t < 0 ? 0 : t > 1 ? 1 : t;
		const float ex = px - ax - dx * t, ey = py - ay - dy * t, d = sqrtf (ex * ex + ey * ey), cov = w / 2 + 0.5f - d;
		if (cov > 0) br_over (x, y, c, cov > 1 ? 1 : cov); }
}
static void br_rect (float x0, float y0, float x1, float y1, unsigned c, float w)
{ br_line (x0, y0, x1, y0, w, c); br_line (x1, y0, x1, y1, w, c); br_line (x1, y1, x0, y1, w, c); br_line (x0, y1, x0, y0, w, c); }
static float br_text (const char *t, float x, float y, float k, unsigned c)   /* (vecrender.py _text_row: 6 k high, top left) */
{
	const float w = fmaxf (1.0f, k * 0.42f);
	for (; *t; t++) { int ch = toupper ((unsigned char) *t); if (ch < 0 || ch > 127) ch = '?';
		const unsigned short *ix = HUD_IDX[ch];
		for (int s = 0; s < ix[1]; s++) { const float *q = HUD_SEG[ix[0] + s]; br_line (x + q[0] * k, y + q[1] * k, x + q[2] * k, y + q[3] * k, w, c); }
		x += 6 * k; }
	return x;
}
static void br_draw (void)
{
	const size_t n = (size_t) FW * FH;
	if (n != br_imgn) { free (br_img); br_img = calloc (n, 4); br_imgn = n; }
	if (!br_img) return;
	dr_img = br_img; dr_w = FW; dr_h = FH;
	if (br_shown) for (int y = br_by0; y <= br_by1; y++) if (y >= 0 && y < FH) memset (br_img + (size_t) y * FW + (br_bx0 > 0 ? br_bx0 : 0), 0, (size_t) ((br_bx1 < FW ? br_bx1 : FW - 1) - (br_bx0 > 0 ? br_bx0 : 0) + 1) * 4);
	br_shown = 0; br_dirty = 0; br_gen++;
	if (!br_open) return;
	br_geom (); const br_geo *g = &br_g; const float k = g->k, rh = g->rh;
	const unsigned GRN = 0x66aa88, HI = 0x88ccbb, DIM = 0x4a7a62, BG = 0x05080a, TOP = 0x0a1210, PAN = 0x0b1511, BODY = 0x0f1c17, SEL = 0x24443a,
	               EDGE = 0x1d3329, EDGE2 = 0x2f5544, UM = 0x66a4b8, UMH = 0x8ac8dc, PX = 0xaa9c88, PXH = 0xd0c0aa;
	br_fill (g->x0, g->y0, g->x1, g->y1, BG, 0.96f);
	br_fill (g->x0, g->y0, g->x1, g->lst[1] - 0.35f * rh, TOP, 1);
	br_fill (g->x0, g->up[1] - 0.35f * rh, g->x1, g->y1, TOP, 1);
	const float lw = fmaxf (1.0f, k * 0.30f);
	br_rect (g->x0, g->y0, g->x1, g->y1, EDGE, lw);
	br_line (g->x0, g->lst[1] - 0.35f * rh, g->x1, g->lst[1] - 0.35f * rh, lw, EDGE); br_line (g->x0, g->up[1] - 0.35f * rh, g->x1, g->up[1] - 0.35f * rh, lw, EDGE);
	/* the title: the folder (its last two names) and how many games */
	{ char t[300], a[1100], nm[2][200] = { "", "" }; snprintf (a, sizeof a, "%s", br_dir);
	  for (int i = 0; i < 2; i++) { size_t L = strlen (a); while (L > 1 && (a[L - 1] == '/' || a[L - 1] == '\\')) a[--L] = 0;
		char *s = strrchr (a, '/'), *s2 = strrchr (a, '\\'); if (s2 > s) s = s2; snprintf (nm[i], sizeof nm[i], "%s", s ? s + 1 : a); if (s) *s = 0; else break; }
	  int games = 0; for (int i = 0; i < br_n; i++) games += br_e[i].kind == 'r' || br_e[i].kind == 'm' || br_e[i].kind == 'p';
	  if (nm[1][0]) snprintf (t, sizeof t, "%s / %s  (%d)", nm[1], nm[0], games); else snprintf (t, sizeof t, "%s  (%d)", nm[0], games);
	  const int cols = (int) ((g->cl[0] - g->x0 - 4 * 6 * k) / (6 * k)); if ((int) strlen (t) > cols && cols > 3) { memmove (t + 2, t + strlen (t) - cols + 2, (size_t) cols - 1); t[0] = t[1] = '.'; }
	  br_text (t, g->x0 + 4 * k, g->y0 + 0.6f * rh, k, HI); }
	/* the list */
	br_fill (g->lst[0], g->lst[1], g->lst[2], g->lst[3], BODY, 1); br_rect (g->lst[0], g->lst[1], g->lst[2], g->lst[3], EDGE2, lw);
	const int cols = (int) ((g->lst[2] - g->lst[0] - 4 * 6 * k) / (6 * k));
	if (!br_n) br_text ("NO ROMS IN THIS FOLDER.", g->lst[0] + 6 * k, g->lst[1] + 0.3f * rh, k, GRN);
	for (int i = 0; i < BR_NVIS; i++) {
		const int j = br_top + i; if (j >= br_n) break;
		const float y = g->lst[1] + i * rh; const br_ent *e = &br_e[j]; const int icon = e->kind == 'u' || e->kind == 'd';
		const unsigned c = e->kind == 'm' ? (j == br_sel ? UMH : UM) : e->kind == 'p' ? (j == br_sel ? PXH : PX) : j == br_sel ? HI : GRN;
		if (j == br_sel) br_fill (g->lst[0] + 0.6f * k, y + 0.08f * rh, g->lst[2] - 2.2f * 6 * k, y + 0.97f * rh, SEL, 1);
		char t[220]; const int room = cols - (icon ? 4 : 0);
		if ((int) strlen (e->name) <= room) snprintf (t, sizeof t, "%s", e->name); else snprintf (t, sizeof t, "%.*s..", room > 2 ? room - 2 : 0, e->name);
		br_text (t, g->lst[0] + 1.5f * k + (2 + (icon ? 4 : 0)) * 6 * k, y + 0.3f * rh, k, c);
		if (icon) { const float ix = g->lst[0] + 1.5f * k + 2 * 6 * k, iy = y + 0.3f * rh, w = fmaxf (1.0f, k * 0.42f);
			#define P(a, b) ix + (a) * k, iy + (b) * k
			if (e->kind == 'd') { const float pt[8][2] = { { 0, 6 }, { 0, 1 }, { 1, 0 }, { 4, 0 }, { 5, 1 }, { 10, 1 }, { 10, 6 }, { 0, 6 } };
				for (int q = 0; q < 7; q++) br_line (P (pt[q][0], pt[q][1]), P (pt[q + 1][0], pt[q + 1][1]), w, c); }
			else { br_line (P (10, 3), P (0, 3), w, c); br_line (P (0, 3), P (3, 0), w, c); br_line (P (0, 3), P (3, 6), w, c); }
			#undef P
		} }
	if (br_n > BR_NVIS) { const float sx = g->lst[2] - 1.0f * 6 * k, ht = (g->lst[3] - g->lst[1]) * BR_NVIS / (float) br_n;
		const float st = g->lst[1] + (g->lst[3] - g->lst[1] - ht) * br_top / (float) (br_n - BR_NVIS > 1 ? br_n - BR_NVIS : 1);
		br_fill (sx, st + 0.2f * k, sx + 0.55f * 6 * k, st + ht - 0.2f * k, EDGE2, 1); }
	for (int u = 0; u < 2; u++) { const float *q = u ? g->dn : g->up, d = u ? 1.0f : -1.0f;   /* (the scroll buttons) */
		br_fill (q[0], q[1], q[2], q[3], PAN, 1); br_rect (q[0], q[1], q[2], q[3], EDGE2, lw);
		const float mx = (q[0] + q[2]) / 2, w3 = (q[2] - q[0]) * 0.28f, h3 = (q[3] - q[1]) * 0.22f, my = (q[1] + q[3]) / 2, ay = my + d * h3, by = my - d * h3, w = fmaxf (1.0f, k * 0.42f);
		br_line (mx - w3, by, mx, ay, w, GRN); br_line (mx, ay, mx + w3, by, w, GRN); br_line (mx + w3, by, mx - w3, by, w, GRN); }
	{ const float *q = g->cl, m = (q[2] - q[0]) * 0.28f, w = fmaxf (1.0f, k * 0.42f);   /* (the close button) */
	  br_fill (q[0], q[1], q[2], q[3], PAN, 1); br_rect (q[0], q[1], q[2], q[3], EDGE2, lw);
	  br_line (q[0] + m, q[1] + m, q[2] - m, q[3] - m, w, GRN); br_line (q[2] - m, q[1] + m, q[0] + m, q[3] - m, w, GRN); }
	{ const float *q = g->plug, s = fminf ((q[2] - q[0]) / 160.0f, (q[3] - q[1]) / 100.0f), ox = (q[0] + q[2]) / 2 - 80 * s, oy = (q[1] + q[3]) / 2 + 50 * s;
	  const unsigned c = br_n ? HI : DIM; const float w = fmaxf (1.0f, k * 0.35f);   /* (LOAD: the slot and the cartridge, turned left) */
	  for (int L = 0; L < (int) (sizeof LOAD_LINES / sizeof LOAD_LINES[0]); L++) for (int q2 = 0; q2 + 1 < LOAD_LINES[L][1]; q2++) {
		const float *a = LOAD_PTS[LOAD_LINES[L][0] + q2], *b = LOAD_PTS[LOAD_LINES[L][0] + q2 + 1];
		br_line (ox + a[1] * s, oy - a[0] * s, ox + b[1] * s, oy - b[0] * s, w, c); }
	  br_text ("LOAD", q[0] - 5.5f * 6 * k, (q[1] + q[3]) / 2 - 3 * k, k, c); }
	{ char H[96]; const char *bn = optv ("vectrex_emu_btn_browser");   /* (the button as set in Controls - L3 by default; off: the X) */
	  snprintf (H, sizeof H, "STICK: CHOOSE / PAGE   BUTTON 4: OPEN/LOAD   BUTTON 1: BACK   %s: CLOSE", !bn[0] ? "L3" : !strcmp (bn, "off") ? "X" : bn);
	  const float kh = fminf (k * 0.55f, (g->x1 - g->x0 - 1.2f * rh) / (6.0f * (float) strlen (H)));   /* (it fits the panel) */
	  br_text (H, g->x0 + 0.6f * rh, g->y1 - 0.85f * rh, kh, DIM); }
	br_bx0 = (int) floorf (g->x0) - 2; br_by0 = (int) floorf (g->y0) - 2; br_bx1 = (int) ceilf (g->x1) + 2; br_by1 = (int) ceilf (g->y1) + 2; br_shown = 1;
}
/* the CPU's frame (XRGB): the panel over it */
static void br_blend_cpu (uint32_t *f, int pitch)
{
	if (!br_shown || !br_img) return;
	for (int y = br_by0 < 0 ? 0 : br_by0; y <= br_by1 && y < FH; y++) for (int x = br_bx0 < 0 ? 0 : br_bx0; x <= br_bx1 && x < FW; x++) {
		const uint32_t s = br_img[(size_t) y * FW + x]; const unsigned a = s >> 24; if (!a) continue;
		uint32_t *d = f + (size_t) y * pitch + x; const unsigned ia = 255 - a, dv = *d;
		const unsigned r = ((s >> 16) & 255) + (((dv >> 16) & 255) * ia + 127) / 255, g = ((s >> 8) & 255) + (((dv >> 8) & 255) * ia + 127) / 255, b = (s & 255) + ((dv & 255) * ia + 127) / 255;
		*d = (r > 255 ? 255u : r) << 16 | (g > 255 ? 255u : g) << 8 | (b > 255 ? 255u : b); }
}
/* each refresh, before the game's input: the browser's own (its button opens / closes it); 1: the game gets none */
static int br_input (const unsigned char *m)
{
	if (opt_ov_btn == 2) return 0;                      /* (EmuVR: SELECT is the overlay's) */
	if (net_on ()) { if (br_open) br_show (0); return 0; }   /* (netplay: another game on one side only - not here) */
	const int bb = btn_id ("vectrex_emu_btn_browser"), sel = bb >= 0 && input_cb (0, RETRO_DEVICE_JOYPAD, 0, (unsigned) bb) != 0;
	if (!br_open) br_prevtouch = input_cb (0, RETRO_DEVICE_POINTER, 0, RETRO_DEVICE_ID_POINTER_PRESSED) != 0;   /* (a touch that opened it - the deck's
	                                                    cartridge - is not a tap in it: it closed again at once) */   /* (Ports: L3 by default) */
	static int sel_was;
	if (sel && !sel_was) br_show (!br_open);
	sel_was = sel;
	if (!br_open) {
		if (br_hold) {                                  /* (closed by a button: the game gets the buttons and the stick only once let go) */
			const int any = (m[2] & 15) || abs ((int) m[0] - 128) >= 64 || abs ((int) m[1] - 128) >= 64;
			if (!any || vt_wall () - br_hold_t > 1.5) br_hold = 0;
			return br_hold;
		}
		return 0;
	}
	/* player 1 as the game reads it (pad: the stick, the d-pad, the right stick; turned with the picture in tate):
	 * up / down chooses, left / right turns a page, button 4 opens or loads, button 1 goes back */
	const unsigned b = m[2]; const int jx = (int) m[0] - 128, jy = (int) m[1] - 128;
	int dy = jy > 64 ? -1 : jy < -64 ? 1 : 0, dx = jx > 64 ? 1 : jx < -64 ? -1 : 0;
	if (dx && dy) { if (abs (jy) >= abs (jx)) dx = 0; else dy = 0; }
	const int d = dy ? dy : dx * 100; const double now = vt_wall ();
	if (!br_prev_ok) { br_prevb = b; br_dprev = d; br_t_next = now + 0.8; br_prev_ok = 1; }   /* (held when it opened: no step yet) */
	if (d) { int step = 0;
		if (d != br_dprev) { br_dprev = d; br_t_next = now + 0.40; step = 1; }
		else if (now >= br_t_next) { br_t_next = now + (dy ? 0.08 : 0.25); step = 1; }
		if (step) br_move (dy ? dy : dx * BR_NVIS); }
	else br_dprev = 0;
	const unsigned nw = b & ~br_prevb; br_prevb = b;
	if (nw & 8) br_activate ();                           /* (button 4) */
	else if (nw & 1) br_back ();                          /* (button 1) */
	/* touch: a row, the buttons; outside: closed */
	const int tp = input_cb (0, RETRO_DEVICE_POINTER, 0, RETRO_DEVICE_ID_POINTER_PRESSED) != 0;
	static double br_t_tap = -1;
	if (tp && !br_prevtouch && br_open) br_ate = 1;
	if (tp && !br_prevtouch && br_open && vt_wall () - br_t_tap >= 0.25 && vt_wall () - br_t_toggle >= 0.3) {   /* (debounced: a touch screen's
		                                                    bounce is no second tap - none within 0.25 s, none 0.3 s after it opened) */
		br_t_tap = vt_wall ();
		double fx = (input_cb (0, RETRO_DEVICE_POINTER, 0, RETRO_DEVICE_ID_POINTER_X) + 32768) / 65536.0, fy = (input_cb (0, RETRO_DEVICE_POINTER, 0, RETRO_DEVICE_ID_POINTER_Y) + 32768) / 65536.0;
		{ const double ax_ = fx, ay_ = fy;     /* (the picture turned: back to its own way up, as the light pen) */
		  if (opt_rot == 1) { fx = 1 - ay_; fy = ax_; } else if (opt_rot == 2) { fx = 1 - ax_; fy = 1 - ay_; } else if (opt_rot == 3) { fx = ay_; fy = 1 - ax_; } }
		const float x = (float) (CX0 + fx * TW - BX), y = (float) (CY0 + fy * TH - BY); const br_geo *g = &br_g;   /* (the deck's block: deck.c) */
		#define IN(q) (x >= (q)[0] && x <= (q)[2] && y >= (q)[1] && y <= (q)[3])
		if (x < g->x0 || x > g->x1 || y < g->y0 || y > g->y1 || IN (g->cl)) br_show (0);
		else if (IN (g->up)) br_move (-BR_NVIS);
		else if (IN (g->dn)) br_move (BR_NVIS);
		else if (IN (g->plug)) br_activate ();
		else if (IN (g->lst)) { const int j = br_top + (int) ((y - g->lst[1]) / g->rh);
			if (j < br_n) { if (j == br_sel) br_activate (); else { br_sel = j; br_dirty = 1; } } }
		#undef IN
	}
	br_prevtouch = tp;
	return 1;
}
