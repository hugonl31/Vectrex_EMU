/* vxbuild.c - the PC renderer's line build in C (vecrender.py: Renderer._build_np with the beam trace on, bt2_prepare
   and bt2_light, the tube's map _tube_f / tubemap.imp_np / unit_warp / glass_flat): the same arithmetic in the same
   order, piece by piece instead of numpy array by array (a picture of ~1700 pieces: 2.6 ms in numpy). Loaded with
   ctypes (Renderer._build_c); the numpy code stays as the reference and the way without this library.
   Three calls a picture: vxb_pieces (the pieces to draw, their count), vxb_load (each piece's beam load: numpy sums
   it, so the brightness bending's load is the same number), vxb_fill (the vertex arrays, into the caller's buffers).
   The tables and all constants come from Python (vxb_tables once, the array P every call): one place to tune them.
   Doubles throughout, float only in the output, no fused multiply-add: the results match numpy's.
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

/* the parameters (P), as Renderer._build_c fills them - the same order there */
enum {
    P_BT2_W, P_BT2_K, P_HALO_ON,                                    /* bt2_prepare; the glass's glow pieces (BT_HALO_F > 0) */
    P_TUBE_K, P_GLASS_G, P_WARP_ON, P_WARP0,                        /* the tube's map: strength, glass_undo, unit_warp (6) */
    P_LIN0 = P_WARP0 + 6,                                           /* TUBE_LIN (4) */
    P_XM = P_LIN0 + 4, P_YM, P_UU, P_UW,                            /* the canvas's middle, half its height, UW */
    P_TM_U0, P_TM_V0, P_TM_SU, P_TM_SV,                             /* tubemap: U0, V0, SU, SV */
    P_MM_UNIT, P_GLASS_RX, P_GLASS_RY, P_GLASS_TM,                  /* glass_flat */
    P_PS, P_PDX, P_PDY, P_TUBE_SEG,                                 /* pic_adj; TUBE_SEG */
    P_BEND_ON, P_GS, P_GSH,                                         /* brightness bending: grow, shift */
    P_WOB_ON, P_A0, P_K50, P_HUM0,                                  /* the hum: phase, step a cycle, PX1-3 QX1-3 PY1-3 QY1-3 */
    P_CSX = P_HUM0 + 12, P_CSY,                                     /* the switch-on bulge */
    P_H, P_SC, P_OX, P_OY,                                          /* the window's height, the map */
    P_CGAIN, P_CWIDTH, P_KB, P_GAIN,
    P_LOWZ, P_LOWW, P_W0, P_WZ, P_SG, P_SPOT, P_HF, P_HS, P_UNITS, P_KMAX, P_FADE,
    P_BLOOK, P_BL_WIDTH,
    P_COL_ON, P_RGB0,                                               /* the 3D Imager: the filter of codes 0..7 (r g b each) */
    P_COUNT = P_RGB0 + 24
};

/* ---- the tables (vxb_tables) */
static double *g_tl; static int g_ntl;                              /* TUBE_LIGHT */
static double *g_tlkx, *g_tlky; static int g_ntlk;                  /* BT2_TLK */
static double *g_d; static int g_nd;                                /* BT2_D */
static double *g_z; static int g_nz;                                /* BT2_Z */
static double *g_l;                                                 /* BT2_L, nz rows of nd */
static double *g_tx, *g_ty; static int g_nu, g_nv;                  /* tubemap DX, DY: nv rows of nu */
static double g_tl0;                                                /* _bt2_tl(BT2_Z[0]) */

static double *dup_d(const double *a, size_t n)
{
    double *p = (double *)malloc((n ? n : 1) * sizeof(double));
    if (p && n) memcpy(p, a, n * sizeof(double));
    return p;
}

