/* smooth.c - Vectrex_EMU's libretro core: Smooth motion's in-between picture, stroke by stroke (included by libretro_vx.c;
 * the desktop's vectrex_emu.py sm_align).
 *
 * The beam draws a stroke - pieces joined end to start, one brightness - and the emulator cuts it into pieces by time, a
 * little differently each frame (16 24 40 200 8 8 ... cycles in one, 16 16 24 40 192 8 8 ... in the next). The pieces of two
 * frames do not pair up; the strokes do. So strokes are matched (their ends near, their brightness the same, about as long
 * to draw), and a piece's ends are looked up along the other frame's stroke at the same share of its drawing time. Matching
 * piece to piece (a day, 2026-11-22) paired neighbours wherever a frame had a piece more: shapes broke into dashes - the
 * owner's recording of Mine Storm's mothership.
 * Copyright (c) 2026 Hugo Bokhorst. Part of Vectrex_EMU; see LICENSE.md. */
typedef struct { int first, last; double tot; } sm_stk;
static sm_stk *sm_SA, *sm_SB; static float *sm_fa, *sm_fb; static int *sm_ma, *sm_mb, sm_capA, sm_capB;
/* a frame's strokes; F: each piece's share of its stroke's time at its start and its end (2 a piece) */
static int sm_build (const gframe *g, sm_stk **S, float **F, int **M, int *cap)
{
	if (*cap < g->n + 1) { free (*S); free (*F); free (*M); *cap = g->n + 256;
		*S = malloc (sizeof (sm_stk) * (size_t) *cap); *F = malloc (sizeof (float) * 2 * (size_t) *cap); *M = malloc (sizeof (int) * (size_t) *cap); }
	if (!*S || !*F || !*M) { free (*S); free (*F); free (*M); *S = NULL; *F = NULL; *M = NULL; *cap = 0; return -1; }
	int ns = 0;
	for (int i = 0; i < g->n; ) { int j = i; double tot = 0;
		do { tot += g->L[j].cyc > 0 ? g->L[j].cyc : 0.001; j++; }
		while (j < g->n && g->L[j].x0 == g->L[j - 1].x1 && g->L[j].y0 == g->L[j - 1].y1 && g->L[j].c == g->L[j - 1].c);
		double c = 0;
		for (int k = i; k < j; k++) { const double w = g->L[k].cyc > 0 ? g->L[k].cyc : 0.001; (*F)[2 * k] = (float) (c / tot); c += w; (*F)[2 * k + 1] = (float) (c / tot); }
		(*S)[ns].first = i; (*S)[ns].last = j - 1; (*S)[ns].tot = tot; ns++; i = j; }
	return ns;
}
static int sm_same (const gframe *a, const sm_stk *p, const gframe *b, const sm_stk *q)   /* (the same stroke a frame on) */
{
	const line_t *a0 = &a->L[p->first], *a1 = &a->L[p->last], *b0 = &b->L[q->first], *b1 = &b->L[q->last]; const double mt = p->tot > q->tot ? p->tot : q->tot;
	return a0->c == b0->c && labs (b0->x0 - a0->x0) < SMOOTH_MATCH && labs (b0->y0 - a0->y0) < SMOOTH_MATCH
	       && labs (b1->x1 - a1->x1) < SMOOTH_MATCH && labs (b1->y1 - a1->y1) < SMOOTH_MATCH && fabs (p->tot - q->tot) <= 0.35 * mt + 16.0;
}
/* the point of stroke s at the share f of its time (cur: the piece last looked at - the shares asked for go up) */
static void sm_at (const gframe *g, const sm_stk *s, const float *F, int *cur, double f, double *x, double *y)
{
	int k = *cur; if (k < s->first || k > s->last) k = s->first;
	while (k < s->last && F[2 * k + 1] < f) k++;
	while (k > s->first && F[2 * k] > f) k--;
	*cur = k; const line_t *l = &g->L[k]; const double w = F[2 * k + 1] - F[2 * k]; double t = w > 1e-9 ? (f - F[2 * k]) / w : 0; t = t < 0 ? 0 : t > 1 ? 1 : t;
	*x = l->x0 + (l->x1 - l->x0) * t; *y = l->y0 + (l->y1 - l->y0) * t;
}
/* the picture between a and b at alpha into W1 (from n; the new n back): before halfway a's pieces moved towards where b draws
 * them, after it b's pieces moved back towards a's; a stroke without a partner stands, in the nearer frame only.
 * ex: a's pieces the moving line has cleared by SM_EXW start their afterglow now (sm_exf, sm_exi: libretro_vx.c) */
