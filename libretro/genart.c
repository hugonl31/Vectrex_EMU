/* genart.c - the game library's pictures for a game with none of its own: a cartridge with the game's title in the blue
 * pill on the label, and a box (the owner's approved look: laid out on his real Clean Sweep box, silver paper with a
 * highlight and a slightly bent carton, the pill, a portrait screen in a coloured frame with one of 12 vector scenes of
 * our own, his small print and GCE at the foot, a little wear - all chosen per game by its title). One implementation for every platform, so they all look the
 * same: the RetroArch core (library.c includes it), the iPhone and Android apps (compiled with the core), the PC (inside
 * src/frontend/native's vxbuild library: genart.py). Overlays get none.
 *
 *   unsigned char *vx_genart_make_w (int look, const char *name, int width, int *w, int *h)
 *       look 0 the cartridge (840 x 808 at the most), 1 the box (840 x 1075); width: the tile's (vx_genart_size; 64 - 840);
 *       name: the game's file name without its extension (UTF-8). RGBA, straight alpha, rows top down; NULL if it could
 *       not be made. Free it with vx_genart_free. vx_genart_make: at 840.
 *   int vx_genart_size (int px)   the width to draw a tile shown px wide at: 256, 384, 512, 640 or 840
 *   int vx_genart_title (const char *name, char *out, int n)   the title the pill shows (UTF-8; "|" where a line breaks)
 *   unsigned char *vx_genart_label_w (const char *path, int width, int *w, int *h)
 *   unsigned char *vx_genart_label_png_w (const unsigned char *png, int len, int width, int *w, int *h)
 *       the cartridge with the player's own label on it (thumbs/labels/<game>.png or .jpg - or a shipped one, thumbs/
 *       shipped/labels: the label alone, 840 x 458 or any size at about 1.835:1) - by the file's path or its bytes (PNG or
 *       JPEG); as vx_genart_make_w's (vx_genart_label(_png): at 840)
 *   unsigned char *vx_genart_fit (const char *path, int maxw, int *w, int *h)    (and vx_genart_fit_png: its bytes)
 *   unsigned char *vx_genart_boxpaper_w (int width, int well, int *w, int *h)      the box's paper, furniture and foot alone
 *   void vx_genart_wear (unsigned char *rgba, int w, int h, const char *name)      the box's wear for name, in place
 *       a picture of the player's own (a PNG or JPEG: cartridge, box, overlay) made at most maxw wide, once; as vx_genart_make's
 *   int vx_genart_key (const char *name, char *out, int n)
 *       a game's key in the pictures that come with the emulator (thumbs/shipped/index.txt: "<key>\t<picture's name>",
 *       tools/make_shipped_art.py): the name without its "(...)" and "[...]" parts, lower case, only a-z and 0-9; its length
 *
 * The title: the file's name without brackets ("(Proto)", "[b1]"), a trailing " 4x" or version ("_v199", " v1.2"), "_" as
 * a space, in capitals - or what libretro/genart_titles.txt sets for it. The pill: one line (cap height up to 190 of the
 * 3120 px label) when it fits; two balanced lines (a " - " preferred, the dash dropped) when one line would get small; the
 * pill as wide as its text (at least 2.4 x its height). Its data: genart_data.h (tools/make_genart_h.py). The font: Archivo
 * Black (SIL OFL 1.1, libretro/fonts), drawn by stb_truetype (public domain). Copyright (c) 2026 Hugo Bokhorst. Part of
 * Vectrex_EMU; see LICENSE.md. */
#if defined (_MSC_VER) && !defined (_CRT_SECURE_NO_WARNINGS)
#define _CRT_SECURE_NO_WARNINGS                             /* (fopen: built alone, MSVC) */
#endif
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#ifndef STBI_INCLUDE_STB_IMAGE_H                    /* (the core has stb_image already; built alone: a private copy) */
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG                                      /* (the pictures: PNG or JPEG) */
#include "stb_image.h"
#endif
#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"
#include "genart_data.h"

#ifndef VX_GENART_API
#define VX_GENART_API
#endif

/* ---- the title */
static int ga_utf8 (const char **s)                 /* (the next code point; bad bytes as Latin-1) */
{
	const unsigned char *p = (const unsigned char *) *s; int c = *p;
	if (c < 0x80) { *s += 1; return c; }
	if ((c & 0xe0) == 0xc0 && (p[1] & 0xc0) == 0x80) { *s += 2; return (c & 0x1f) << 6 | (p[1] & 0x3f); }
	if ((c & 0xf0) == 0xe0 && (p[1] & 0xc0) == 0x80 && (p[2] & 0xc0) == 0x80) { *s += 3; return (c & 0x0f) << 12 | (p[1] & 0x3f) << 6 | (p[2] & 0x3f); }
	if ((c & 0xf8) == 0xf0 && (p[1] & 0xc0) == 0x80 && (p[2] & 0xc0) == 0x80 && (p[3] & 0xc0) == 0x80) { *s += 4; return (c & 0x07) << 18 | (p[1] & 0x3f) << 12 | (p[2] & 0x3f) << 6 | (p[3] & 0x3f); }
	*s += 1; return c;
}
static int ga_put8 (char *o, int c)
{
	if (c < 0x80) { o[0] = (char) c; return 1; }
	if (c < 0x800) { o[0] = (char) (0xc0 | c >> 6); o[1] = (char) (0x80 | (c & 0x3f)); return 2; }
	if (c < 0x10000) { o[0] = (char) (0xe0 | c >> 12); o[1] = (char) (0x80 | (c >> 6 & 0x3f)); o[2] = (char) (0x80 | (c & 0x3f)); return 3; }
	o[0] = (char) (0xf0 | c >> 18); o[1] = (char) (0x80 | (c >> 12 & 0x3f)); o[2] = (char) (0x80 | (c >> 6 & 0x3f)); o[3] = (char) (0x80 | (c & 0x3f)); return 4;
}
static int ga_upper (int c) { return (c >= 'a' && c <= 'z') || (c >= 0xe0 && c <= 0xfe && c != 0xf7) ? c - 0x20 : c == 0xff ? 0x178 : c; }

VX_GENART_API int vx_genart_title (const char *name, char *out, int n)
{
	char t[512]; int k = 0;
	if (!out || n < 2) return 0; out[0] = 0; if (!name) return 0;
	{ const char *b = name;                          /* (a path: its last part) */
	  for (const char *p = name; *p; p++) if (*p == '/' || *p == '\\') b = p + 1;
	  for (int depth = 0; *b && k < (int) sizeof t - 1; b++) {   /* (brackets and what they hold: gone) */
		if (*b == '(' || *b == '[') { depth++; continue; }
		if ((*b == ')' || *b == ']') && depth) { depth--; continue; }
		if (!depth) t[k++] = *b == '_' ? ' ' : *b; } }
	t[k] = 0;
	for (;;) {                                       /* (trailing spaces and dashes; " 4x"; a version: " v12", "-v1.2", " v010b") */
		while (k && (t[k - 1] == ' ' || t[k - 1] == '-')) t[--k] = 0;
		if (k > 3 && (t[k - 1] == 'x' || t[k - 1] == 'X') && t[k - 2] == '4' && t[k - 3] == ' ') { k -= 3; t[k] = 0; continue; }
		int j = k; if (j && t[j - 1] >= 'a' && t[j - 1] <= 'z' && j > 1 && t[j - 2] >= '0' && t[j - 2] <= '9') j--;   /* (a letter after the number) */
		int digits = 0; while (j && ((t[j - 1] >= '0' && t[j - 1] <= '9') || t[j - 1] == '.')) { if (t[j - 1] != '.') digits++; j--; }
		if (digits && j >= 2 && (t[j - 1] == 'v' || t[j - 1] == 'V') && (t[j - 2] == ' ' || t[j - 2] == '-')) { k = j - 2; t[k] = 0; continue; }
		break;
	}
	char u[1024]; int m = 0; int sp = 1;             /* (capitals; spaces single) */
	for (const char *p = t; *p && m < (int) sizeof u - 8; ) { int c = ga_utf8 (&p);
		if (c == ' ' || c == '\t') { if (!sp) u[m++] = ' '; sp = 1; continue; }
		sp = 0; m += ga_put8 (u + m, ga_upper (c)); }
	while (m && u[m - 1] == ' ') m--; u[m] = 0;
	const char *r = u;
	for (int i = 0; GA_TITLES[i][0]; i++) if (!strcmp (GA_TITLES[i][0], u)) { r = GA_TITLES[i][1]; break; }   /* (set by hand) */
	snprintf (out, (size_t) n, "%s", r);
	return (int) strlen (out);
}

/* ---- the font */
typedef struct { stbtt_fontinfo f; int ok; } ga_font;
static void ga_font_init (ga_font *g) { g->ok = stbtt_InitFont (&g->f, GA_FONT, stbtt_GetFontOffsetForIndex (GA_FONT, 0)); }
#define GA_TRACK 0.025f                             /* letter spacing, x the cap height */
#define GA_WIDEN 1.0f                               /* horizontal scale (Archivo Black is wide and black already) */
static int ga_cps (const char *s, int *cp, int max) { int n = 0; while (*s && n < max) { int c = ga_utf8 (&s); cp[n++] = c == 0x178 ? 'Y' : c; } return n; }
/* (a line's ink width at cap height 1, its ink's left edge from the pen: x unscaled by widen) */
static float ga_width1 (const ga_font *g, const int *cp, int n, float *left)
{
	if (!n) { if (left) *left = 0; return 0; }
	const float s = 1.0f / GA_FONT_CAP; float pen = 0; int x0, y0, x1, y1;
	stbtt_GetCodepointBox (&g->f, cp[0], &x0, &y0, &x1, &y1); const float l = x0 * s; if (left) *left = l;
	for (int i = 0; i < n; i++) { int adv, lsb; stbtt_GetCodepointHMetrics (&g->f, cp[i], &adv, &lsb);
		if (i == n - 1) { stbtt_GetCodepointBox (&g->f, cp[i], &x0, &y0, &x1, &y1); if (x1 <= x0) x1 = adv; return pen + x1 * s - l; }
		pen += adv * s + GA_TRACK + stbtt_GetCodepointKernAdvance (&g->f, cp[i], cp[i + 1]) * s; }
	return pen - l;
}
/* (a line drawn into an RGBA picture: cap height cap px, its ink centred on cx, baseline at by, in the colour col) */
static void ga_text (const ga_font *g, unsigned char *img, int iw, int ih, const int *cp, int n, float cap, float widen, float cx, float by, const unsigned char *col)
{
	float left; const float w = ga_width1 (g, cp, n, &left) * cap * widen;
	const float sy = cap / GA_FONT_CAP, sx = sy * widen; float pen = cx - w / 2 - left * cap * widen;
	for (int i = 0; i < n; i++) {
		const float fx = floorf (pen), fy = floorf (by); int x0, y0, x1, y1;
		stbtt_GetCodepointBitmapBoxSubpixel (&g->f, cp[i], sx, sy, pen - fx, by - fy, &x0, &y0, &x1, &y1);
		const int bw = x1 - x0, bh = y1 - y0;
		if (bw > 0 && bh > 0) { unsigned char *bm = calloc ((size_t) bw * bh, 1);
			if (bm) { stbtt_MakeCodepointBitmapSubpixel (&g->f, bm, bw, bh, bw, sx, sy, pen - fx, by - fy, cp[i]);
				for (int y = 0; y < bh; y++) { const int Y = (int) fy + y0 + y; if (Y < 0 || Y >= ih) continue;
					for (int x = 0; x < bw; x++) { const int X = (int) fx + x0 + x; const unsigned a = bm[y * bw + x]; if (X < 0 || X >= iw || !a) continue;
						unsigned char *d = img + ((size_t) Y * iw + X) * 4;
						for (int c = 0; c < 3; c++) d[c] = (unsigned char) ((col[c] * a + d[c] * (255 - a) + 127) / 255);
						if (d[3] < a) d[3] = (unsigned char) a; } }
				free (bm); } }
		int adv, lsb; stbtt_GetCodepointHMetrics (&g->f, cp[i], &adv, &lsb);
		pen += (adv * sy + (i + 1 < n ? stbtt_GetCodepointKernAdvance (&g->f, cp[i], cp[i + 1]) * sy : 0)) * widen + GA_TRACK * cap * widen;
	}
}

