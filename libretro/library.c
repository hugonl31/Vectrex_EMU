/* library.c - the game library over the picture, as the phone apps' and the PC's (src/frontend/library.py): the tiles' look
 * (cartridges, boxes, overlays), 2 - 6 a row or titles only, the categories (All, Recent, Games, UVMC2, PiTrex, Tests);
 * it fills the picture. The pictures: system/vectrex_emu/thumbs/{carts,boxes,overlays}/<game>.png, else a cartridge with
 * the player's own label from thumbs/labels/<game>.png (genart.c), the PiTrex's own from pitrex_tiles.h; a game without one gets a cartridge / box drawn for it (genart.c: its title in the blue pill), an overlay its name. Included by browser.c (its image, its drawing, its input); the setting
 * Video > Game list chooses it or the simple list.
 *
 * The controller: the stick or the d-pad moves the choice - up from the top row into the head (categories, then the looks and
 * the tiles a row), button 4 plays (in the head: sets), button 1 closes, L / R turn a page. Touch: a tap, a drag scrolls.
 * Buttons 2 and 3 are free: held while button 4 starts a UVMC2 game they reach it (its calibration screen, as on the cartridge).
 * A UVMC2 game's choices - button 4 held on it, or a finger held on it: Play, Calibration (2 + 3 held: the game started with
 * buttons 2 + 3 held for 30 s - with the intro on they must still be held when it ends), as the apps' long press. */
#include "pitrex_tiles.h"

typedef struct { char name[200]; char path[1100]; char kind; char tests, boot; } lib_ent;   /* (kind: r ROM, m UVMC2, p PiTrex program, x the PiTrex) */
static lib_ent *lib_all; static int lib_n, lib_cap, *lib_show, lib_ns;
static int lib_on = 1;                             /* (Video > Game list: 1 the library, 0 the simple list) */
static int lib_look = 1, lib_cols = 4, lib_cat, lib_sel, lib_focus = 2, lib_hidx, lib_loaded;   /* (focus: 0 looks / per row, 1 categories, 2 the tiles) */
static float lib_scroll; static double lib_drag_y = -1, lib_drag_s; static int lib_dragged;
static int lib_menu, lib_msel, lib_mskip, lib_press_i = -1;   /* (a UVMC2 game's choices open; the row chosen; the finger that opened it; the tile a finger is on) */
static double lib_b4_t = -1, lib_press_t;          /* (button 4 down on a UVMC2 game since; the finger down since) */
static double lib_cal_until;                       /* (Calibration (2 + 3 held): player 1's buttons 2 + 3 held until then - libretro_vx.c) */
static float lib_mbox[4], lib_mrow[2][4];          /* (the choices' panel and its rows, for touch) */
static const char *const LIB_LOOK[3] = { "carts", "boxes", "overlays" }, *const LIB_LOOKN[3] = { "CARTRIDGES", "BOXES", "OVERLAYS" };
static const char *const LIB_CAT[6] = { "ALL", "RECENT", "GAMES", "UVMC2", "PITREX", "TESTS" };
static const int LIB_COLS[6] = { 0, 2, 3, 4, 5, 6 };
static const float LIB_ASPECT[3] = { 0.96f, 1.39f, 207.0f / 162.0f };

static void lib_file (char *o, size_t n)
{
	const char *sv = NULL; env_cb (RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY, &sv);
	if (sv && *sv) snprintf (o, n, "%s" SEP "vectrex_emu_library.txt", sv); else o[0] = 0;
}
static void lib_save (void) { char fn[1200]; lib_file (fn, sizeof fn); FILE *f = fn[0] ? fopen (fn, "wb") : NULL; if (f) { fprintf (f, "%d %d %d\n", lib_look, lib_cols, lib_cat); fclose (f); } }
static void lib_load (void)
{
	if (lib_loaded) return; lib_loaded = 1;
	char fn[1200]; lib_file (fn, sizeof fn); FILE *f = fn[0] ? fopen (fn, "rb") : NULL; int a, b, c;
	if (f) { if (fscanf (f, "%d %d %d", &a, &b, &c) == 3 && a >= 0 && a < 3 && b >= 0 && b <= 6 && b != 1 && c >= 0 && c < 6) { lib_look = a; lib_cols = b; lib_cat = c; } fclose (f); }
}
static void lib_add (const char *name, const char *path, char kind, int tests, int boot)
{
	if (lib_n == lib_cap) { lib_cap = lib_cap ? lib_cap * 2 : 256; lib_all = realloc (lib_all, sizeof (lib_ent) * (size_t) lib_cap); lib_show = realloc (lib_show, sizeof (int) * (size_t) lib_cap); }
	if (!lib_all || !lib_show) { lib_n = lib_cap = 0; return; }
	lib_ent *e = &lib_all[lib_n++]; snprintf (e->name, sizeof e->name, "%s", name); snprintf (e->path, sizeof e->path, "%s", path); e->kind = kind; e->tests = (char) tests; e->boot = (char) boot;
}
static void lib_walk (const char *d, int depth, int tests)
{
	if (depth > 4) return;
#ifdef _WIN32
	char pat[1200]; snprintf (pat, sizeof pat, "%s\\*", d); WIN32_FIND_DATAA fd; HANDLE h = FindFirstFileA (pat, &fd);
	if (h == INVALID_HANDLE_VALUE) return;
	do { const char *f = fd.cFileName; if (f[0] == '.') continue;
		char p[1200]; snprintf (p, sizeof p, "%s\\%s", d, f);
		if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) { if (vx_stricmp (f, "overlays")) lib_walk (p, depth + 1, tests || (!depth && !vx_stricmp (f, "Tests"))); }
		else { char k = br_kind (f); if (k) { char nm[200]; snprintf (nm, sizeof nm, "%s", f); char *x = strrchr (nm, '.'); if (x) *x = 0; lib_add (nm, p, k, tests, 0); } }
	} while (FindNextFileA (h, &fd)); FindClose (h);
