/* smooth.c - Vectrex_EMU's libretro core: Smooth motion's in-between picture, stroke by stroke (included by libretro_vx.c).
 * The desktop's src/frontend/native/vxsmooth.c (vxs_frame, vxs_merge) - itself bit for bit vectrex_emu.py's sm_align,
 * smooth_frame and sm_merge - in the same arithmetic and the same order: the same two frames and alpha give the same
 * in-between lines as the PC's, to the last bit (all builds: no fused multiply-add, -ffp-contract=off).
 *
 * The beam draws a stroke - pieces joined end to start, one brightness - and the emulator cuts it into pieces by time, a
 * little differently each frame (16 24 40 200 8 8 ... cycles in one, 16 16 24 40 192 8 8 ... in the next). The pieces of two
 * frames do not pair up; the strokes do. So strokes are paired, and a piece's ends are looked up along the other frame's
 * stroke at the same share of its drawing time. Matching piece to piece (a day, 2026-11-22) paired neighbours wherever a
 * frame had a piece more: shapes broke into dashes - the owner's recording of Mine Storm's mothership.
 *
 * Paired by place (the PC since 2026-11-23): each stroke with the other frame's stroke nearest to it of those alike (its ends
 * near, the same brightness, about as long to draw, going the same way about as far), where each is the other's nearest and
 * the pair is certain - its ends moved SM_NEAR at most, or no other stroke alike lies within SM_OK x the move + SM_OA of
 * either, in the other frame or its own. The others are drawn as in their own frame (a's until halfway, b's after), not
 * glided. Laid along each other for the least total distance (the core until then) Vectrexians' identical aliens were
 * paired with the alien a row away and drawn between the rows; paired by the list, a frame with a stroke more or less
 * stood still as a whole.
 * Copyright (c) 2026 Hugo Bokhorst. Part of Vectrex_EMU; see LICENSE.md. */
#define SM_NEAR 400.0                     /* (vectrex_emu.py SM_NEAR, SM_OTHER) */
#define SM_OK 2.0
#define SM_OA 300.0

/* a frame's strokes (vxsmooth.c Fr) */
typedef struct {
	int n, ns;                            /* pieces, strokes */
	int *sid;                             /* a piece's stroke */
	double *f0, *f1;                      /* a piece's ends: the share of its stroke's drawing time */
	double *pu, *px, *py;                 /* the path: stroke + 0.999 x share -> the place (2 a piece) */
	double *S;                            /* a stroke: its ends, brightness, time (6) */
	double *c;                            /* (the cycles summed) */
	size_t cap;
} sm_fr;
static sm_fr sm_FA, sm_FB;
static int *sm_ma, *sm_mb; static size_t sm_cma, sm_cmb;   /* the pairing: a's stroke -> b's or -1, b's -> a's */

static int sm_room (sm_fr *f, int n)
{
	if ((size_t) n <= f->cap) return 0;
	const size_t c = (size_t) n + 256;
	free (f->sid); free (f->f0); free (f->f1); free (f->pu); free (f->px); free (f->py); free (f->S); free (f->c);
	f->sid = malloc (c * sizeof (int)); f->f0 = malloc (c * sizeof (double)); f->f1 = malloc (c * sizeof (double));
	f->pu = malloc (2 * c * sizeof (double)); f->px = malloc (2 * c * sizeof (double)); f->py = malloc (2 * c * sizeof (double));
	f->S = malloc (6 * c * sizeof (double)); f->c = malloc (c * sizeof (double));
	if (!f->sid || !f->f0 || !f->f1 || !f->pu || !f->px || !f->py || !f->S || !f->c) {
		free (f->sid); free (f->f0); free (f->f1); free (f->pu); free (f->px); free (f->py); free (f->S); free (f->c);
		f->sid = NULL; f->f0 = f->f1 = f->pu = f->px = f->py = f->S = f->c = NULL; f->cap = 0; return -1; }
	f->cap = c; return 0;
}
/* a piece's drawing time (as the PC gets it: live.c sends it as 16 bits) */
static double sm_cy (const line_t *l) { const double v = l->cyc > 65535 ? 65535.0 : (double) l->cyc; return v > 1e-3 ? v : 1e-3; }
static double sm_max (double a, double b) { return a > b ? a : b; }

