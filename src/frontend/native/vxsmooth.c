/* vxsmooth.c - the PC's Smooth motion in C (vectrex_emu.py: App.sm_align, the in-between picture of App.smooth_frame,
   App.sm_merge): the same arithmetic in the same order, piece by piece instead of numpy array by array. Part of
   vxbuild.dll, loaded with ctypes (App.sm_frame_c, App.sm_merge); the numpy code stays as the reference and the way
   without this library. One call a picture (vxs_frame), one more when two frames were newly paired (vxs_pair: the
   pairing, for Python to keep and hand back each refresh), one a game frame for the afterglow's lines (vxs_merge).
   numpy's own ways are followed where the order of the work shows in the last bit: interp's search (it starts near the
   place found before - a stroke's pieces' ends are not always in order by a rounding) and add.reduceat's sum in pairs.
   Doubles throughout, no fused multiply-add: the results are numpy's.
   Part of Vectrex_EMU; see LICENSE.md. */
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>

#ifdef _MSC_VER
#pragma fp_contract(off)
#define VXB_API __declspec(dllexport)
#else
#pragma STDC FP_CONTRACT OFF
#define VXB_API __attribute__((visibility("default")))
#endif

/* a frame's strokes (sm_align: strokes) */
typedef struct {
    int n, ns;                                                      /* pieces, strokes */
    int *sid;                                                       /* a piece's stroke */
    double *f0, *f1;                                                /* a piece's ends: the share of its stroke's drawing time */
    double *pu, *px, *py;                                           /* the path: stroke + 0.999 x share -> the place (2 a piece) */
    double *S;                                                      /* a stroke: its ends, brightness, time (6) */
    double *c;                                                      /* (the cycles summed) */
    size_t cap;
} Fr;
static Fr g_fa, g_fb;
static int64_t *g_ma, *g_mb; static size_t g_cma, g_cmb;            /* the pairing: a's stroke -> b's or -1, b's -> a's */
static double *g_dA; static size_t g_cdA;

static int fr_room(Fr *f, int n)
{
    size_t c;
    if ((size_t)n <= f->cap) return 0;
    c = (size_t)n + 256;
    free(f->sid); free(f->f0); free(f->f1); free(f->pu); free(f->px); free(f->py); free(f->S); free(f->c);
    f->sid = (int *)malloc(c * sizeof(int));
    f->f0 = (double *)malloc(c * sizeof(double)); f->f1 = (double *)malloc(c * sizeof(double));
    f->pu = (double *)malloc(2 * c * sizeof(double)); f->px = (double *)malloc(2 * c * sizeof(double));
    f->py = (double *)malloc(2 * c * sizeof(double)); f->S = (double *)malloc(6 * c * sizeof(double));
    f->c = (double *)malloc(c * sizeof(double));
    if (!f->sid || !f->f0 || !f->f1 || !f->pu || !f->px || !f->py || !f->S || !f->c) {
        free(f->sid); free(f->f0); free(f->f1); free(f->pu); free(f->px); free(f->py); free(f->S); free(f->c);
        f->sid = NULL; f->f0 = f->f1 = f->pu = f->px = f->py = f->S = f->c = NULL; f->cap = 0;
        return -1;
    }
    f->cap = c;
    return 0;
}

static double cyc_of(double v) { return v != v ? v : (v > 1e-3 ? v : 1e-3); }