/* ---- the pill: laid out in the label's own pixels (3120 x 1700), drawn at any scale */
#define GA_LBL_W 3120.0f
#define GA_PILL_MAXW 2860.0f                         /* the widest pill */
#define GA_PILL_CY 965.36f                           /* the box's pill: its own frame (label px -> box px) */
#define GA_PILL_CY_CART 889.0f                      /* the cartridge's: midway between ARCADE SYSTEM CARTRIDGE (495) and the GCE logo (1283), as the GCE labels' titles */
#define GA_EDGE 24.0f                                /* its outline */
static const unsigned char GA_FILL[3] = { 99, 175, 249 }, GA_EDGEC[3] = { 240, 55, 28 }, GA_INK[3] = { 10, 10, 10 };
typedef struct { int cp[2][160], n[2], lines; float cap, pw, ph, widen; } ga_lay;
static void ga_fit (const ga_font *g, const char *title, ga_lay *L)
{
	int cp[320]; int n = ga_cps (title, cp, 320); memset (L, 0, sizeof *L); L->widen = GA_WIDEN;
	int bar = -1; for (int i = 0; i < n; i++) if (cp[i] == '|') { bar = i; break; }
	const float C1 = 190, C1MIN = 150, C2 = 140, CMIN = 96;
	if (bar >= 0) {                                  /* (a break set by hand) */
		int a0 = 0, a1 = bar, b0 = bar + 1, b1 = n;
		while (a1 > a0 && cp[a1 - 1] == ' ') a1--; while (b0 < b1 && cp[b0] == ' ') b0++;
		if (a1 - a0 > 160) a1 = a0 + 160; if (b1 - b0 > 160) b1 = b0 + 160;
		memcpy (L->cp[0], cp + a0, sizeof (int) * (size_t) (a1 - a0)); L->n[0] = a1 - a0; memcpy (L->cp[1], cp + b0, sizeof (int) * (size_t) (b1 - b0)); L->n[1] = b1 - b0;
		const float m = fmaxf (ga_width1 (g, L->cp[0], L->n[0], NULL), ga_width1 (g, L->cp[1], L->n[1], NULL)) * GA_WIDEN;
		float c = GA_PILL_MAXW / (m + 2 * 0.58f * 3.5f); if (c > C2) c = C2; if (c < 60) c = 60;
		L->lines = 2; L->cap = c; L->ph = c * 3.5f; L->pw = fmaxf (2.4f * L->ph, m * c + 2 * 0.58f * L->ph); return;
	}
	if (n > 160) n = 160;
	const float w1 = ga_width1 (g, cp, n, NULL) * GA_WIDEN;
	float c1 = w1 > 0 ? GA_PILL_MAXW / (w1 + 2 * 0.64f * 2.65f) : C1; if (c1 > C1) c1 = C1;   /* (cap x w1 + the ends 0.64 x 2.65 cap each <= the widest) */
	int best_k = -1; float best_c = 0, best_s = -1, best_bal = 0;
	if (c1 < C1MIN) {                                /* (two lines: the best split - at a space; " - " preferred, the dash dropped) */
		for (int k = 1; k < n - 1; k++) { if (cp[k] != ' ') continue;
			int a1 = k, b0 = k + 1; const int dash = a1 >= 2 && cp[a1 - 1] == '-' && cp[a1 - 2] == ' ', dash2 = b0 + 1 < n && cp[b0] == '-' && cp[b0 + 1] == ' ';
			if (dash) a1 -= 2; if (dash2) b0 += 2; if (a1 <= 0 || b0 >= n) continue;
			const float wa = ga_width1 (g, cp, a1, NULL), wb = ga_width1 (g, cp + b0, n - b0, NULL), m = fmaxf (wa, wb) * GA_WIDEN;
			float c = GA_PILL_MAXW / (m + 2 * 0.58f * 3.5f); if (c > C2) c = C2;
			const float s = c * (dash || dash2 ? 1.3f : 1.0f), bal = -fabsf (wa - wb);
			if (s > best_s || (s == best_s && bal > best_bal)) { best_s = s; best_bal = bal; best_k = k; best_c = c; } }
	}
	if (best_k >= 0 && best_c >= c1 * 1.05f) {
		int a1 = best_k, b0 = best_k + 1;
		if (a1 >= 2 && cp[a1 - 1] == '-' && cp[a1 - 2] == ' ') a1 -= 2; if (b0 + 1 < n && cp[b0] == '-' && cp[b0 + 1] == ' ') b0 += 2;
		memcpy (L->cp[0], cp, sizeof (int) * (size_t) a1); L->n[0] = a1; memcpy (L->cp[1], cp + b0, sizeof (int) * (size_t) (n - b0)); L->n[1] = n - b0;
		const float c = fmaxf (best_c, CMIN), m = fmaxf (ga_width1 (g, L->cp[0], L->n[0], NULL), ga_width1 (g, L->cp[1], L->n[1], NULL)) * GA_WIDEN;
		L->lines = 2; L->cap = c; L->ph = c * 3.5f; L->pw = fmaxf (2.4f * L->ph, m * c + 2 * 0.58f * L->ph); return;
	}
	memcpy (L->cp[0], cp, sizeof (int) * (size_t) n); L->n[0] = n; L->lines = 1;
	if (c1 < 110 && w1 > 0) {                        /* (one long word: kept legible, narrowed - down to 0.85) */
		c1 = 110; L->widen = fmaxf (0.85f, GA_WIDEN * (GA_PILL_MAXW - 2 * 0.64f * 2.65f * c1) / (w1 * c1));
		const float w = w1 / GA_WIDEN * L->widen; c1 = fminf (c1, GA_PILL_MAXW / (w + 2 * 0.64f * 2.65f));
	}
	L->cap = c1; L->ph = c1 * 2.65f; L->pw = fmaxf (2.4f * L->ph, ga_width1 (g, cp, n, NULL) * L->widen * c1 + 2 * 0.64f * L->ph);
}
static float ga_clamp01 (float v) { return v < 0 ? 0 : v > 1 ? 1 : v; }
static void ga_mix (unsigned char *d, const unsigned char *c, float a)
{
	if (a <= 0) return; for (int i = 0; i < 3; i++) d[i] = (unsigned char) (c[i] * a + d[i] * (1 - a) + 0.5f);
	const float A = d[3] / 255.0f; d[3] = (unsigned char) ((A + (1 - A) * a) * 255 + 0.5f);
}
/* (the pill and its title into img: label px -> picture px as x = ox + lx * s; the pill's centre at label (cx, cy)) */
static void ga_pill (const ga_font *g, const char *title, unsigned char *img, int iw, int ih, float ox, float oy, float s, float cx, float cy)
{
	ga_lay L; ga_fit (g, title, &L);
	const float pcx = ox + cx * s, pcy = oy + cy * s, r = L.ph * s / 2, hl = fmaxf (0, L.pw * s / 2 - r), e = GA_EDGE * s;
	const int x0 = (int) floorf (pcx - hl - r - 2), x1 = (int) ceilf (pcx + hl + r + 2), y0 = (int) floorf (pcy - r - 2), y1 = (int) ceilf (pcy + r + 2);
	for (int y = y0; y <= y1; y++) { if (y < 0 || y >= ih) continue;
		for (int x = x0; x <= x1; x++) { if (x < 0 || x >= iw) continue;
			const float px = x + 0.5f - pcx, py = y + 0.5f - pcy, qx = fmaxf (fabsf (px) - hl, 0), d = sqrtf (qx * qx + py * py) - r;   /* (the stadium's distance) */
			unsigned char *p = img + ((size_t) y * iw + x) * 4;
			ga_mix (p, GA_EDGEC, ga_clamp01 (0.5f - d)); ga_mix (p, GA_FILL, ga_clamp01 (0.5f - (d + e))); } }
	const float cap = L.cap * s, pitch = 1.5f * cap, top = pcy - (cap + pitch * (L.lines - 1)) / 2;
	for (int i = 0; i < L.lines; i++) ga_text (g, img, iw, ih, L.cp[i], L.n[i], cap, L.widen, pcx, top + i * pitch + cap, GA_INK);
}

/* ---- the box's shapes */
static void ga_rrect (unsigned char *img, int iw, int ih, float x0, float y0, float x1, float y1, float rad, const unsigned char *col)
{
	const float cx = (x0 + x1) / 2, cy = (y0 + y1) / 2, hx = (x1 - x0) / 2 - rad, hy = (y1 - y0) / 2 - rad;
	for (int y = (int) floorf (y0) - 1; y <= (int) ceilf (y1) + 1; y++) { if (y < 0 || y >= ih) continue;
		for (int x = (int) floorf (x0) - 1; x <= (int) ceilf (x1) + 1; x++) { if (x < 0 || x >= iw) continue;
			const float qx = fmaxf (fabsf (x + 0.5f - cx) - hx, 0), qy = fmaxf (fabsf (y + 0.5f - cy) - hy, 0), d = sqrtf (qx * qx + qy * qy) - rad;
			ga_mix (img + ((size_t) y * iw + x) * 4, col, ga_clamp01 (0.5f - d)); } }
}
static float ga_rrect_cov (float x, float y, float w, float h, float rad)
{
	const float qx = fmaxf (fabsf (x - w / 2) - (w / 2 - rad), 0), qy = fmaxf (fabsf (y - h / 2) - (h / 2 - rad), 0);
	return ga_clamp01 (0.5f - (sqrtf (qx * qx + qy * qy) - rad));
}
/* ---- the box's picture (v2, the owner's choice 2026-11-23): a portrait screen as the real boxes' (measured on the 22 GCE
 * fronts of the box-art pack: the template's grid-free well 176, 279, 448 x 574 of 800 x 1024; the picture's frame in it
 * 192, 295, 416 x 542, corners 24, a coloured edge ~10 px), with one of 12 vector scenes of our own. Per game, from its
 * title's hash (FNV-1a, mixed): the scene (12), the frame's colour (6), the colour of the scene's second set of lines (6),
 * a cream line inside the frame or not, and the scene's own randomness - so a library of them looks like a collection.
 * The lines as the preview drew them (G:/AI/Vectrex_Artwork/boxart_labels/gen/make_b1v2_previews.py): each a capsule of
 * the preview's width (drawn there at 3x: its width in thirds of a pixel), circles and ellipses drawn inside their
 * outline, phosphor green and the accent colour, each with its glow (a blur of sigma 4 px at 800 wide) */
