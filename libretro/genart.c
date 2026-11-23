/* genart.c - the game library's pictures for a game with none of its own: a cartridge with the game's title in the blue
 * pill on the label, and a box (the owner's choice "B1": grid paper, the Vectrex furniture, the pill, a black screen in a
 * blue frame with a vector scene of our own, the legal line). One implementation for every platform, so they all look the
 * same: the RetroArch core (library.c includes it), the iPhone and Android apps (compiled with the core), the PC (inside
 * src/frontend/native's vxbuild library: genart.py). Overlays get none.
 *
 *   unsigned char *vx_genart_make (int look, const char *name, int *w, int *h)
 *       look 0 the cartridge (840 x 808), 1 the box (840 x 1075); name: the game's file name without its extension (UTF-8).
 *       RGBA, straight alpha, rows top down; NULL if it could not be made. Free it with vx_genart_free.
 *   int vx_genart_title (const char *name, char *out, int n)   the title the pill shows (UTF-8; "|" where a line breaks)
 *   unsigned char *vx_genart_label (const char *path, int *w, int *h)
 *   unsigned char *vx_genart_label_png (const unsigned char *png, int len, int *w, int *h)
 *       the cartridge (840 x 808) with the player's own label on it (thumbs/labels/<game>.png: the label alone, 3120 x 1700
 *       or any size at about 1.835:1) - by the file's path or its bytes; as vx_genart_make's
 *   unsigned char *vx_genart_fit (const char *path, int maxw, int *w, int *h)    (and vx_genart_fit_png: its bytes)
 *       a picture of the player's own (a PNG: cartridge, box, overlay) made at most maxw wide, once; as vx_genart_make's
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
#define GA_PILL_CY 965.36f                           /* midway between ARCADE SYSTEM CARTRIDGE (495) and the legal box (1435.7) */
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
/* (a mask picture - grey or RGBA from genart_data.h - blended in a colour at x, y) */
static void ga_stamp (unsigned char *img, int iw, int ih, const unsigned char *png, int len, int ax, int ay, const unsigned char *col)
{
	int w, h, n; unsigned char *m = stbi_load_from_memory (png, len, &w, &h, &n, 4); if (!m) return;
	for (int y = 0; y < h; y++) { const int Y = ay + y; if (Y < 0 || Y >= ih) continue;
		for (int x = 0; x < w; x++) { const int X = ax + x; if (X < 0 || X >= iw) continue;
			const unsigned char *q = m + ((size_t) y * w + x) * 4; unsigned char *d = img + ((size_t) Y * iw + X) * 4;
			if (n <= 2) ga_mix (d, col, q[0] / 255.0f);    /* (grey: a mask) */
			else ga_mix (d, q, q[3] / 255.0f); } }
	stbi_image_free (m);
}
/* (the vector scene: a perspective floor to the horizon, a mountain line, stars, perhaps a moon - from the title's hash,
 *  the same everywhere; phosphor green with its glow) */