/* the strokes of a frame's pieces (rows of `cols` doubles: x0 y0 x1 y1 brightness cycles time ..) */
static int strokes(const double *X, int n, int cols, Fr *f)
{
    int i, s = -1, first = 0;
    double c = 0.0, base = 0.0, tot;
    if (fr_room(f, n) < 0) return -1;
    f->n = n;
    for (i = 0; i < n; i++) {                                       /* a piece's stroke; the cycles summed, as numpy's cumsum */
        const double *r = X + (size_t)i * cols;
        if (i == 0 || !(r[0] == r[2 - cols] && r[1] == r[3 - cols] && r[4] == r[4 - cols])) s++;
        f->sid[i] = s;
        c = i ? c + cyc_of(r[5]) : cyc_of(r[5]);
        f->c[i] = c;
    }
    f->ns = s + 1;
    for (i = 0; i < n; i++) {
        const double *r = X + (size_t)i * cols; double cy = cyc_of(r[5]);
        int last;
        if (i == 0 || f->sid[i] != f->sid[i - 1]) {                 /* a stroke starts: its time, its ends */
            double *S = f->S + 6 * (size_t)f->sid[i]; const double *rl;
            first = i; for (last = i; last + 1 < n && f->sid[last + 1] == f->sid[i]; last++) ;
            base = f->c[first] - cy; tot = f->c[last] - base;
            rl = X + (size_t)last * cols;
            S[0] = r[0]; S[1] = r[1]; S[2] = rl[2]; S[3] = rl[3]; S[4] = r[4]; S[5] = tot;
        }
        tot = f->S[6 * (size_t)f->sid[i] + 5];
        f->f0[i] = (f->c[i] - cy - base) / tot; f->f1[i] = (f->c[i] - base) / tot;
        f->pu[2 * i] = (double)f->sid[i] + 0.999 * f->f0[i]; f->pu[2 * i + 1] = (double)f->sid[i] + 0.999 * f->f1[i];
        f->px[2 * i] = r[0]; f->px[2 * i + 1] = r[2]; f->py[2 * i] = r[1]; f->py[2 * i + 1] = r[3];
    }
    return 0;
}

static double max2(double a, double b) { return a != a ? a : (b != b ? b : (a > b ? a : b)); }      /* numpy's maximum */

/* how far two strokes' ends are apart (the largest of the four) */
static double far4(const double *X, const double *Y)
{
    return max2(max2(fabs(Y[0] - X[0]), fabs(Y[1] - X[1])), max2(fabs(Y[2] - X[2]), fabs(Y[3] - X[3])));
}

/* the same stroke a frame on (sm_align: same) */
static int same(const double *X, const double *Y, double M)
{
    double ex = (X[2] - X[0]) - (Y[2] - Y[0]), ey = (X[3] - X[1]) - (Y[3] - Y[1]);
    double lx = max2(max2(fabs(X[2] - X[0]), fabs(X[3] - X[1])), max2(fabs(Y[2] - Y[0]), fabs(Y[3] - Y[1])));
    return far4(X, Y) < M && Y[4] == X[4]
           && fabs(Y[5] - X[5]) <= 0.35 * max2(X[5], Y[5]) + 16.0
           && max2(fabs(ex), fabs(ey)) <= 0.35 * lx + 200.0;
}

/* the pairing's work: the strokes by their square (by their start), and for each stroke of a and of b the nearest of the
   other frame's strokes alike, its distance and the next nearest's */
#define SQ_OFF ((int64_t)1 << 21)
#define SQ_W ((int64_t)1 << 23)
typedef struct { int64_t k; int i; } Sq;
static Sq *g_qa, *g_qb, *g_qt; static int *g_ba, *g_bb; static double *g_da1, *g_da2, *g_fa1, *g_db1, *g_db2, *g_fb1;
static size_t g_cp;

/* in order of their square, those in one square in the order given (a radix sort, 12 bits a pass: the squares' numbers
   are below 2^48); t: room for n */
static void sq_sort(Sq *g, int n, Sq *t)
{
    int cnt[4096], pass, i, b, sum;
    for (pass = 0; pass < 4; pass++) {
        int sh = 12 * pass; Sq *x;
        memset(cnt, 0, sizeof cnt);
        for (i = 0; i < n; i++) cnt[(g[i].k >> sh) & 4095]++;
        for (b = 0, sum = 0; b < 4096; b++) { int c = cnt[b]; cnt[b] = sum; sum += c; }
        for (i = 0; i < n; i++) t[cnt[(g[i].k >> sh) & 4095]++] = g[i];
        x = g; g = t; t = x;
    }
}

/* a stroke's square of side s by its start (0: far off, none) */
static int square(const double *S, double s, int64_t *cx, int64_t *cy)
{
    if (!(fabs(S[0]) <= 1e9) || !(fabs(S[1]) <= 1e9)) return 0;
    *cx = (int64_t)floor(S[0] / s); *cy = (int64_t)floor(S[1] / s);
    return 1;
}

/* a frame's strokes by their squares of side s, in order (the count) */
static int grid(const double *S, int n, double s, Sq *g)
{
    int i, m = 0; int64_t cx, cy;
    for (i = 0; i < n; i++)
        if (square(S + 6 * (size_t)i, s, &cx, &cy)) { g[m].k = (cy + SQ_OFF) * SQ_W + cx + SQ_OFF; g[m].i = i; m++; }
    sq_sort(g, m, g_qt);
    return m;
}