typedef struct { uint32_t s; } ga_rng;
static double ga_u (ga_rng *r, double a, double b) { r->s ^= r->s << 13; r->s ^= r->s >> 17; r->s ^= r->s << 5; return a + (b - a) * ((r->s >> 8) / 16777216.0); }
static int ga_i (ga_rng *r, int a, int b) { return (int) ga_u (r, a, b + 0.999); }
static void ga_seg (float *L, int w, int h, float ax, float ay, float bx, float by, float hw, float v)
{
	const int x0 = (int) floorf (fminf (ax, bx) - hw - 1), x1 = (int) ceilf (fmaxf (ax, bx) + hw + 1), y0 = (int) floorf (fminf (ay, by) - hw - 1), y1 = (int) ceilf (fmaxf (ay, by) + hw + 1);
	const float dx = bx - ax, dy = by - ay, ll = dx * dx + dy * dy;
	for (int y = y0 < 0 ? 0 : y0; y <= y1 && y < h; y++) for (int x = x0 < 0 ? 0 : x0; x <= x1 && x < w; x++) {
		const float px = x + 0.5f - ax, py = y + 0.5f - ay; float t = ll > 0 ? (px * dx + py * dy) / ll : 0; t = t < 0 ? 0 : t > 1 ? 1 : t;
		const float ex = px - t * dx, ey = py - t * dy, c = ga_clamp01 (hw + 0.5f - sqrtf (ex * ex + ey * ey)) * v;
		if (L[(size_t) y * w + x] < c) L[(size_t) y * w + x] = c; }
}
static void ga_blur (float *a, int w, int h, int rad)        /* (three box blurs: about a Gaussian) */
{
	float *t = malloc (sizeof (float) * (size_t) (w > h ? w : h)); if (!t || rad < 1) { free (t); return; }
	for (int pass = 0; pass < 3; pass++) {
		for (int y = 0; y < h; y++) { float *row = a + (size_t) y * w; float acc = 0;
			for (int x = -rad; x < w + rad; x++) { const int xa = x + rad, xr = x - rad - 1;
				if (xa < w && xa >= 0) acc += row[xa]; if (xr >= 0 && xr < w) acc -= row[xr];
				if (x >= 0 && x < w) t[x] = acc / (2 * rad + 1); }
			memcpy (row, t, sizeof (float) * (size_t) w); }
		for (int x = 0; x < w; x++) { float acc = 0;
			for (int y = -rad; y < h + rad; y++) { const int ya = y + rad, yr = y - rad - 1;
				if (ya < h && ya >= 0) acc += a[(size_t) ya * w + x]; if (yr >= 0 && yr < h) acc -= a[(size_t) yr * w + x];
				if (y >= 0 && y < h) t[y] = acc / (2 * rad + 1); }
			for (int y = 0; y < h; y++) a[(size_t) y * w + x] = t[y]; } }
	free (t);
}
typedef struct { float *L[2]; int w, h; double k, W, H; ga_rng r; } ga_pen;
static float ga_hw (const ga_pen *P, double width) { double s = floor (width * P->k * 3 + 0.5); if (s < 1) s = 1; return (float) (s / 6.0); }   /* (half the line's width) */
static void ga_ln (ga_pen *P, double ax, double ay, double bx, double by, int layer, int v, double width)
{
	ga_seg (P->L[layer], P->w, P->h, (float) ax, (float) ay, (float) bx, (float) by, ga_hw (P, width), v / 255.0f);
}
static void ga_poly (ga_pen *P, const double *xy, int n, int closed, int layer, int v, double width)
{
	for (int i = 0; i + 1 < n; i++) ga_ln (P, xy[2 * i], xy[2 * i + 1], xy[2 * i + 2], xy[2 * i + 3], layer, v, width);
	if (closed && n > 2) ga_ln (P, xy[2 * n - 2], xy[2 * n - 1], xy[0], xy[1], layer, v, width);
}
/* (an ellipse's outline - inside rx, ry, as the preview's - or an arc of it: a0..a1 degrees from 3 o'clock, clockwise) */
static void ga_ell (ga_pen *P, double cx, double cy, double rx, double ry, int layer, int v, double width, double a0, double a1)
{
	const float hw = ga_hw (P, width); const double ex = rx - hw, ey = ry - hw; if (ex <= 0.3 || ey <= 0.3) return;
	float *L = P->L[layer]; const float vv = v / 255.0f; const int arc = !(a0 <= 0 && a1 >= 360);
	for (int y = (int) floor (cy - ry - 2); y <= (int) ceil (cy + ry + 2); y++) { if (y < 0 || y >= P->h) continue;
		for (int x = (int) floor (cx - rx - 2); x <= (int) ceil (cx + rx + 2); x++) { if (x < 0 || x >= P->w) continue;
			const double dx = x + 0.5 - cx, dy = y + 0.5 - cy, g = (dx / ex) * (dx / ex) + (dy / ey) * (dy / ey) - 1;
			const double gx = 2 * dx / (ex * ex), gy = 2 * dy / (ey * ey), gl = sqrt (gx * gx + gy * gy); if (gl <= 0) continue;
			const float c = ga_clamp01 (hw + 0.5f - (float) (fabs (g) / gl)) * vv; if (c <= 0) continue;
			if (arc) { double a = atan2 (dy / ey, dx / ex) * 57.29577951308232; if (a < 0) a += 360; if (a < a0 || a > a1) continue; }
			if (L[(size_t) y * P->w + x] < c) L[(size_t) y * P->w + x] = c; } }
}
static void ga_circ (ga_pen *P, double cx, double cy, double r, int layer, int v, double width) { ga_ell (P, cx, cy, r, r, layer, v, width, 0, 360); }
static void ga_dot (ga_pen *P, double x, double y, double r, int layer)
{
	r *= P->k; float *L = P->L[layer];
	for (int yy = (int) floor (y - r - 2); yy <= (int) ceil (y + r + 2); yy++) { if (yy < 0 || yy >= P->h) continue;
		for (int xx = (int) floor (x - r - 2); xx <= (int) ceil (x + r + 2); xx++) { if (xx < 0 || xx >= P->w) continue;
			const float c = ga_clamp01 ((float) (r + 0.5 - sqrt ((xx + 0.5 - x) * (xx + 0.5 - x) + (yy + 0.5 - y) * (yy + 0.5 - y))));
			if (L[(size_t) yy * P->w + xx] < c) L[(size_t) yy * P->w + xx] = c; } }
}
static void ga_stars (ga_pen *P, int n, double ymax, int layer)
{
	for (int i = 0; i < n; i++) { const double x = ga_u (&P->r, 0, P->W), y = ga_u (&P->r, 0, ymax), r = ga_u (&P->r, 0.6, 1.4); ga_dot (P, x, y, r, layer); }
}
static void ga_floor (ga_pen *P, double hz, double vx, int layer, int v)
{
	const double W = P->W, H = P->H;
	for (int i = -9; i <= 9; i++) ga_ln (P, vx, hz, W / 2 + i * W * 0.18, H, layer, v, 1.2);
	double y = hz + 5 * P->k, st = 4 * P->k;
	while (y < H) { ga_ln (P, 0, y, W, y, layer, v, 1.2); st *= 1.3; y += st; }
}
#define GA_PI 3.14159265358979323846
static void ga_theme (ga_pen *P, int theme)
{
	ga_rng *r = &P->r; const double W = P->W, H = P->H, k = P->k; double xy[512];
	const double hz = H * ga_u (r, 0.55, 0.64);
	switch (theme) {
	case 0: {                                        /* grid floor, mountains, moon */
		const int n = ga_i (r, 25, 45); ga_stars (P, n, hz - 0.2 * H, 0); ga_floor (P, hz, W * ga_u (r, 0.35, 0.65), 0, 150);
		int m = 0; double x = 0;
		while (x < W && m < 250) { xy[2 * m] = x; xy[2 * m + 1] = hz - ga_u (r, 0.03, 0.24) * H; m++; x += ga_u (r, 0.06, 0.15) * W; }
		xy[2 * m] = W; xy[2 * m + 1] = hz - 0.05 * H; m++;
		ga_poly (P, xy, m, 0, 0, 235, 2.0); ga_ln (P, 0, hz, W, hz, 0, 230, 1.5);
		{ const double cx = ga_u (r, 0.2, 0.8) * W, cy = ga_u (r, 0.1, 0.28) * H, rr = ga_u (r, 0.07, 0.12) * W; ga_circ (P, cx, cy, rr, 1, 235, 2.0); }
		break; }
	case 1: {                                        /* a ringed planet in a starfield, a small moon */
		ga_stars (P, ga_i (r, 60, 90), H, 0);
		const double cx = ga_u (r, 0.35, 0.65) * W, cy = ga_u (r, 0.38, 0.55) * H, R = ga_u (r, 0.18, 0.26) * W;
		ga_circ (P, cx, cy, R, 0, 235, 2.0);
		for (int j = 0; j < 3; j++) ga_ell (P, cx, cy, R * (1.55 + 0.18 * j), R * (0.36 + 0.05 * j), 1, 220, 1.6, 200, 340);
		for (int j = 0; j < 3; j++) ga_ell (P, cx, cy, R * (1.55 + 0.18 * j), R * (0.36 + 0.05 * j), 1, 220, 1.6, 0, 160);
		for (int t = -2; t <= 2; t++) ga_ell (P, cx, cy + t * R * 0.3, R * sqrt (fmax (0.05, 1 - (t * 0.3) * (t * 0.3))), R * 0.08, 0, 120, 1.0, 0, 360);
		{ const double mx = cx > W / 2 ? ga_u (r, 0.1, 0.3) * W : ga_u (r, 0.7, 0.9) * W, my = ga_u (r, 0.1, 0.25) * H, mr = ga_u (r, 0.03, 0.05) * W; ga_circ (P, mx, my, mr, 0, 230, 1.5); }
		break; }
	case 2: {                                        /* an asteroid field and a ship */
		ga_stars (P, ga_i (r, 30, 50), H, 0);
		const int na = ga_i (r, 7, 11);
		for (int i = 0; i < na; i++) {
			const double cx = ga_u (r, 0, W), cy = ga_u (r, 0, H), R = ga_u (r, 0.04, 0.13) * W; const int n = ga_i (r, 7, 10);
			for (int j = 0; j < n; j++) { const double a = 2 * GA_PI * j / n, fx = ga_u (r, 0.7, 1.1), fy = ga_u (r, 0.7, 1.1); xy[2 * j] = cx + R * fx * cos (a); xy[2 * j + 1] = cy + R * fy * sin (a); }
			ga_poly (P, xy, n, 1, 0, 230, 1.7); }
		{ const double sx = ga_u (r, 0.3, 0.7) * W, sy = ga_u (r, 0.6, 0.85) * H, a = ga_u (r, -0.6, 0.6) - GA_PI / 2, b = a + GA_PI / 2;
		  static const double SH[8] = { 0, -18, 11, 12, 0, 6, -11, 12 };
		  for (int j = 0; j < 4; j++) { xy[2 * j] = sx + (SH[2 * j] * cos (b) - SH[2 * j + 1] * sin (b)) * k; xy[2 * j + 1] = sy + (SH[2 * j] * sin (b) + SH[2 * j + 1] * cos (b)) * k; }
		  ga_poly (P, xy, 4, 1, 1, 240, 1.8);
		  for (int j = 0; j < 4; j++) ga_dot (P, sx + cos (a) * (30 + 14 * j) * k, sy + sin (a) * (30 + 14 * j) * k, 1.3, 1); }
		break; }
	case 3: {                                        /* a city skyline under a moon */
		ga_stars (P, ga_i (r, 30, 50), hz * 0.7, 0);
		const double base = H * ga_u (r, 0.72, 0.8); double x = 0;
		while (x < W) {
			const double bw = ga_u (r, 0.07, 0.16) * W, bh = ga_u (r, 0.12, 0.42) * H;
			xy[0] = x; xy[1] = base; xy[2] = x; xy[3] = base - bh; xy[4] = x + bw; xy[5] = base - bh; xy[6] = x + bw; xy[7] = base; ga_poly (P, xy, 4, 0, 0, 230, 1.6);
			const double y0 = base - bh + 8 * k, y1 = base - 6 * k, st = 11 * k;
			for (int i = 0; ; i++) { const double wy = y0 + i * st; if (wy >= y1) break;
				if (ga_u (r, 0, 1) < 0.55) ga_ln (P, x + bw * 0.25, wy, x + bw * 0.45, wy, 1, 200, 1.2);
				if (ga_u (r, 0, 1) < 0.55) ga_ln (P, x + bw * 0.55, wy, x + bw * 0.75, wy, 1, 200, 1.2); }
			if (ga_u (r, 0, 1) < 0.3) { const double ah = ga_u (r, 10, 30) * k; ga_ln (P, x + bw / 2, base - bh, x + bw / 2, base - bh - ah, 0, 230, 1.2); }
			x += bw + ga_u (r, 0, 0.03) * W; }
		ga_ln (P, 0, base, W, base, 0, 230, 1.6);
		for (int j = 1; j < 5; j++) ga_ln (P, 0, base + j * j * 3 * k, W, base + j * j * 3 * k, 0, 110, 1.0);
		{ const double cx = ga_u (r, 0.15, 0.85) * W, cy = ga_u (r, 0.08, 0.2) * H, rr = ga_u (r, 0.05, 0.08) * W; ga_circ (P, cx, cy, rr, 0, 235, 1.8); }
		break; }
	case 4: {                                        /* a tunnel of nested polygons, turning */
		const double cx = W * ga_u (r, 0.4, 0.6), cy = H * ga_u (r, 0.4, 0.6); const int n = ga_i (r, 4, 6); const double tw = ga_u (r, 0.08, 0.2);
		for (int j = 0; j < 14; j++) { const double R = (0.95 * pow (0.78, j)) * fmax (W, H) * 0.7, a0 = j * tw;
			for (int q = 0; q < n; q++) { xy[2 * q] = cx + R * cos (a0 + 2 * GA_PI * q / n); xy[2 * q + 1] = cy + R * sin (a0 + 2 * GA_PI * q / n); }
			ga_poly (P, xy, n, 1, j % 2, 235 - j * 8, 1.7); }
		for (int q = 0; q < n; q++) { const double a0 = 2 * GA_PI * q / n; ga_ln (P, cx, cy, cx + W * cos (a0), cy + W * sin (a0), 0, 90, 1.0); }
		ga_stars (P, 12, H, 0);
		break; }
	case 5: {                                        /* a wireframe cube / pyramid over the grid */
		ga_stars (P, ga_i (r, 25, 40), hz - 0.15 * H, 0); ga_floor (P, hz, W / 2, 0, 130); ga_ln (P, 0, hz, W, hz, 0, 220, 1.5);
		const double cx = W * ga_u (r, 0.4, 0.6), cy = hz - H * ga_u (r, 0.15, 0.22), s = W * ga_u (r, 0.16, 0.22), ay = ga_u (r, 0.3, 1.2), ax = ga_u (r, 0.3, 0.6);
		double V[8][3]; int E[16][2], nv, ne = 0;
		if (ga_u (r, 0, 1) < 0.5) { nv = 8;
			for (int i = 0; i < 8; i++) { V[i][0] = i & 4 ? 1 : -1; V[i][1] = i & 2 ? 1 : -1; V[i][2] = i & 1 ? 1 : -1; }
			for (int a = 0; a < 8; a++) for (int b = a + 1; b < 8; b++) { int d = 0; for (int q = 0; q < 3; q++) d += V[a][q] != V[b][q]; if (d == 1) { E[ne][0] = a; E[ne][1] = b; ne++; } } }
		else { static const double PV[5][3] = { { -1, 1, -1 }, { 1, 1, -1 }, { 1, 1, 1 }, { -1, 1, 1 }, { 0, -1.2, 0 } }; static const int PE[8][2] = { { 0, 1 }, { 1, 2 }, { 2, 3 }, { 3, 0 }, { 0, 4 }, { 1, 4 }, { 2, 4 }, { 3, 4 } };
			nv = 5; memcpy (V, PV, sizeof PV); ne = 8; memcpy (E, PE, sizeof PE); }
		double S2[8][2];
		for (int i = 0; i < nv; i++) { double x = V[i][0], y = V[i][1], z = V[i][2], x2 = x * cos (ay) - z * sin (ay), z2 = x * sin (ay) + z * cos (ay);
			const double y3 = y * cos (ax) - z2 * sin (ax), z3 = y * sin (ax) + z2 * cos (ax), f = 4 / (4 + z3); S2[i][0] = cx + x2 * s * f; S2[i][1] = cy + y3 * s * f; }
		for (int i = 0; i < ne; i++) ga_ln (P, S2[E[i][0]][0], S2[E[i][0]][1], S2[E[i][1]][0], S2[E[i][1]][1], 1, 245, 2.0);
		ga_ell (P, cx, hz + 0.05 * H, s * 0.9, s * 0.12, 0, 120, 1.0, 0, 360);
		break; }
	case 6: {                                        /* the sun setting over the grid */
		ga_floor (P, hz, W * ga_u (r, 0.4, 0.6), 0, 150); ga_ln (P, 0, hz, W, hz, 0, 230, 1.5);
		const double cx = W * ga_u (r, 0.4, 0.6), R = W * ga_u (r, 0.26, 0.34);
		ga_ell (P, cx, hz, R, R, 1, 240, 2.0, 180, 360);
		for (int j = 1; j < 8; j++) { const double y = hz - R * pow (j / 8.0, 0.8), dx = sqrt (fmax (0, R * R - (hz - y) * (hz - y)));
			if (j % 2) ga_ln (P, cx - dx, y, cx + dx, y, 1, 200, 1.4); }
		ga_stars (P, ga_i (r, 20, 35), hz - R - 10 * k, 0);
		break; }
	case 7: {                                        /* a space station: a wheel, spokes, panels */
		ga_stars (P, ga_i (r, 50, 80), H, 0);
		const double cx = W * ga_u (r, 0.4, 0.6), cy = H * ga_u (r, 0.38, 0.52), R = W * ga_u (r, 0.26, 0.32), t = ga_u (r, 0.25, 0.45);
		ga_ell (P, cx, cy, R, R * t, 0, 235, 2.0, 0, 360); ga_ell (P, cx, cy, R * 0.82, R * t * 0.82, 0, 160, 1.2, 0, 360);
		for (int q = 0; q < 8; q++) { const double a = GA_PI * q / 4; ga_ln (P, cx, cy, cx + R * 0.82 * cos (a), cy + R * t * 0.82 * sin (a), 0, 170, 1.2); }
		ga_circ (P, cx, cy, R * 0.12, 1, 240, 1.8); ga_ln (P, cx, cy - R * 0.12, cx, cy - R * 0.9, 1, 220, 1.6);
		for (int sg = -1; sg <= 1; sg += 2) { const double px = cx + sg * R * 1.25, py = cy + R * t * 1.2;
			xy[0] = px - R * 0.22; xy[1] = py - R * 0.1; xy[2] = px + R * 0.22; xy[3] = py - R * 0.1; xy[4] = px + R * 0.22; xy[5] = py + R * 0.1; xy[6] = px - R * 0.22; xy[7] = py + R * 0.1;
			ga_poly (P, xy, 4, 1, 1, 220, 1.4); ga_ln (P, px, py - R * 0.1, px, py + R * 0.1, 1, 180, 1.0); ga_ln (P, cx + sg * R, cy + R * t * 0.4, px - sg * R * 0.22, py, 0, 160, 1.0); }
		{ const double mx = W * ga_u (r, 0.15, 0.85), my = H * ga_u (r, 0.8, 0.9), mr = W * ga_u (r, 0.08, 0.12); ga_circ (P, mx, my, mr, 0, 200, 1.6); }
		break; }
	case 8: {                                        /* rolling wireframe hills */
		ga_stars (P, ga_i (r, 30, 50), hz - 0.18 * H, 0);
		double ph[3]; for (int i = 0; i < 3; i++) ph[i] = ga_u (r, 0, 6.28);
		for (int j = 0; j < 14; j++) { const double y0 = hz - 0.12 * H + (H - hz + 0.12 * H) * pow (j / 13.0, 1.6), amp = (0.03 + 0.07 * j / 14.0) * H;
			for (int i = 0; i < 60; i++) { const double x = W * i / 59.0; xy[2 * i] = x; xy[2 * i + 1] = y0 - amp * (0.6 * sin (x / W * 6 + ph[0] + j * 0.3) + 0.4 * sin (x / W * 13 + ph[1])); }
			ga_poly (P, xy, 60, 0, 0, 120 + 8 * j, 1.3); }
		{ const double mx = W * ga_u (r, 0.2, 0.8), my = H * ga_u (r, 0.1, 0.25), mr = W * ga_u (r, 0.06, 0.1); ga_circ (P, mx, my, mr, 1, 235, 1.8); }
		break; }
	case 9: {                                        /* a corridor in one-point perspective */
		const double cx = W * ga_u (r, 0.42, 0.58), cy = H * ga_u (r, 0.42, 0.52); static const double D[7] = { 1.0, 0.72, 0.52, 0.37, 0.26, 0.18, 0.12 };
		double R[7][4];
		for (int j = 0; j < 7; j++) { R[j][0] = cx - D[j] * W * 0.55; R[j][1] = cy - D[j] * H * 0.55; R[j][2] = cx + D[j] * W * 0.55; R[j][3] = cy + D[j] * H * 0.55;
			xy[0] = R[j][0]; xy[1] = R[j][1]; xy[2] = R[j][2]; xy[3] = R[j][1]; xy[4] = R[j][2]; xy[5] = R[j][3]; xy[6] = R[j][0]; xy[7] = R[j][3]; ga_poly (P, xy, 4, 1, 0, 200, 1.4); }
		for (int c = 0; c < 4; c++) { const int ix = (c == 0 || c == 3) ? 0 : 2, iy = c < 2 ? 1 : 3; ga_ln (P, R[0][ix], R[0][iy], R[6][ix], R[6][iy], 0, 220, 1.6); }
		for (int j = 1; j < 6; j++) if (ga_u (r, 0, 1) < 0.5) {
			const int side = ga_u (r, 0, 1) < 0.5; const double xa = side ? R[j][0] : R[j][2], xb = side ? R[j + 1][0] : R[j + 1][2];
			xy[0] = xa; xy[1] = R[j][3]; xy[2] = xa; xy[3] = R[j][1] + (R[j][3] - R[j][1]) * 0.35; xy[4] = xb; xy[5] = R[j + 1][1] + (R[j + 1][3] - R[j + 1][1]) * 0.35; xy[6] = xb; xy[7] = R[j + 1][3];
			ga_poly (P, xy, 4, 0, 1, 230, 1.6); }
		ga_dot (P, cx, cy, 2.5, 1);
		break; }
	case 10: {                                       /* a comet over a horizon line */
		ga_stars (P, ga_i (r, 60, 90), H, 0);
		const double hx = W * ga_u (r, 0.55, 0.8), hy = H * ga_u (r, 0.2, 0.35), R = W * ga_u (r, 0.03, 0.05), a = ga_u (r, 2.4, 2.9);
		ga_circ (P, hx, hy, R, 0, 255, 2.0);
		for (int j = -4; j <= 4; j++) { const double L = W * ga_u (r, 0.55, 0.9), aa = a + j * 0.035; const int aj = j < 0 ? -j : j;
			ga_ln (P, hx + R * cos (aa), hy + R * sin (aa), hx + L * cos (aa), hy + L * sin (aa), aj % 2 ? 1 : 0, 200 - aj * 15, 1.2); }
		for (int i = 0; i < 40; i++) { const double x = W * i / 39.0; xy[2 * i] = x; xy[2 * i + 1] = H * 0.86 - 0.03 * H * sin (x / W * 5); }
		ga_poly (P, xy, 40, 0, 0, 200, 1.6);
		break; }
	default: {                                       /* a moonscape with craters, the earth in the sky */
		ga_stars (P, ga_i (r, 40, 70), hz - 0.1 * H, 0);
		const double ph0 = ga_u (r, 0, 6);
		for (int i = 0; i < 40; i++) { const double x = W * i / 39.0; xy[2 * i] = x; xy[2 * i + 1] = hz + 0.02 * H * sin (x / W * 3 + ph0); }
		ga_poly (P, xy, 40, 0, 0, 230, 1.8);
		const int nc = ga_i (r, 6, 9);
		for (int i = 0; i < nc; i++) { const double t = ga_u (r, 0.05, 1), y = hz + (H - hz) * t, rx = W * (0.05 + 0.14 * t) * ga_u (r, 0.6, 1.2);
			const double x1 = ga_u (r, 0, W); ga_ell (P, x1, y, rx, rx * 0.28, 0, 200, 1.4, 0, 360);
			const double x2 = ga_u (r, 0, W); ga_ell (P, x2, y, rx * 0.6, rx * 0.15, 0, 120, 1.0, 180, 360); }
		const double ex = W * ga_u (r, 0.2, 0.8), ey = H * ga_u (r, 0.12, 0.28), er = W * ga_u (r, 0.09, 0.13);
		ga_circ (P, ex, ey, er, 1, 240, 1.8);
		for (int j = -1; j <= 1; j++) { const double f = j * 0.5; ga_ell (P, ex, ey + f * er, er * sqrt (1 - f * f) * 0.95, er * 0.12, 1, 150, 1.0, 0, 360); }
		break; }
	}
}
/* (the scene drawn into img at ax, ay, w x h, its corners rounded rad; k: the box's scale against 800 wide) */
static void ga_scene (unsigned char *img, int iw, int ih, int ax, int ay, int w, int h, float rad, uint32_t seed, double k, int theme, const unsigned char *accent)
{
	ga_pen P; memset (&P, 0, sizeof P); P.w = w; P.h = h; P.W = w; P.H = h; P.k = k; P.r.s = seed ? seed : 0x9e3779b9u;
	float *G[2] = { NULL, NULL }; P.L[0] = calloc ((size_t) w * h, sizeof (float)); P.L[1] = calloc ((size_t) w * h, sizeof (float));
	G[0] = malloc (sizeof (float) * (size_t) w * h); G[1] = malloc (sizeof (float) * (size_t) w * h);
	if (!P.L[0] || !P.L[1] || !G[0] || !G[1] || w < 4 || h < 4) { free (P.L[0]); free (P.L[1]); free (G[0]); free (G[1]); return; }
	for (int i = 0; i < 3; i++) ga_u (&P.r, 0, 1);
	ga_theme (&P, theme);
	const double sg = 4 * k; const int br = (int) ((-1 + sqrt (1 + 4 * sg * sg)) / 2 + 0.5);   /* (three box blurs of br: variance br (br + 1) = sigma^2) */
	for (int l = 0; l < 2; l++) { memcpy (G[l], P.L[l], sizeof (float) * (size_t) w * h); ga_blur (G[l], w, h, br); }
	const float core[3] = { 136, 204, 187 }, halo[3] = { 102, 170, 136 }, bg[3] = { 2, 4, 4 };
	for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) {
		const int X = ax + x, Y = ay + y; if (X < 0 || Y < 0 || X >= iw || Y >= ih) continue;
		const size_t i = (size_t) y * w + x; const float a0 = P.L[0][i], g0 = G[0][i], a1 = P.L[1][i], g1 = G[1][i];
		unsigned char c[3];
		for (int q = 0; q < 3; q++) { const float v = bg[q] + a0 * core[q] + g0 * halo[q] * 0.9f + a1 * accent[q] + g1 * accent[q] * 0.75f * 0.9f; c[q] = (unsigned char) (v > 255 ? 255 : v); }
		ga_mix (img + ((size_t) Y * iw + X) * 4, c, ga_rrect_cov (x + 0.5f, y + 0.5f, (float) w, (float) h, rad)); }
	free (P.L[0]); free (P.L[1]); free (G[0]); free (G[1]);
}