/* numpy's interp (its edges: the first and last value) */
static double interp(double x, const double *xp, const double *fp, int n)
{
    int lo, hi, mid; double slope, r;
    if (x != x) return x;
    if (x < xp[0]) return fp[0];
    if (x > xp[n - 1]) return fp[n - 1];
    lo = 0; hi = n - 1;                                             /* xp[lo] <= x < xp[lo + 1], or lo = n - 1 */
    while (lo < hi) { mid = (lo + hi + 1) >> 1; if (xp[mid] <= x) lo = mid; else hi = mid - 1; }
    if (lo == n - 1 || xp[lo] == x) return fp[lo];
    slope = (fp[lo + 1] - fp[lo]) / (xp[lo + 1] - xp[lo]);
    r = slope * (x - xp[lo]) + fp[lo];
    if (r != r) { r = slope * (x - xp[lo + 1]) + fp[lo + 1]; if (r != r && fp[lo] == fp[lo + 1]) r = fp[lo]; }
    return r;
}

/* _bt2_tl: TUBE_LIGHT (its places 0, 1, 2 ..) x BT2_TLK */
static double bt2_tl(double q)
{
    double a;
    if (q != q) a = q;
    else if (q < 0.0) a = g_tl[0];
    else if (q > (double)(g_ntl - 1)) a = g_tl[g_ntl - 1];
    else {
        int j = (int)q; double xj = (double)j;
        if (j == g_ntl - 1 || xj == q) a = g_tl[j];
        else {
            double slope = (g_tl[j + 1] - g_tl[j]) / (((double)(j + 1)) - xj);
            a = slope * (q - xj) + g_tl[j];
        }
    }
    return a * interp(q, g_tlkx, g_tlky, g_ntlk);
}

VXB_API int vxb_tables(const double *tube_light, int ntl, const double *tlk_x, const double *tlk_y, int ntlk,
                       const double *bt2_d, int nd, const double *bt2_z, int nz, const double *bt2_l,
                       const double *tm_dx, const double *tm_dy, int nu, int nv)
{
    if (ntl < 2 || ntlk < 2 || nd < 2 || nz < 2 || nu < 2 || nv < 2) return -1;
    free(g_tl); free(g_tlkx); free(g_tlky); free(g_d); free(g_z); free(g_l); free(g_tx); free(g_ty);
    g_tl = dup_d(tube_light, ntl); g_ntl = ntl;
    g_tlkx = dup_d(tlk_x, ntlk); g_tlky = dup_d(tlk_y, ntlk); g_ntlk = ntlk;
    g_d = dup_d(bt2_d, nd); g_nd = nd; g_z = dup_d(bt2_z, nz); g_nz = nz; g_l = dup_d(bt2_l, (size_t)nz * nd);
    g_tx = dup_d(tm_dx, (size_t)nu * nv); g_ty = dup_d(tm_dy, (size_t)nu * nv); g_nu = nu; g_nv = nv;
    if (!g_tl || !g_tlkx || !g_tlky || !g_d || !g_z || !g_l || !g_tx || !g_ty) return -2;
    g_tl0 = bt2_tl(g_z[0]);
    return P_COUNT;
}

/* bt2_light: the light a unit of path, z the drive (intensity steps), d the dwell there (cycles a unit) */
static double bt2_light(double z, double d, double K)
{
    double ld = log(d > 1e-3 ? d : 1e-3), f, zc, g, r0, r1, out;
    int lo = 0, hi = g_nd, mid, j, i; const double *a, *b;
    while (lo < hi) { mid = (lo + hi) >> 1; if (g_d[mid] < ld) lo = mid + 1; else hi = mid; }
    j = lo - 1; if (j < 0) j = 0; if (j > g_nd - 2) j = g_nd - 2;
    f = (ld - g_d[j]) / (g_d[j + 1] - g_d[j]);
    zc = z < g_z[0] ? g_z[0] : (z > g_z[g_nz - 1] ? g_z[g_nz - 1] : z);
    lo = 0; hi = g_nz;
    while (lo < hi) { mid = (lo + hi) >> 1; if (g_z[mid] < zc) lo = mid + 1; else hi = mid; }
    i = lo - 1; if (i < 0) i = 0; if (i > g_nz - 2) i = g_nz - 2;
    g = (zc - g_z[i]) / (g_z[i + 1] - g_z[i]);
    a = g_l + (size_t)i * g_nd; b = a + g_nd;
    r0 = a[j] + (a[j + 1] - a[j]) * f; r1 = b[j] + (b[j + 1] - b[j]) * f;
    out = exp(r0 + (r1 - r0) * g);
    if (z < g_z[0]) out = out * bt2_tl(z) / g_tl0;
    if (z > g_z[g_nz - 1]) out = out * bt2_tl(z) / bt2_tl(g_z[g_nz - 1]);
    return K * out;
}