/* how far apart two strokes are, for the nearest: their ends moved + the stroke itself changed (sm_align: dist) */
static double dist(const double *X, const double *Y)
{
    double ex = (X[2] - X[0]) - (Y[2] - Y[0]), ey = (X[3] - X[1]) - (Y[3] - Y[1]);
    return far4(X, Y) + max2(fabs(ex), fabs(ey));
}

/* stroke q of the other frame, d away (its ends f), for stroke p: the nearest (between equals the one nearer p's place in
   the list), the next nearest's distance, how far its ends are from the nearest's */
static void nearest(int *bq, double *d1, double *d2, double *f1, int p, int q, double d, double f)
{
    int b = bq[p];
    if (b < 0 || d < d1[p] || (d == d1[p] && (abs(q - p) < abs(b - p) || (abs(q - p) == abs(b - p) && q < b)))) {
        d2[p] = d1[p]; d1[p] = d; f1[p] = f; bq[p] = q;
    }
    else if (d < d2[p]) d2[p] = d;
}

/* the first of g's squares (ng of them) numbered k0 or more */
static int sq_find(const Sq *g, int ng, int64_t k0)
{
    int lo = 0, hi = ng;
    while (lo < hi) { int mid = lo + ((hi - lo) >> 1); if (g[mid].k < k0) lo = mid + 1; else hi = mid; }
    return lo;
}

/* .. stroke p of SP against SQ's strokes alike in the 3 x 3 squares of side s around it (g: SQ's squares, ng of them) */
static void side(const double *SP, int p, const double *SQ, const Sq *g, int ng, double s, double M, int *bq, double *d1,
                 double *d2, double *f1)
{
    const double *X = SP + 6 * (size_t)p; int64_t cx, cy; int dy, lo;
    if (!square(X, s, &cx, &cy)) return;
    for (dy = -1; dy <= 1; dy++) {
        int64_t k0 = (cy + dy + SQ_OFF) * SQ_W + cx - 1 + SQ_OFF, k1 = k0 + 2;
        for (lo = sq_find(g, ng, k0); lo < ng && g[lo].k <= k1; lo++) {
            int q = g[lo].i; const double *Y = SQ + 6 * (size_t)q;
            if (same(X, Y, M)) nearest(bq, d1, d2, f1, p, q, dist(X, Y), far4(X, Y));
        }
    }
}

/* another stroke of S alike to stroke p within lim of it (g: S's squares of side M, R squares each way)? */
static int crowd(const double *S, int p, const Sq *g, int ng, double M, int R, double lim)
{
    const double *X = S + 6 * (size_t)p; int64_t cx, cy; int dy, lo;
    if (!square(X, M, &cx, &cy)) return 0;
    for (dy = -R; dy <= R; dy++) {
        int64_t k0 = (cy + dy + SQ_OFF) * SQ_W + cx - R + SQ_OFF, k1 = k0 + 2 * R;
        for (lo = sq_find(g, ng, k0); lo < ng && g[lo].k <= k1; lo++) {
            int q = g[lo].i; const double *Y = S + 6 * (size_t)q;
            if (q != p && same(X, Y, lim) && dist(X, Y) < lim) return 1;
        }
    }
    return 0;
}

/* not settled by the squares of side s1: the nearest, or whether the next nearest is far enough, could lie further off
   (any stroke outside the 3 x 3 squares is more than s1 away) */
static int open_(double d1, double d2, double f1, double s1, double nearr, double ok, double oa)
{
    double lim = ok * d1 + oa, need = f1 <= nearr ? d1 : (d2 < lim ? d2 : lim);
    return s1 - 1.0 < need;
}

/* the pairing of the two frames' strokes -> g_ma, g_mb (sm_align): by place - each stroke with the nearest of the other
   frame's strokes alike, where each is the other's nearest and the pair is certain: its ends moved no further than `nearr`, or no
   other stroke alike within `ok` x the move + `oa` of either, in the other frame or its own (identical shapes in a
   formation, a row left out of a frame: vectrex_emu.py SM_NEAR, SM_OTHER). Looked for within M / 4 first, within M where
   that does not settle it */