#else
	DIR *dd = opendir (d); if (!dd) return;
	struct dirent *de; while ((de = readdir (dd))) { const char *f = de->d_name; if (f[0] == '.') continue;
		char p[1200]; snprintf (p, sizeof p, "%s%s%s", d, d[strlen (d) - 1] == '/' ? "" : "/", f); struct stat st;
		if (stat (p, &st)) continue;
		if (S_ISDIR (st.st_mode)) { if (vx_stricmp (f, "overlays")) lib_walk (p, depth + 1, tests || (!depth && !vx_stricmp (f, "Tests"))); }
		else { char k = br_kind (f); if (k) { char nm[200]; snprintf (nm, sizeof nm, "%s", f); char *x = strrchr (nm, '.'); if (x) *x = 0; lib_add (nm, p, k, tests, 0); } } }
	closedir (dd);
#endif
}
static int lib_cmp (const void *a, const void *b) { const lib_ent *x = a, *y = b; if (x->boot != y->boot) return y->boot - x->boot; return vx_stricmp (x->name, y->name); }
static void lib_filter (void)
{
	lib_ns = 0;
	if (lib_cat == 1) { br_rec_load ();              /* (Recent: the last games played, the newest first) */
		for (int r = 0; r < br_nrec; r++) for (int i = 0; i < lib_n; i++) if (!strcmp (lib_all[i].path, br_rec[r])) { lib_show[lib_ns++] = i; break; } }
	else for (int i = 0; i < lib_n; i++) { const lib_ent *e = &lib_all[i];
		const int ok = lib_cat == 0 ? !e->tests : lib_cat == 2 ? e->kind == 'r' && !e->tests : lib_cat == 3 ? e->kind == 'm' : lib_cat == 4 ? e->kind == 'p' || e->kind == 'x' : e->tests;
		if (ok) lib_show[lib_ns++] = i; }
	if (lib_sel > lib_ns - 1) lib_sel = lib_ns > 0 ? lib_ns - 1 : 0;
}
static void lib_scan (const char *select)
{
	lib_n = 0; lib_load ();
	if (br_root[0]) lib_walk (br_root, 0, 0);
	{ char px[1200]; br_px_boot (px, sizeof px);     /* (the PiTrex itself: first; none in the SD card folder: the one in the games folder; not on this device: fetched - pxfetch.c) */
	  if (!br_exists (px) && br_root[0]) snprintf (px, sizeof px, "%s" SEP "pitrex.img", br_root);
	  lib_add ("PiTrex", br_exists (px) ? px : ":getpitrex", 'x', 0, 1); }
	if (lib_n > 1) qsort (lib_all, (size_t) lib_n, sizeof (lib_ent), lib_cmp);
	lib_filter ();
	if (select) for (int i = 0; i < lib_ns; i++) if (!strcmp (lib_all[lib_show[i]].path, select)) lib_sel = i;
	lib_focus = 2; lib_menu = 0; lib_b4_t = -1; lib_press_i = -1;
}