/* ---- resampling (Lanczos 3, as PIL's): the pictures drawn smaller, the player's own pictures made tile-sized */
static float ga_lanczos (float x) { x = fabsf (x); if (x >= 3) return 0; if (x < 1e-6f) return 1; const float p = 3.14159265f * x; return 3 * sinf (p) * sinf (p / 3) / (p * p); }
typedef struct { int lo, n; float *w; } ga_tap;
/* (for each picture pixel o0 .. o0+no-1: the source pixels under it; the source's n pixels lie at [r0, r0 + rn) in the picture) */
static ga_tap *ga_taps (int n, float r0, float rn, int o0, int no)
{
	const float scale = n / rn, fs = scale > 1 ? scale : 1, sup = 3 * fs; const int maxn = (int) ceilf (2 * sup) + 2;
	ga_tap *t = malloc (sizeof (ga_tap) * (size_t) no); float *w = malloc (sizeof (float) * (size_t) no * (size_t) maxn); if (!t || !w) { free (t); free (w); return NULL; }
	for (int i = 0; i < no; i++) {
		float c = (o0 + i + 0.5f - r0) * scale; c = c < 0 ? 0 : c > n ? (float) n : c;
		int lo = (int) (c - sup + 0.5f), hi = (int) (c + sup + 0.5f); if (lo < 0) lo = 0; if (hi > n) hi = n; if (hi - lo > maxn) hi = lo + maxn; if (hi <= lo) { lo = (int) fminf (c, n - 1.0f); hi = lo + 1; }
		float *k = w + (size_t) i * maxn, sum = 0;
		for (int j = lo; j < hi; j++) { k[j - lo] = ga_lanczos ((j + 0.5f - c) / fs); sum += k[j - lo]; }
		if (sum == 0) { k[0] = 1; hi = lo + 1; sum = 1; }
		for (int j = lo; j < hi; j++) k[j - lo] /= sum;
		t[i].lo = lo; t[i].n = hi - lo; t[i].w = k; }
	return t;
}
static void ga_taps_free (ga_tap *t) { if (t) { free (t[0].w); free (t); } }
/* (an RGBA picture made dw x dh: over premultiplied alpha, a row at a time - never a full-size float copy; malloc'd) */
static unsigned char *ga_resize (const unsigned char *im, int sw, int sh, int dw, int dh)
{
	ga_tap *tx = ga_taps (sw, 0, (float) dw, 0, dw), *ty = ga_taps (sh, 0, (float) dh, 0, dh);
	float *t = malloc (sizeof (float) * 4 * (size_t) sw); unsigned char *o = malloc ((size_t) dw * dh * 4);
	if (!tx || !ty || !t || !o) { ga_taps_free (tx); ga_taps_free (ty); free (t); free (o); return NULL; }
	for (int y = 0; y < dh; y++) { const ga_tap *k = &ty[y];
		memset (t, 0, sizeof (float) * 4 * (size_t) sw);   /* (down) */
		for (int j = 0; j < k->n; j++) { const unsigned char *q = im + (size_t) (k->lo + j) * sw * 4; const float wt = k->w[j];
			for (int x = 0; x < sw; x++, q += 4) { const float a = q[3] * wt; t[x * 4] += q[0] * a; t[x * 4 + 1] += q[1] * a; t[x * 4 + 2] += q[2] * a; t[x * 4 + 3] += a; } }
		for (int x = 0; x < dw; x++) { float acc[4] = { 0, 0, 0, 0 }; const ga_tap *kx = &tx[x];   /* (across) */
			for (int j = 0; j < kx->n; j++) { const float *s = t + (size_t) (kx->lo + j) * 4; for (int c = 0; c < 4; c++) acc[c] += kx->w[j] * s[c]; }
			unsigned char *d = o + ((size_t) y * dw + x) * 4; const float a = acc[3];
			for (int c = 0; c < 3; c++) { const float v = a > 0.5f ? acc[c] / a : 0; d[c] = (unsigned char) (v < 0 ? 0 : v > 255 ? 255 : v + 0.5f); }
			d[3] = (unsigned char) (a < 0 ? 0 : a > 255 ? 255 : a + 0.5f); } }
	ga_taps_free (tx); ga_taps_free (ty); free (t);
	return o;
}
/* (a mask picture - grey or RGBA from genart_data.h - at scale k, blended in a colour at x, y; its size out) */
static unsigned char *ga_mask (const unsigned char *png, int len, float k, int *w, int *h, int *grey)
{
	int n; unsigned char *m = stbi_load_from_memory (png, len, w, h, &n, 4), *o; if (!m) return NULL; *grey = n <= 2;
	if (k == 1.0f) { o = malloc ((size_t) *w * *h * 4); if (o) memcpy (o, m, (size_t) *w * *h * 4); stbi_image_free (m); return o; }
	const int dw = (int) (*w * k + 0.5f) < 1 ? 1 : (int) (*w * k + 0.5f), dh = (int) (*h * k + 0.5f) < 1 ? 1 : (int) (*h * k + 0.5f);
	o = ga_resize (m, *w, *h, dw, dh); stbi_image_free (m); *w = dw; *h = dh; return o;
}
static void ga_stamp (unsigned char *img, int iw, int ih, const unsigned char *m, int w, int h, int grey, int ax, int ay, const unsigned char *col)
{
	for (int y = 0; y < h; y++) { const int Y = ay + y; if (Y < 0 || Y >= ih) continue;
		for (int x = 0; x < w; x++) { const int X = ax + x; if (X < 0 || X >= iw) continue;
			const unsigned char *q = m + ((size_t) y * w + x) * 4; unsigned char *d = img + ((size_t) Y * iw + X) * 4;
			if (grey) ga_mix (d, col, q[0] / 255.0f);    /* (grey: a mask) */
			else ga_mix (d, q, q[3] / 255.0f); } }
}