/* the strokes of a frame's pieces (vxsmooth.c strokes) */
static int sm_strokes (const gframe *g, sm_fr *f)
{
	const int n = g->n; int s = -1, first = 0; double c = 0.0, base = 0.0, tot;
	if (sm_room (f, n) < 0) return -1;
	f->n = n;
	for (int i = 0; i < n; i++) {         /* (a piece's stroke; the cycles summed, as numpy's cumsum) */
		const line_t *r = &g->L[i];
		if (i == 0 || !(r->x0 == r[-1].x1 && r->y0 == r[-1].y1 && r->c == r[-1].c)) s++;
		f->sid[i] = s; c = i ? c + sm_cy (r) : sm_cy (r); f->c[i] = c; }
	f->ns = s + 1;
	for (int i = 0; i < n; i++) {
		const line_t *r = &g->L[i]; const double cy = sm_cy (r);
		if (i == 0 || f->sid[i] != f->sid[i - 1]) {   /* (a stroke starts: its time, its ends) */
			double *S = f->S + 6 * (size_t) f->sid[i]; int last;
			first = i; for (last = i; last + 1 < n && f->sid[last + 1] == f->sid[i]; last++) ;
			base = f->c[first] - cy; tot = f->c[last] - base;
			S[0] = (double) r->x0; S[1] = (double) r->y0; S[2] = (double) g->L[last].x1; S[3] = (double) g->L[last].y1; S[4] = (double) r->c; S[5] = tot; }
		tot = f->S[6 * (size_t) f->sid[i] + 5];
		f->f0[i] = (f->c[i] - cy - base) / tot; f->f1[i] = (f->c[i] - base) / tot;
		f->pu[2 * i] = (double) f->sid[i] + 0.999 * f->f0[i]; f->pu[2 * i + 1] = (double) f->sid[i] + 0.999 * f->f1[i];
		f->px[2 * i] = (double) r->x0; f->px[2 * i + 1] = (double) r->x1; f->py[2 * i] = (double) r->y0; f->py[2 * i + 1] = (double) r->y1; }
	return 0;
}

/* how far two strokes' ends are apart (the largest of the four) */
static double sm_far4 (const double *X, const double *Y)
{
	return sm_max (sm_max (fabs (Y[0] - X[0]), fabs (Y[1] - X[1])), sm_max (fabs (Y[2] - X[2]), fabs (Y[3] - X[3])));
}
/* the same stroke a frame on: its ends near, its brightness the same, about as long to draw - and going the same way about
 * as far (a line and the one next to it, a side of the same shape, are near each other too - the Ship Test, 2026-11-22) */
static int sm_same (const double *X, const double *Y, double M)
{
	const double ex = (X[2] - X[0]) - (Y[2] - Y[0]), ey = (X[3] - X[1]) - (Y[3] - Y[1]);
	const double lx = sm_max (sm_max (fabs (X[2] - X[0]), fabs (X[3] - X[1])), sm_max (fabs (Y[2] - Y[0]), fabs (Y[3] - Y[1])));
	return sm_far4 (X, Y) < M && Y[4] == X[4] && fabs (Y[5] - X[5]) <= 0.35 * sm_max (X[5], Y[5]) + 16.0
	       && sm_max (fabs (ex), fabs (ey)) <= 0.35 * lx + 200.0;
}
/* how far apart, for the nearest: the ends moved + the stroke itself changed (by its ends alone a short line was nearer to
 * the short line across it next to it a frame on than to itself - the Ship Test's small parts) */
static double sm_dist (const double *X, const double *Y)
{
	const double ex = (X[2] - X[0]) - (Y[2] - Y[0]), ey = (X[3] - X[1]) - (Y[3] - Y[1]);
	return sm_far4 (X, Y) + sm_max (fabs (ex), fabs (ey));
}