static int pairing(double M, double nearr, double ok, double oa)
{
    const double *SA = g_fa.S, *SB = g_fb.S; int la = g_fa.ns, lb = g_fb.ns, i, na, nb, ua = 0, ub = 0, coarse = 0;
    double s1 = M / 4.0; size_t c = (size_t)(la > lb ? la : lb);
    for (i = 0; i < la; i++) g_ma[i] = -1;
    for (i = 0; i < lb; i++) g_mb[i] = -1;
    if (g_cp < c) {
        c += 256;
        free(g_qa); free(g_qb); free(g_qt); free(g_ba); free(g_bb);
        free(g_da1); free(g_da2); free(g_fa1); free(g_db1); free(g_db2); free(g_fb1);
        g_qa = (Sq *)malloc(c * sizeof(Sq)); g_qb = (Sq *)malloc(c * sizeof(Sq)); g_qt = (Sq *)malloc(c * sizeof(Sq));
        g_ba = (int *)malloc(c * sizeof(int)); g_bb = (int *)malloc(c * sizeof(int));
        g_da1 = (double *)malloc(c * sizeof(double)); g_da2 = (double *)malloc(c * sizeof(double)); g_fa1 = (double *)malloc(c * sizeof(double));
        g_db1 = (double *)malloc(c * sizeof(double)); g_db2 = (double *)malloc(c * sizeof(double)); g_fb1 = (double *)malloc(c * sizeof(double));
        if (!g_qa || !g_qb || !g_qt || !g_ba || !g_bb || !g_da1 || !g_da2 || !g_fa1 || !g_db1 || !g_db2 || !g_fb1) {
            free(g_qa); free(g_qb); free(g_qt); free(g_ba); free(g_bb);
            free(g_da1); free(g_da2); free(g_fa1); free(g_db1); free(g_db2); free(g_fb1);
            g_qa = g_qb = g_qt = NULL; g_ba = g_bb = NULL; g_da1 = g_da2 = g_fa1 = g_db1 = g_db2 = g_fb1 = NULL; g_cp = 0;
            return -1;
        }
        g_cp = c;
    }
    for (i = 0; i < la; i++) { g_ba[i] = -1; g_da1[i] = g_da2[i] = g_fa1[i] = HUGE_VAL; }
    for (i = 0; i < lb; i++) { g_bb[i] = -1; g_db1[i] = g_db2[i] = g_fb1[i] = HUGE_VAL; }
    na = grid(SA, la, s1, g_qa); nb = grid(SB, lb, s1, g_qb);
    for (i = 0; i < la; i++) side(SA, i, SB, g_qb, nb, s1, M, g_ba, g_da1, g_da2, g_fa1);
    for (i = 0; i < lb; i++) side(SB, i, SA, g_qa, na, s1, M, g_bb, g_db1, g_db2, g_fb1);
    for (i = 0; i < la; i++) ua += open_(g_da1[i], g_da2[i], g_fa1[i], s1, nearr, ok, oa);
    for (i = 0; i < lb; i++) ub += open_(g_db1[i], g_db2[i], g_fb1[i], s1, nearr, ok, oa);
    if (ua || ub) {                                                 /* (those: among all within M) */
        unsigned char *oa_ = (unsigned char *)malloc((size_t)la + 1), *ob_ = (unsigned char *)malloc((size_t)lb + 1);
        if (!oa_ || !ob_) { free(oa_); free(ob_); return -1; }
        for (i = 0; i < la; i++) oa_[i] = (unsigned char)open_(g_da1[i], g_da2[i], g_fa1[i], s1, nearr, ok, oa);
        for (i = 0; i < lb; i++) ob_[i] = (unsigned char)open_(g_db1[i], g_db2[i], g_fb1[i], s1, nearr, ok, oa);
        na = grid(SA, la, M, g_qa); nb = grid(SB, lb, M, g_qb); coarse = 1;
        for (i = 0; i < la; i++)
            if (oa_[i]) { g_ba[i] = -1; g_da1[i] = g_da2[i] = g_fa1[i] = HUGE_VAL; side(SA, i, SB, g_qb, nb, M, M, g_ba, g_da1, g_da2, g_fa1); }
        for (i = 0; i < lb; i++)
            if (ob_[i]) { g_bb[i] = -1; g_db1[i] = g_db2[i] = g_fb1[i] = HUGE_VAL; side(SB, i, SA, g_qa, na, M, M, g_bb, g_db1, g_db2, g_fb1); }
        free(oa_); free(ob_);
    }
    for (i = 0; i < la; i++) {
        int j = g_ba[i]; double d = g_da1[i], lim;
        if (j < 0 || g_bb[j] != i) continue;
        lim = ok * d + oa;
        if (g_fa1[i] <= nearr) { g_ma[i] = j; g_mb[j] = i; continue; }
        if (!(g_da2[i] >= lim && g_db2[j] >= lim)) continue;
        if (!coarse) { na = grid(SA, la, M, g_qa); nb = grid(SB, lb, M, g_qb); coarse = 1; }
        {                                                           /* (moved further: another like it that near in its own frame?) */
            int R = (int)(ok + oa / M) + 1;
            if (crowd(SA, i, g_qa, na, M, R, lim) || crowd(SB, j, g_qb, nb, M, R, lim)) continue;
        }
        g_ma[i] = j; g_mb[j] = i;
    }
    return 0;
}