/* ---- the size: a tile is drawn at the width it is shown at, one of a few steps (so a change of tiles a row or a turn of
 * the screen finds it made already); 840 the largest - the pictures' own */
#define GA_W 840
static const int GA_STEPS[] = { 256, 384, 512, 640, 840 };
VX_GENART_API int vx_genart_size (int px)
{
	for (int i = 0; i < (int) (sizeof GA_STEPS / sizeof GA_STEPS[0]); i++) if (px <= GA_STEPS[i]) return GA_STEPS[i];
	return GA_W;
}
static int ga_w (int width) { return width <= 0 || width > GA_W ? GA_W : width < 64 ? 64 : width; }

/* the cartridge without a title, at a width: GA_CART_PNG decoded once and kept, made smaller once per width (6 kept:
 * ~6 MB with every step); *k its scale against the 840 px one. A spin lock: the apps and the PC may ask from two threads */
#if defined (_MSC_VER)
#include <intrin.h>
static volatile long ga_lk;
#define GA_LOCK() while (_InterlockedCompareExchange (&ga_lk, 1, 0)) { }
#define GA_UNLOCK() _InterlockedExchange (&ga_lk, 0)
#else
static volatile int ga_lk;
#define GA_LOCK() while (__sync_lock_test_and_set (&ga_lk, 1)) { }
#define GA_UNLOCK() __sync_lock_release (&ga_lk)
#endif
typedef struct { int w, h; unsigned char *px; } ga_base_t;
static ga_base_t ga_bases[6]; static int ga_nbase;
static unsigned char *ga_base (int width, int *w, int *h, float *k)
{
	unsigned char *o = NULL; ga_base_t *b = NULL;
	GA_LOCK ();
	for (int i = 0; i < 6; i++) if (ga_bases[i].px && ga_bases[i].w == width) { b = &ga_bases[i]; break; }
	if (!b) { int bw, bh, n; unsigned char *im = stbi_load_from_memory (GA_CART_PNG, (int) sizeof GA_CART_PNG, &bw, &bh, &n, 4), *px = NULL; int dh = bh;
		if (im) { if (width >= bw) { width = bw; px = malloc ((size_t) bw * bh * 4); if (px) memcpy (px, im, (size_t) bw * bh * 4); }
			else { dh = (int) ((double) bh * width / bw + 0.5); px = ga_resize (im, bw, bh, width, dh); }
			stbi_image_free (im); }
		if (px) { b = &ga_bases[ga_nbase++ % 6]; free (b->px); b->w = width; b->h = dh; b->px = px; } }
	if (b && (o = malloc ((size_t) b->w * b->h * 4))) { memcpy (o, b->px, (size_t) b->w * b->h * 4); *w = b->w; *h = b->h; *k = b->w / (float) GA_W; }
	GA_UNLOCK ();
	return o;
}