typedef struct { uint32_t s; } ga_rng;
static float ga_u (ga_rng *r, float a, float b) { r->s ^= r->s << 13; r->s ^= r->s >> 17; r->s ^= r->s << 5; return a + (b - a) * (float) ((r->s >> 8) / 16777216.0); }
static void ga_seg (float *L, int w, int h, float ax, float ay, float bx, float by, float hw, float v)
{
	const int x0 = (int) floorf (fminf (ax, bx) - hw - 1), x1 = (int) ceilf (fmaxf (ax, bx) + hw + 1), y0 = (int) floorf (fminf (ay, by) - hw - 1), y1 = (int) ceilf (fmaxf (ay, by) + hw + 1);
	const float dx = bx - ax, dy = by - ay, ll = dx * dx + dy * dy;
	for (int y = y0 < 0 ? 0 : y0; y <= y1 && y < h; y++) for (int x = x0 < 0 ? 0 : x0; x <= x1 && x < w; x++) {
		const float px = x + 0.5f - ax, py = y + 0.5f - ay; float t = ll > 0 ? (px * dx + py * dy) / ll : 0; t = t < 0 ? 0 : t > 1 ? 1 : t;
		const float ex = px - t * dx, ey = py - t * dy, c = ga_clamp01 (hw + 0.5f - sqrtf (ex * ex + ey * ey)) * v;
		if (L[y * w + x] < c) L[y * w + x] = c; }
}
static void ga_ring (float *L, int w, int h, float cx, float cy, float r, float hw, float v, int filled)
{
	for (int y = (int) (cy - r - hw - 2); y <= (int) (cy + r + hw + 2); y++) { if (y < 0 || y >= h) continue;
		for (int x = (int) (cx - r - hw - 2); x <= (int) (cx + r + hw + 2); x++) { if (x < 0 || x >= w) continue;
			const float d = sqrtf ((x + 0.5f - cx) * (x + 0.5f - cx) + (y + 0.5f - cy) * (y + 0.5f - cy));
			const float c = ga_clamp01 (hw + 0.5f - (filled ? fmaxf (d - r, 0) : fabsf (d - r))) * v; if (L[y * w + x] < c) L[y * w + x] = c; } }
}
static void ga_blur (float *a, int w, int h, int rad)        /* (three box blurs: about a Gaussian) */
{
	float *t = malloc (sizeof (float) * (size_t) (w > h ? w : h)); if (!t) return;
	for (int pass = 0; pass < 3; pass++) {
		for (int y = 0; y < h; y++) { float *row = a + (size_t) y * w; float acc = 0; int cnt = 0;
			for (int x = -rad; x < w + rad; x++) { const int xa = x + rad, xr = x - rad - 1;
				if (xa < w && xa >= 0) { acc += row[xa]; cnt++; } if (xr >= 0 && xr < w) { acc -= row[xr]; cnt--; }
				if (x >= 0 && x < w) t[x] = acc / (2 * rad + 1); }
			memcpy (row, t, sizeof (float) * (size_t) w); }
		for (int x = 0; x < w; x++) { float acc = 0;
			for (int y = -rad; y < h + rad; y++) { const int ya = y + rad, yr = y - rad - 1;
				if (ya < h && ya >= 0) acc += a[(size_t) ya * w + x]; if (yr >= 0 && yr < h) acc -= a[(size_t) yr * w + x];
				if (y >= 0 && y < h) t[y] = acc / (2 * rad + 1); }
			for (int y = 0; y < h; y++) a[(size_t) y * w + x] = t[y]; } }
	free (t);
}
static void ga_scene (unsigned char *img, int iw, int ih, int ax, int ay, int w, int h, float rad, uint32_t seed, float k)
{
	float *L = calloc ((size_t) w * h, sizeof (float)), *G = malloc (sizeof (float) * (size_t) w * h); if (!L || !G) { free (L); free (G); return; }
	ga_rng r = { seed ? seed : 0x9e3779b9u }; for (int i = 0; i < 4; i++) ga_u (&r, 0, 1);
	const float W = (float) w, H = (float) h, hz = H * ga_u (&r, 0.52f, 0.62f), vx = W * ga_u (&r, 0.35f, 0.65f), fl = 0.75f * k;
	for (int i = -9; i <= 9; i++) ga_seg (L, w, h, vx, hz, W / 2 + i * W * 0.16f, H, fl, 150 / 255.0f);
	{ float y = hz + 6 * k, st = 5 * k; while (y < H) { ga_seg (L, w, h, 0, y, W, y, fl, 150 / 255.0f); st *= 1.32f; y += st; } }
	{ float x = 0, yy = hz - ga_u (&r, 0.04f, 0.12f) * H, px = 0, py = yy; int first = 1;
	  while (x < W) { if (!first) ga_seg (L, w, h, px, py, x, yy, 1.0f * k, 230 / 255.0f); first = 0; px = x; py = yy;
		x += ga_u (&r, 0.05f, 0.14f) * W; yy = hz - ga_u (&r, 0.02f, 0.22f) * H; }
	  ga_seg (L, w, h, px, py, W, hz - 0.05f * H, 1.0f * k, 230 / 255.0f); }
	ga_seg (L, w, h, 0, hz, W, hz, fl, 230 / 255.0f);
	{ const int ns = 18 + (int) ga_u (&r, 0, 12.999f);
	  for (int i = 0; i < ns; i++) { const float sx = ga_u (&r, 0, W), sy = ga_u (&r, 0, fmaxf (1, hz - 0.24f * H)), sr = (ga_u (&r, 0, 3) < 2 ? 1.0f : 2.0f) * k;
		ga_ring (L, w, h, sx, sy, sr * 0.5f, 0.5f, 1.0f, 1); } }
	if (ga_u (&r, 0, 1) < 0.7f) { const float cx = ga_u (&r, 0.15f, 0.85f) * W, cy = ga_u (&r, 0.12f, 0.3f) * H, rr = ga_u (&r, 0.05f, 0.09f) * W;
		ga_ring (L, w, h, cx, cy, rr, 1.0f * k, 230 / 255.0f, 0); }
	memcpy (G, L, sizeof (float) * (size_t) w * h); ga_blur (G, w, h, (int) (4 * k + 0.5f));
	for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) {
		const int X = ax + x, Y = ay + y; if (X < 0 || Y < 0 || X >= iw || Y >= ih) continue;
		const float a = L[(size_t) y * w + x], gl = G[(size_t) y * w + x] * 0.9f, m = ga_rrect_cov (x + 0.5f, y + 0.5f, W, H, rad);
		const unsigned char c[3] = { (unsigned char) fminf (255, a * 136 + gl * 102 + 3), (unsigned char) fminf (255, a * 204 + gl * 170 + 6), (unsigned char) fminf (255, a * 187 + gl * 136 + 5) };
		ga_mix (img + ((size_t) Y * iw + X) * 4, c, m); }
	free (L); free (G);
}