static int sm_draw (const gframe *a, const gframe *b, double alpha, int n, int ex)
{
	const int na = sm_build (a, &sm_SA, &sm_fa, &sm_ma, &sm_capA), nb = sm_build (b, &sm_SB, &sm_fb, &sm_mb, &sm_capB);
	if (na < 0 || nb < 0) return wl_from (W1, n, a->L, a->n, NULL, 0);
	for (int i = 0; i < na; i++) sm_ma[i] = -1;
	for (int j = 0; j < nb; j++) sm_mb[j] = -1;
	if (na == nb) { for (int k = 0; k < na; k++) if (sm_same (a, &sm_SA[k], b, &sm_SB[k])) sm_ma[k] = sm_mb[k] = k; }
	else {
		/* (a stroke more or less: the two lists laid along each other for the least total distance - a stroke paired with the
		 *  one at its place, or left out at the price of a stroke SMOOTH_MATCH away; within a band of the count's difference
		 *  + SM_LOOK. Walked side by side with "near enough" as the test (an hour, 2026-11-22), a small shape's strokes are
		 *  all near each other: the lists went on one out of step, the shape scrambled for that frame - the owner's
		 *  recording, Mine Storm's mothership at its smallest) */
		const int Wb = abs (na - nb) + SM_LOOK, K = 2 * Wb + 1; const double INF = 1e18, G = SMOOTH_MATCH;
		static double *D; static unsigned char *BP; static size_t cap; const size_t need = (size_t) (na + 1) * K;
		if (cap < need) { free (D); free (BP); cap = need + 4096; D = malloc (sizeof (double) * cap); BP = malloc (cap); if (!D || !BP) { free (D); free (BP); D = NULL; BP = NULL; cap = 0; } }
		if (D && BP) {
			for (size_t q = 0; q < need; q++) { D[q] = INF; BP[q] = 0; }
			D[Wb] = 0;
			for (int i = 0; i <= na; i++) for (int k = 0; k < K; k++) { const double d = D[(size_t) i * K + k]; const int j = i + k - Wb;
				if (d >= INF || j < 0 || j > nb) continue;
				if (k + 1 < K && j + 1 <= nb && d + G < D[(size_t) i * K + k + 1]) { D[(size_t) i * K + k + 1] = d + G; BP[(size_t) i * K + k + 1] = 2; }   /* (b's stroke left out) */
				if (i < na) {
					if (j < nb && sm_same (a, &sm_SA[i], b, &sm_SB[j])) { const line_t *a0 = &a->L[sm_SA[i].first], *a1 = &a->L[sm_SA[i].last], *b0 = &b->L[sm_SB[j].first], *b1 = &b->L[sm_SB[j].last];
						long c = labs (b0->x0 - a0->x0), e = labs (b0->y0 - a0->y0); if (e > c) c = e; e = labs (b1->x1 - a1->x1); if (e > c) c = e; e = labs (b1->y1 - a1->y1); if (e > c) c = e;
						if (d + c < D[(size_t) (i + 1) * K + k]) { D[(size_t) (i + 1) * K + k] = d + c; BP[(size_t) (i + 1) * K + k] = 1; } }   /* (paired) */
					if (k - 1 >= 0 && d + G < D[(size_t) (i + 1) * K + k - 1]) { D[(size_t) (i + 1) * K + k - 1] = d + G; BP[(size_t) (i + 1) * K + k - 1] = 3; } } }   /* (a's left out) */
			int i = na, k = nb - na + Wb;
			if (k >= 0 && k < K && D[(size_t) i * K + k] < INF)
				while (i > 0 || i + k - Wb > 0) { const int t = BP[(size_t) i * K + k];
					if (t == 1) { i--; sm_ma[i] = i + k - Wb; sm_mb[i + k - Wb] = i; }
					else if (t == 2) k--;
					else if (t == 3) { i--; k++; }
					else break; } } }
	const int first = alpha < 0.5;
	for (int s = 0; s < na; s++) { const sm_stk *p = &sm_SA[s]; const int m = sm_ma[s]; int cur = m >= 0 ? sm_SB[m].first : 0;
		for (int i = p->first; i <= p->last; i++) { const line_t *l = &a->L[i];
			if (m < 0) { if (first) n = wl_from (W1, n, l, 1, NULL, 0); continue; }
			line_t q = *l; double x, y;
			sm_at (b, &sm_SB[m], sm_fb, &cur, sm_fa[2 * i], &x, &y); q.x0 = lround (x); q.y0 = lround (y);
			sm_at (b, &sm_SB[m], sm_fb, &cur, sm_fa[2 * i + 1], &x, &y); q.x1 = lround (x); q.y1 = lround (y);
			if (first) n = wl_from (W1, n, l, 1, &q, alpha);
			if (ex && i < sm_exf_n && !sm_exf[i]) { long d = labs (q.x0 - l->x0), e = labs (q.y0 - l->y0); if (e > d) d = e;
				e = labs (q.x1 - l->x1); if (e > d) d = e; e = labs (q.y1 - l->y1); if (e > d) d = e;
				if (d < 2) sm_exf[i] = 2;                 /* (drawn in the same place by b: it stays lit - no afterglow to start, nothing to build) */
				else if (d >= SM_EXW && alpha * d >= SM_EXW) { sm_exf[i] = 1; sm_exi[sm_exc++] = i; } } } }
	if (!first) for (int s = 0; s < nb; s++) { const sm_stk *p = &sm_SB[s]; const int m = sm_mb[s]; int cur = m >= 0 ? sm_SA[m].first : 0;
		for (int i = p->first; i <= p->last; i++) { const line_t *l = &b->L[i];
			if (m < 0) { n = wl_from (W1, n, l, 1, NULL, 0); continue; }
			line_t q = *l; double x, y;
			sm_at (a, &sm_SA[m], sm_fa, &cur, sm_fb[2 * i], &x, &y); q.x0 = lround (x); q.y0 = lround (y);
			sm_at (a, &sm_SA[m], sm_fa, &cur, sm_fb[2 * i + 1], &x, &y); q.x1 = lround (x); q.y1 = lround (y);
			n = wl_from (W1, n, l, 1, &q, 1.0 - alpha); } }
	return n;
}