/* ---- the two pictures */
static unsigned char *ga_cart (const ga_font *g, const char *title, int width, int *w, int *h)
{
	float k; unsigned char *o = ga_base (width, w, h, &k); if (!o) return NULL;
	ga_pill (g, title, o, *w, *h, GA_CART_X0 * k, GA_CART_Y0 * k, GA_CART_S * k, GA_LBL_W / 2, GA_PILL_CY_CART);
	return o;
}
/* ---- the box (the owner's approved look, 2026-11-23: B1v14): laid out on his real Clean Sweep box (his photo, 2022 x
 * 2576: box px = 400 + (photo x - 997) x 800 / 2022, photo y x 800 / 2022 - 6 - the content 6 px higher than on the
 * photo), on silver paper. In the 800 x 1024 front's px:
 *   the grid: the photo's - a line every 15.47 px (39.1 photo px), 1.78 px wide, pure white; the paper (190, 192, 200)
 *     with a fine grain, lit from the upper left (+-14 levels), a steep highlight streak at the upper left (32 levels,
 *     0.08 W across, fading out along 0.25 H), a slightly bent carton (a bow of 6 levels across, a ripple of 3, the
 *     corners 6 darker, the grid and the streak bent by up to 1.2 px) - all computed once per box
 *   the well (no grid): 29 x 37 cells, its edges on grid lines, centred on the box; the picture's frame in it (photo x
 *     455..1498, y 780..2150, centred with the well), corners 11.9 px, a 10 px edge
 *   the pill: one line 91 px high, centred on the photo's badge body (y 201.7); the furniture and the foot: genart_data.h
 *   the wear (per game, from its title's hash): the edges a little darker (the top most), some corners rubbed light and
 *     slightly rounded, and on about 2 in 3 boxes a faint crease / tear in from an edge */