/* the pairing's work: the strokes by their square (by their start), and for each stroke of a and of b the nearest of the
 * other frame's strokes alike, its distance and the next nearest's */
#define SM_SQOFF ((long long) 1 << 21)
#define SM_SQW ((long long) 1 << 23)
typedef struct { long long k; int i; } sm_sq;
static sm_sq *sm_qa, *sm_qb, *sm_qt; static int *sm_ba, *sm_bb; static unsigned char *sm_oa, *sm_ob;
static double *sm_da1, *sm_da2, *sm_fa1, *sm_db1, *sm_db2, *sm_fb1; static size_t sm_cp;
static int sm_cnt[4096];
/* in order of their square, those in one square in the order given (a radix sort, 12 bits a pass: the squares' numbers are
 * below 2^48) */
static void sm_sqsort (sm_sq *g, int n, sm_sq *t)
{
	for (int pass = 0; pass < 4; pass++) {
		const int sh = 12 * pass; sm_sq *x;
		memset (sm_cnt, 0, sizeof sm_cnt);
		for (int i = 0; i < n; i++) sm_cnt[(g[i].k >> sh) & 4095]++;
		for (int b = 0, sum = 0; b < 4096; b++) { const int c = sm_cnt[b]; sm_cnt[b] = sum; sum += c; }
		for (int i = 0; i < n; i++) t[sm_cnt[(g[i].k >> sh) & 4095]++] = g[i];
		x = g; g = t; t = x; }
}
/* a stroke's square of side s by its start (0: far off, none) */
static int sm_square (const double *S, double s, long long *cx, long long *cy)
{
	if (!(fabs (S[0]) <= 1e9) || !(fabs (S[1]) <= 1e9)) return 0;
	*cx = (long long) floor (S[0] / s); *cy = (long long) floor (S[1] / s); return 1;
}
/* a frame's strokes by their squares of side s, in order (the count) */
static int sm_grid (const double *S, int n, double s, sm_sq *g)
{
	int m = 0; long long cx, cy;
	for (int i = 0; i < n; i++) if (sm_square (S + 6 * (size_t) i, s, &cx, &cy)) { g[m].k = (cy + SM_SQOFF) * SM_SQW + cx + SM_SQOFF; g[m].i = i; m++; }
	sm_sqsort (g, m, sm_qt);
	return m;
}
/* stroke q of the other frame, d away (its ends f), for stroke p: the nearest (between equals the one nearer p's place in the
 * list), the next nearest's distance, how far its ends are from the nearest's */
static void sm_nearest (int *bq, double *d1, double *d2, double *f1, int p, int q, double d, double f)
{
	const int b = bq[p];
	if (b < 0 || d < d1[p] || (d == d1[p] && (abs (q - p) < abs (b - p) || (abs (q - p) == abs (b - p) && q < b)))) { d2[p] = d1[p]; d1[p] = d; f1[p] = f; bq[p] = q; }
	else if (d < d2[p]) d2[p] = d;
}
/* the first of g's squares (ng of them) numbered k0 or more */
static int sm_sqfind (const sm_sq *g, int ng, long long k0)
{
	int lo = 0, hi = ng;
	while (lo < hi) { const int mid = lo + ((hi - lo) >> 1); if (g[mid].k < k0) lo = mid + 1; else hi = mid; }
	return lo;
}
/* .. stroke p of SP against SQ's strokes alike in the 3 x 3 squares of side s around it (g: SQ's squares, ng of them) */
static void sm_side (const double *SP, int p, const double *SQ, const sm_sq *g, int ng, double s, double M, int *bq, double *d1, double *d2, double *f1)
{
	const double *X = SP + 6 * (size_t) p; long long cx, cy;
	if (!sm_square (X, s, &cx, &cy)) return;
	for (int dy = -1; dy <= 1; dy++) {
		const long long k0 = (cy + dy + SM_SQOFF) * SM_SQW + cx - 1 + SM_SQOFF, k1 = k0 + 2;
		for (int lo = sm_sqfind (g, ng, k0); lo < ng && g[lo].k <= k1; lo++) {
			const int q = g[lo].i; const double *Y = SQ + 6 * (size_t) q;
			if (sm_same (X, Y, M)) sm_nearest (bq, d1, d2, f1, p, q, sm_dist (X, Y), sm_far4 (X, Y)); } }
}
/* another stroke of S alike to stroke p within lim of it (g: S's squares of side M, R squares each way)? */
static int sm_crowd (const double *S, int p, const sm_sq *g, int ng, double M, int R, double lim)
{
	const double *X = S + 6 * (size_t) p; long long cx, cy;
	if (!sm_square (X, M, &cx, &cy)) return 0;
	for (int dy = -R; dy <= R; dy++) {
		const long long k0 = (cy + dy + SM_SQOFF) * SM_SQW + cx - R + SM_SQOFF, k1 = k0 + 2 * R;
		for (int lo = sm_sqfind (g, ng, k0); lo < ng && g[lo].k <= k1; lo++) {
			const int q = g[lo].i; const double *Y = S + 6 * (size_t) q;
			if (q != p && sm_same (X, Y, lim) && sm_dist (X, Y) < lim) return 1; } }
	return 0;
}
/* not settled by the squares of side s1: the nearest, or whether the next nearest is far enough, could lie further off (any
 * stroke outside the 3 x 3 squares is more than s1 away) */