/* ---- the two pictures */
static unsigned char *ga_cart (const ga_font *g, const char *title, int *w, int *h)
{
	int n; unsigned char *im = stbi_load_from_memory (GA_CART_PNG, (int) sizeof GA_CART_PNG, w, h, &n, 4); if (!im) return NULL;
	unsigned char *o = malloc ((size_t) *w * *h * 4); if (!o) { stbi_image_free (im); return NULL; }
	memcpy (o, im, (size_t) *w * *h * 4); stbi_image_free (im);
	ga_pill (g, title, o, *w, *h, GA_CART_X0, GA_CART_Y0, GA_CART_S, GA_LBL_W / 2, GA_PILL_CY);
	return o;
}
static unsigned char *ga_box (const ga_font *g, const char *title, int *w, int *h)
{
	const float k = 840 / 800.0f; const int BW = 840, BH = (int) (1024 * k + 0.5f);
	unsigned char *o = malloc ((size_t) BW * BH * 4); if (!o) return NULL; *w = BW; *h = BH;
	{ float *cx = malloc (sizeof (float) * (size_t) (BW + BH)); if (!cx) { free (o); return NULL; }   /* (the grid paper: 18 px squares, 2 px white lines) */
	  float *cy = cx + BW; const float P = 18 * k, lw = 2 * k;
	  for (int a = 0; a < 2; a++) { const int n = a ? BH : BW; float *c = a ? cy : cx; const float ph = (a ? 7 : 12) * k;
		for (int i = 0; i < n; i++) { const float t = fmodf (i - ph + 100 * P, P);   /* (the pixel [i, i+1) against the line [0, lw) of its square) */
			c[i] = fminf (1.0f, fmaxf (0, fminf (t + 1, lw) - fmaxf (t, 0)) + fmaxf (0, fminf (t + 1, P + lw) - fmaxf (t, P))); } }
	  for (int y = 0; y < BH; y++) for (int x = 0; x < BW; x++) { const float c = 1 - (1 - cx[x]) * (1 - cy[y]); unsigned char *p = o + ((size_t) y * BW + x) * 4;
		p[0] = p[1] = p[2] = (unsigned char) (174 + 81 * c + 0.5f); p[3] = 255; }
	  free (cx); }
	{ int fw, fh, n; unsigned char *f = stbi_load_from_memory (GA_FURN_PNG, (int) sizeof GA_FURN_PNG, &fw, &fh, &n, 4);   /* (the furniture on top) */
	  if (f) { stbi_image_free (f); ga_stamp (o, BW, BH, GA_FURN_PNG, (int) sizeof GA_FURN_PNG, (BW - fw) / 2, (int) (18 * k), NULL); } }
	ga_pill (g, title, o, BW, BH, BW / 2.0f - GA_LBL_W / 2 * 0.205f * k, 205 * k - GA_PILL_CY * 0.205f * k, 0.205f * k, GA_LBL_W / 2, GA_PILL_CY);
	const float x0 = 92 * k, y0 = 292 * k, x1 = BW - 92 * k, y1 = 832 * k;
	static const unsigned char FRAME[3] = { 28, 104, 196 }, BLACK[3] = { 0, 0, 0 }, WHITE[3] = { 255, 255, 255 }, GREY[3] = { 174, 174, 174 }, INK[3] = { 10, 10, 10 };
	ga_rrect (o, BW, BH, x0, y0, x1, y1, 26 * k, FRAME); ga_rrect (o, BW, BH, x0 + 12 * k, y0 + 12 * k, x1 - 12 * k, y1 - 50 * k, 20 * k, BLACK);
	{ uint32_t hsh = 2166136261u; for (const char *p = title; *p; p++) hsh = (hsh ^ (unsigned char) *p) * 16777619u;   /* (FNV-1a: the scene's seed) */
	  ga_scene (o, BW, BH, (int) (x0 + 22 * k), (int) (y0 + 22 * k), (int) (x1 - x0 - 44 * k), (int) (y1 - y0 - 82 * k), 14 * k, hsh, k); }
	{ int cp[320]; char t1[640]; int m = 0;          /* (the title in the frame, one line) */
	  for (const char *p = title; *p && m < (int) sizeof t1 - 1; p++) t1[m++] = *p == '|' ? ' ' : *p; t1[m] = 0;
	  const int n = ga_cps (t1, cp, 320); float cap = 15 * k; const float wmax = x1 - x0 - 40 * k, w1 = ga_width1 (g, cp, n, NULL);
	  if (w1 * cap > wmax && w1 > 0) cap = wmax / w1;
	  ga_text (g, o, BW, BH, cp, n, cap, 1.0f, BW / 2.0f, y1 - 25 * k + cap / 2, WHITE); }
	ga_rrect (o, BW, BH, 96 * k, 870 * k, BW - 96 * k, 914 * k, 0.01f, GREY);   /* (the legal line) */
	{ int lw2, lh2, n; unsigned char *l = stbi_load_from_memory (GA_LEGAL_PNG, (int) sizeof GA_LEGAL_PNG, &lw2, &lh2, &n, 4);
	  if (l) { stbi_image_free (l); ga_stamp (o, BW, BH, GA_LEGAL_PNG, (int) sizeof GA_LEGAL_PNG, (BW - lw2) / 2, (int) (892 * k) - lh2 / 2, INK); } }
	return o;
}

