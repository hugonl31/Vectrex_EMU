/* overlay_art.c - the desktop's vart.py in C, for the RetroArch core (included by libretro_vx.c): an overlay, MiSTer
 * .art (VART v1: the largest plane - 1360 x 1080, the sheet its centre 846 columns) or PNG (the sheet, or a whole
 * 1360 x 1080 plane; without alpha black is clear and the rest a colour filter, alpha 51), cut to the overlay sheet,
 * with vart.soft_edge: an overlay with a clear border - a shaped custom one - gets the Vectrex's black bezel outside
 * and its soft edge faded to black (printed); classic overlays stay as they are.
 * Copyright (c) 2026 Hugo Bokhorst. Part of Vectrex_EMU; see LICENSE.md. */

static uint32_t art_crc (const unsigned char *p, size_t n)
{ uint32_t c = 0xFFFFFFFFu; while (n--) { c ^= *p++; for (int k = 0; k < 8; k++) c = c & 1 ? (c >> 1) ^ 0xEDB88320u : c >> 1; } return ~c; }
static unsigned art_u16 (const unsigned char *p) { return (unsigned) (p[0] | p[1] << 8); }
static uint32_t art_u32 (const unsigned char *p) { return (uint32_t) (p[0] | p[1] << 8 | p[2] << 16 | (uint32_t) p[3] << 24); }
static unsigned art_bits (const unsigned char *d, size_t dlen, unsigned *pos, int b)   /* (b bits from bit pos, low first) */
{
	unsigned v = 0;
	for (int k = 0; k < b; k++) { const unsigned q = *pos + (unsigned) k; if (q / 8 < dlen) v |= ((d[q / 8] >> (q % 8)) & 1u) << k; }
	*pos += (unsigned) b; return v;
}
static int art_row (const unsigned char *rec, size_t rlen, int width, const unsigned char *prev, int bits, unsigned char *out)
{
	const unsigned nbits = art_u16 (rec); const unsigned char *d = rec + 2; const size_t dl = rlen - 2; unsigned pos = 0; int n = 0;
	const unsigned mask = (1u << bits) - 1;
	while (n < width) {
		if (pos + 8 > nbits) return -1;
		const unsigned head = art_bits (d, dl, &pos, 8);
		if (!(head & 0x80)) { int k = (int) (head & 0x7f) + 1; while (k--) { unsigned v = art_bits (d, dl, &pos, bits) & mask; if (n < width) out[n++] = (unsigned char) v; } }
		else if (!(head & 0x40)) { const unsigned v = art_bits (d, dl, &pos, bits) & mask; int k = (int) (head & 0x3f) + 1; while (k-- && n < width) out[n++] = (unsigned char) v; }
		else { if (!prev) return -1; int k = (int) (head & 0x3f) + 1; while (k-- && n < width) { out[n] = prev[n]; n++; } } }
	return n == width && pos == nbits ? 0 : -1;
}
static unsigned char *art_load (const char *path, int *pw, int *ph)   /* (VART v1 -> RGBA of its largest plane) */
{
	FILE *f = fopen (path, "rb"); if (!f) return NULL;
	fseek (f, 0, SEEK_END); long n = ftell (f); fseek (f, 0, SEEK_SET);
	unsigned char *c = n > 64 && n < (64L << 20) ? malloc ((size_t) n) : NULL;
	if (!c || fread (c, 1, (size_t) n, f) != (size_t) n) { fclose (f); free (c); return NULL; }
	fclose (f);
	unsigned char *rgba = NULL, *idx = NULL;
	if (memcmp (c, "VART", 4) || c[4] != 1) goto bad;
	{ /* (vart.py: '<4sBBBBIIIH' - magic, version, -, layers, planes, total size, CRC, descriptors' offset, their size) */
	  const unsigned planes = c[7]; const uint32_t total = art_u32 (c + 8), crc = art_u32 (c + 12), doff = art_u32 (c + 16); const unsigned dsize = art_u16 (c + 20);
	  if (total != (uint32_t) n || doff != 64 || dsize != 48 || !planes) goto bad;
	  if (art_crc (c + doff, (size_t) n - doff) != crc) goto bad;
	  const unsigned char *best = NULL;
	  for (unsigned i = 0; i < planes; i++) { const unsigned char *de = c + doff + i * 48; if ((size_t) (de - c) + 48 > (size_t) n) goto bad;
		if (!best || art_u16 (de) * art_u16 (de + 2) > art_u16 (best) * art_u16 (best + 2)) best = de; }
	  const int w = (int) art_u16 (best), h = (int) art_u16 (best + 2), bits = best[6];
	  const uint32_t pal_off = art_u32 (best + 12), pal_size = art_u32 (best + 16), pay_off = art_u32 (best + 20), pay_size = art_u32 (best + 24);
	  if ((bits != 4 && bits != 6 && bits != 8) || pal_size != (1u << bits) * 4 || pal_off + pal_size > (uint32_t) n || pay_off + pay_size > (uint32_t) n || w <= 0 || h <= 0) goto bad;
	  rgba = malloc ((size_t) w * h * 4); idx = malloc ((size_t) w * 2); if (!rgba || !idx) goto bad;
	  const unsigned char *pal = c + pal_off, *pay = c + pay_off; size_t off = 0; unsigned char *prev = NULL, *cur = idx;
	  for (int y = 0; y < h; y++) {
		if (off + 2 > pay_size) goto bad;
		const unsigned nb = art_u16 (pay + off); const size_t size = (2 + (nb + 7) / 8 + 7) / 8 * 8;
		if (off + size > pay_size || art_row (pay + off, size, w, prev, bits, cur)) goto bad;
		for (int x = 0; x < w; x++) memcpy (rgba + ((size_t) y * w + x) * 4, pal + cur[x] * 4, 4);
		prev = cur; cur = cur == idx ? idx + w : idx; off += size; }
	  free (idx); free (c); *pw = w; *ph = h; return rgba; }
  bad:
	free (c); free (rgba); free (idx); return NULL;
}