static int sm_open (double d1, double d2, double f1, double s1)
{
	const double lim = SM_OK * d1 + SM_OA, need = f1 <= SM_NEAR ? d1 : (d2 < lim ? d2 : lim);
	return s1 - 1.0 < need;
}
/* the pairing of the two frames' strokes -> sm_ma, sm_mb (vxsmooth.c pairing): looked for within M / 4 first, within M where
 * that does not settle it */
static int sm_pairing (double M)
{
	const double *SA = sm_FA.S, *SB = sm_FB.S; const int la = sm_FA.ns, lb = sm_FB.ns; int na, nb, ua = 0, ub = 0, coarse = 0;
	const double s1 = M / 4.0; size_t c = (size_t) (la > lb ? la : lb);
	if (sm_cma < (size_t) la + 1) { free (sm_ma); sm_cma = (size_t) la + 256; sm_ma = malloc (sm_cma * sizeof (int)); if (!sm_ma) { sm_cma = 0; return -1; } }
	if (sm_cmb < (size_t) lb + 1) { free (sm_mb); sm_cmb = (size_t) lb + 256; sm_mb = malloc (sm_cmb * sizeof (int)); if (!sm_mb) { sm_cmb = 0; return -1; } }
	for (int i = 0; i < la; i++) sm_ma[i] = -1;
	for (int i = 0; i < lb; i++) sm_mb[i] = -1;
	if (sm_cp < c + 1) {
		c += 256;
		free (sm_qa); free (sm_qb); free (sm_qt); free (sm_ba); free (sm_bb); free (sm_oa); free (sm_ob);
		free (sm_da1); free (sm_da2); free (sm_fa1); free (sm_db1); free (sm_db2); free (sm_fb1);
		sm_qa = malloc (c * sizeof (sm_sq)); sm_qb = malloc (c * sizeof (sm_sq)); sm_qt = malloc (c * sizeof (sm_sq));
		sm_ba = malloc (c * sizeof (int)); sm_bb = malloc (c * sizeof (int)); sm_oa = malloc (c); sm_ob = malloc (c);
		sm_da1 = malloc (c * sizeof (double)); sm_da2 = malloc (c * sizeof (double)); sm_fa1 = malloc (c * sizeof (double));
		sm_db1 = malloc (c * sizeof (double)); sm_db2 = malloc (c * sizeof (double)); sm_fb1 = malloc (c * sizeof (double));
		if (!sm_qa || !sm_qb || !sm_qt || !sm_ba || !sm_bb || !sm_oa || !sm_ob || !sm_da1 || !sm_da2 || !sm_fa1 || !sm_db1 || !sm_db2 || !sm_fb1) {
			free (sm_qa); free (sm_qb); free (sm_qt); free (sm_ba); free (sm_bb); free (sm_oa); free (sm_ob);
			free (sm_da1); free (sm_da2); free (sm_fa1); free (sm_db1); free (sm_db2); free (sm_fb1);
			sm_qa = sm_qb = sm_qt = NULL; sm_ba = sm_bb = NULL; sm_oa = sm_ob = NULL; sm_da1 = sm_da2 = sm_fa1 = sm_db1 = sm_db2 = sm_fb1 = NULL; sm_cp = 0;
			return -1; }
		sm_cp = c; }
	for (int i = 0; i < la; i++) { sm_ba[i] = -1; sm_da1[i] = sm_da2[i] = sm_fa1[i] = HUGE_VAL; }
	for (int i = 0; i < lb; i++) { sm_bb[i] = -1; sm_db1[i] = sm_db2[i] = sm_fb1[i] = HUGE_VAL; }
	na = sm_grid (SA, la, s1, sm_qa); nb = sm_grid (SB, lb, s1, sm_qb);
	for (int i = 0; i < la; i++) sm_side (SA, i, SB, sm_qb, nb, s1, M, sm_ba, sm_da1, sm_da2, sm_fa1);
	for (int i = 0; i < lb; i++) sm_side (SB, i, SA, sm_qa, na, s1, M, sm_bb, sm_db1, sm_db2, sm_fb1);
	for (int i = 0; i < la; i++) ua += (sm_oa[i] = (unsigned char) sm_open (sm_da1[i], sm_da2[i], sm_fa1[i], s1));
	for (int i = 0; i < lb; i++) ub += (sm_ob[i] = (unsigned char) sm_open (sm_db1[i], sm_db2[i], sm_fb1[i], s1));
	if (ua || ub) {                       /* (those: among all within M) */
		na = sm_grid (SA, la, M, sm_qa); nb = sm_grid (SB, lb, M, sm_qb); coarse = 1;
		for (int i = 0; i < la; i++)
			if (sm_oa[i]) { sm_ba[i] = -1; sm_da1[i] = sm_da2[i] = sm_fa1[i] = HUGE_VAL; sm_side (SA, i, SB, sm_qb, nb, M, M, sm_ba, sm_da1, sm_da2, sm_fa1); }
		for (int i = 0; i < lb; i++)
			if (sm_ob[i]) { sm_bb[i] = -1; sm_db1[i] = sm_db2[i] = sm_fb1[i] = HUGE_VAL; sm_side (SB, i, SA, sm_qa, na, M, M, sm_bb, sm_db1, sm_db2, sm_fb1); } }
	for (int i = 0; i < la; i++) {
		const int j = sm_ba[i]; const double d = sm_da1[i]; double lim;
		if (j < 0 || sm_bb[j] != i) continue;
		lim = SM_OK * d + SM_OA;
		if (sm_fa1[i] <= SM_NEAR) { sm_ma[i] = j; sm_mb[j] = i; continue; }
		if (!(sm_da2[i] >= lim && sm_db2[j] >= lim)) continue;
		if (!coarse) { na = sm_grid (SA, la, M, sm_qa); nb = sm_grid (SB, lb, M, sm_qb); coarse = 1; }
		{                                 /* (moved further: another like it that near in its own frame?) */
			const int R = (int) (SM_OK + SM_OA / M) + 1;
			if (sm_crowd (SA, i, sm_qa, na, M, R, lim) || sm_crowd (SB, j, sm_qb, nb, M, R, lim)) continue; }
		sm_ma[i] = j; sm_mb[j] = i; }
	return 0;
}