/* numpy's interp, its search: the place j with arr[j] <= key < arr[j + 1], looked for near the last one found first
   (-1: before the first, len: past the last) - numpy's binary_search_with_guess, step for step */
static int find(double key, const double *arr, int len, int guess)
{
    int imin = 0, imax = len;
    if (key > arr[len - 1]) return len;
    else if (key < arr[0]) return -1;
    if (len <= 4) {
        int i;
        for (i = 1; i < len && key >= arr[i]; ++i) ;
        return i - 1;
    }
    if (guess > len - 3) guess = len - 3;
    if (guess < 1) guess = 1;
    if (key < arr[guess]) {
        if (key < arr[guess - 1]) {
            imax = guess - 1;
            if (guess > 8 && key >= arr[guess - 8]) imin = guess - 8;
        }
        else return guess - 1;
    }
    else {
        if (key < arr[guess + 1]) return guess;
        else {
            if (key < arr[guess + 2]) return guess + 1;
            else {
                imin = guess + 2;
                if (guess < len - 8 - 1 && key < arr[guess + 8]) imax = guess + 8;
            }
        }
    }
    while (imin < imax) {
        const int imid = imin + ((imax - imin) >> 1);
        if (key >= arr[imid]) imin = imid + 1;
        else imax = imid;
    }
    return imin - 1;
}

/* .. and its value at the place found */
static double at(double x, int j, const double *dx, const double *dy, int len)
{
    double slope, r;
    if (j == -1) return dy[0];
    if (j == len) return dy[len - 1];
    if (j == len - 1) return dy[j];
    if (dx[j] == x) return dy[j];
    slope = (dy[j + 1] - dy[j]) / (dx[j + 1] - dx[j]);
    r = slope * (x - dx[j]) + dy[j];
    if (r != r) {
        r = slope * (x - dx[j + 1]) + dy[j + 1];
        if (r != r && dy[j] == dy[j + 1]) r = dy[j];
    }
    return r;
}

/* this refresh's picture (smooth_frame, from alpha and k to the lines): a's pieces A, b's B (nb 0: none to go to);
   R: the picture's lines, their times squeezed by k (*k: made smaller when the frame reaches past the refresh, lim);
   exf: a's pieces whose afterglow has started (kept by the caller, brought up to date); now: those starting it now.
   kma, kmb: the pairing kept from the last call for these two frames (or none). info: R's rows, how many start
   their afterglow now, a's strokes, b's strokes, the pairing 1 kept / 0 new (vxs_pair) / -1 none. nearr, ok, oa: when
   a stroke's partner is certain (pairing). Below 0: not done */