#define SOFT_EDGE 0.06
#define EDGE_BLUR 2.0
static void ov_soft_edge (unsigned char *px, int w, int h)   /* (vart.soft_edge, on the sheet) */
{
	const size_t N = (size_t) w * h; int border = 0;
	for (int x = 0; x < w && !border; x++) border = !px[(size_t) x * 4 + 3] || !px[((size_t) (h - 1) * w + x) * 4 + 3];
	for (int y = 0; y < h && !border; y++) border = !px[((size_t) y * w) * 4 + 3] || !px[((size_t) y * w + w - 1) * 4 + 3];
	if (!border) return;
	unsigned char *out = calloc (N, 1), *reach = calloc (N, 1), *g = calloc (N, 1); int *q = malloc (N * sizeof (int));
	if (!out || !reach || !g || !q) goto done;
	{ size_t qh = 0, qt = 0;                     /* (outside: the clear area joined to the border - a flood fill) */
	  for (int x = 0; x < w; x++) for (int e = 0; e < 2; e++) { size_t i = (size_t) (e ? h - 1 : 0) * w + x; if (!px[i * 4 + 3] && !out[i]) { out[i] = 1; q[qt++] = (int) i; } }
	  for (int y = 0; y < h; y++) for (int e = 0; e < 2; e++) { size_t i = (size_t) y * w + (e ? w - 1 : 0); if (!px[i * 4 + 3] && !out[i]) { out[i] = 1; q[qt++] = (int) i; } }
	  while (qh < qt) { int i = q[qh++], x = i % w, y = i / w; const int nb[4] = { x > 0 ? i - 1 : -1, x < w - 1 ? i + 1 : -1, y > 0 ? i - w : -1, y < h - 1 ? i + w : -1 };
		for (int k = 0; k < 4; k++) if (nb[k] >= 0 && !out[nb[k]] && !px[(size_t) nb[k] * 4 + 3]) { out[nb[k]] = 1; q[qt++] = nb[k]; } } }
	memcpy (reach, out, N);
	{ int n = (int) (SOFT_EDGE * (w < h ? w : h)); if (n < 2) n = 2;
	  for (int it = 0; it < n; it++) {          /* (the soft edge: half-clear pixels reached from outside, each step to MORE alpha) */
		memcpy (g, reach, N); int changed = 0;
		for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) { size_t i = (size_t) y * w + x; const int a = px[i * 4 + 3];
			if (g[i] || a == 0 || a == 255) continue;
			const int nbx[4] = { x - 1, x + 1, x, x }, nby[4] = { y, y, y - 1, y + 1 };
			for (int k = 0; k < 4; k++) { if (nbx[k] < 0 || nbx[k] >= w || nby[k] < 0 || nby[k] >= h) continue;
				size_t j = (size_t) nby[k] * w + nbx[k];
				if (reach[j] && a > (out[j] ? 0 : px[j * 4 + 3])) { g[i] = 1; changed = 1; break; } } }
		memcpy (reach, g, N); if (!changed) break; } }
	for (size_t i = 0; i < N; i++) { unsigned char *p = px + i * 4;
		if (reach[i] && p[3] > 0 && p[3] < 255) { const float k = p[3] / 255.0f; for (int c = 0; c < 3; c++) p[c] = (unsigned char) (p[c] * k + 0.5f); p[3] = 255; }
		if (out[i]) { p[0] = p[1] = p[2] = 0; p[3] = 255; } }
	{ /* (the outline blurred a little: a cut edge fades into the black - a Gaussian of EDGE_BLUR pixels) */
	  float *a = malloc (N * sizeof (float)), *b = malloc (N * sizeof (float)); float kw[13]; float ks = 0;
	  for (int k = -6; k <= 6; k++) { kw[k + 6] = expf (-0.5f * k * k / (float) (EDGE_BLUR * EDGE_BLUR)); ks += kw[k + 6]; }
	  if (a && b) {
		for (size_t i = 0; i < N; i++) a[i] = out[i] ? 0.0f : 255.0f;
		for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) { float s = 0; for (int k = -6; k <= 6; k++) { int xx = x + k; xx = xx < 0 ? 0 : xx >= w ? w - 1 : xx; s += kw[k + 6] * a[(size_t) y * w + xx]; } b[(size_t) y * w + x] = s / ks; }
		for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) { float s = 0; for (int k = -6; k <= 6; k++) { int yy = y + k; yy = yy < 0 ? 0 : yy >= h ? h - 1 : yy; s += kw[k + 6] * b[(size_t) yy * w + x]; } a[(size_t) y * w + x] = s / ks; }
		for (size_t i = 0; i < N; i++) { const int f = (int) a[i]; if (out[i] || f >= 254) continue;
			unsigned char *p = px + i * 4; const float k = f / 255.0f; for (int c = 0; c < 3; c++) p[c] = (unsigned char) (p[c] * k + 0.5f); p[3] = 255; } }
	  free (a); free (b); }
  done:
	free (out); free (reach); free (g); free (q);
}
/* an overlay file (.art or .png) -> the sheet's RGBA (malloc'd), as the desktop shows it */
static unsigned char *ov_file_load (const char *path, int *pw, int *ph)
{
	const char *x = strrchr (path, '.'); int w, h; unsigned char *px; double sheet;
	if (x && !vx_stricmp (x, ".art")) { px = art_load (path, &w, &h); sheet = h * 846.0 / 1080; }
	else { int n; px = stbi_load (path, &w, &h, &n, 4); if (!px) return NULL;
		if (n == 1 || n == 3) for (size_t i = 0; i < (size_t) w * h; i++) { unsigned char *p = px + i * 4; p[3] = p[0] | p[1] | p[2] ? 51 : 0; }   /* (no alpha: black clear, the rest a filter) */
		sheet = fabs (w / (double) h - 1360 / 1080.0) < 0.02 ? w * 846.0 / 1360 : w; }
	if (!px) return NULL;
	int sw = (int) (sheet + 0.5); if (sw < 1 || sw > w) sw = w; const int x0 = (w - sw) / 2;
	unsigned char *o = malloc ((size_t) sw * h * 4);
	if (o) for (int y = 0; y < h; y++) memcpy (o + (size_t) y * sw * 4, px + ((size_t) y * w + x0) * 4, (size_t) sw * 4);
	free (px);
	if (o) ov_soft_edge (o, sw, h);
	*pw = sw; *ph = h; return o;
}