/* ---- the work space: kept between calls, grown when needed */
typedef struct { double x0, y0, x1, y1, e, t0, bl; int64_t b; int col; } Row;      /* a piece from bt2_prepare */
typedef struct { double x0, y0, x1, y1, e, t0, bl; int64_t b; int col, same; } Piece;   /* .. through the tube's map */
static Row *g_rows; static size_t g_nrows, g_crows;
static Piece *g_pc; static size_t g_npc, g_cpc;
static double *g_ld; static size_t g_cld;                           /* each piece's beam load */
static double *g_w; static size_t g_cw;                             /* bt2_prepare's arrays */
static int *g_k; static size_t g_ck;
static double *g_xs, *g_ys; static int g_n2;                        /* the run's path: place -> the beam's cycles */

static Row *row_new(void)
{
    if (g_nrows == g_crows) {
        size_t c = g_crows ? g_crows * 2 : 4096; Row *p = (Row *)realloc(g_rows, c * sizeof(Row));
        if (!p) return NULL;
        g_rows = p; g_crows = c;
    }
    return &g_rows[g_nrows++];
}

static Piece *piece_new(void)
{
    if (g_npc == g_cpc) {
        size_t c = g_cpc ? g_cpc * 2 : 8192; Piece *p = (Piece *)realloc(g_pc, c * sizeof(Piece));
        if (!p) return NULL;
        g_pc = p; g_cpc = c;
    }
    return &g_pc[g_npc++];
}

static double clip01(double v) { return v < 0.0 ? 0.0 : (v > 1.0 ? 1.0 : v); }

/* the beam's cycles up to s, a rest at s counted (bt2_prepare: t_hi) */
static double t_hi(double s, int lo)
{
    const double *xs = g_xs, *ys = g_ys; int n = g_n2, i; double dxs, v;
    if (s >= xs[n - 1]) return ys[n - 1];
    if (s < xs[0]) return ys[0];
    while (lo < n && xs[lo] <= s) lo++;                             /* (how many places up to s: looked for from lo, near it) */
    while (lo > 0 && xs[lo - 1] > s) lo--;
    i = lo - 1; if (i < 0) i = 0; if (i > n - 2) i = n - 2;
    dxs = xs[i + 1] - xs[i];
    v = ys[i] + (dxs > 1e-9 ? clip01((s - xs[i]) / dxs) : 0.0) * (ys[i + 1] - ys[i]);
    return v;
}

/* .. a rest at s not counted (t_lo) */
static double t_lo(double s, int lo)
{
    const double *xs = g_xs, *ys = g_ys; int n = g_n2, i; double dxs, v;
    if (s <= xs[0]) return ys[0];
    if (s > xs[n - 1]) return ys[n - 1];
    while (lo < n && xs[lo] < s) lo++;                              /* (how many places before s) */
    while (lo > 0 && xs[lo - 1] >= s) lo--;
    i = lo; if (i < 1) i = 1; if (i > n - 1) i = n - 1;
    dxs = xs[i] - xs[i - 1];
    v = ys[i - 1] + (dxs > 1e-9 ? clip01((s - xs[i - 1]) / dxs) : 1.0) * (ys[i] - ys[i - 1]);
    return v;
}

/* the dwell at s: the beam's cycles within W of it (at: a place of the path near s, where the looking starts) */
static double dwell(double s, double W, int at) { return (t_hi(s + W / 2, at) - t_lo(s - W / 2, at)) / W; }