/* ---- the pictures: read on demand (one a refresh), kept at the tile's size, 30 % less bright (as every library) */
typedef struct { int ent, w, h; uint32_t *px; } lib_pic;
#define LIB_PICS 160
static lib_pic lib_pc[LIB_PICS]; static int lib_npc, lib_pc_look = -1, lib_pc_tw, lib_missing_n; static int *lib_missing;
static void lib_pics_clear (void) { for (int i = 0; i < lib_npc; i++) free (lib_pc[i].px); lib_npc = 0; free (lib_missing); lib_missing = NULL; lib_missing_n = 0; }
static lib_pic *lib_pic_of (int ent) { for (int i = 0; i < lib_npc; i++) if (lib_pc[i].ent == ent) return &lib_pc[i]; return NULL; }
static int lib_is_missing (int ent) { for (int i = 0; i < lib_missing_n; i++) if (lib_missing[i] == ent) return 1; return 0; }
static void lib_pic_load (int ent, int tw, int th)
{
	const lib_ent *e = &lib_all[ent]; int w = 0, h = 0, n; unsigned char *im = NULL; const char *sys = NULL; env_cb (RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY, &sys);
	char base[200]; snprintf (base, sizeof base, "%s", e->name); { char *q = strstr (base, " ("); if (q) *q = 0; }
	for (int t = 0; t < 2 && !im && sys; t++) { char p[1400]; snprintf (p, sizeof p, "%s" SEP "vectrex_emu" SEP "thumbs" SEP "%s" SEP "%s.png", sys, LIB_LOOK[lib_look], t ? base : e->name);
		im = stbi_load (p, &w, &h, &n, 4); }
	int gen = 0;
	for (int t = 0; t < 2 && !im && sys && lib_look == 0; t++) { char p[1400];   /* (no cartridge picture: the player's own label on our cartridge - genart.c) */
		snprintf (p, sizeof p, "%s" SEP "vectrex_emu" SEP "thumbs" SEP "labels" SEP "%s.png", sys, t ? base : e->name);
		im = vx_genart_label_w (p, vx_genart_size (tw), &w, &h); gen = im != NULL; }
	if (!im && e->boot)                               /* (the PiTrex: its own cartridge and box, the program's) */
		im = lib_look == 1 ? stbi_load_from_memory (VX_PITREX_BOX, (int) sizeof VX_PITREX_BOX, &w, &h, &n, 4) : stbi_load_from_memory (VX_PITREX_CART, (int) sizeof VX_PITREX_CART, &w, &h, &n, 4);
	if (!im && lib_look != 2) { im = vx_genart_make_w (lib_look, e->name, vx_genart_size (tw), &w, &h); gen = im != NULL; }   /* (none of its own: drawn - genart.c, as every platform; at about the tile's width) */
	if (!im || w < 2 || h < 2) { if (im) { if (gen) vx_genart_free (im); else stbi_image_free (im); }
		lib_missing = realloc (lib_missing, sizeof (int) * (size_t) (lib_missing_n + 1)); if (lib_missing) lib_missing[lib_missing_n++] = ent; return; }
	if (lib_npc == LIB_PICS) lib_pics_clear ();
	float sc = fminf (tw / (float) w, th / (float) h); int dw = (int) (w * sc + 0.5f), dh = (int) (h * sc + 0.5f); if (dw < 1) dw = 1; if (dh < 1) dh = 1;
	uint32_t *px = malloc ((size_t) dw * dh * 4); if (!px) { if (gen) vx_genart_free (im); else stbi_image_free (im); return; }
	for (int y = 0; y < dh; y++) for (int x = 0; x < dw; x++) {   /* (each tile pixel: the mean of the picture's pixels under it) */
		int sx0 = x * w / dw, sx1 = (x + 1) * w / dw, sy0 = y * h / dh, sy1 = (y + 1) * h / dh; if (sx1 <= sx0) sx1 = sx0 + 1; if (sy1 <= sy0) sy1 = sy0 + 1;
		unsigned r = 0, g = 0, b = 0, a = 0, cnt = 0;
		for (int sy = sy0; sy < sy1; sy++) for (int sx = sx0; sx < sx1; sx++) { const unsigned char *q = im + ((size_t) sy * w + sx) * 4; r += q[0] * q[3]; g += q[1] * q[3]; b += q[2] * q[3]; a += q[3]; cnt++; }
		const unsigned A = a / cnt; r = a ? (unsigned) (r / (float) a * 0.7f) : 0; g = a ? (unsigned) (g / (float) a * 0.7f) : 0; b = a ? (unsigned) (b / (float) a * 0.7f) : 0;
		px[(size_t) y * dw + x] = A << 24 | r << 16 | g << 8 | b; }
	if (gen) vx_genart_free (im); else stbi_image_free (im);
	lib_pic *p = &lib_pc[lib_npc++]; p->ent = ent; p->w = dw; p->h = dh; p->px = px;
}
static void lib_blit (const lib_pic *p, float fx, float fy, int cy0, int cy1)
{
	const int x0 = (int) (fx + 0.5f), y0 = (int) (fy + 0.5f);
	for (int y = 0; y < p->h; y++) { const int Y = y0 + y; if (Y < cy0 || Y >= cy1 || Y < 0 || Y >= dr_h) continue;
		for (int x = 0; x < p->w; x++) { const int X = x0 + x; if (X < 0 || X >= dr_w) continue;
			const uint32_t s = p->px[(size_t) y * p->w + x]; const unsigned a = s >> 24; if (!a) continue;
			uint32_t *d = dr_img + (size_t) Y * dr_w + X; const unsigned ia = 255 - a, dv = *d;
			const unsigned r = (((s >> 16) & 255) * a + ((dv >> 16) & 255) * ia) / 255, g = (((s >> 8) & 255) * a + ((dv >> 8) & 255) * ia) / 255, b = ((s & 255) * a + (dv & 255) * ia) / 255;
			unsigned A = a + (dv >> 24) * ia / 255; if (A > 255) A = 255;
			*d = A << 24 | r << 16 | g << 8 | b; } }
}