#define GA_BX_S (800.0 / 2022.0)
#define GA_BX_P (39.1 * GA_BX_S)                     /* grid pitch, line width */
#define GA_BX_LW (4.5 * GA_BX_S)
#define GA_BX_DXW (20.5 * GA_BX_S)                   /* (the well and frame centred: 20.5 photo px right of the photo's place) */
#define GA_BX_GX0 (400.0 + (410 - 997) * GA_BX_S + GA_BX_DXW)   /* a grid line on the well's left edge, one on its top */
#define GA_BX_GY0 (745 * GA_BX_S - 6.0)
#define GA_BOX_PS (230 * GA_BX_S / (2.65f * 190))     /* (the pill: a line 91 px high) */
#define GA_BOX_PY (525 * GA_BX_S - 6.0)
static const float GA_FRM[4] = { (float) (400 + (455 - 997) * GA_BX_S + GA_BX_DXW), (float) (780 * GA_BX_S - 6), (float) (400 + (1498 - 997) * GA_BX_S + GA_BX_DXW), (float) (2150 * GA_BX_S - 6) };
#define GA_FRM_R ((float) (30 * GA_BX_S))
static uint32_t ga_title_hash (const char *t)
{
	uint32_t h = 2166136261u; for (const char *p = t; *p; p++) h = (h ^ (unsigned char) *p) * 16777619u;   /* (FNV-1a, then mixed: every bit counts) */
	h ^= h >> 16; h *= 0x85ebca6bu; h ^= h >> 13; h *= 0xc2b2ae35u; h ^= h >> 16; return h;
}
static float ga_grain (int x, int y)                 /* (-1 .. 1 from the pixel's place: no pattern, no seams) */
{
	uint32_t h = ((uint32_t) x * 73856093u) ^ ((uint32_t) y * 19349663u); h ^= h >> 13; h *= 0x5bd1e995u; h ^= h >> 15;
	return (float) (h & 0xffff) / 65535.0f * 2 - 1;
}
/* (the silver paper with its grid into o (BW x BH, k = BW / 800); well: leave the well's lines out) */
static void ga_paper (unsigned char *o, int BW, int BH, float k, int well)
{
	const double P = GA_BX_P * k, lw = GA_BX_LW * k, ph_x = GA_BX_GX0 * k, ph_y = GA_BX_GY0 * k, sh = 1.2 * k;
	const double wx0 = ph_x + lw / 2 + 0.5, wy0 = ph_y + lw / 2 + 0.5, wx1 = ph_x + 29 * P - lw / 2 - 0.5, wy1 = ph_y + 37 * P - lw / 2 - 0.5;
	double *dx = malloc (sizeof (double) * (size_t) (BW + BH)); if (!dx) return;   /* (the bend's shift: across, varying down; down, varying across) */
	double *dy = dx + BH;
	for (int y = 0; y < BH; y++) dx[y] = sh * sin (3.14159265358979 * ((y + 0.5) / BH) * 1.1 + 0.3);
	for (int x = 0; x < BW; x++) dy[x] = 0.6 * sh * sin (3.14159265358979 * ((x + 0.5) / BW) * 0.9);
	const double SX = 0.39, SY = 0.92, scx = 0.22 * BW, scy = 0.20 * BH, saw = 0.08 * BW, sal = 0.25 * BH;   /* (the streak: H's steep direction) */
	for (int y = 0; y < BH; y++) for (int x = 0; x < BW; x++) {
		const double xv = x + 0.5, yv = y + 0.5, xw = xv + dx[y], yw = yv + dy[x], nx = xv / BW - 0.5, ny = yv / BH - 0.5;
		double t = fmod (xw - ph_x, P); if (t < 0) t += P; double d = t < P - t ? t : P - t; double cx = lw / 2 + 0.5 - d; cx = cx < 0 ? 0 : cx > 1 ? 1 : cx;
		t = fmod (yw - ph_y, P); if (t < 0) t += P; d = t < P - t ? t : P - t; double cy = lw / 2 + 0.5 - d; cy = cy < 0 ? 0 : cy > 1 ? 1 : cy;
		double c = 1 - (1 - cx) * (1 - cy);
		if (well && xv > wx0 && xv < wx1 && yv > wy0 && yv < wy1) c = 0;
		const double tl = xv / BW * 0.35 + yv / BH * 0.65;
		double light = (0.5 - tl) * 2 * 14;
		{ const double px = xw - scx, py = yw - scy, u = px * SX + py * SY; double v = px * SY - py * SX; v += 0.18 * u * u / BW;
		  light += 32 * exp (-(v / saw) * (v / saw)) * exp (-(u / sal) * (u / sal)); }
		light += -6 * (2 * nx) * (2 * nx) + 3 * sin (2 * 3.14159265358979 * (0.7 * nx + 0.45 * ny) * 1.3 + 0.8) - 6 * (2 * nx) * (2 * nx) * (2 * ny) * (2 * ny);
		light += ga_grain (x, y) * 6;
		static const double PAPER[3] = { 190, 192, 200 };
		unsigned char *p = o + ((size_t) y * BW + x) * 4;
		for (int q = 0; q < 3; q++) { const double v = (PAPER[q] + light) * (1 - c) + 255 * c; p[q] = (unsigned char) (v < 0 ? 0 : v > 255 ? 255 : v + 0.5); }
		p[3] = 255; }
	free (dx);
}
/* (the furniture on top and the foot, from genart_data.h, at the box's size) */
static void ga_box_print (unsigned char *o, int BW, int BH, float k)
{
	const float ks = BW / (float) GA_W; int fw, fh, gr; unsigned char *f;
	if ((f = ga_mask (GA_FURN_PNG, (int) sizeof GA_FURN_PNG, ks, &fw, &fh, &gr))) { ga_stamp (o, BW, BH, f, fw, fh, gr, (int) floorf (GA_FURN_X * k + 0.5f), (int) floorf (GA_FURN_Y * k + 0.5f), NULL); free (f); }
	if ((f = ga_mask (GA_FOOT_PNG, (int) sizeof GA_FOOT_PNG, ks, &fw, &fh, &gr))) { static const unsigned char BLACK[3] = { 0, 0, 0 };
		ga_stamp (o, BW, BH, f, fw, fh, gr, (int) floorf (GA_FOOT_X * k + 0.5f), (int) floorf (GA_FOOT_Y * k + 0.5f), BLACK); free (f); }
}
/* (a soft line into a float layer: max of v x coverage of a capsule of half width hw; soft: a Gaussian of sigma hw) */
static void ga_fline (float *L, int w, int h, double ax, double ay, double bx, double by, double hw, float v, int soft)
{
	const double r = soft ? 3 * hw : hw + 1; const int x0 = (int) floor (fmin (ax, bx) - r), x1 = (int) ceil (fmax (ax, bx) + r), y0 = (int) floor (fmin (ay, by) - r), y1 = (int) ceil (fmax (ay, by) + r);
	const double dx = bx - ax, dy = by - ay, ll = dx * dx + dy * dy;
	for (int y = y0 < 0 ? 0 : y0; y <= y1 && y < h; y++) for (int x = x0 < 0 ? 0 : x0; x <= x1 && x < w; x++) {
		const double px = x + 0.5 - ax, py = y + 0.5 - ay; double t = ll > 0 ? (px * dx + py * dy) / ll : 0; t = t < 0 ? 0 : t > 1 ? 1 : t;
		const double ex = px - t * dx, ey = py - t * dy, d = sqrt (ex * ex + ey * ey);
		const float c = (float) (soft ? exp (-(d / hw) * (d / hw)) : fmin (1, fmax (0, hw + 0.5 - d))) * v;
		if (L[(size_t) y * w + x] < c) L[(size_t) y * w + x] = c; }
}
/* (the wear, from the title's hash: edges, corners, perhaps a crease - over everything already drawn) */
static void ga_wear (unsigned char *o, int W, int H, float k, const char *title)
{
	ga_rng r = { ga_title_hash (title) ^ 0x2545F491u }; if (!r.s) r.s = 0x9e3779b9u; for (int i = 0; i < 3; i++) ga_u (&r, 0, 1);
	float *A = calloc ((size_t) W * H, sizeof (float)); if (!A) return;    /* (the change in levels) */
	unsigned char *al = malloc ((size_t) W * H); if (!al) { free (A); return; } memset (al, 255, (size_t) W * H);
	const double e_amt = ga_u (&r, 18, 30), t_amt = ga_u (&r, 12, 22);
	for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) {
		const double xv = x + 0.5, yv = y + 0.5, dist = fmin (fmin (xv, W - xv), fmin (yv, H - yv));
		A[(size_t) y * W + x] = (float) (-e_amt * exp (-dist / (5 * k)) - t_amt * exp (-yv / (10 * k))); }
	for (int ci = 0; ci < 4; ci++) {                 /* (the corners: rubbed light, slightly rounded) */
		if (ga_u (&r, 0, 1) >= 0.75) continue;
		const double cx = ci & 1 ? W : 0, cy = ci & 2 ? H : 0, rc = ga_u (&r, 8, 24) * k, amt = ga_u (&r, 18, 34), rr = ga_u (&r, 2.5, 6.0) * k;
		const int bx0 = ci & 1 ? (int) (W - 3 * rc) : 0, bx1 = ci & 1 ? W : (int) (3 * rc) + 1, by0 = ci & 2 ? (int) (H - 3 * rc) : 0, by1 = ci & 2 ? H : (int) (3 * rc) + 1;
		const double cell = 3 * k;                  /* (the rub's patchiness: a soft value noise) */
		for (int y = by0 < 0 ? 0 : by0; y < by1 && y < H; y++) for (int x = bx0 < 0 ? 0 : bx0; x < bx1 && x < W; x++) {
			const double xv = x + 0.5, yv = y + 0.5, d2 = (xv - cx) * (xv - cx) + (yv - cy) * (yv - cy);
			const double gx = xv / cell, gy = yv / cell; const int ix = (int) gx, iy = (int) gy; const double fx = gx - ix, fy = gy - iy;
			const double g = (ga_grain (ix, iy) * (1 - fx) + ga_grain (ix + 1, iy) * fx) * (1 - fy) + (ga_grain (ix, iy + 1) * (1 - fx) + ga_grain (ix + 1, iy + 1) * fx) * fy;
			const double m = fmin (1, fmax (0, 0.6 + g * 1.5));
			A[(size_t) y * W + x] += (float) (exp (-d2 / (rc * rc)) * m * amt);
			const double qx = fmax (rr - fmin (xv, W - xv), 0), qy = fmax (rr - fmin (yv, H - yv), 0), dd = sqrt (qx * qx + qy * qy) - rr;
			const double a = fmin (1, fmax (0, 0.5 - dd)) * 255; if (al[(size_t) y * W + x] > a) al[(size_t) y * W + x] = (unsigned char) a; } }
	float *L = NULL, *D = NULL, *B = NULL;
	if (ga_u (&r, 0, 1) < 0.65 && (L = calloc ((size_t) W * H * 3, sizeof (float)))) {   /* (a crease from an edge inward) */
		D = L + (size_t) W * H; B = D + (size_t) W * H;
		const double side = ga_u (&r, 0, 4); double x, y, ang;
		if (side < 2) { x = ga_u (&r, 0.55, 0.95) * W; y = 0; ang = ga_u (&r, 100, 140) * 3.14159265358979 / 180; }
		else if (side < 3) { x = W; y = ga_u (&r, 0.04, 0.35) * H; ang = ga_u (&r, 115, 160) * 3.14159265358979 / 180; }
		else { x = 0; y = ga_u (&r, 0.04, 0.30) * H; ang = ga_u (&r, 20, 65) * 3.14159265358979 / 180; }
		const double length = ga_u (&r, 0.12, 0.40) * H, step = 5 * k; const int n = (int) (length / step);
		for (int i = 0; i < n; i++) {
			ang += ga_u (&r, -0.12, 0.12); const double x1 = x + cos (ang) * step, y1 = y + sin (ang) * step, nx = -(y1 - y) / step, ny = (x1 - x) / step;
			const float fade = (float) fmin (1.0, (n - i) / 6.0), v = fade * (float) ga_u (&r, 0.45, 1.0);
			ga_fline (L, W, H, x, y, x1, y1, 0.65 * k, v, 0);
			ga_fline (D, W, H, x + nx * 1.6 * k, y + ny * 1.6 * k, x1 + nx * 1.6 * k, y1 + ny * 1.6 * k, 0.5 * k, v * 0.8f, 0);
			ga_fline (B, W, H, x - nx * 7 * k, y - ny * 7 * k, x1 - nx * 7 * k, y1 - ny * 7 * k, 8 * k, fade, 1);
			x = x1; y = y1; }
	}
	for (size_t i = 0, n = (size_t) W * H; i < n; i++) {
		unsigned char *p = o + i * 4;
		for (int q = 0; q < 3; q++) { double v = p[q] + A[i];
			if (L) { v = v * (1 - 0.85 * L[i]) + 252 * 0.85 * L[i] - 55 * D[i] + 7 * B[i]; }
			p[q] = (unsigned char) (v < 0 ? 0 : v > 255 ? 255 : v + 0.5); }
		if (al[i] < 255) p[3] = (unsigned char) (p[3] * al[i] / 255); }
	free (L); free (A); free (al);
}
static unsigned char *ga_box (const ga_font *g, const char *title, int width, int *w, int *h)
{
	const float k = width / 800.0f; const int BW = width, BH = (int) (1024 * k + 0.5f);
	unsigned char *o = malloc ((size_t) BW * BH * 4); if (!o) return NULL; *w = BW; *h = BH;
	ga_paper (o, BW, BH, k, 1);
	ga_box_print (o, BW, BH, k);
	ga_pill (g, title, o, BW, BH, BW / 2.0f - GA_LBL_W / 2 * GA_BOX_PS * k, (float) GA_BOX_PY * k - GA_PILL_CY * GA_BOX_PS * k, GA_BOX_PS * k, GA_LBL_W / 2, GA_PILL_CY);
	static const unsigned char FRAMES[6][3] = { { 226, 38, 34 }, { 30, 100, 200 }, { 244, 128, 22 }, { 250, 222, 34 }, { 58, 168, 60 }, { 128, 62, 156 } },
	                           ACCENTS[6][3] = { { 90, 205, 255 }, { 255, 80, 200 }, { 255, 150, 40 }, { 255, 232, 90 }, { 255, 72, 60 }, { 100, 140, 255 } },
	                           CREAM[3] = { 240, 232, 214 };
	const uint32_t hs = ga_title_hash (title);
	const int theme = (int) (hs % 12), fc = (int) (hs / 12 % 6), ac = (int) (hs / 72 % 6), cream = (int) (hs >> 20 & 1);
	const float fx0 = GA_FRM[0] * k, fy0 = GA_FRM[1] * k, fx1 = GA_FRM[2] * k, fy1 = GA_FRM[3] * k; float e = 10 * k;   /* (the frame) */
	ga_rrect (o, BW, BH, fx0, fy0, fx1, fy1, GA_FRM_R * k, FRAMES[fc]);
	if (cream) { ga_rrect (o, BW, BH, fx0 + e, fy0 + e, fx1 - e, fy1 - e, fmaxf (2, GA_FRM_R - 6) * k, CREAM); e += 3 * k; }
	{ const int sx0 = (int) floorf (fx0 + e + 0.5f), sy0 = (int) floorf (fy0 + e + 0.5f), sx1 = (int) floorf (fx1 - e + 0.5f), sy1 = (int) floorf (fy1 - e + 0.5f);
	  ga_scene (o, BW, BH, sx0, sy0, sx1 - sx0, sy1 - sy0, fmaxf (2, GA_FRM_R - 8) * k, hs ^ 0x5bd1e995u, k, theme, ACCENTS[ac]); }
	ga_wear (o, BW, BH, k, title);
	return o;
}
/* the box's paper alone, at width px (800 x 1024 scaled): the silver, its grid (well: 1 leaves the well's lines out), the
 * furniture on top and the foot - for a box made elsewhere (the PiTrex's, Last Human's: tools/make_pitrex_box.py,
 * make_lh_box.py), which then puts its own title and picture on it and calls vx_genart_wear */