/* bt2_prepare: the core's pieces (rows of `cols` doubles: x0 y0 x1 y1 flags energy t0 [colour]) -> g_rows */
static int prepare(const double *L, int rows, int cols, const double *P)
{
    const double U = 134.0, W = P[P_BT2_W], K = P[P_BT2_K];
    int i, j, n = 0, any_lg = 0, pass;
    double *X0, *Y0, *X1, *Y1, *T0, *T, *Ln, *Z, *S0, *S1, *Tc, *H; int64_t *B; int *k, *fl;
    size_t need;
    g_nrows = 0;
    /* the others - the dark beam's glow, fades: piece by piece */
    for (i = 0; i < rows; i++) {
        const double *r = L + (size_t)i * cols; int64_t b = (int64_t)r[4]; double z = (double)(b & 127);
        if (z > 0 && (b & 128) == 0) { n++; continue; }
        {
            double dx = r[2] - r[0], dy = r[3] - r[1], l = sqrt(dx * dx + dy * dy) / U, t = r[5] / 8.0;
            Row *o = row_new(); if (!o) return -1;
            o->x0 = r[0]; o->y0 = r[1]; o->x1 = r[2]; o->y1 = r[3]; o->b = b; o->e = r[5]; o->t0 = r[6];
            o->bl = (l + W) * bt2_light(z, t / (l + W), K); o->col = cols > 7 ? (int)(int64_t)r[7] : 0;
        }
    }
    if (!n) return 0;
    need = (size_t)n * 16;                                          /* (12 arrays, and the path's two of twice the length) */
    if (g_cw < need) { double *p = (double *)realloc(g_w, need * sizeof(double)); if (!p) return -1; g_w = p; g_cw = need; }
    if (g_ck < (size_t)n * 4) { int *p = (int *)realloc(g_k, (size_t)n * 4 * sizeof(int)); if (!p) return -1; g_k = p; g_ck = (size_t)n * 4; }
    X0 = g_w; Y0 = X0 + n; X1 = Y0 + n; Y1 = X1 + n; T0 = Y1 + n; T = T0 + n; Ln = T + n; Z = Ln + n; S0 = Z + n; S1 = S0 + n;
    Tc = S1 + n; H = Tc + n; g_xs = H + n; g_ys = g_xs + 2 * (size_t)n; g_n2 = 2 * n;
    k = g_k; fl = k + n; B = (int64_t *)(void *)(fl + n);            /* (k: the row; fl: bit 0 continues, 1 moves, 2 long) */
    for (i = 0, j = 0; i < rows; i++) {
        const double *r = L + (size_t)i * cols; int64_t b = (int64_t)r[4]; double z = (double)(b & 127), dx, dy;
        if (!(z > 0 && (b & 128) == 0)) continue;
        dx = r[2] - r[0]; dy = r[3] - r[1];
        X0[j] = r[0]; Y0[j] = r[1]; X1[j] = r[2]; Y1[j] = r[3]; T0[j] = r[6]; T[j] = r[5] / 8.0;
        Ln[j] = sqrt(dx * dx + dy * dy) / U; Z[j] = z; B[j] = b; k[j] = i; j++;
    }
    {
        double cs = 0.0, s = 0.0;
        for (j = 0; j < n; j++) {
            int cont = j > 0 && fabs(X0[j] - X1[j - 1]) <= 3.0 && fabs(Y0[j] - Y1[j - 1]) <= 3.0
                       && fabs(T0[j] - (T0[j - 1] + T[j - 1])) <= 1.5 && B[j] == B[j - 1] && k[j] == k[j - 1] + 1;
            int mv = Ln[j] > 1e-6, lg = mv && Ln[j] > 1.5 * W;
            double add = (cont ? 0.0 : 4.0 * W) + (j > 0 ? Ln[j - 1] : 0.0);
            s = j > 0 ? s + add : add;
            S0[j] = s; S1[j] = s + Ln[j];
            Tc[j] = j > 0 ? cs : 0.0;
            cs = j > 0 ? cs + T[j] : T[j];
            g_xs[2 * j] = S0[j]; g_xs[2 * j + 1] = S1[j]; g_ys[2 * j] = Tc[j]; g_ys[2 * j + 1] = Tc[j] + T[j];
            fl[j] = (cont ? 1 : 0) | (mv ? 2 : 0) | (lg ? 4 : 0);
            if (lg) { any_lg = 1; H[j] = (W / 2) / Ln[j]; }
        }
    }
    /* the short pieces whole; the long ones: an end, the middle, the other end (each its own pass, as numpy's blocks) */
    for (pass = 0; pass < (any_lg ? 4 : 1); pass++) {
        for (j = 0; j < n; j++) {
            double f0, f1, fs, fd, sm, ddx, ddy; Row *o;
            if (pass == 0) { if ((fl[j] & 6) != 2) continue; f0 = 0.0; f1 = 1.0; }
            else {
                if (!(fl[j] & 4)) continue;
                if (pass == 1) { f0 = 0.0; f1 = H[j]; } else if (pass == 2) { f0 = H[j]; f1 = 1.0 - H[j]; } else { f0 = 1.0 - H[j]; f1 = 1.0; }
            }
            fs = f0 + f1; fd = f1 - f0;
            sm = S0[j] + Ln[j] * fs / 2.0;
            ddx = X1[j] - X0[j]; ddy = Y1[j] - Y0[j];
            o = row_new(); if (!o) return -1;
            o->x0 = X0[j] + ddx * f0; o->y0 = Y0[j] + ddy * f0; o->x1 = X0[j] + ddx * f1; o->y1 = Y0[j] + ddy * f1;
            o->b = B[j]; o->e = T[j] * fd * 8.0; o->t0 = T0[j] + T[j] * f0;
            o->bl = Ln[j] * fd * bt2_light(Z[j], dwell(sm, W, 2 * j + 1), K);
            o->col = cols > 7 ? (int)(int64_t)L[(size_t)k[j] * cols + 7] : 0;
        }
    }
    /* a run's two ends: a cap */
    for (pass = 0; pass < 2; pass++) {
        for (j = 0; j < n; j++) {
            Row *o; double px, py, s, tt;
            if (pass == 0) { if (fl[j] & 1) continue; px = X0[j]; py = Y0[j]; s = S0[j] - W / 4; tt = T0[j]; }
            else { if (j < n - 1 && (fl[j + 1] & 1)) continue; px = X1[j]; py = Y1[j]; s = S1[j] + W / 4; tt = T0[j] + T[j]; }
            o = row_new(); if (!o) return -1;
            o->x0 = o->x1 = px; o->y0 = o->y1 = py; o->b = B[j]; o->e = 8.0; o->t0 = tt;
            o->bl = (W / 2) * bt2_light(Z[j], dwell(s, W, 2 * j + 1), K);
            o->col = cols > 7 ? (int)(int64_t)L[(size_t)k[j] * cols + 7] : 0;
        }
    }
    return 0;
}