/* ---- the layout (the sheet's pixels), as library.py geom */
typedef struct { float x0, y0, x1, y1, u, k, close[4], look[3][4], col[6][4], cat[6][4], per[2], help[2], title[2], head, grid[4], tw, th, gap, rowh; } lib_geo;
static lib_geo lib_g;
static void lib_geom (void)
{
	lib_geo g; g.x0 = (float) (CX0 + 2); g.x1 = (float) (CX0 + CW - 3); g.y0 = (float) (CY0 + 2); g.y1 = (float) (CY0 + CH - 3);
	const float W = g.x1 - g.x0, u = W / 60.0f, k = 0.15f * u, m = 1.2f * u; g.u = u; g.k = k;
	g.close[0] = g.x1 - m - 2.6f * u; g.close[1] = g.y0 + 0.7f * u; g.close[2] = g.x1 - m; g.close[3] = g.y0 + 3.3f * u;
	g.title[0] = g.x0 + m; g.title[1] = g.y0 + 1.25f * u;
	float y = g.y0 + 4.2f * u, x = g.x0 + m;
	for (int i = 0; i < 3; i++) { g.look[i][0] = x; g.look[i][1] = y; g.look[i][2] = x + 8.6f * u; g.look[i][3] = y + 2.6f * u; x += 8.9f * u; }
	x += 0.9f * u; g.per[0] = x; g.per[1] = y + 0.85f * u; x += 5.6f * u;
	for (int i = 0; i < 6; i++) { const float w = i ? 3.0f * u : 4.0f * u; g.col[i][0] = x; g.col[i][1] = y; g.col[i][2] = x + w; g.col[i][3] = y + 2.6f * u; x += w + 0.3f * u; }
	y += 3.4f * u; x = g.x0 + m;
	for (int i = 0; i < 6; i++) { const float w = (float) (strlen (LIB_CAT[i]) + 3) * 6 * k; g.cat[i][0] = x; g.cat[i][1] = y; g.cat[i][2] = x + w; g.cat[i][3] = y + 2.6f * u; x += w + 0.6f * u; }
	g.help[0] = g.x0 + m; g.help[1] = y + 3.3f * u; g.head = y + 4.9f * u;
	g.grid[0] = g.x0 + m; g.grid[1] = g.head + 0.4f * u; g.grid[2] = g.x1 - m; g.grid[3] = g.y1 - 0.4f * u;
	if (lib_cols) { g.gap = 1.0f * u; g.tw = (g.grid[2] - g.grid[0] - g.gap * (lib_cols - 1)) / lib_cols; g.th = g.tw * LIB_ASPECT[lib_look]; g.rowh = g.th + 2.6f * u; }
	else { g.gap = 0; g.tw = g.grid[2] - g.grid[0]; g.th = 0; g.rowh = 2.4f * u; }
	lib_g = g;
}
static int lib_rows (void) { const int c = lib_cols ? lib_cols : 1; return (lib_ns + c - 1) / c; }
static void lib_clamp (void)
{
	const float vis = lib_g.grid[3] - lib_g.grid[1], tot = lib_rows () * lib_g.rowh, mx = tot > vis ? tot - vis : 0;
	if (lib_scroll > mx) lib_scroll = mx; if (lib_scroll < 0) lib_scroll = 0;
}
static void lib_view (void)                        /* (the choice kept in view) */
{
	lib_geom (); const int c = lib_cols ? lib_cols : 1, r = lib_sel / c; const float top = r * lib_g.rowh, bot = top + lib_g.rowh, vis = lib_g.grid[3] - lib_g.grid[1];
	if (top < lib_scroll) lib_scroll = top; if (bot > lib_scroll + vis) lib_scroll = bot - vis;
	lib_clamp ();
}
static void lib_button (const float *q, const char *word, int on, int focus, float kk)
{
	const unsigned HI = 0x88ccbb, HI2 = 0xbbffee, BODY = 0x0f1c17, SEL = 0x24443a, EDGE2 = 0x2f5544;
	br_fill (q[0], q[1], q[2], q[3], on ? SEL : BODY, 1); br_rect (q[0], q[1], q[2], q[3], focus ? HI2 : EDGE2, focus ? fmaxf (2.0f, kk * 0.6f) : 1.0f);
	br_text (word, (q[0] + q[2]) / 2 - (float) strlen (word) * 3.0f * kk, (q[1] + q[3]) / 2 - 3.0f * kk, kk, on ? HI2 : HI);
}
static const char *lib_name (const lib_ent *e)      /* (the PiTrex not here yet: getting it, how far) */
{
	static char t[64]; if (strcmp (e->path, ":getpitrex")) return e->name;
	snprintf (t, sizeof t, px_st == 1 ? "PiTrex: getting it %d%%" : px_st == 2 ? "PiTrex: unpacking %d%%" : px_st == 4 ? "PiTrex: failed, again?" : "PiTrex: get it (190 MB)", px_pct); return t;
}
static int lib_want = -1;                          /* (a picture to read at the next refresh) */
static void lib_draw (void)
{
	lib_geom (); lib_clamp (); const lib_geo *g = &lib_g; const float u = g->u, k = g->k;
	const unsigned HI = 0x88ccbb, HI2 = 0xbbffee, DIM = 0x7f978b, FG = 0xc8d6cf, BG = 0x05080a, TOP = 0x0a1210, PAN = 0x0b1511, SEL = 0x24443a, EDGE = 0x1d3329, EDGE2 = 0x2f5544,
	               UM = 0x66a4b8, UMH = 0x8ac8dc, PX = 0xaa9c88, PXH = 0xd0c0aa;
	if (lib_pc_look != lib_look || lib_pc_tw != (int) g->tw) { lib_pics_clear (); lib_pc_look = lib_look; lib_pc_tw = (int) g->tw; }
	br_fill (g->x0, g->y0, g->x1, g->y1, BG, 1); br_fill (g->x0, g->y0, g->x1, g->head, TOP, 1); br_line (g->x0, g->head, g->x1, g->head, 1, EDGE);
	{ char t[64]; snprintf (t, sizeof t, "GAMES  (%d)", lib_ns); br_text (t, g->title[0], g->title[1], 1.25f * k, HI); }
	lib_button (g->close, "X", 0, 0, k);
	for (int i = 0; i < 3; i++) lib_button (g->look[i], LIB_LOOKN[i], i == lib_look, lib_focus == 0 && lib_hidx == i, 0.85f * k);
	br_text ("PER ROW", g->per[0], g->per[1], 0.8f * k, DIM);
	for (int i = 0; i < 6; i++) { char w[8]; if (i) snprintf (w, sizeof w, "%d", LIB_COLS[i]); else snprintf (w, sizeof w, "TEXT"); lib_button (g->col[i], w, LIB_COLS[i] == lib_cols, lib_focus == 0 && lib_hidx == 3 + i, 0.8f * k); }
	for (int i = 0; i < 6; i++) lib_button (g->cat[i], LIB_CAT[i], i == lib_cat, lib_focus == 1 && lib_hidx == i, 0.85f * k);
	{ const int um = lib_focus == 2 && lib_ns && lib_all[lib_show[lib_sel]].kind == 'm';   /* (a UVMC2 game chosen: its choices too) */
	  const char *h = lib_menu ? "STICK: CHOOSE   BUTTON 4: OK   BUTTON 1: BACK"
	                : um ? "STICK: CHOOSE (UP: LOOK, ROW, CATEGORY)   BUTTON 4: PLAY (HOLD IT: CALIBRATION)   BUTTON 1: CLOSE   L / R: PAGE"
	                : "STICK: CHOOSE (UP: LOOK, ROW, CATEGORY)   BUTTON 4: PLAY   BUTTON 1: CLOSE   L / R: PAGE";
	  br_text (h, g->help[0], g->help[1], fminf (0.6f * k, (g->x1 - g->x0 - 2.4f * u) / (6.0f * (float) (strlen (h) > 92 ? strlen (h) : 92))), DIM); }
	const int c = lib_cols ? lib_cols : 1, cy0 = (int) g->grid[1], cy1 = (int) g->grid[3];
	if (!lib_ns) br_text (lib_n ? "NOTHING HERE." : "NO GAMES FOUND IN THE GAMES FOLDER.", g->grid[0] + u, g->grid[1] + u, k, DIM);
	dr_cy0 = cy0; dr_cy1 = cy1;
	const int r0 = (int) (lib_scroll / g->rowh), r1 = (int) ((lib_scroll + (g->grid[3] - g->grid[1])) / g->rowh) + 1; lib_want = -1;
	for (int i = r0 * c; i < lib_ns && i < r1 * c; i++) {
		const int r = i / c, col = i % c, ent = lib_show[i], sel = i == lib_sel && lib_focus == 2; const lib_ent *e = &lib_all[ent];
		const float tx = g->grid[0] + col * (g->tw + g->gap), ty = g->grid[1] + r * g->rowh - lib_scroll;
		if (ty + g->rowh <= g->grid[1] || ty >= g->grid[3]) continue;
		if (!lib_cols) {                                /* (titles only: UVMC2 games and PiTrex programs tinted, the PiTrex with its raspberry - as the simple list) */
			if (sel) br_fill (tx, ty, tx + g->tw, ty + g->rowh - 2, SEL, 1);
			const unsigned cc = e->boot ? (sel ? HI2 : HI) : e->kind == 'm' ? (sel ? UMH : UM) : e->kind == 'p' ? (sel ? PXH : PX) : sel ? HI2 : FG;
			const float ty_ = ty + (g->rowh - 6 * k) / 2 - 1, ind = e->boot ? 2.4f * 6 * k : 0; char nm[200]; const int room = (int) (g->tw / (6 * k)) - 5;
			snprintf (nm, sizeof nm, "%.*s", room > 3 ? room : 3, lib_name (e)); br_text (nm, tx + 0.8f * u + ind, ty_, k, cc);
			if (e->boot) { const float ix = tx + 0.8f * u, iy = ty_, w = fmaxf (1.0f, k * 0.42f);
				#define P(a, b) ix + (a) * k, iy + (b) * k
				for (int q = 0; q < 14; q++) { const float a0 = 6.2831853f * q / 14, a1 = 6.2831853f * (q + 1) / 14;
					br_line (P (5 + 2.6f * cosf (a0), 3.6f + 2.4f * sinf (a0)), P (5 + 2.6f * cosf (a1), 3.6f + 2.4f * sinf (a1)), w, cc); }
				static const float L[6][4] = { { 5, 1.2f, 2.8f, -0.2f }, { 5, 1.2f, 7.2f, -0.2f }, { 2.8f, -0.2f, 4.2f, 0.6f }, { 7.2f, -0.2f, 5.8f, 0.6f }, { 3.6f, 3.4f, 6.4f, 3.4f }, { 5, 1.6f, 5, 5.6f } };
				for (int q = 0; q < 6; q++) br_line (P (L[q][0], L[q][1]), P (L[q][2], L[q][3]), w, cc);
				#undef P
			}
			continue; }
		const lib_pic *p = lib_pic_of (ent);
		if (p) lib_blit (p, tx + (g->tw - p->w) / 2, ty + (g->th - p->h) / 2, cy0, cy1);
		else { br_fill (tx, ty, tx + g->tw, ty + g->th, PAN, 1); br_rect (tx, ty, tx + g->tw, ty + g->th, EDGE2, 1);
			if (!lib_is_missing (ent)) { if (lib_want < 0) lib_want = ent; }
			const float kk = fminf (k, g->tw / (6.0f * 12)); const int per = (int) (g->tw / (6 * kk)) - 2; char ln[4][64]; int nl = 0; ln[0][0] = 0;
			char nm[200]; snprintf (nm, sizeof nm, "%s", lib_name (e));
			for (char *w = strtok (nm, " "); w && nl < 4; w = strtok (NULL, " ")) {
				if (ln[nl][0] && (int) (strlen (ln[nl]) + 1 + strlen (w)) > per) { if (++nl >= 4) break; ln[nl][0] = 0; }
				if (ln[nl][0]) strncat (ln[nl], " ", sizeof ln[0] - strlen (ln[nl]) - 1); strncat (ln[nl], w, sizeof ln[0] - strlen (ln[nl]) - 1); if ((int) strlen (ln[nl]) > per && per > 0) ln[nl][per] = 0; }
			if (nl < 4 && ln[nl][0]) nl++;
			for (int q = 0; q < nl; q++) br_text (ln[q], tx + g->tw / 2 - (float) strlen (ln[q]) * 3.0f * kk, ty + g->th / 2 + (q - (nl - 1) / 2.0f) * 9 * kk - 3 * kk, kk, HI); }
		if (sel) br_rect (tx - 0.2f * u, ty - 0.2f * u, tx + g->tw + 0.2f * u, ty + g->th + 0.2f * u, HI2, fmaxf (2.0f, 0.22f * u));
		{ const float kn = fminf (0.8f * k, g->tw / (6.0f * 16)); char nm[200]; const int room = (int) (g->tw / (6 * kn));
		  snprintf (nm, sizeof nm, "%.*s", room > 3 ? room : 3, lib_name (e)); br_text (nm, tx + g->tw / 2 - (float) strlen (nm) * 3.0f * kn, ty + g->th + 1.2f * u - 3 * kn, kn, sel ? HI2 : FG); } }
	dr_cy0 = 0; dr_cy1 = 1 << 30;
	{ const float vis = g->grid[3] - g->grid[1], tot = lib_rows () * g->rowh;   /* (how far down: a bar at the right edge) */
	  if (tot > vis + 1) { const float bh = vis * vis / tot, by = g->grid[1] + (vis - bh) * lib_scroll / (tot - vis); br_fill (g->x1 - 0.5f * u, by, g->x1 - 0.2f * u, by + bh, EDGE2, 1); } }
	if (lib_menu && lib_ns) {                          /* (a UVMC2 game's choices: a panel in the middle of the tiles) */
		const float cx = (g->grid[0] + g->grid[2]) / 2, cy = (g->grid[1] + g->grid[3]) / 2, w = fminf (g->grid[2] - g->grid[0], 30.0f * u), h = 11.6f * u;
		float *b = lib_mbox; b[0] = cx - w / 2; b[1] = cy - h / 2; b[2] = cx + w / 2; b[3] = cy + h / 2;
		br_fill (b[0], b[1], b[2], b[3], PAN, 1); br_rect (b[0], b[1], b[2], b[3], EDGE2, fmaxf (1.0f, 0.15f * u));
		char nm[200]; const int room = (int) ((w - 2.4f * u) / (6 * k)); snprintf (nm, sizeof nm, "%.*s", room > 3 ? room : 3, lib_all[lib_show[lib_sel]].name);
		br_text (nm, b[0] + 1.2f * u, b[1] + 1.2f * u, k, HI);
		static const char *const MW[2] = { "PLAY", "CALIBRATION (2 + 3 HELD)" };
		for (int i = 0; i < 2; i++) { float *q = lib_mrow[i]; q[0] = b[0] + 1.2f * u; q[2] = b[2] - 1.2f * u; q[1] = b[1] + 3.6f * u + i * 3.6f * u; q[3] = q[1] + 3.0f * u;
			const float kk = fminf (0.9f * k, (q[2] - q[0] - 1.6f * u) / (6.0f * (float) strlen (MW[i])));
			br_fill (q[0], q[1], q[2], q[3], i == lib_msel ? SEL : 0x0f1c17, 1); br_rect (q[0], q[1], q[2], q[3], i == lib_msel ? HI2 : EDGE2, i == lib_msel ? fmaxf (2.0f, 0.5f * kk) : 1.0f);
			br_text (MW[i], q[0] + 0.8f * u, (q[1] + q[3]) / 2 - 3.0f * kk, kk, i == lib_msel ? HI2 : FG); } }
	br_rect (g->x0, g->y0, g->x1, g->y1, EDGE, 1);
	br_bx0 = (int) floorf (g->x0) - 2; br_by0 = (int) floorf (g->y0) - 2; br_bx1 = (int) ceilf (g->x1) + 2; br_by1 = (int) ceilf (g->y1) + 2; br_shown = 1;
}
/* each refresh while it is open: a picture read (the list drawn again with it) */
static void lib_tick (void)
{
	if (lib_want >= 0 && lib_want < lib_n) { const int e = lib_want; lib_want = -1; lib_pic_load (e, (int) lib_g.tw, (int) lib_g.th); br_dirty = 1; }
}
static void lib_set (int row, int idx)             /* (a head button: a look, the tiles a row, a category) */
{
	if (row == 0 && idx < 3) lib_look = idx; else if (row == 0) lib_cols = LIB_COLS[idx - 3]; else { lib_cat = idx; lib_sel = 0; lib_scroll = 0; lib_filter (); }
	lib_save (); lib_view (); br_dirty = 1;
}
static void lib_play (void)
{
	if (!lib_ns) return;
	char p[1100]; snprintf (p, sizeof p, "%s", lib_all[lib_show[lib_sel]].path);
	if (!strcmp (p, ":getpitrex")) { const char *sys = NULL; env_cb (RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY, &sys); px_start (sys); br_dirty = 1; return; }
	br_from[0] = 0; br_show (0); br_hold = 1; br_hold_t = vt_wall ();
	game_switch (p);
}
static void lib_menu_open (int touch)              /* (a UVMC2 game's choices: Play chosen) */
{
	lib_menu = 1; lib_msel = 0; lib_mskip = touch; lib_b4_t = -1; lib_press_i = -1; br_dirty = 1;
	fprintf (stderr, "[vectrex_emu] library: %s - its choices\n", lib_all[lib_show[lib_sel]].name);
}
static void lib_menu_pick (int i)
{
	lib_menu = 0; br_dirty = 1;
	if (i == 1) { lib_cal_until = vt_wall () + 30.0; fprintf (stderr, "[vectrex_emu] library: %s with buttons 2 + 3 held (calibration)\n", lib_all[lib_show[lib_sel]].name); }
	lib_play ();
}
static void lib_move (int dx, int dy)
{
	const int c = lib_cols ? lib_cols : 1;
	if (lib_focus == 2) {
		if (dy < 0 && lib_sel < c) { lib_focus = 1; lib_hidx = lib_cat; br_dirty = 1; return; }   /* (up from the top row: the categories) */
		if (!lib_ns) return;
		int s = lib_sel + dx + dy * c; if (s < 0) s = 0; if (s > lib_ns - 1) s = lib_ns - 1; lib_sel = s; lib_view ();
	} else if (lib_focus == 1) {
		if (dy > 0) lib_focus = 2; else if (dy < 0) { lib_focus = 0; lib_hidx = lib_look; }
		else { lib_hidx += dx; if (lib_hidx < 0) lib_hidx = 0; if (lib_hidx > 5) lib_hidx = 5; }
	} else {
		if (dy > 0) { lib_focus = 1; lib_hidx = lib_cat; }
		else { lib_hidx += dx; if (lib_hidx < 0) lib_hidx = 0; if (lib_hidx > 8) lib_hidx = 8; }
	}
	br_dirty = 1;
}
static void lib_page (int d)
{
	lib_geom (); const int c = lib_cols ? lib_cols : 1; int rows = (int) ((lib_g.grid[3] - lib_g.grid[1]) / lib_g.rowh); if (rows < 1) rows = 1;
	if (lib_focus != 2) { lib_focus = 2; br_dirty = 1; return; }
	int s = lib_sel + d * rows * c; if (s < 0) s = 0; if (s > lib_ns - 1) s = lib_ns - 1; if (s < 0) s = 0; lib_sel = s; lib_view (); br_dirty = 1;
}
/* the library's input, once a refresh while it is open (from br_input: b the buttons held, nw the buttons just pressed, dx / dy the
 * stick's step - step 1: that call is the step's alone) */