/* numpy's interp (the PC's lookup), its search: the place j with arr[j] <= key < arr[j + 1], looked for near the last one
 * found first (-1: before the first, len: past the last) - numpy's binary_search_with_guess, step for step (a stroke's
 * pieces' ends are not always in order by a rounding: where it looks first shows) */
static int sm_find (double key, const double *arr, int len, int guess)
{
	int imin = 0, imax = len;
	if (key > arr[len - 1]) return len;
	else if (key < arr[0]) return -1;
	if (len <= 4) { int i; for (i = 1; i < len && key >= arr[i]; ++i) ; return i - 1; }
	if (guess > len - 3) guess = len - 3;
	if (guess < 1) guess = 1;
	if (key < arr[guess]) {
		if (key < arr[guess - 1]) { imax = guess - 1; if (guess > 8 && key >= arr[guess - 8]) imin = guess - 8; }
		else return guess - 1; }
	else {
		if (key < arr[guess + 1]) return guess;
		else if (key < arr[guess + 2]) return guess + 1;
		else { imin = guess + 2; if (guess < len - 8 - 1 && key < arr[guess + 8]) imax = guess + 8; } }
	while (imin < imax) { const int imid = imin + ((imax - imin) >> 1); if (key >= arr[imid]) imin = imid + 1; else imax = imid; }
	return imin - 1;
}
/* .. and its value at the place found */
static double sm_at (double x, int j, const double *dx, const double *dy, int len)
{
	double slope, r;
	if (j == -1) return dy[0];
	if (j == len) return dy[len - 1];
	if (j == len - 1) return dy[j];
	if (dx[j] == x) return dy[j];
	slope = (dy[j + 1] - dy[j]) / (dx[j + 1] - dx[j]);
	r = slope * (x - dx[j]) + dy[j];
	if (r != r) { r = slope * (x - dx[j + 1]) + dy[j + 1]; if (r != r && dy[j] == dy[j + 1]) r = dy[j]; }
	return r;
}
/* the pieces of frame g (its strokes f, their partners mp in the other frame o) as o draws them: each one's ends looked up
 * along its partner at the same share of its drawing time (T: 4 a piece; d: how far it moves, 0 for one without a partner) */