/* _tube_f: a point of the integrators where it shows on the curved tube (the lines' units) */
static void tube_f(double x, double y, const double *P, double *ox, double *oy)
{
    double XM = P[P_XM], YM = P[P_YM], U = P[P_UU], k = P[P_TUBE_K], g = P[P_GLASS_G];
    double u = (x - XM) / U, v = (y - YM) / U, X, Y;
    if (P[P_WARP_ON] != 0.0) {                                      /* unit_warp */
        const double *w = P + P_WARP0; double UW = P[P_UW], a = u / UW, b = v, Uo, Vo;
        Uo = u * (1.0 + 0.5 * w[0] * b) * (1.0 + w[2] * b * b) + 0.5 * w[4] * UW * a * a;
        Vo = v * (1.0 + 0.5 * w[1] * a) * (1.0 + w[3] * a * a) + 0.5 * w[5] * b * b;
        u = Uo; v = Vo;
    }
    X = P[P_LIN0] * u + P[P_LIN0 + 1]; Y = P[P_LIN0 + 2] * v + P[P_LIN0 + 3];
    if (k != 0.0) {                                                 /* tubemap.imp_np: the measured rest, bilinear */
        double fu = (u - P[P_TM_U0]) * P[P_TM_SU], fv = (v - P[P_TM_V0]) * P[P_TM_SV];
        double mu = (double)g_nu - 1.000001, mv = (double)g_nv - 1.000001, a, b, dx, dy;
        int i, j; const double *t;
        fu = fu < 0.0 ? 0.0 : (fu > mu ? mu : fu); fv = fv < 0.0 ? 0.0 : (fv > mv ? mv : fv);
        i = (int)fu; j = (int)fv; a = fu - i; b = fv - j;
        t = g_tx + (size_t)j * g_nu + i;
        dx = (t[0] * (1 - a) + t[1] * a) * (1 - b) + (t[g_nu] * (1 - a) + t[g_nu + 1] * a) * b;
        t = g_ty + (size_t)j * g_nu + i;
        dy = (t[0] * (1 - a) + t[1] * a) * (1 - b) + (t[g_nu] * (1 - a) + t[g_nu + 1] * a) * b;
        X = X + k * dx; Y = Y + k * dy;
    }
    if (g > 0) {                                                    /* glass_flat */
        double tm = P[P_GLASS_TM], slope = 1.0 / sqrt(1 - tm * tm), R, t, tc;
        R = P[P_GLASS_RX] / g / P[P_MM_UNIT]; t = X / R; tc = t < -tm ? -tm : (t > tm ? tm : t);
        X = R * (asin(tc) + (t - tc) * slope);
        R = P[P_GLASS_RY] / g / P[P_MM_UNIT]; t = Y / R; tc = t < -tm ? -tm : (t > tm ? tm : t);
        Y = R * (asin(tc) + (t - tc) * slope);
    }
    *ox = XM + X * U * P[P_PS] + P[P_PDX]; *oy = YM + Y * U * P[P_PS] + P[P_PDY];
}