VXB_API int vxs_frame(const double *A, int na, const double *B, int nb, int cols, double alpha, double M, double exw,
                      double lim, double *k, const int64_t *kma, int nkma, const int64_t *kmb, int nkmb,
                      double *R, unsigned char *exf, unsigned char *now, int *info, double nearr, double ok, double oa)
{
    int i, j, nr = na, nnow = 0, al = B != NULL && nb > 0, kept = -1, useb;
    double span = 0.0, kk;
    if (!A || na <= 0 || cols < 7 || !R || !exf || !now || !k || !info) return -1;
    if (g_cdA < (size_t)na) {
        double *p = (double *)realloc(g_dA, ((size_t)na + 256) * sizeof(double)); if (!p) return -2;
        g_dA = p; g_cdA = (size_t)na + 256;
    }
    useb = al && !(alpha < 0.5);
    if (al) {
        const int64_t *ma, *mb;
        if (strokes(A, na, cols, &g_fa) < 0 || strokes(B, nb, cols, &g_fb) < 0) return -2;
        if (g_cma < (size_t)na) { int64_t *p = (int64_t *)realloc(g_ma, ((size_t)na + 256) * sizeof(int64_t)); if (!p) return -2; g_ma = p; g_cma = (size_t)na + 256; }
        if (g_cmb < (size_t)nb) { int64_t *p = (int64_t *)realloc(g_mb, ((size_t)nb + 256) * sizeof(int64_t)); if (!p) return -2; g_mb = p; g_cmb = (size_t)nb + 256; }
        if (kma && kmb && nkma == g_fa.ns && nkmb == g_fb.ns) {     /* (worked out once for the two frames) */
            ma = kma; mb = kmb; kept = 1;
            for (i = 0; i < nkma; i++) if (kma[i] < -1 || kma[i] >= nkmb) return -3;
            for (i = 0; i < nkmb; i++) if (kmb[i] < -1 || kmb[i] >= nkma) return -3;
        }
        else { if (pairing(M, nearr, ok, oa) < 0) return -2; ma = g_ma; mb = g_mb; kept = 0; }
        /* a's pieces' ends as b draws them: looked up along b's stroke at the same share of its drawing time */
        {
            int j0 = 0, j1 = 0, len = 2 * nb; const double *pu = g_fb.pu, *px = g_fb.px, *py = g_fb.py;
            for (i = 0; i < na; i++) {
                const double *a = A + (size_t)i * cols; double *r = R + (size_t)i * cols;
                int64_t m = ma[g_fa.sid[i]]; double q0 = (double)m + 0.999 * g_fa.f0[i], q1 = (double)m + 0.999 * g_fa.f1[i], T[4], d;
                if (q0 == q0) { j0 = find(q0, pu, len, j0); T[0] = at(q0, j0, pu, px, len); T[1] = at(q0, j0, pu, py, len); } else T[0] = T[1] = q0;
                if (q1 == q1) { j1 = find(q1, pu, len, j1); T[2] = at(q1, j1, pu, px, len); T[3] = at(q1, j1, pu, py, len); } else T[2] = T[3] = q1;
                if (m >= 0) {
                    d = fabs(T[0] - a[0]);
                    for (j = 1; j < 4; j++) d = max2(d, fabs(T[j] - a[j]));
                }
                else d = 0.0;
                g_dA[i] = d;
                if (d < 1.5 && m >= 0) exf[i] = 1;                   /* (drawn in the same place a frame on: stays lit) */
                if (!useb) {
                    memcpy(r, a, (size_t)cols * sizeof(double));
                    if (m >= 0) for (j = 0; j < 4; j++) r[j] = a[j] + (T[j] - a[j]) * alpha;
                }
            }
        }
        if (useb) {                                                 /* past halfway: b's pieces, moved back towards a's */
            int j0 = 0, j1 = 0, len = 2 * na; const double *pu = g_fa.pu, *px = g_fa.px, *py = g_fa.py; double be = 1.0 - alpha;
            nr = nb;
            for (i = 0; i < nb; i++) {
                const double *b = B + (size_t)i * cols; double *r = R + (size_t)i * cols;
                int64_t m = mb[g_fb.sid[i]]; double q0 = (double)m + 0.999 * g_fb.f0[i], q1 = (double)m + 0.999 * g_fb.f1[i], T[4];
                if (q0 == q0) { j0 = find(q0, pu, len, j0); T[0] = at(q0, j0, pu, px, len); T[1] = at(q0, j0, pu, py, len); } else T[0] = T[1] = q0;
                if (q1 == q1) { j1 = find(q1, pu, len, j1); T[2] = at(q1, j1, pu, px, len); T[3] = at(q1, j1, pu, py, len); } else T[2] = T[3] = q1;
                memcpy(r, b, (size_t)cols * sizeof(double));
                if (m >= 0) for (j = 0; j < 4; j++) r[j] = b[j] + (T[j] - b[j]) * be;
            }
        }
    }
    else {
        memcpy(R, A, (size_t)na * cols * sizeof(double));
        for (i = 0; i < na; i++) g_dA[i] = 0.0;
    }
    /* the whole frame inside the refresh it is shown in */
    for (i = 0; i < na; i++) { const double *a = A + (size_t)i * cols; double v = a[6] + a[5]; if (i == 0 || v > span) span = v; }
    if (useb) for (i = 0; i < nb; i++) { const double *b = B + (size_t)i * cols; double v = b[6] + b[5]; if (v > span) span = v; }
    kk = *k;
    if (span > lim) kk = kk * (lim / span);
    for (i = 0; i < nr; i++) { double *r = R + (size_t)i * cols; r[5] = r[5] * kk; r[6] = r[6] * kk; }
    *k = kk;
    /* the lines of a the gliding ones have just cleared: their afterglow starts now */
    for (i = 0; i < na; i++) {
        unsigned char s = !exf[i] && g_dA[i] >= exw && alpha * g_dA[i] >= exw;
        now[i] = s; if (s) { exf[i] = 1; nnow++; }
    }
    info[0] = nr; info[1] = nnow; info[2] = al ? g_fa.ns : 0; info[3] = al ? g_fb.ns : 0; info[4] = kept;
    return 0;
}