/* ---- the player's own label on the cartridge (thumbs/labels/<game>.png): pasted into the label's place on the shell as
 * tools/ios_pack.py's cart() + shrink() does it (3120 x 1700 there: the whole place, top-aligned), resampled once straight
 * to the cartridge's pixels (Lanczos 3, as PIL's). Another aspect (more than 2 % off): fitted inside the place, centred, not
 * stretched; the strips left over and the label's clear pixels take the colour of the label's outer edge. The cartridge's
 * plastic and its dark edge: GA_CART_PNG's, untouched. */
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
static float ga_cov (int p, float a, float b) { return ga_clamp01 (fminf (p + 1.0f, b) - fmaxf ((float) p, a)); }   /* (how much of pixel p lies in [a, b)) */
static unsigned char *ga_label_cart (const unsigned char *lab, int lw, int lh, int *w, int *h)
{
	int n; if (!lab || lw < 2 || lh < 2) return NULL;
	unsigned char *o = stbi_load_from_memory (GA_CART_PNG, (int) sizeof GA_CART_PNG, w, h, &n, 4); if (!o) return NULL;
	const int W = *w, H = *h;
	/* the place (cartridge px) and the label in it */
	const float ax = GA_CART_X0, ay = GA_CART_LY0, aw = GA_LBL_W * GA_CART_S, ah = GA_CART_LH, r = lw / (float) lh, ra = aw / ah;
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
	if (!tx || !ty || !t) { if (tx) free (tx[0].w); if (ty) free (ty[0].w); free (tx); free (ty); free (t); stbi_image_free (o); return NULL; }
	for (int i = 0; i < ny; i++) { const int Y = Y0 + i; if (Y < 0 || Y >= H) continue;
		const float cya = ga_cov (Y, ay, ay + ah), cyl = ga_cov (Y, ry, ry + dh); const ga_tap *k = &ty[i]; if (cya <= 0) continue;
		memset (t, 0, sizeof (float) * 3 * (size_t) lw);   /* (down: the source rows under it, each over the edge's colour) */
		for (int j = 0; j < k->n; j++) { const unsigned char *q = lab + (size_t) (k->lo + j) * lw * 4; const float wt = k->w[j];
			for (int x = 0; x < lw; x++, q += 4) { const float a = q[3] / 255.0f; for (int c = 0; c < 3; c++) t[x * 3 + c] += wt * (q[c] * a + fill[c] * (1 - a)); } }
		for (int x = 0; x < nx; x++) { const int X = X0 + x; if (X < 0 || X >= W) continue;   /* (across, then into the cartridge) */
			unsigned char *p = o + ((size_t) Y * W + X) * 4; if (p[3] < 255) continue;   /* (the plastic only) */
			const float ca = cya * ga_cov (X, ax, ax + aw), cl = cyl * ga_cov (X, rx, rx + dw); if (ca <= 0) continue;
			float acc[3] = { 0, 0, 0 }; const ga_tap *kx = &tx[x];
			for (int j = 0; j < kx->n; j++) { const float *s = t + (size_t) (kx->lo + j) * 3; for (int c = 0; c < 3; c++) acc[c] += kx->w[j] * s[c]; }
			for (int c = 0; c < 3; c++) { const float v = acc[c] * cl + fill[c] * (ca - cl) + p[c] * (1 - ca); p[c] = (unsigned char) (v < 0 ? 0 : v > 255 ? 255 : v + 0.5f); } } }
	free (tx[0].w); free (ty[0].w); free (tx); free (ty); free (t);
	return o;
}
/* (its file: a PNG - by its path, or its bytes) */
VX_GENART_API unsigned char *vx_genart_label_png (const unsigned char *png, int len, int *w, int *h)
{
	int lw, lh, n; unsigned char *lab, *o; if (w) *w = 0; if (h) *h = 0; if (!png || len < 8) return NULL;
	if (!stbi_info_from_memory (png, len, &lw, &lh, &n) || lw < 2 || lh < 2 || (double) lw * lh > 64e6) return NULL;
	if (!(lab = stbi_load_from_memory (png, len, &lw, &lh, &n, 4))) return NULL;
	int ww = 0, hh = 0; o = ga_label_cart (lab, lw, lh, &ww, &hh); stbi_image_free (lab);
	if (!o) return NULL;
	{ unsigned char *c = malloc ((size_t) ww * hh * 4); if (c) memcpy (c, o, (size_t) ww * hh * 4); stbi_image_free (o); o = c; }   /* (freed by vx_genart_free) */
	if (o) { if (w) *w = ww; if (h) *h = hh; }
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
VX_GENART_API unsigned char *vx_genart_label (const char *path, int *w, int *h)
{
	int len = 0; unsigned char *b = ga_read (path, &len), *o; if (w) *w = 0; if (h) *h = 0; if (!b) return NULL;
	o = vx_genart_label_png (b, len, w, h); free (b);
	return o;
}

/* ---- a picture of the player's own (a cartridge, a box, an overlay) at most maxw wide: read once, made smaller once
 * (Lanczos 3 over premultiplied alpha), never kept at its full size - the library's tiles are at most ~840 px wide */
VX_GENART_API unsigned char *vx_genart_fit_png (const unsigned char *png, int len, int maxw, int *w, int *h)
{
	int sw, sh, n; unsigned char *im, *o; if (w) *w = 0; if (h) *h = 0; if (!png || len < 8 || maxw < 1) return NULL;
	if (!stbi_info_from_memory (png, len, &sw, &sh, &n) || sw < 1 || sh < 1 || (double) sw * sh > 64e6) return NULL;
	if (!(im = stbi_load_from_memory (png, len, &sw, &sh, &n, 4))) return NULL;
	if (sw <= maxw) { o = malloc ((size_t) sw * sh * 4); if (o) { memcpy (o, im, (size_t) sw * sh * 4); if (w) *w = sw; if (h) *h = sh; } stbi_image_free (im); return o; }
	const int dw = maxw, dh = (int) ((double) sh * maxw / sw + 0.5) < 1 ? 1 : (int) ((double) sh * maxw / sw + 0.5);
	ga_tap *tx = ga_taps (sw, 0, (float) dw, 0, dw), *ty = ga_taps (sh, 0, (float) dh, 0, dh);
	float *t = malloc (sizeof (float) * 4 * (size_t) sw); o = malloc ((size_t) dw * dh * 4);   /* (one row at a time) */
	if (!tx || !ty || !t || !o) { if (tx) free (tx[0].w); if (ty) free (ty[0].w); free (tx); free (ty); free (t); free (o); stbi_image_free (im); return NULL; }
	for (int y = 0; y < dh; y++) { const ga_tap *k = &ty[y];
		memset (t, 0, sizeof (float) * 4 * (size_t) sw);   /* (down: premultiplied) */
		for (int j = 0; j < k->n; j++) { const unsigned char *q = im + (size_t) (k->lo + j) * sw * 4; const float wt = k->w[j];
			for (int x = 0; x < sw; x++, q += 4) { const float a = q[3] * wt; t[x * 4] += q[0] * a; t[x * 4 + 1] += q[1] * a; t[x * 4 + 2] += q[2] * a; t[x * 4 + 3] += a; } }
		for (int x = 0; x < dw; x++) { float acc[4] = { 0, 0, 0, 0 }; const ga_tap *kx = &tx[x];   /* (across) */
			for (int j = 0; j < kx->n; j++) { const float *s = t + (size_t) (kx->lo + j) * 4; for (int c = 0; c < 4; c++) acc[c] += kx->w[j] * s[c]; }
			unsigned char *d = o + ((size_t) y * dw + x) * 4; const float a = acc[3];
			for (int c = 0; c < 3; c++) { const float v = a > 0.5f ? acc[c] / a : 0; d[c] = (unsigned char) (v < 0 ? 0 : v > 255 ? 255 : v + 0.5f); }
			d[3] = (unsigned char) (a < 0 ? 0 : a > 255 ? 255 : a + 0.5f); } }
	free (tx[0].w); free (ty[0].w); free (tx); free (ty); free (t); stbi_image_free (im);
	if (w) *w = dw; if (h) *h = dh;
	return o;
}
VX_GENART_API unsigned char *vx_genart_fit (const char *path, int maxw, int *w, int *h)
{
	int len = 0; unsigned char *b = ga_read (path, &len), *o; if (w) *w = 0; if (h) *h = 0; if (!b) return NULL;
	o = vx_genart_fit_png (b, len, maxw, w, h); free (b);
	return o;
}

VX_GENART_API unsigned char *vx_genart_make (int look, const char *name, int *w, int *h)
{
	char t[1024]; ga_font g; int ww = 0, hh = 0; unsigned char *o;
	if (w) *w = 0; if (h) *h = 0;
	if (!name || (look != 0 && look != 1) || !vx_genart_title (name, t, (int) sizeof t) || !t[0]) return NULL;
	ga_font_init (&g); if (!g.ok) return NULL;
	o = look ? ga_box (&g, t, &ww, &hh) : ga_cart (&g, t, &ww, &hh);
	if (o) { if (w) *w = ww; if (h) *h = hh; }
	return o;
}
VX_GENART_API void vx_genart_free (void *p) { free (p); }