/* .. the point before kept: a piece mostly starts where the one before ended (the same numbers: the same place) */
static double g_cx, g_cy, g_cox, g_coy; static int g_chave;
static void tube_fc(double x, double y, const double *P, double *ox, double *oy)
{
    if (g_chave && x == g_cx && y == g_cy) { *ox = g_cox; *oy = g_coy; return; }
    tube_f(x, y, P, ox, oy);
    g_cx = x; g_cy = y; g_cox = *ox; g_coy = *oy; g_chave = 1;
}

/* a row through the tube's map, a long one in pieces */
static int tube_row(const Row *r, int bend, const double *P)
{
    double ddx = r->x1 - r->x0, ddy = r->y1 - r->y0, Ls = sqrt(ddx * ddx + ddy * ddy), seg = P[P_TUBE_SEG], mm;
    int64_t m = (Ls > seg && bend) ? (int64_t)(Ls / seg) + 1 : 1, i;
    int same = (r->x0 == r->x1) && (r->y0 == r->y1);
    mm = (double)m;
    for (i = 1; i <= m; i++) {
        double s0 = (double)(i - 1) / mm, s1 = (double)i / mm, ax, ay, bx, by;
        Piece *p = piece_new(); if (!p) return -1;
        ax = r->x0 + ddx * s0; ay = r->y0 + ddy * s0;
        if (m == 1) { bx = r->x1; by = r->y1; } else { bx = r->x0 + ddx * s1; by = r->y0 + ddy * s1; }
        tube_fc(ax, ay, P, &p->x0, &p->y0); tube_fc(bx, by, P, &p->x1, &p->y1);
        if (same) { p->x0 = p->x1; p->y0 = p->y1; }                 /* (a resting spot: where the spot came to rest) */
        p->e = r->e / mm; p->bl = r->bl / mm; p->t0 = r->t0 + (r->e / 8.0) * (double)(i - 1) / mm;
        p->b = r->b; p->col = r->col; p->same = same;
    }
    return 0;
}