VX_GENART_API unsigned char *vx_genart_boxpaper_w (int width, int well, int *w, int *h)
{
	const int BW = width <= 0 || width > 2400 ? 800 : width; const float k = BW / 800.0f; const int BH = (int) (1024 * k + 0.5f);
	unsigned char *o = malloc ((size_t) BW * BH * 4); if (w) *w = 0; if (h) *h = 0; if (!o) return NULL;
	ga_paper (o, BW, BH, k, well); ga_box_print (o, BW, BH, k);
	if (w) *w = BW; if (h) *h = BH; return o;
}
/* the wear of the box for name (its title's hash) on an RGBA box picture, in place (w: its width, 800 x 1024 scaled) */
VX_GENART_API void vx_genart_wear (unsigned char *rgba, int w, int h, const char *name)
{
	char t[1024]; if (!rgba || w < 8 || h < 8 || !name || !vx_genart_title (name, t, (int) sizeof t)) return;
	ga_wear (rgba, w, h, w / 800.0f, t);
}

/* ---- the player's own label on the cartridge (thumbs/labels/<game>.png): pasted into the label's place on the shell as
 * tools/ios_pack.py's cart() + shrink() does it (3120 x 1700 there: the whole place, top-aligned), resampled once straight
 * to the cartridge's pixels. Another aspect (more than 2 % off): fitted inside the place, centred, not stretched; the
 * strips left over and the label's clear pixels take the colour of the label's outer edge. The cartridge's plastic and
 * its dark edge: GA_CART_PNG's, untouched. */
static float ga_cov (int p, float a, float b) { return ga_clamp01 (fminf (p + 1.0f, b) - fmaxf ((float) p, a)); }   /* (how much of pixel p lies in [a, b)) */
static unsigned char *ga_label_cart (const unsigned char *lab, int lw, int lh, int width, int *w, int *h)
{
	float k; if (!lab || lw < 2 || lh < 2) return NULL;
	unsigned char *o = ga_base (width, w, h, &k); if (!o) return NULL;
	const int W = *w, H = *h;
	/* the place (cartridge px) and the label in it */
	const float ax = GA_CART_X0 * k, ay = GA_CART_LY0 * k, aw = GA_LBL_W * GA_CART_S * k, ah = GA_CART_LH * k, r = lw / (float) lh, ra = aw / ah;
	float rx = ax, ry = ay, dw = aw, dh = ah;
	if (fabsf (r / ra - 1) > 0.02f) { if (r > ra) { dh = aw / r; ry = ay + (ah - dh) / 2; } else { dw = ah * r; rx = ax + (aw - dw) / 2; } }
	/* the outer edge's colour (its clear pixels left out) */
	float fill[3] = { 205, 205, 205 }; { double s[3] = { 0, 0, 0 }, sa = 0; const int e = lw < lh ? (lw / 100 > 1 ? lw / 100 : 1) : (lh / 100 > 1 ? lh / 100 : 1);
	  for (int y = 0; y < lh; y++) for (int x = 0; x < lw; x++) { if (y >= e && y < lh - e && x >= e && x < lw - e) { x = lw - e - 1; continue; }
		const unsigned char *q = lab + ((size_t) y * lw + x) * 4; for (int c = 0; c < 3; c++) s[c] += q[c] * (double) q[3]; sa += q[3]; }
	  if (sa > 255.0 * e * 4) for (int c = 0; c < 3; c++) fill[c] = (float) (s[c] / sa); }
	const int X0 = (int) floorf (ax), X1 = (int) ceilf (ax + aw), Y0 = (int) floorf (ay), Y1 = (int) ceilf (ay + ah), nx = X1 - X0, ny = Y1 - Y0;
	ga_tap *tx = ga_taps (lw, rx, dw, X0, nx), *ty = ga_taps (lh, ry, dh, Y0, ny);
	float *t = malloc (sizeof (float) * 3 * (size_t) lw);   /* (one row at a time: never the label at its full size in floats) */
	if (!tx || !ty || !t) { ga_taps_free (tx); ga_taps_free (ty); free (t); free (o); return NULL; }
	for (int i = 0; i < ny; i++) { const int Y = Y0 + i; if (Y < 0 || Y >= H) continue;
		const float cya = ga_cov (Y, ay, ay + ah), cyl = ga_cov (Y, ry, ry + dh); const ga_tap *kk = &ty[i]; if (cya <= 0) continue;
		memset (t, 0, sizeof (float) * 3 * (size_t) lw);   /* (down: the source rows under it, each over the edge's colour) */
		for (int j = 0; j < kk->n; j++) { const unsigned char *q = lab + (size_t) (kk->lo + j) * lw * 4; const float wt = kk->w[j];
			for (int x = 0; x < lw; x++, q += 4) { const float a = q[3] / 255.0f; for (int c = 0; c < 3; c++) t[x * 3 + c] += wt * (q[c] * a + fill[c] * (1 - a)); } }
		for (int x = 0; x < nx; x++) { const int X = X0 + x; if (X < 0 || X >= W) continue;   /* (across, then into the cartridge) */
			unsigned char *p = o + ((size_t) Y * W + X) * 4; if (p[3] < 255) continue;   /* (the plastic only) */
			const float ca = cya * ga_cov (X, ax, ax + aw), cl = cyl * ga_cov (X, rx, rx + dw); if (ca <= 0) continue;
			float acc[3] = { 0, 0, 0 }; const ga_tap *kx = &tx[x];
			for (int j = 0; j < kx->n; j++) { const float *s = t + (size_t) (kx->lo + j) * 3; for (int c = 0; c < 3; c++) acc[c] += kx->w[j] * s[c]; }
			for (int c = 0; c < 3; c++) { const float v = acc[c] * cl + fill[c] * (ca - cl) + p[c] * (1 - ca); p[c] = (unsigned char) (v < 0 ? 0 : v > 255 ? 255 : v + 0.5f); } } }
	ga_taps_free (tx); ga_taps_free (ty); free (t);
	return o;
}
static unsigned char *ga_read (const char *path, int *len)
{
	FILE *f; long n; unsigned char *b;
	if (!path || !(f = fopen (path, "rb"))) return NULL;
	if (fseek (f, 0, SEEK_END) || (n = ftell (f)) < 8 || n > (256L << 20) || fseek (f, 0, SEEK_SET) || !(b = malloc ((size_t) n))) { fclose (f); return NULL; }
	if (fread (b, 1, (size_t) n, f) != (size_t) n) { fclose (f); free (b); return NULL; }
	fclose (f); *len = (int) n; return b;
}
/* (its file: a PNG or JPEG - by its bytes, or its path) */
VX_GENART_API unsigned char *vx_genart_label_png_w (const unsigned char *png, int len, int width, int *w, int *h)
{
	int lw, lh, n, ww = 0, hh = 0; unsigned char *lab, *o; if (w) *w = 0; if (h) *h = 0; if (!png || len < 8) return NULL;
	if (!stbi_info_from_memory (png, len, &lw, &lh, &n) || lw < 2 || lh < 2 || (double) lw * lh > 64e6) return NULL;
	if (!(lab = stbi_load_from_memory (png, len, &lw, &lh, &n, 4))) return NULL;
	o = ga_label_cart (lab, lw, lh, ga_w (width), &ww, &hh); stbi_image_free (lab);   /* (the label at its full size: gone at once) */
	if (o) { if (w) *w = ww; if (h) *h = hh; }
	return o;
}
VX_GENART_API unsigned char *vx_genart_label_w (const char *path, int width, int *w, int *h)
{
	int len = 0; unsigned char *b = ga_read (path, &len), *o; if (w) *w = 0; if (h) *h = 0; if (!b) return NULL;
	o = vx_genart_label_png_w (b, len, width, w, h); free (b);
	return o;
}
VX_GENART_API unsigned char *vx_genart_label_png (const unsigned char *png, int len, int *w, int *h) { return vx_genart_label_png_w (png, len, GA_W, w, h); }
VX_GENART_API unsigned char *vx_genart_label (const char *path, int *w, int *h) { return vx_genart_label_w (path, GA_W, w, h); }

/* ---- a picture of the player's own (a cartridge, a box, an overlay) at most maxw wide: read once, made smaller once,
 * never kept at its full size */
VX_GENART_API unsigned char *vx_genart_fit_png (const unsigned char *png, int len, int maxw, int *w, int *h)
{
	int sw, sh, n; unsigned char *im, *o; if (w) *w = 0; if (h) *h = 0; if (!png || len < 8 || maxw < 1) return NULL;
	if (!stbi_info_from_memory (png, len, &sw, &sh, &n) || sw < 1 || sh < 1 || (double) sw * sh > 64e6) return NULL;
	if (!(im = stbi_load_from_memory (png, len, &sw, &sh, &n, 4))) return NULL;
	if (sw <= maxw) { o = malloc ((size_t) sw * sh * 4); if (o) { memcpy (o, im, (size_t) sw * sh * 4); if (w) *w = sw; if (h) *h = sh; } stbi_image_free (im); return o; }
	const int dw = maxw, dh = (int) ((double) sh * maxw / sw + 0.5) < 1 ? 1 : (int) ((double) sh * maxw / sw + 0.5);
	o = ga_resize (im, sw, sh, dw, dh); stbi_image_free (im);
	if (o) { if (w) *w = dw; if (h) *h = dh; }
	return o;
}
VX_GENART_API unsigned char *vx_genart_fit (const char *path, int maxw, int *w, int *h)
{
	int len = 0; unsigned char *b = ga_read (path, &len), *o; if (w) *w = 0; if (h) *h = 0; if (!b) return NULL;
	o = vx_genart_fit_png (b, len, maxw, w, h); free (b);
	return o;
}

/* ---- a game's key in the shipped pictures' index (thumbs/shipped/index.txt - tools/make_shipped_art.py's key()):
 * "Armor..Attack (World)", "Armor Attack" and "armor attack [b1]" are all "armorattack". Every platform the same: the PC
 * (library.py), RetroArch (library.c), the iPhone (VXUI.m), Android (Library.java) */
VX_GENART_API int vx_genart_key (const char *name, char *out, int n)
{
	int k = 0; if (!out || n < 1) return 0; out[0] = 0; if (!name) return 0;
	for (const char *p = name; *p; p++) {
		if (*p == '(' || *p == '[') { const char *q = strchr (p + 1, *p == '(' ? ')' : ']'); if (q) { p = q; continue; } }   /* (a "(...)" / "[...]" part left out) */
		char c = *p; if (c >= 'A' && c <= 'Z') c = (char) (c - 'A' + 'a');
		if (((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) && k < n - 1) out[k++] = c; }
	out[k] = 0; return k;
}

VX_GENART_API unsigned char *vx_genart_make_w (int look, const char *name, int width, int *w, int *h)
{
	char t[1024]; ga_font g; int ww = 0, hh = 0; unsigned char *o;
	if (w) *w = 0; if (h) *h = 0;
	if (!name || (look != 0 && look != 1) || !vx_genart_title (name, t, (int) sizeof t) || !t[0]) return NULL;
	ga_font_init (&g); if (!g.ok) return NULL;
	o = look ? ga_box (&g, t, ga_w (width), &ww, &hh) : ga_cart (&g, t, ga_w (width), &ww, &hh);
	if (o) { if (w) *w = ww; if (h) *h = hh; }
	return o;
}
VX_GENART_API unsigned char *vx_genart_make (int look, const char *name, int *w, int *h) { return vx_genart_make_w (look, name, GA_W, w, h); }
VX_GENART_API void vx_genart_free (void *p) { free (p); }