static void sm_look (const gframe *g, const sm_fr *f, const int *mp, const sm_fr *o, double *T, double *d)
{
	int j0 = 0, j1 = 0; const int len = 2 * o->n;
	for (int i = 0; i < g->n; i++) {
		const line_t *l = &g->L[i]; const int m = mp[f->sid[i]]; double *t = T + 4 * (size_t) i;
		const double q0 = (double) m + 0.999 * f->f0[i], q1 = (double) m + 0.999 * f->f1[i];
		j0 = sm_find (q0, o->pu, len, j0); t[0] = sm_at (q0, j0, o->pu, o->px, len); t[1] = sm_at (q0, j0, o->pu, o->py, len);
		j1 = sm_find (q1, o->pu, len, j1); t[2] = sm_at (q1, j1, o->pu, o->px, len); t[3] = sm_at (q1, j1, o->pu, o->py, len);
		if (d) d[i] = m < 0 ? 0.0 : sm_max (sm_max (sm_max (fabs (t[0] - (double) l->x0), fabs (t[1] - (double) l->y0)), fabs (t[2] - (double) l->x1)), fabs (t[3] - (double) l->y1)); }
}
static double *sm_T, *sm_d; static size_t sm_cT;
/* .. frame g's pieces into W1 (from n; the new n back): those with a partner moved by w towards where the other frame draws
 * them, the others as drawn */
static int sm_put (int n, const gframe *g, const sm_fr *f, const int *mp, double w)
{
	for (int i = 0; i < g->n && n < NW; i++) {
		const line_t *l = &g->L[i]; n = wl_from (W1, n, l, 1, NULL, 0);
		if (mp[f->sid[i]] >= 0) { wline *o = &W1[n - 1]; const double *t = sm_T + 4 * (size_t) i;
			o->x0 = (double) l->x0 + (t[0] - (double) l->x0) * w; o->y0 = (double) l->y0 + (t[1] - (double) l->y0) * w;
			o->x1 = (double) l->x1 + (t[2] - (double) l->x1) * w; o->y1 = (double) l->y1 + (t[3] - (double) l->y1) * w; } }
	return n;
}
/* the picture between a and b at alpha into W1 (from n; the new n back): before halfway a's pieces moved towards where b draws
 * them, after it b's pieces moved back towards a's; a stroke without a certain partner stands, in the nearer frame only.
 * ex: a's pieces the moving line has cleared by SM_EXW start their afterglow now (sm_exf, sm_exi: libretro_vx.c); a piece
 * b draws in the same place stays lit - none to start */