/* the pieces to draw for these lines: their count (0: none, below 0: no memory or no tables) */
VXB_API int vxb_pieces(const double *L, int rows, int cols, const double *P)
{
    size_t i, j, nh = 0; int bend;
    if (!g_tl || rows <= 0 || cols < 7) return -1;
    if (prepare(L, rows, cols, P) < 0) return -2;
    bend = P[P_TUBE_K] > 0 || P[P_GLASS_G] > 0 || P[P_WARP_ON] != 0.0;
    g_npc = 0; g_chave = 0;
    for (i = 0; i < g_nrows; i++)
        if (tube_row(&g_rows[i], bend, P) < 0) return -2;
    if (P[P_HALO_ON] != 0.0)                                        /* the glass's glow: every lit piece once more (bit 256), */
        for (i = 0; i < g_npc; i++)                                 /*  before all the pieces - the same places, so copies */
            if ((g_pc[i].b & 127) != 0 && (g_pc[i].b & 128) == 0) nh++;
    if (nh) {
        if (g_cpc < g_npc + nh) { Piece *p = (Piece *)realloc(g_pc, (g_npc + nh) * sizeof(Piece)); if (!p) return -2; g_pc = p; g_cpc = g_npc + nh; }
        memmove(g_pc + nh, g_pc, g_npc * sizeof(Piece));
        for (i = 0, j = 0; i < g_npc; i++)
            if ((g_pc[nh + i].b & 127) != 0 && (g_pc[nh + i].b & 128) == 0) { g_pc[j] = g_pc[nh + i]; g_pc[j].b |= 256; j++; }
        g_npc += nh;
    }
    if (g_cld < g_npc) { double *p = (double *)realloc(g_ld, g_npc * sizeof(double)); if (!p) return -2; g_ld = p; g_cld = g_npc; }
    for (i = 0; i < g_npc; i++)                                     /* the beam load: cycles x the intensity's share */
        g_ld[i] = (g_pc[i].e / 8.0) * ((double)(g_pc[i].b & 127) / 127.0);
    return (int)g_npc;
}

VXB_API const double *vxb_load(void) { return g_ld; }