/* the pairing vxs_frame just worked out (info[4] 0): a's strokes' partners, b's */
VXB_API int vxs_pair(int64_t *ma, int64_t *mb)
{
    if (!g_ma || !g_mb || !ma || !mb) return -1;
    memcpy(ma, g_ma, (size_t)g_fa.ns * sizeof(int64_t)); memcpy(mb, g_mb, (size_t)g_fb.ns * sizeof(int64_t));
    return 0;
}

/* numpy's sum of a run (add.reduceat, as add.reduce: the first, then the rest summed in pairs) */
static double pw_sum(const double *a, int n, int st)
{
    if (n < 8) {
        int i; double res = 0.;
        for (i = 0; i < n; i++) res += a[(size_t)i * st];
        return res;
    }
    else if (n <= 128) {
        int i, q; double r[8], res;
        for (q = 0; q < 8; q++) r[q] = a[(size_t)q * st];
        for (i = 8; i < n - (n % 8); i += 8)
            for (q = 0; q < 8; q++) r[q] += a[(size_t)(i + q) * st];
        res = ((r[0] + r[1]) + (r[2] + r[3])) + ((r[4] + r[5]) + (r[6] + r[7]));
        for (; i < n; i++) res += a[(size_t)i * st];
        return res;
    }
    else {
        int n2 = n / 2;
        n2 -= n2 % 8;
        return pw_sum(a, n2, st) + pw_sum(a + (size_t)n2 * st, n - n2, st);
    }
}

/* sm_merge: the pieces of one straight line as one line - joined end to start, one brightness, one direction (and one
   colour); their drawing time together. Into R (room for n rows); the count */
VXB_API int vxs_merge(const double *X, int n, int cols, double *R)
{
    int i, m = 0, first = 0, okp = 0; double uxp = 0.0, uyp = 0.0;
    if (!X || !R || n < 0 || cols < 7) return -1;
    if (n < 2) { if (n) memcpy(R, X, (size_t)cols * sizeof(double)); return n; }
    for (i = 0; i <= n; i++) {
        int nw = 1, ok = 0; double ux = 0.0, uy = 0.0;
        if (i < n) {
            const double *r = X + (size_t)i * cols; double dx = r[2] - r[0], dy = r[3] - r[1], ln = hypot(dx, dy), l9;
            ok = ln > 0; l9 = max2(ln, 1e-9);
            ux = ok ? dx / l9 : 0.0; uy = ok ? dy / l9 : 0.0;
            if (i > 0) {
                const double *p = r - cols;
                nw = !(r[0] == p[2] && r[1] == p[3] && r[4] == p[4] && ok && okp
                       && fabs(ux * uyp - uy * uxp) < 0.02 && (ux * uxp + uy * uyp) > 0);
                if (cols > 7 && r[7] != p[7]) nw = 1;               /* (the 3D Imager's colour) */
            }
        }
        if (nw && i > 0) {                                          /* the line before ends: first .. i - 1 */
            double *o = R + (size_t)m * cols; const double *f = X + (size_t)first * cols, *l = X + (size_t)(i - 1) * cols;
            memcpy(o, f, (size_t)cols * sizeof(double));
            o[2] = l[2]; o[3] = l[3];
            o[5] = i - 1 > first ? f[5] + pw_sum(f + cols + 5, i - 1 - first, cols) : f[5];
            m++; first = i;
        }
        uxp = ux; uyp = uy; okp = ok;
    }
    return m;
}