static int sm_draw (const gframe *a, const gframe *b, double alpha, int n, int ex)
{
	const size_t need = (size_t) (a->n > b->n ? a->n : b->n) + 1;
	if (sm_cT < need) { free (sm_T); free (sm_d); sm_cT = need + 256; sm_T = malloc (4 * sm_cT * sizeof (double)); sm_d = malloc (sm_cT * sizeof (double));
		if (!sm_T || !sm_d) { free (sm_T); free (sm_d); sm_T = sm_d = NULL; sm_cT = 0; } }
	if (!sm_T || a->n <= 0 || b->n <= 0 || sm_strokes (a, &sm_FA) < 0 || sm_strokes (b, &sm_FB) < 0 || sm_pairing (SMOOTH_MATCH) < 0)
		return wl_from (W1, n, a->L, a->n, NULL, 0);   /* (as the PC: b without lines - or no memory - a as drawn) */
	sm_look (a, &sm_FA, sm_ma, &sm_FB, sm_T, sm_d);
	if (ex) for (int i = 0; i < a->n && i < sm_exf_n; i++) if (!sm_exf[i] && sm_ma[sm_FA.sid[i]] >= 0) { const double d = sm_d[i];
		if (d < 1.5) sm_exf[i] = 2;          /* (drawn in the same place by b: it stays lit - no afterglow to start, nothing to build) */
		else if (d >= SM_EXW && alpha * d >= SM_EXW) { sm_exf[i] = 1; sm_exi[sm_exc++] = i; } }
	if (alpha < 0.5) return sm_put (n, a, &sm_FA, sm_ma, alpha);
	sm_look (b, &sm_FB, sm_mb, &sm_FA, sm_T, NULL);
	return sm_put (n, b, &sm_FB, sm_mb, 1.0 - alpha);
}

/* the afterglow's lines (vxsmooth.c vxs_merge, vectrex_emu.py sm_merge): the pieces of one straight line as one line - joined
 * end to start, one brightness, one direction, one colour; their drawing time together - into W1 (from m; the new m back).
 * Where an afterglow starts, how a line was cut in time shows nothing, and a picture of 1200 moving pieces (Mine Storm's
 * mothership) is about a hundred lines */
static int sm_merge (int m, const line_t *X, int n)
{
	int first = 0, okp = 0; double uxp = 0.0, uyp = 0.0;
	for (int i = 0; i <= n; i++) {
		int nw = 1, ok = 0; double ux = 0.0, uy = 0.0;
		if (i < n) {
			const line_t *r = &X[i]; const double dx = (double) r->x1 - (double) r->x0, dy = (double) r->y1 - (double) r->y0, ln = hypot (dx, dy), l9 = sm_max (ln, 1e-9);
			ok = ln > 0; ux = ok ? dx / l9 : 0.0; uy = ok ? dy / l9 : 0.0;
			if (i > 0) { const line_t *p = r - 1;
				nw = !(r->x0 == p->x1 && r->y0 == p->y1 && r->c == p->c && r->rgb == p->rgb && ok && okp
				       && fabs (ux * uyp - uy * uxp) < 0.02 && (ux * uxp + uy * uyp) > 0); } }
		if (nw && i > 0) {                /* (the line before ends: first .. i - 1) */
			line_t o = X[first]; unsigned long long cy = 0;
			for (int k = first; k <= i - 1; k++) cy += X[k].cyc;
			o.x1 = X[i - 1].x1; o.y1 = X[i - 1].y1; o.cyc = cy > 0xffffffffu ? 0xffffffffu : (unsigned) cy;
			m = wl_from (W1, m, &o, 1, NULL, 0); first = i; }
		uxp = ux; uyp = uy; okp = ok; }
	return m;
}