/* the vertex arrays of the pieces vxb_pieces made: V 8, T 16, C 16, TT 4, T2 16, T3 8 floats a piece; the count */
VXB_API int vxb_fill(const double *P, float *V, float *T, float *C, float *TT, float *T2, float *T3, double *tmax)
{
    const double XM = P[P_XM], YM = P[P_YM];
    const int bend = P[P_BEND_ON] != 0.0, wob = P[P_WOB_ON] != 0.0, crt = P[P_CSX] != 1.0 || P[P_CSY] != 1.0;
    const int blook = P[P_BLOOK] != 0.0, colon = P[P_COL_ON] != 0.0;
    const double gs = P[P_GS], gsh = P[P_GSH], a0 = P[P_A0], k50 = P[P_K50], *hm = P + P_HUM0;
    const double csx = P[P_CSX], csy = P[P_CSY], h = P[P_H], sc = P[P_SC], ox = P[P_OX], oy = P[P_OY];
    const double cgain = P[P_CGAIN], cwidth = P[P_CWIDTH], kb = P[P_KB], gain = P[P_GAIN];
    const double gp = 160.0 * sc, upx = gp / P[P_UNITS], hf = P[P_HF], hsu = P[P_HS] * upx, twopi = 2.0 * 3.141592653589793;
    float tm = 0.0f; size_t i, n = g_npc; int q;
    for (i = 0; i < n; i++) {
        const Piece *p = &g_pc[i];
        double x0 = p->x0, y0 = p->y0, x1 = p->x1, y1 = p->y1, cyc = p->e / 8.0;
        double ax, ay, bx, by, dx, dy, lp, zi, zw, fwb, sg, kk, mq, ux, uy, nx, ny, lps, al, r, g, b;
        int64_t bb = p->b; int web = (bb & 255) == 128, hal = (bb & 256) != 0, moved; float f;
        if (bend) {
            x0 = XM + (x0 - XM) * gs + gsh; x1 = XM + (x1 - XM) * gs + gsh;
            y0 = YM + (y0 - YM) * gs + gsh; y1 = YM + (y1 - YM) * gs + gsh;
        }
        if (wob) {                                                  /* the hum: offsets at each end */
            int e_;
            for (e_ = 0; e_ < 2; e_++) {
                double a = a0 + (e_ ? p->t0 + cyc : p->t0) * k50, s1 = sin(a), c1 = cos(a);
                double s2 = 2 * s1 * c1, c2 = c1 * c1 - s1 * s1, s3 = s2 * c1 + c2 * s1, c3 = c2 * c1 - s2 * s1;
                double wx = hm[0] * s1 + hm[3] * c1 + hm[1] * s2 + hm[4] * c2 + hm[2] * s3 + hm[5] * c3;
                double wy = hm[6] * s1 + hm[9] * c1 + hm[7] * s2 + hm[10] * c2 + hm[8] * s3 + hm[11] * c3;
                if (e_) { x1 = x1 + wx; y1 = y1 + wy; } else { x0 = x0 + wx; y0 = y0 + wy; }
            }
        }
        if (crt) {                                                  /* the switch-on bulge */
            x0 = XM + (x0 - XM) * csx; x1 = XM + (x1 - XM) * csx; y0 = YM + (y0 - YM) * csy; y1 = YM + (y1 - YM) * csy;
        }
        ax = (x0 + ox) * sc; ay = h - (y0 + oy) * sc; bx = (x1 + ox) * sc; by = h - (y1 + oy) * sc;
        dx = bx - ax; dy = by - ay; lp = sqrt(dx * dx + dy * dy);
        zi = (double)(bb & 127) / 127.0 + kb; if (!(zi > 0.0)) zi = 0.0;      /* the drive: intensity + the knob's bias */
        zw = zi >= P[P_LOWZ] ? 1.0 : P[P_LOWW] + (1.0 - P[P_LOWW]) * zi / P[P_LOWZ];
        fwb = (P[P_W0] + P[P_WZ] * zi) * zw * cwidth;
        sg = P[P_SG] * fwb * gp / 2.355 * P[P_SPOT]; if (!(sg > 0.5)) sg = 0.5;
        if (hal) sg = hsu;                                          /* (the glass's glow of a piece: wide) */
        r = sg / upx;
        kk = p->bl / (twopi * (r * r)) * (hal ? hf : 1.0 - hf) * gain * cgain / P[P_KMAX];
        if ((bb & 128) != 0 && !web) kk = kk * P[P_FADE];
        if (blook && !hal) sg = sg * P[P_BL_WIDTH];
        mq = 3.5 * sg + 1.0;
        moved = lp > 1e-3; lps = moved ? lp : 1.0;
        ux = moved ? dx / lps * mq : mq; uy = moved ? dy / lps * mq : 0.0;
        nx = -uy; ny = ux;
        V[0] = (float)(ax - ux * 1.0 + nx); V[1] = (float)(ay - uy * 1.0 + ny);
        V[2] = (float)(bx + ux * 1.0 + nx); V[3] = (float)(by + uy * 1.0 + ny);
        V[4] = (float)(bx + ux * 1.0 - nx); V[5] = (float)(by + uy * 1.0 - ny);
        V[6] = (float)(ax - ux * 1.0 - nx); V[7] = (float)(ay - uy * 1.0 - ny);
        V += 8;
        if (colon) { const double *c3 = P + P_RGB0 + 3 * (p->col & 7); r = kk * c3[0]; g = kk * c3[1]; b = kk * c3[2]; }
        else r = g = b = kk;
        al = sg / 16.0; if (!(al < 1.0)) al = 1.0;
        f = (float)(p->t0 / 1.5e6); if (i == 0 || f > tm) tm = f;
        for (q = 0; q < 4; q++) {
            T[0] = (float)ax; T[1] = (float)ay; T[2] = (float)bx; T[3] = (float)by; T += 4;      /* (no end open: the soft ends add up) */
            T2[0] = T2[1] = T2[2] = T2[3] = 0.0f; T2 += 4;          /* (so no joints' cuts either) */
            T3[0] = (float)(0.0 * ax + 0.0 * ay); T3[1] = (float)(0.0 * bx + 0.0 * by); T3 += 2;
            C[0] = (float)r; C[1] = (float)g; C[2] = (float)b; C[3] = (float)al; C += 4;
            *TT++ = f;
        }
    }
    if (tmax) *tmax = n ? (double)tm : 0.0;
    return (int)n;
}