static void lib_input (unsigned b, unsigned nw, int dx, int dy, int step)
{
	if (step) { if (lib_menu) { if (dy) { lib_msel = dy < 0 ? 0 : 1; br_dirty = 1; } } else if (lib_b4_t < 0) lib_move (dx, dy); return; }
	static int lr_was; const int L = input_cb (0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L) != 0, R = input_cb (0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R) != 0, lr = L | R << 1;
	if ((lr & ~lr_was) && !lib_menu && lib_b4_t < 0) lib_page (lr & ~lr_was & 2 ? 1 : -1);
	lr_was = lr;
	if (lib_menu) { if (nw & 8) lib_menu_pick (lib_msel); else if (nw & 1) { lib_menu = 0; br_dirty = 1; } return; }   /* (the choices: button 1 back to the tiles) */
	if (lib_b4_t >= 0) {                               /* (button 4 down on a UVMC2 game: let go soon - it plays; held - its choices) */
		if (!(b & 8)) { lib_b4_t = -1; lib_play (); } else if (vt_wall () - lib_b4_t >= 0.5) lib_menu_open (0);
		return; }
	if (nw & 8) { if (lib_focus != 2) lib_set (lib_focus, lib_hidx);
		else if (lib_ns && lib_all[lib_show[lib_sel]].kind == 'm' && (b & 6) != 6) lib_b4_t = vt_wall ();   /* (2 + 3 held with it: it plays at once - they reach the game) */
		else lib_play (); }
	else if (nw & 1) { br_show (0); br_hold = 1; br_hold_t = vt_wall (); }   /* (button 1 closes; 2 and 3 are free - the game's start-up chord) */
}
static int lib_tile_at (float x, float y)          /* (the tile under a point: its place in the list, -1 none) */
{
	const lib_geo *g = &lib_g;
	if (x < g->grid[0] || x > g->grid[2] || y < g->grid[1] || y > g->grid[3]) return -1;
	const int c = lib_cols ? lib_cols : 1, col = lib_cols ? (int) ((x - g->grid[0]) / (g->tw + g->gap)) : 0;
	if (col >= c) return -1;
	const int i = (int) ((y - g->grid[1] + lib_scroll) / g->rowh) * c + col;
	return i >= 0 && i < lib_ns ? i : -1;
}
static void lib_touch (float x, float y, int down, int held, int up)
{
	const lib_geo *g = &lib_g;
	#define IN(q) (x >= (q)[0] && x <= (q)[2] && y >= (q)[1] && y <= (q)[3])
	if (lib_menu) {                                    /* (a UVMC2 game's choices: a row tapped; outside: back to the tiles) */
		if (!up) return;
		lib_drag_y = -1; if (lib_mskip) { lib_mskip = 0; return; }   /* (the finger that opened them let go) */
		if (IN (lib_mrow[0])) lib_menu_pick (0); else if (IN (lib_mrow[1])) lib_menu_pick (1); else if (!IN (lib_mbox)) { lib_menu = 0; br_dirty = 1; }
		return; }
	if (down) { lib_drag_y = y; lib_drag_s = lib_scroll; lib_dragged = 0; lib_press_t = vt_wall (); lib_press_i = y >= g->head ? lib_tile_at (x, y) : -1; return; }
	if (held && lib_drag_y >= 0) { if (fabs (y - lib_drag_y) > 0.6f * g->u) { lib_dragged = 1; lib_press_i = -1; }
		if (lib_dragged && y >= g->head) { lib_scroll = (float) (lib_drag_s - (y - lib_drag_y)); lib_clamp (); br_dirty = 1; }
		if (!lib_dragged && lib_press_i >= 0 && lib_all[lib_show[lib_press_i]].kind == 'm' && vt_wall () - lib_press_t >= 0.5) {   /* (a finger held on a UVMC2 game: its choices) */
			lib_sel = lib_press_i; lib_focus = 2; lib_menu_open (1); }
		return; }
	if (!up) return;
	lib_drag_y = -1; lib_press_i = -1; if (lib_dragged) return;
	if (IN (g->close)) { br_show (0); return; }
	for (int i = 0; i < 3; i++) if (IN (g->look[i])) { lib_set (0, i); return; }
	for (int i = 0; i < 6; i++) if (IN (g->col[i])) { lib_set (0, 3 + i); return; }
	for (int i = 0; i < 6; i++) if (IN (g->cat[i])) { lib_set (1, i); return; }
	if (IN (g->grid)) { const int c = lib_cols ? lib_cols : 1, col = lib_cols ? (int) ((x - g->grid[0]) / (g->tw + g->gap)) : 0;
		if (col >= c) return;
		const int i = (int) ((y - g->grid[1] + lib_scroll) / g->rowh) * c + col;
		if (i >= 0 && i < lib_ns) { lib_sel = i; lib_focus = 2; lib_play (); } }
	#undef IN
}
