#include <ctype.h>
/* vtest - headless scripted Vectrex test harness (vecx core, GPL).
 *
 * Models the Last Human cartridge: 64 KiB image, PB6 -> A15 with a pull-up
 * (bank = DDRB.6 ? ORB.6 : 1).
 *
 * usage: vtest bios.bin cart.bin script.txt [outdir]
 *
 * script commands (one per line, '#' comments):
 *   run N            run N game frames (a frame ends at each Wait_Recal call;
 *                    falls back to 30000 cycles if Wait_Recal is not called)
 *   cycles N         run N CPU cycles
 *   joy X Y          joystick 1 analog value, -128..127 (Y positive = up)
 *   btn MASK         buttons held, bit0..3 = controller 1 buttons 1..4
 *   shot FILE.png    write the vectors of the last completed frame
 *   wav FILE.wav     start recording PSG audio
 *   wavend           stop recording
 *   peek ADDR [N]    print N RAM bytes (hex address)
 *   stats            print frame/bank statistics
 *   expectbank B     fail unless current cart bank == B
 *   bot SYM N [SKILL] simulated player for N frames (see bot.c)
 *
 * or: vtest bios cart --live REC.bin [SYM]          interactive play over a pipe (see live.c)
 *     vtest bios cart --replay REC.bin SYM OUT.csv  replay a recording, per-frame telemetry
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#define _USE_MATH_DEFINES
#include <math.h>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#ifdef _WIN32                           /* (also builds with MSVC: the standalone Windows core) */
#include <io.h>
#include <fcntl.h>
#define dup _dup
#define dup2 _dup2
#define fdopen _fdopen
#else
#include <unistd.h>
#endif
#include "vecx.h"
#if defined(_M_X64) || defined(__x86_64__)              /* (the time-stamp counter, for the test timings) */
#ifdef _MSC_VER
#include <intrin.h>
#else
#include <x86intrin.h>
#endif
#define VT_TSC() __rdtsc ()
#else
#define VT_TSC() 0ull
#endif
#include "e8910.h"

unsigned e6809_get_pc (void);
unsigned e6809_get_s (void);
unsigned e6809_get_x (void);
unsigned e6809_get_u (void);
unsigned e6809_get_y (void);
unsigned e6809_get_a (void);
unsigned e6809_get_b (void);
void e8910_init_headless (void);
void e8910_render (unsigned char *buf, int len);
extern unsigned char (*e6809_read8) (unsigned address);

/* ---------- line capture ---------- */
typedef struct { long x0, y0, x1, y1; unsigned char c, rgb; unsigned cyc, t0; } line_t;   /* (rgb: the 3D Imager's filter, imager.c) */
#define MAXL 40000
static line_t cur[MAXL], last[MAXL];
static int ncur, nlast;

/* ---------- beam trace (VT_BEAMTRACE=1, the beam-trace renderer): the lit cycles of a frame ---------- */
typedef struct { float x, y, lev; unsigned char z; unsigned t; } beam_t;
#define MAXB 400000
static beam_t *bcur, *blast; static int nbcur, nblast;
extern void (*vt_beam_hook) (double x, double y, double lev, unsigned char z, unsigned long long t);
static void beam_take (double x, double y, double lev, unsigned char z, unsigned long long t)
{
	extern unsigned long long vt_frame_t0;
	if (lev <= 0.0 || nbcur >= MAXB) return;
	beam_t *b = &bcur[nbcur++]; b->x = (float) x; b->y = (float) y; b->lev = (float) lev; b->z = z;
	b->t = (unsigned) (t > vt_frame_t0 ? t - vt_frame_t0 : 0);
}
/* ---------- the 3D Imager's centre view (vt_imager_centre - live setting 30, the libretro option): one picture as from between
 * the eyes, for aiming (Tony, 2026-11-26: in 3D Narrow Escape the ship moves sideways between the eyes' pictures). The games
 * draw each eye the same lines in the same order, every object moved sideways by its own depth (3D Narrow Escape: its text 1918
 * units, its ship 3169, the rest 3968). So the left eye's lines (rgb bit 3) each go halfway to their partner - the right eye's
 * line of the same colour drawn as many lines into its pass, of the same shape and height (a few lines' slip allowed); a line
 * with no partner stays the left eye's. The frontends then show the left eye's lines alone. The right eye's last pass of each
 * colour is kept here (not in a save: a picture only) */
int vt_imager_centre;
#define IMC_N (MAXL / 4)
static line_t imc_r[3][IMC_N]; static int imc_nr[3];         /* (the right eye's last pass of each colour) */
static unsigned char imc_u[3][IMC_N];                         /* (its pieces already paired in the left eye's pass now) */
static int imc_key = -1, imc_j[3], imc_hp[3]; static long imc_ps[3];   /* (the pass going on; each colour's place in its partner, the last shift) */
static unsigned char imc_pl[MAXL]; static float imc_s0[MAXL], imc_s1[MAXL];   /* (this frame's left-eye pieces: paired, the shift found at each end) */
static int imc_col (unsigned char rgb) { const int c = rgb & 7; return c == 1 ? 0 : c == 2 ? 1 : c == 4 ? 2 : -1; }
/* step 2's strokes: pieces in a row that join and go on straight; their ends in order (low y first, a level one low x first) */
typedef struct { int i0, i1; double ax, ay, bx, by; } imc_stroke;
static int imc_strokes (line_t *const *P, int n, imc_stroke *S, int smax)
{
	int ns = 0, s0 = 0;
	for (int i = 1; i <= n && ns < smax; i++) {
		if (i < n) {
			const line_t *a = P[i - 1], *b = P[i];
			const double ax = a->x1 - a->x0, ay = a->y1 - a->y0, bx = b->x1 - b->x0, by = b->y1 - b->y0;
			const double na = sqrt (ax * ax + ay * ay), nb = sqrt (bx * bx + by * by);
			const int joined = labs (b->x0 - a->x1) <= 2 && labs (b->y0 - a->y1) <= 2;
			const int straight = na < 1 || nb < 1 || (fabs (ax * by - ay * bx) <= 0.08 * na * nb && ax * bx + ay * by > 0);   /* (a line's start bends a little: the beam's lag) */
			if (joined && straight) continue;
		}
		imc_stroke *t = &S[ns++]; t->i0 = s0; t->i1 = i - 1; s0 = i;
		double x0 = (double) P[t->i0]->x0, y0 = (double) P[t->i0]->y0, x1 = (double) P[t->i1]->x1, y1 = (double) P[t->i1]->y1;
		const int flip = fabs (y0 - y1) > 2 ? y0 > y1 : x0 > x1;
		if (flip) { t->ax = x1; t->ay = y1; t->bx = x0; t->by = y0; } else { t->ax = x0; t->ay = y0; t->bx = x1; t->by = y1; }
	}
	return ns;
}
/* step 2: the left eye's pieces step 1 left, by strokes - each to the right eye's stroke at the same height (in order, a few
 * strokes' slip; else the nearest - a pass drawn the other way round), every point moved by half the shift there along it
 * (a rail going into the distance: its near end moves more than its far one) */
#define IMC_S 4096
static void imc_step2 (int c, line_t **LP, int nl)
{
	static line_t *RP[IMC_N]; static int RI[IMC_N]; static imc_stroke SL[IMC_S], SR[IMC_S]; static unsigned char su[IMC_S];
	int nr = 0;
	for (int k = 0; k < imc_nr[c]; k++) if (!imc_u[c][k]) { RI[nr] = k; RP[nr++] = &imc_r[c][k]; }
	if (!nl || !nr) return;
	const int nsl = imc_strokes (LP, nl, SL, IMC_S), nsr = imc_strokes (RP, nr, SR, IMC_S);
	memset (su, 0, (size_t) nsr); int j = 0, hp = 0; double p0 = 0, p1 = 0;
	for (int k = 0; k < nsl; k++) {
		const imc_stroke *a = &SL[k]; int got = -1; double g0 = 0, g1 = 0, gc = 0;
		for (int pass = 0; pass < 2 && got < 0; pass++)
			for (int m = pass ? 0 : j; m < (pass ? nsr : (j + 8 < nsr ? j + 8 : nsr)); m++) {
				const imc_stroke *b = &SR[m];
				if (su[m] || fabs (b->ay - a->ay) > 48 || fabs (b->by - a->by) > 48) continue;
				const double s0 = b->ax - a->ax, s1 = b->bx - a->bx;
				if (fabs (s0) > 16000 || fabs (s1) > 16000) continue;
				{ const double ln = sqrt ((a->bx - a->ax) * (a->bx - a->ax) + (a->by - a->ay) * (a->by - a->ay));
				  if (fabs (s1 - s0) > 0.5 * ln + 200) continue; }   /* (its ends' shifts as far apart as its length allows: a wrong partner stretched it right across) */
				const double cst = hp ? fabs (s0 - p0) + fabs (s1 - p1) : (pass ? fabs (s0) + fabs (s1) : 0);
				if (got < 0 || cst < gc) { got = m; g0 = s0; g1 = s1; gc = cst; }
			}
		if (got < 0) continue;
		if (got >= j && got < j + 8) j = got + 1;
		su[got] = 1; hp = 1; p0 = g0; p1 = g1;
		for (int i = SR[got].i0; i <= SR[got].i1; i++) imc_u[c][RI[i]] = 1;
		const double vx = a->bx - a->ax, vy = a->by - a->ay, vv = vx * vx + vy * vy;
		for (int i = a->i0; i <= a->i1; i++) {
			const line_t *l = LP[i]; const int q = (int) (l - last);
			double t = vv > 0 ? ((l->x0 - a->ax) * vx + (l->y0 - a->ay) * vy) / vv : 0; t = t < 0 ? 0 : t > 1 ? 1 : t;
			imc_s0[q] = (float) (g0 + (g1 - g0) * t);
			t = vv > 0 ? ((l->x1 - a->ax) * vx + (l->y1 - a->ay) * vy) / vv : 0; t = t < 0 ? 0 : t > 1 ? 1 : t;
			imc_s1[q] = (float) (g0 + (g1 - g0) * t); imc_pl[q] = 1;
		}
	}
}
/* the left eye's lines of one pass in this frame (last[i0..i1] of colour c): step 1 - pieces in draw order with the same shape
 * and height (a slip of 8 allowed, the one shifted most like the last pair) halfway to their partner; then step 2 */
static void imc_left (int c, int i0, int i1)
{
	static line_t *LP[MAXL]; int nl = 0;
	for (int i = i0; i <= i1; i++) {
		line_t *a = &last[i]; imc_pl[i] = 0;
		if (imc_col (a->rgb) != c || !(a->rgb & 8)) continue;
		int got = -1; long gs = 0, gc = 0;
		for (int pass = 0; pass < 2 && got < 0; pass++)    /* (near in order; else the same shape anywhere in the pass - the
		                                                     eyes may draw their objects in another order) */
			for (int m = pass ? 0 : imc_j[c]; m < imc_nr[c] && (pass || m < imc_j[c] + 8); m++) {
				const line_t *b = &imc_r[c][m];
				if (imc_u[c][m] || labs ((a->x1 - a->x0) - (b->x1 - b->x0)) > 8 || labs ((a->y1 - a->y0) - (b->y1 - b->y0)) > 8 || labs (a->y0 - b->y0) > 48) continue;
				const long sh = b->x0 - a->x0; if (labs (sh) > 16000) continue;
				const long cst = imc_hp[c] ? labs (sh - imc_ps[c]) : (pass ? labs (sh) : 0);
				if (got < 0 || cst < gc) { got = m; gs = sh; gc = cst; }
			}
		if (got >= 0) { imc_u[c][got] = 1; imc_j[c] = got + 1; imc_hp[c] = 1; imc_ps[c] = gs; imc_s0[i] = imc_s1[i] = (float) gs; imc_pl[i] = 1; }
		else LP[nl++] = a;
	}
	imc_step2 (c, LP, nl);
	/* then each chain of joined pieces as one: a piece with no partner takes its neighbours' shift (a tumbling figure is not the
	 * same shape in the two half turns), and where two pieces meet both ends get one shift - so a shape stays whole; a path into
	 * the distance (a corridor's walls) keeps its shifts as found */
	for (int k0 = i0; k0 <= i1; ) {
		if (imc_col (last[k0].rgb) != c || !(last[k0].rgb & 8)) { k0++; continue; }
		int k1 = k0;
		while (k1 + 1 <= i1 && imc_col (last[k1 + 1].rgb) == c && (last[k1 + 1].rgb & 8)
		       && labs (last[k1 + 1].x0 - last[k1].x1) <= 2 && labs (last[k1 + 1].y0 - last[k1].y1) <= 2) k1++;
		int known = 0; for (int k = k0; k <= k1; k++) known += imc_pl[k];
		if (known) {
			static float m[MAXL];
			for (int k = k0; k <= k1; k++) m[k] = imc_pl[k] ? 0.5f * (imc_s0[k] + imc_s1[k]) : 1e30f;
			for (int k = k0; k <= k1; k++) {                /* (the neighbours' shift: the median of up to 7 round it) */
				float w[7]; int nw = 0;
				for (int q = k - 3; q <= k + 3; q++) if (q >= k0 && q <= k1 && m[q] < 1e29f) w[nw++] = m[q];
				if (!nw) {                                  /* (none near: the nearest known along the chain) */
					for (int d = 4; d <= k1 - k0 && !nw; d++) { if (k - d >= k0 && m[k - d] < 1e29f) w[nw++] = m[k - d]; else if (k + d <= k1 && m[k + d] < 1e29f) w[nw++] = m[k + d]; }
				}
				for (int a1 = 1; a1 < nw; a1++) for (int b1 = a1; b1 > 0 && w[b1 - 1] > w[b1]; b1--) { float t = w[b1]; w[b1] = w[b1 - 1]; w[b1 - 1] = t; }
				const float med = nw ? w[nw / 2] : 0;
				if (!imc_pl[k]) { imc_s0[k] = imc_s1[k] = med; imc_pl[k] = 1; }
			}
			for (int k = k0; k < k1; k++) if (fabsf (imc_s1[k] - imc_s0[k + 1]) <= 400) { const float v = 0.5f * (imc_s1[k] + imc_s0[k + 1]); imc_s1[k] = imc_s0[k + 1] = v; }
			for (int k = k0; k <= k1; k++) { last[k].x0 += (long) floor (imc_s0[k] / 2 + 0.5f); last[k].x1 += (long) floor (imc_s1[k] / 2 + 0.5f); }
		}
		k0 = k1 + 1;
	}
}
static void imager_centre (void)
{
	extern int vt_imager;
	if (!vt_imager || !vt_imager_centre || !nlast) return;
	int run = -1, r0 = 0;                                  /* (this frame's run of left-eye lines of one colour) */
	for (int i = 0; i <= nlast; i++) {
		const int c = i < nlast ? imc_col (last[i].rgb) : -1, left = i < nlast && (last[i].rgb & 8);
		const int key = i < nlast && c >= 0 ? left * 4 + c : -1;
		if (run >= 0 && (key != run || i == nlast)) {   /* (a left run ends here: its pieces paired) */
			imc_left (run - 4, r0, i - 1); run = -1;
		}
		if (key < 0) continue;
		const int fresh = key != imc_key; imc_key = key;
		if (!left) {                                        /* (the right eye's pass: kept - a new one in place of the last) */
			if (fresh) imc_nr[c] = 0;
			if (imc_nr[c] < IMC_N) { imc_u[c][imc_nr[c]] = 0; imc_r[c][imc_nr[c]++] = last[i]; }
			continue;
		}
		if (fresh) { imc_j[c] = 0; imc_hp[c] = 0; memset (imc_u[c], 0, (size_t) imc_nr[c]); }   /* (a new left pass: its partner's pieces all free) */
		if (run < 0) { run = key; r0 = i; }
	}
}
static void beam_swap (void)
{
	imager_centre ();                                       /* (every frame's end comes here) */
	if (!bcur) return;
	beam_t *k = blast; blast = bcur; bcur = k; nblast = nbcur; nbcur = 0;
}

unsigned long vt_nlines;
void vt_line (long x0, long y0, long x1, long y1, unsigned char c, unsigned cyc, unsigned t0)
{
	vt_nlines++;
	extern unsigned char vt_line_rgb;
	if (ncur < MAXL) { line_t *l = &cur[ncur++]; l->x0 = x0; l->y0 = y0; l->x1 = x1; l->y1 = y1; l->c = c; l->rgb = vt_line_rgb; l->cyc = cyc; l->t0 = t0; }
}

/* ---------- stats ---------- */
static unsigned long long total_cycles;
static unsigned long long frame_start_cycles;
static unsigned long long last_busframe;  /* (a UVMC2's / PiTrex's own frame end: run_frame's budget) */
static unsigned long frames;          /* Wait_Recal entries */
static unsigned long long idle_cycles, idle_frame;
static unsigned long long busy_max, busy_sum;
static unsigned long frames_over;     /* frames whose busy time > 30000 */
static int frame_done, in_wait;
int vt_slice_frame;                         /* (online VecLink slices: a game frame ended in the last one) */
int vt_t2w_new;                       /* cartridge code restarted timer 2 (vecx.c) */
static unsigned long long last_recal; /* the last Wait_Recal (cycles) */
static int crashed;
static unsigned crash_pc;
static unsigned long bankchg_log;
static int verbose_bank;
static uint32_t prof[5][65536];   /* [cart bank 0-3][pc < $C000], [4][pc >= $C000] */ static int prof_on;
extern int vt_cur_bank (void);

/* bank switches made by code outside the common areas ($0000-$00FF, VT_COMMON..$7FFF, default
   $7F00): the instructions after such a switch come from the other bank = a banking bug */
static unsigned long bad_switches; static unsigned bad_switch_pc; static unsigned common_base = 0x7F00;
void vt_bankchange (int from, int to)
{
	unsigned pc0 = e6809_get_pc () & 0xFFFF;
	if (pc0 >= 0x100 && pc0 < common_base) { if (!bad_switches) bad_switch_pc = pc0; bad_switches++; }
	if (verbose_bank && bankchg_log < 50)
		printf ("  bank %d -> %d at PC=%04X cyc=%llu\n", from, to, e6809_get_pc (), total_cycles);
	bankchg_log++;
}

/* a UVMC2 game made with its SDK starts every frame's list with DDRB = $9F (vecx.c): that frame is over, the whole
 * picture, as at a 6809 game's Wait_Recal (the bus frame is ~32000-55000 cycles, not 30000) */
void vt_u2_frame (void)
{
	if (total_cycles - frame_start_cycles < 15000) return;
	extern unsigned long long alg_cycles, vt_frame_t0;
	frame_start_cycles = total_cycles;
	idle_frame = 0;
	frames++;
	memcpy (last, cur, sizeof (line_t) * ncur);
	nlast = ncur; ncur = 0; beam_swap ();
	frame_done = 1;
	last_busframe = total_cycles;
	vt_frame_t0 = alg_cycles;
	{ extern unsigned long long vt_piece_t0; vt_piece_t0 = alg_cycles; }
}
void vt_step (unsigned icycles)
{
	unsigned pc = e6809_get_pc () & 0xFFFF;
	total_cycles += icycles;
	if (prof_on) {
		if (pc < 0xC000) prof[vt_cur_bank () & 3][pc] += icycles;
		else prof[4][pc] += icycles;
		static int tpc = -2;                    /* VT_TRACEPC=hex: while profiling, X, U, S and the caller at that PC */
		if (tpc == -2) { const char *z = getenv ("VT_TRACEPC"); tpc = z ? (int) strtol (z, 0, 16) : -1; }
		static int tall = -1;                   /* VT_TRACEALL=1: every instruction in $E000-$EFFF (Mine Storm) */
		if (tall < 0) tall = getenv ("VT_TRACEALL") != NULL;
		if (tall && pc >= 0xE000 && pc < 0xF000)
			printf ("T %04X A=%02X B=%02X X=%04X Y=%04X U=%04X S=%04X op=%02X %02X %02X\n", pc, e6809_get_a (), e6809_get_b (), e6809_get_x (), e6809_get_y (), e6809_get_u (), e6809_get_s (), e6809_read8 (pc), e6809_read8 (pc + 1), e6809_read8 (pc + 2));
		if ((int) pc == tpc) {
			unsigned s = e6809_get_s ();
			printf ("trace %04X: X=%04X U=%04X S=%04X ret=%02X%02X  X->", pc, e6809_get_x (), e6809_get_u (), s, e6809_read8 (s), e6809_read8 (s + 1));
			for (int i = 0; i < 12; i++) printf (" %02X", e6809_read8 ((e6809_get_x () + i) & 0xFFFF));
			printf ("\n");
		}
	}
	/* Wait_Recal busy-waits on T2 inside the BIOS ($F192..$F1AE region) */
	if (pc >= 0xF192 && pc < 0xF1AF) { idle_cycles += icycles; idle_frame += icycles; in_wait = 1; }
	else if (in_wait) {                  /* Wait_Recal's T2 wait is over: the frame's drawing starts */
		extern unsigned long long alg_cycles, vt_frame_t0;
		in_wait = 0; vt_frame_t0 = alg_cycles;
	}
	if (pc == 0xF192) {
		unsigned long long len = total_cycles - frame_start_cycles;
		unsigned long long busy = len > idle_frame ? len - idle_frame : 0;
		if (frames > 0) {
			busy_sum += busy;
			if (busy > busy_max) busy_max = busy;
			if (busy > 30000) frames_over++;
		}
		frame_start_cycles = total_cycles;
		idle_frame = 0;
		frames++;
		{ /* (a frame longer than the BIOS's 20 ms - the Full Grid Test, 25 a second: on the tube it flickers and the eye sees the whole
		   *  picture, each part at its average light; a screen showing one picture per game frame (G-Sync) showed the frame's end, its
		   *  first lines ~35 ms old and gone (the owner's PC, 2026-11-25: the lower left missing). So its lines' times go into its last
		   *  20 ms, in their order - every line at most 20 ms old when the frame is shown, as in a 50 fps game - and their light is
		   *  scaled by 20 ms / the frame: the eye's average of the slower flicker (the dwell's light ~ dwell^0.75). VT_LONGFRAME=0: off */
		  extern unsigned long long vt_piece_t0, alg_cycles; extern int vt_uvm2, vt_pitrex; static int lf = -1;
		  if (lf < 0) { const char *z = getenv ("VT_LONGFRAME"); lf = z ? atoi (z) : 1; }
		  const unsigned long long D = alg_cycles > vt_piece_t0 ? alg_cycles - vt_piece_t0 : 0; const double W = 30000.0;
		  if (lf && !vt_uvm2 && !vt_pitrex && D > 30000 && D < 400000) {   /* (from 20 ms on, smoothly: no step in a busy game) */
			const double k = W / (double) D, kc = pow (k, 1.0 / 0.75);
			for (int i = 0; i < ncur; i++) { line_t *l = &cur[i];
				const double t = l->t0 < D ? (double) l->t0 : (double) D; l->t0 = (unsigned) ((double) D - W + t * k + 0.5);
				l->cyc = (unsigned) (l->cyc * kc + 0.5); if (!l->cyc) l->cyc = 1; } } }
		memcpy (last, cur, sizeof (line_t) * ncur);
		nlast = ncur; ncur = 0; beam_swap ();
		frame_done = 1;
		last_recal = total_cycles;
		{ extern unsigned long long vt_piece_t0, alg_cycles; vt_piece_t0 = alg_cycles; }   /* (the next piece begins) */
	}
	/* a game that paces itself without Wait_Recal (Vectorblade) restarts timer 2 once a frame from its
	 * own code, as Wait_Recal does: its frame ends there, at its own rate (50 fps) - not cut every
	 * frame_budget cycles (45 fps: the display then held the last copy of what moved too long, and a
	 * rising title shook). Only without a Wait_Recal in the last 100000 cycles, at least 15000 apart. */
	if (vt_t2w_new) {
		vt_t2w_new = 0;
		if (total_cycles - last_recal > 100000 && total_cycles - frame_start_cycles >= 15000) {
			extern unsigned long long alg_cycles, vt_frame_t0;
			if (getenv ("VT_LOG_T2FRAME")) fprintf (stderr, "T2 frame %llu cycles\n", total_cycles - frame_start_cycles);
			frame_start_cycles = total_cycles;
			idle_frame = 0;
			frames++;
			memcpy (last, cur, sizeof (line_t) * ncur);
			nlast = ncur; ncur = 0; beam_swap ();
			frame_done = 1;
			{ extern int vt_uvm2, vt_pitrex; if (vt_uvm2 || vt_pitrex) last_busframe = total_cycles; }   /* (a PiTrex: its frame) */
			vt_frame_t0 = alg_cycles;
			{ extern unsigned long long vt_piece_t0; vt_piece_t0 = alg_cycles; }
		}
	}
	/* crash detection: execution outside cart window / BIOS */
	{ extern int vt_vb; if (vt_vb && pc >= 0x8000 && pc < 0xC000) return; }   /* (the 48K cartridge) */
	if (!crashed && ((pc >= 0x8000 && pc < 0xC800) || (pc >= 0xD000 && pc < 0xE000))) {
		crashed = 1; crash_pc = pc;
		printf ("CRASH: PC=%04X at cycle %llu frame %lu\n", pc, total_cycles, frames);
	}
}

/* ---------- PNG writer (stored deflate) ---------- */
static uint32_t crc_table[256];
static void crc_init (void) { for (uint32_t n = 0; n < 256; n++) { uint32_t c = n; for (int k = 0; k < 8; k++) c = c & 1 ? 0xedb88320u ^ (c >> 1) : c >> 1; crc_table[n] = c; } }
static uint32_t crc (const unsigned char *b, size_t n, uint32_t c) { c ^= 0xffffffffu; while (n--) c = crc_table[(c ^ *b++) & 0xff] ^ (c >> 8); return c ^ 0xffffffffu; }
static void be32 (unsigned char *p, uint32_t v) { p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v; }
static void chunk (FILE *f, const char *t, const unsigned char *d, uint32_t n)
{
	unsigned char h[8]; be32 (h, n); memcpy (h + 4, t, 4); fwrite (h, 1, 8, f);
	if (n) fwrite (d, 1, n, f);
	uint32_t c = crc ((const unsigned char *) t, 4, 0); c = crc (d, n, c);
	unsigned char cc[4]; be32 (cc, c); fwrite (cc, 1, 4, f);
}
static void write_png (const char *fn, const unsigned char *rgb, int w, int h)
{
	FILE *f = fopen (fn, "wb"); if (!f) { perror (fn); return; }
	static const unsigned char sig[8] = { 137, 80, 78, 71, 13, 10, 26, 10 };
	fwrite (sig, 1, 8, f);
	unsigned char ihdr[13]; be32 (ihdr, w); be32 (ihdr + 4, h); ihdr[8] = 8; ihdr[9] = 2; ihdr[10] = 0; ihdr[11] = 0; ihdr[12] = 0;
	chunk (f, "IHDR", ihdr, 13);
	size_t raw_n = (size_t) h * (w * 3 + 1);
	unsigned char *raw = malloc (raw_n);
	for (int y = 0; y < h; y++) { raw[y * (w * 3 + 1)] = 0; memcpy (raw + y * (w * 3 + 1) + 1, rgb + (size_t) y * w * 3, w * 3); }
	size_t nblk = (raw_n + 65534) / 65535;
	size_t z_n = 2 + raw_n + nblk * 5 + 4;
	unsigned char *z = malloc (z_n), *p = z;
	*p++ = 0x78; *p++ = 0x01;
	size_t off = 0; uint32_t a = 1, b = 0;
	for (size_t i = 0; i < raw_n; i++) { a = (a + raw[i]) % 65521; b = (b + a) % 65521; }
	while (off < raw_n) {
		size_t n = raw_n - off > 65535 ? 65535 : raw_n - off;
		*p++ = off + n == raw_n; *p++ = n & 0xff; *p++ = n >> 8; *p++ = ~n & 0xff; *p++ = (~n >> 8) & 0xff;
		memcpy (p, raw + off, n); p += n; off += n;
	}
	be32 (p, (b << 16) | a); p += 4;
	chunk (f, "IDAT", z, (uint32_t) (p - z));
	chunk (f, "IEND", NULL, 0);
	fclose (f); free (raw); free (z);
}

#define IW 495
#define IH 615
static float acc[IH][IW];
static void plot (int x, int y, float v) { if (x >= 0 && y >= 0 && x < IW && y < IH) acc[y][x] += v; }
/* shothr: anti-aliased high-resolution shot (k x the 495 x 615 size): each line is sampled every
 * 1/4 px and splatted bilinearly with a 1.2 px beam, so fine text shows its real shape. */
static void render_shot_hr (const char *fn, double k)
{
	int w = (int) (IW * k), h = (int) (IH * k);
	float *a = calloc ((size_t) w * h, sizeof (float));
	const double sx = (double) w / ALG_MAX_X, sy = (double) h / ALG_MAX_Y;
	double beam = 0.6 * k;                                   /* beam radius in px */
	for (int i = 0; i < nlast; i++) {
		line_t *l = &last[i];
		float v = 0.35f + l->c / 127.0f;
		double x0 = l->x0 * sx, y0 = l->y0 * sy, x1 = l->x1 * sx, y1 = l->y1 * sy;
		double len = hypot (x1 - x0, y1 - y0);
		int n = 1 + (int) (len * 4);
		for (int s = 0; s <= n; s++) {
			double t = (double) s / n, px = x0 + (x1 - x0) * t, py = y0 + (y1 - y0) * t;
			int r = (int) ceil (beam) + 1;
			for (int yy = (int) py - r; yy <= (int) py + r; yy++)
				for (int xx = (int) px - r; xx <= (int) px + r; xx++) {
					if (xx < 0 || yy < 0 || xx >= w || yy >= h) continue;
					double d = hypot (xx + 0.5 - px, yy + 0.5 - py);
					if (d < beam + 1) a[(size_t) yy * w + xx] += v * (float) (d < beam ? 1.0 : beam + 1 - d) * 0.25f;
				}
		}
	}
	unsigned char *rgb = malloc ((size_t) w * h * 3);
	for (size_t p = 0; p < (size_t) w * h; p++) {
		float v = a[p]; if (v > 1) v = 1;
		rgb[p * 3] = (unsigned char) (v * 210); rgb[p * 3 + 1] = (unsigned char) (v * 235); rgb[p * 3 + 2] = (unsigned char) (v * 255);
	}
	write_png (fn, rgb, w, h); free (rgb); free (a);
	printf ("shothr %s (%d vectors)\n", fn, nlast);
}

static void render_shot (const char *fn)
{
	memset (acc, 0, sizeof acc);
	const double sx = (double) IW / ALG_MAX_X, sy = (double) IH / ALG_MAX_Y;
	for (int i = 0; i < nlast; i++) {
		line_t *l = &last[i];
		float v = 0.25f + l->c / 127.0f * 1.2f;
		double x0 = l->x0 * sx, y0 = l->y0 * sy, x1 = l->x1 * sx, y1 = l->y1 * sy;
		double dx = x1 - x0, dy = y1 - y0;
		double len = dx * dx + dy * dy; int n = 1 + (int) (len > 0 ? sqrt (len) * 2 : 0);
		if (n == 1) { /* dot */ for (int oy = -1; oy <= 1; oy++) for (int ox = -1; ox <= 1; ox++) plot ((int) x0 + ox, (int) y0 + oy, v * (ox || oy ? 0.5f : 1.5f)); continue; }
		for (int k = 0; k <= n; k++) {
			double t = (double) k / n; int px = (int) (x0 + dx * t), py = (int) (y0 + dy * t);
			plot (px, py, v * 0.6f); plot (px + 1, py, v * 0.2f); plot (px, py + 1, v * 0.2f);
		}
	}
	unsigned char *rgb = malloc (IW * IH * 3);
	for (int y = 0; y < IH; y++) for (int x = 0; x < IW; x++) {
		float v = acc[y][x]; if (v > 1) v = 1;
		unsigned char *p = rgb + (y * IW + x) * 3;
		p[0] = (unsigned char) (v * 200); p[1] = (unsigned char) (v * 230); p[2] = (unsigned char) (v * 255);
	}
	write_png (fn, rgb, IW, IH); free (rgb);
	printf ("shot %s (%d vectors)\n", fn, nlast);
}

/* ---------- audio ---------- */
static FILE *wavf; static long wav_bytes; static double snd_acc;
static void wav_hdr (FILE *f, long n)
{
	unsigned char h[44] = "RIFF\0\0\0\0WAVEfmt \x10\0\0\0\x01\0\x01\0\0\0\0\0\0\0\0\0\x02\0\x10\0data";
	long r = 44100, br = 88200;                   /* 16-bit mono 44.1 kHz (psg.c) */
	h[24] = r; h[25] = r >> 8; h[26] = r >> 16; h[28] = br; h[29] = br >> 8; h[30] = br >> 16;
	long t = n + 36; h[4] = t; h[5] = t >> 8; h[6] = t >> 16; h[7] = t >> 24;
	fseek (f, 0, SEEK_SET); fwrite (h, 1, 40, f);
	unsigned char d[4] = { n, n >> 8, n >> 16, n >> 24 }; fwrite (d, 1, 4, f);
}

static void live_audio (const unsigned char *b, int n);

/* run until frame end or cycle budget */
static void emu_chunk (long cyc)
{
	vecx_emu (cyc);
	static int16_t buf[4096];
	extern int psg_take (int16_t *buf, int max);
	int ns = psg_take (buf, 4096);
	if (ns > 0) {
		extern int vt_speech_sep, vt_vecvoice; extern int psg_take_speech (int16_t *buf, int max);
		extern int vt_uvm2, vt_uvm2_cart; extern int uvm2_jack44 (int16_t *, int);
		extern int vt_imager;
		if ((vt_speech_sep && vt_vecvoice) || vt_uvm2_cart || vt_imager) {   /* (a UVMC2: two channels in its intro too, the jack silent) */     /* (two channels: the console, the speech module /
		                                                                     the UVMC2's jack / the 3D Imager's motor) */
			static int16_t sb[4096], st[8192];
			if (vt_uvm2) uvm2_jack44 (sb, ns); else if (vt_uvm2_cart) memset (sb, 0, ns * sizeof sb[0]); else psg_take_speech (sb, ns);
			for (int i = 0; i < ns; i++) { st[2 * i] = buf[i]; st[2 * i + 1] = sb[i]; }
			if (wavf) { for (int i = 0; i < ns; i++) { int v = buf[i] + sb[i]; buf[i] = (int16_t) (v > 32767 ? 32767 : v < -32768 ? -32768 : v); }
			            fwrite (buf, 2, ns, wavf); wav_bytes += ns * 2; }
			live_audio ((const unsigned char *) st, ns * 4);
		} else {
			if (wavf) { fwrite (buf, 2, ns, wavf); wav_bytes += ns * 2; }
			live_audio ((const unsigned char *) buf, ns * 2);
		}
	}
}
static FILE *recf;                      /* "recstart": log every frame's input like --live */
/* a frame ends at Wait_Recal; code that never calls it (Mine Storm's warp between levels paces
 * itself) ends one after frame_budget cycles: 200000 in scripts (frame counts stay as they were),
 * 33000 in --live play, so such effects show at ~45 Hz like on the tube instead of 7.5 Hz */
static long frame_budget = 200000;
/* how the last piece (run_frame) ended: 0 the game's own frame end (Wait_Recal, its timer 2, a card's frame), 1 cut by the
 * budget, 2 cut by the budget while the game ends its own frames (one in the last 100000 cycles) - the game's frame goes on in
 * the next piece: the display joins the two (libretro_vx.c frame_push, vectrex_emu.py's frame queue - the same rule). The Test
 * Cartridge Rev. 4's screen 1 times its 42000-cycle frames with timer 2 and was cut at 33000: the cut moved through its
 * picture, and a joined frame had a line twice and the next none (the owner, 2026-11-23: steady on the real Vectrex) */
int vt_piece_cut;
static void run_frame (void)
{
	if (recf) {
		unsigned char m[4] = { alg_jch0, alg_jch1, (~snd_regs[14]) & 15, 0 };
		fwrite (m, 1, 4, recf);
	}
	frame_done = 0; vt_piece_cut = 0;
	/* online VecLink: a slice of the cable's clock, not the game's frame - both consoles of a PC stop at the same
	 * cable time (veclink.c) */
	{ extern int vt_veclink; extern long vl_slice;
	  if (vt_veclink && vl_slice > 0) {
		extern unsigned long long alg_cycles, vl_slice_begin (void); extern void vl_slice_park (void); extern int vt_slice_frame;
		unsigned long long end = vl_slice_begin (); int any = 0;
		while (alg_cycles < end) { unsigned long long left = end - alg_cycles; emu_chunk (left > 500 ? 500 : (long) left); if (frame_done) any = 1; }
		vl_slice_park ();
		{ extern unsigned long long vt_frame_t0;
		  if (!any && alg_cycles - vt_frame_t0 > 66000) {   /* (a game without Wait_Recal - Berzerk Arena: its frame cut here,
			                                                as the budget does in the game's own frames) */
			memcpy (last, cur, sizeof (line_t) * ncur); nlast = ncur; ncur = 0; beam_swap ();
			vt_frame_t0 = alg_cycles;
			{ extern unsigned long long vt_piece_t0; vt_piece_t0 = alg_cycles; }
			any = 1;
		  } }
		vt_slice_frame = any;                       /* (a game frame ended in it: its picture is new - else nothing to send) */
		return;
	  } }
	/* a game that calls Wait_Recal ends its frames itself (the BIOS's timer 2: at most 65535 cycles): the whole
	 * frame, so its picture is never cut in two - a slow game (Armor Attack's helicopter, a 30 fps game) was cut
	 * at frame_budget in the middle of its drawing, and the display could not tell its pictures apart (flicker).
	 * A game without it (Mine Storm's warp) or a UVMC2 / PiTrex bus frame: frame_budget, as before */
	long budget = frame_budget;
	{ extern int vt_uvm2, vt_pitrex;
	  if (!vt_uvm2 && !vt_pitrex && last_recal && total_cycles - last_recal < 100000) budget = 70000;
	  /* a UVMC2 game / a PiTrex that ends its own frames (vt_u2_frame, timer 2): the whole frame, as with Wait_Recal -
	   * cut at 30000 a UVMC2 frame lost a moving slice of its picture, and a PiTrex's 30006-cycle frame came out empty
	   * every ~85 frames */
	  if ((vt_uvm2 || vt_pitrex) && last_busframe && total_cycles - last_busframe < 100000) budget = 70000; }
	{ static int ht = -1; if (ht < 0) ht = getenv ("VT_HANGTRACE") != NULL;
	  if (ht) { while (!frame_done && budget > 0) { fprintf (stderr, "chunk pc=%04X s=%X budget=%ld\n", e6809_get_pc () & 0xffff, e6809_get_s (), budget); emu_chunk (500); budget -= 500; } }
	  else
	while (!frame_done && budget > 0) { emu_chunk (500); budget -= 500; } }
	{ static int pl = -1; if (pl < 0) pl = getenv ("VT_PROTLOG") != NULL;   /* (debugging the deflection protect: frames it dimmed) */
	  if (pl) { extern double vt_protect_f (void), vt_protect_v (void); static unsigned long fno; fno++;
	    if (vt_protect_f () < 0.98) { long ymin = 1 << 30, ymax = -1, xmin = 1 << 30, xmax = -1; for (int i = 0; i < ncur; i++) { if (cur[i].y0 < ymin) ymin = cur[i].y0; if (cur[i].y1 < ymin) ymin = cur[i].y1; if (cur[i].y0 > ymax) ymax = cur[i].y0; if (cur[i].y1 > ymax) ymax = cur[i].y1; if (cur[i].x0 < xmin) xmin = cur[i].x0; if (cur[i].x0 > xmax) xmax = cur[i].x0; }
	      fprintf (stderr, "PROT frame %lu f=%.2f v=%.2f lines=%d y %ld..%ld x %ld..%ld\n", fno, vt_protect_f (), vt_protect_v (), ncur, ymin, ymax, xmin, xmax); } } }
	if (!frame_done) {                  /* no Wait_Recal: show what was drawn and start a new frame */
		extern unsigned long long alg_cycles, vt_frame_t0; extern int vt_uvm2, vt_pitrex, vt_imager;
		vt_piece_cut = !vt_uvm2 && !vt_pitrex && !vt_imager && frames > 0 && total_cycles - frame_start_cycles < 100000 ? 2 : 1;   /* (see vt_piece_cut) */
		memcpy (last, cur, sizeof (line_t) * ncur); nlast = ncur; ncur = 0; beam_swap ();
		vt_frame_t0 = alg_cycles;
		{ extern unsigned long long vt_piece_t0; vt_piece_t0 = alg_cycles; }
	}
}

#include "bot.c"
#include "live.c"

static void stats (void)
{
	extern unsigned long vt_via_mirror; extern unsigned vt_via_mirror_pc, vt_via_mirror_addr;
	printf ("stats: cycles=%llu frames=%lu bankswitches=%lu crashed=%d", total_cycles, frames, vt_bank_switches, crashed);
	printf (" badswitches=%lu", bad_switches);
	if (bad_switches) printf (" (first at PC=%04X)", bad_switch_pc);
	printf (" viamirror=%lu", vt_via_mirror);
	if (vt_via_mirror) printf (" (first at PC=%04X addr=%04X)", vt_via_mirror_pc, vt_via_mirror_addr);
	if (frames > 1) printf (" avgbusy=%llu maxbusy=%llu over30000=%lu", busy_sum / (frames - 1), busy_max, frames_over);
	{ extern double vt_protect_min (void); extern void vt_protect_stats (unsigned long *, unsigned long *, double *, unsigned long long *); unsigned long pl, pd; double pm; unsigned long long pt; vt_protect_stats (&pl, &pd, &pm, &pt);
	  printf (" protmin=%.2f protdim=%lu/%lu dimmin=%.2f dimat=%.1fs", vt_protect_min (), pd, pl, pm, pt / 1500000.0); }   /* (the deflection protect: 1.00 = never dimmed) */
	{ extern double vt_alg_skipped (void); printf (" algskip=%.2f", vt_alg_skipped ()); }   /* (the analog side's cycles that were only counted) */
	printf (" ram=%08x\n", ram_sum ());
	{ extern void arm11_jit_stats (void); extern int vt_pitrex; if (vt_pitrex) { extern void pitrex_digest (void); arm11_jit_stats (); pitrex_digest (); } }
}

/* the game's title in the cartridge header (the first 64 bytes): does it hold this word? */
/* (tests, hitch: the wall clock in seconds) */
#ifdef _WIN32
#include <windows.h>
static double vt_wall (void) { LARGE_INTEGER f, t; QueryPerformanceFrequency (&f); QueryPerformanceCounter (&t); return (double) t.QuadPart / (double) f.QuadPart; }
#else
#include <time.h>
static double vt_wall (void) { struct timespec ts; clock_gettime (CLOCK_MONOTONIC, &ts); return ts.tv_sec + ts.tv_nsec * 1e-9; }
#endif
static int memmem_title (const unsigned char *c, size_t n, const char *w)
{
	size_t L = strlen (w), m = n < 64 ? n : 64;
	for (size_t i = 0; i + L <= m; i++) if (!memcmp (c + i, w, L)) return 1;
	return 0;
}

int main (int argc, char **argv)
{
	if (argc < 4) { fprintf (stderr, "usage: vtest bios cart script [outdir]\n"); return 2; }
	const char *outdir = argc > 4 ? argv[4] : ".";
	crc_init ();
	FILE *f = fopen (argv[1], "rb"); if (!f || fread (rom, 1, 8192, f) != 8192) { fprintf (stderr, "bad bios\n"); return 2; } fclose (f);
	memset (cart, 0xff, sizeof cart);
	f = fopen (argv[2], "rb"); if (!f) { perror (argv[2]); return 2; }
	size_t n = fread (cart, 1, sizeof cart, f); fclose (f); { extern int vt_nocart; vt_nocart = (n == 0); }   /* (an empty slot: open bus) */
	{ size_t L = strlen (argv[2]);                  /* a UVMC2 game (.um2): native ARM on the cartridge (uvm2_cart.c) */
	  if (L > 4 && argv[2][L - 4] == '.' && (argv[2][L - 3] | 32) == 'u' && (argv[2][L - 2] | 32) == 'm' && argv[2][L - 1] == '2') {
		extern int uvm2_open (const char *); extern int vt_nocart;
		memset (cart, 0xff, sizeof cart); n = 0; vt_nocart = 1;
		if (uvm2_open (argv[2])) return 2;
	  }
	  /* a PiTrex program (.img / .pit): a Raspberry Pi Zero on the cartridge (pitrex_cart.c) */
	  if (L > 4 && argv[2][L - 4] == '.' && (((argv[2][L - 3] | 32) == 'i' && (argv[2][L - 2] | 32) == 'm' && (argv[2][L - 1] | 32) == 'g')
	                                    || ((argv[2][L - 3] | 32) == 'p' && (argv[2][L - 2] | 32) == 'i' && (argv[2][L - 1] | 32) == 't'))) {
		extern int pitrex_open (const char *); extern int vt_nocart;
		memset (cart, 0xff, sizeof cart); n = 0; vt_nocart = 1;
		if (pitrex_open (argv[2])) return 2;
	  } }
	if (n <= 32768) memcpy (cart + 32768, cart, 32768); /* 32K image: both halves */
	/* VT_CARTHW=0: a bone-stock Vectrex with a plain 32K cartridge - the first 32K of any image, no
	 * bank switching of any kind (PB6, Vectorblade's IRQ, multicart latch) */
	/* VT_CARTHW: 1 (or unset) = by the image's size, or a board by name: pb6 (64K), vb (Vectorblade's
	 * 4 x 48K flash), hb512 (the 512K latch flash), multi (Sean Kelly's Multi-Cart) */
	const char *hw = getenv ("VT_CARTHW");
	int kind = n == 524288 ? 'r' : n > 262144 ? 'm' : n > 65536 ? 'v' : 'p';   /* (the 512K latch cart: exactly 512K; other big images: the Multi-Cart) */
	if (hw && !strcmp (hw, "pb6")) kind = 'p';
	else if (hw && !strcmp (hw, "vb")) kind = 'v';
	else if (hw && !strcmp (hw, "hb512")) kind = 'r';
	else if (hw && !strcmp (hw, "multi")) kind = 'm';
	else if (hw && atoi (hw) == 0 && isdigit ((unsigned char) hw[0])) kind = 0;
	{ /* a cartridge with 8K RAM at $2000 (vecx.c vt_cart_ram): by name, or Auto by the title in the header (AnimAction) */
	  extern int vt_cart_ram; extern unsigned char vt_cram[0x2000];
	  int auto_hw = !hw || (isdigit ((unsigned char) hw[0]) && atoi (hw) == 1);
	  vt_cart_ram = (hw && !strcmp (hw, "ram")) || (auto_hw && n >= 48 && memmem_title (cart, n, "ANIMACTION"));
	  if (hw && !strcmp (hw, "ram")) kind = 'p';
	  memset (vt_cram, 0, sizeof vt_cram); }
	if (kind == 0) {
		memcpy (cart + 32768, cart, 32768); memset (cart + 65536, 0xff, sizeof cart - 65536);
	} else if (kind == 'm') {                           /* Sean Kelly's Multi-Cart (latch at $7FFF) */
		extern int vt_multicart; vt_multicart = 1;
	} else if (kind == 'v' || kind == 'r') {            /* the flash cartridges: Vectorblade's 4 x 48K, the 512K latch cart */
		extern int vt_vb, vt_hb512; extern const char *vt_flash_file; extern size_t vt_cart_len;
		if (kind == 'v') { vt_vb = 1; vt_cart_len = n > 65536 ? n : 262144; }
		else { vt_hb512 = 1; vt_cart_len = 524288; }  /* (the whole chip: a game may save anywhere) */
		/* the flash's saved state (high scores, settings) - next to the ROM, the dump stays as it is */
		static char sav[4096]; snprintf (sav, sizeof sav, "%s.sav", argv[2]); vt_flash_file = sav;
		const char *fc = getenv ("VT_FLASH_CLEAN"), *fi = getenv ("VT_FLASH_IMG");   /* (online: the host's flash on both sides (fi) or as it came -
		                                                  each player's own differed; none written) */
		if (fc && fc[0] && fc[0] != '0') vt_flash_file = NULL;
		FILE *fs = fi && fi[0] ? fopen (fi, "rb") : vt_flash_file ? fopen (sav, "rb") : NULL;
		if (fs) { unsigned char *tmp = malloc (524288); size_t m = tmp ? fread (tmp, 1, 524288, fs) : 0; fclose (fs);   /* (on the heap: not in a save) */
		          if (m == vt_cart_len) memcpy (cart, tmp, m);
		          free (tmp); }
	}
	{ extern int vt_cart_mirror; vt_cart_mirror = !memcmp (cart, cart + 32768, 32768); }
	{ extern int vt_speech_sep; vt_speech_sep = getenv ("VT_SPEECHOUT") != NULL; }
	{ /* VT_VECVOICE = the SP0256-AL2 ROM file: a speech accessory on controller port 2 - the VecVoice (SP0256),
	   * or with VT_SPEECHDEV=vecvox the VecVox (SpeakJet, spoken with the SP0256) */
	  const char *dev = getenv ("VT_SPEECHDEV"), *z = getenv ("VT_VECVOICE");
	  extern int sp_load (const char *); extern void sp_start (const char *);
	  if (z && *z) { int e = sp_load (z);              /* (the ROM loaded whenever it is there: a module can be plugged
	                                                    in while the game runs - VT_SPEECHDEV says one is in now) */
	                      if (!e && dev && *dev) sp_start (dev);
	                      else if (e) fprintf (stderr, "VecVoice: %s\n", e == -1 ? "no SP0256-AL2 ROM file" : "the file is not the SP0256-AL2 ROM"); } }   /* (loaded, no
	                                                    module plugged in: nothing to say - it said the ROM was wrong) */
	if (getenv ("VT_BANKLOG")) verbose_bank = 1;
	if (getenv ("VT_BEAMTRACE")) { bcur = malloc (sizeof (beam_t) * MAXB); blast = malloc (sizeof (beam_t) * MAXB); vt_beam_hook = beam_take; }
	{ extern long vt_text_shift; extern double vt_text_shift_w; if (getenv ("VT_TEXTSHIFT")) vt_text_shift = atol (getenv ("VT_TEXTSHIFT")); if (getenv ("VT_TEXTSHIFTW")) vt_text_shift_w = atof (getenv ("VT_TEXTSHIFTW")); }
	if (getenv ("VT_COMMON")) common_base = (unsigned) strtol (getenv ("VT_COMMON"), 0, 16);
	e8910_init_headless ();
	vecx_reset ();
	alg_jch0 = alg_jch1 = 128;
	if (!strcmp (argv[3], "--live")) {
		/* stdout carries the binary frame stream; anything printed goes to stderr */
		int fd = dup (1); dup2 (2, 1);
#ifdef _WIN32
		_setmode (fd, _O_BINARY); _setmode (_fileno (stdin), _O_BINARY);
#endif
		stdout_bin = fdopen (fd, "wb");
		return live_main (argc > 4 ? argv[4] : "session.rec", argc > 5 ? argv[5] : NULL);
	}
	if (!strcmp (argv[3], "--replay")) {
		if (argc < 7) { fprintf (stderr, "usage: vtest bios cart --replay REC SYM OUT.csv\n"); return 2; }
		return replay_main (argv[4], argv[5], argv[6]);
	}
	FILE *s = fopen (argv[3], "r"); if (!s) { perror (argv[3]); return 2; }
	char line[512]; int fail = 0;
	while (fgets (line, sizeof line, s)) {
		char cmd[64], a1[256] = "", a2[256] = "";
		char *h = strchr (line, '#'); if (h) *h = 0;
		if (sscanf (line, "%63s %255s %255s", cmd, a1, a2) < 1) continue;
		char path[600];
		if (!strcmp (cmd, "run")) { long k = atol (a1); for (long i = 0; i < k; i++) run_frame (); }
		else if (!strcmp (cmd, "cycles")) { long k = atol (a1); while (k > 0) { emu_chunk (k > 1000 ? 1000 : k); k -= 1000; } }
		else if (!strcmp (cmd, "joy")) { int x = atoi (a1), y = atoi (a2); alg_jch0 = (unsigned) (x + 128); alg_jch1 = (unsigned) (y + 128); }
		else if (!strcmp (cmd, "btn")) { int m = (int) strtol (a1, 0, 0); snd_regs[14] = 0xff & ~m; }
		else if (!strcmp (cmd, "shot")) { snprintf (path, sizeof path, "%s/%s", outdir, a1); render_shot (path); }
		else if (!strcmp (cmd, "analog")) { extern void vt_set_analog (int id, double v); vt_set_analog (atoi (a1), atof (a2)); }   /* analog ID V: a live setting, as --live sends it */
		else if (!strcmp (cmd, "reset")) { extern void vecx_reset_button (void); vecx_reset_button (); }   /* the RESET button (RAM kept), as --live flags bit 2 */
		else if (!strcmp (cmd, "poke")) {       /* poke ADDR VALUE (hex): set a RAM byte */
			e6809_write8 ((unsigned) strtol (a1, 0, 16), (unsigned char) strtol (a2, 0, 16));
		}
		else if (!strcmp (cmd, "dumpbeam")) {   /* the last frame's lit cycles (VT_BEAMTRACE=1): x y lev z t, binary */
			snprintf (path, sizeof path, "%s/%s", outdir, a1);
			FILE *bf = fopen (path, "wb");
			if (bf) { if (nblast) fwrite (blast, sizeof (beam_t), nblast, bf); fclose (bf); }
		}
		else if (!strcmp (cmd, "dumpvec")) {    /* the last frame's vectors as text: x0 y0 x1 y1 c */
			snprintf (path, sizeof path, "%s/%s", outdir, a1);
			FILE *vf = fopen (path, "w");
			if (!vf) { perror (path); continue; }
			{ extern int vt_imager;                 /* (with the 3D Imager: each line's colour as an 8th column) */
			  for (int i = 0; i < nlast; i++) {
				fprintf (vf, "%ld %ld %ld %ld %d %u %u", last[i].x0, last[i].y0, last[i].x1, last[i].y1, last[i].c, last[i].cyc, last[i].t0);
				if (vt_imager) fprintf (vf, " %d", last[i].rgb);
				fprintf (vf, "\n");
			  } }
			fclose (vf);
		}
		else if (!strcmp (cmd, "slope")) {      /* mean tilt of the horizontal vectors (zero-ref drift) */
			double sum = 0, wsum = 0;
			for (int i = 0; i < nlast; i++) {
				double dx = last[i].x1 - last[i].x0, dy = last[i].y1 - last[i].y0;
				if (fabs (dx) > 3 * fabs (dy) && fabs (dx) > 200) { sum += dy; wsum += fabs (dx) * (dx > 0 ? 1 : -1); }
			}
			printf ("slope %.5f (%d vectors)\n", wsum ? sum / wsum : 0.0, nlast);
		}
		else if (!strcmp (cmd, "shothr")) { snprintf (path, sizeof path, "%s/%s", outdir, a1); render_shot_hr (path, a2[0] ? atof (a2) : 3); }
		else if (!strcmp (cmd, "wav")) { snprintf (path, sizeof path, "%s/%s", outdir, a1); wavf = fopen (path, "wb"); wav_bytes = 0; wav_hdr (wavf, 0); }
		else if (!strcmp (cmd, "wavend")) { if (wavf) { wav_hdr (wavf, wav_bytes); fclose (wavf); wavf = 0; } }
		else if (!strcmp (cmd, "peek")) { unsigned a = (unsigned) strtol (a1, 0, 16); int k = a2[0] ? atoi (a2) : 1; printf ("peek %04X:", a); for (int i = 0; i < k; i++) printf (" %02X", e6809_read8 (a + i)); printf ("\n"); }
		else if (!strcmp (cmd, "stats")) stats ();
		else if (!strcmp (cmd, "sigrun")) {      /* sigrun N: run N frames; per frame the online state checksum and the vectors, folded into one hash (exactness tests) */
			extern int vt_uvm2, vt_pitrex; extern uint32_t uvm2_sig (void), pitrex_sig (void); long k = atol (a1); uint64_t h = 1469598103934665603ull;
			for (long i = 0; i < k; i++) { run_frame (); uint32_t s = ram_sum () ^ (uint32_t) total_cycles * 2654435761u; if (vt_uvm2) s ^= uvm2_sig (); else if (vt_pitrex) s ^= pitrex_sig ();
			  h = (h ^ s) * 1099511628211ull; h = (h ^ (uint64_t) nlast) * 1099511628211ull;
			  for (int j = 0; j < nlast; j++) {          /* (the coordinates as 32-bit words: the same on every system, whatever the size of a long) */
				h = (h ^ ((uint64_t) (uint32_t) last[j].x0 << 32 | (uint32_t) last[j].y0)) * 1099511628211ull;
				h = (h ^ ((uint64_t) (uint32_t) last[j].x1 << 32 | (uint32_t) last[j].y1)) * 1099511628211ull;
				h = (h ^ (last[j].c | (uint64_t) last[j].cyc << 8 | (uint64_t) last[j].t0 << 32)) * 1099511628211ull; } }
			printf ("sigrun %ld frames: %016llx cycles=%llu\n", k, (unsigned long long) h, (unsigned long long) total_cycles); fflush (stdout);
		}
		else if (!strcmp (cmd, "hitch")) {       /* hitch N: run N frames, each one's wall time against its own length (tests: stalls) */
			long k = atol (a1); double worst = 0, sum = 0; long late = 0, late2 = 0; double *r = malloc (100000 * sizeof (double)); long nr = 0;   /* (on the heap: not in a save) */
			long fb0 = frame_budget; frame_budget = 30000;  /* (the frames as --live makes them for a UVMC2 / PiTrex: 50 a second) */
			for (long i = 0; i < k; i++) {
				unsigned long long c0 = total_cycles; double t0 = vt_wall ();
				run_frame ();
				double dt = vt_wall () - t0, et = (double) (total_cycles - c0) / 1500000.0;
				double q = et > 0 ? dt / et : 0; sum += dt; if (q > worst) worst = q; if (q > 1.0) late++; if (q > 2.0) late2++; if (nr < 100000) r[nr++] = q;
			}
			frame_budget = fb0;
			for (long i = 1; i < nr; i++) { double x = r[i]; long j = i - 1; while (j >= 0 && r[j] > x) { r[j + 1] = r[j]; j--; } r[j + 1] = x; }
			printf ("hitch %ld frames: wall %.3f s, frame time / its length: median %.3f p99 %.3f max %.3f, over 1x %ld, over 2x %ld\n", k, sum,
			        nr ? r[nr / 2] : 0, nr ? r[(nr * 99) / 100] : 0, worst, late, late2); fflush (stdout); free (r);
		}
		else if (!strcmp (cmd, "memsig")) {      /* memsig: the cartridge computer's whole RAM in one hash (the RP2350's SRAM + PSRAM, the Pi's RAM) */
			extern int vt_uvm2, vt_pitrex; extern uint64_t uvm2_memsig (void), pitrex_memsig (void);
			printf ("memsig %016llx\n", (unsigned long long) (vt_uvm2 ? uvm2_memsig () : vt_pitrex ? pitrex_memsig () : 0)); fflush (stdout);
		}
		else if (!strcmp (cmd, "tsc")) { extern unsigned long long px_tsc_arm; static unsigned long long l0, a0; unsigned long long n = VT_TSC ();
			printf ("tsc: all %.3f G arm %.3f G\n", (n - l0) / 1e9, (px_tsc_arm - a0) / 1e9); l0 = n; a0 = px_tsc_arm; }
		else if (!strcmp (cmd, "pxram")) { extern void pitrex_ramdump (const char *); snprintf (path, sizeof path, "%s/%s", outdir, a1); pitrex_ramdump (path); }
		else if (!strcmp (cmd, "jitmap")) { extern void arm11_jit_map (const char *); snprintf (path, sizeof path, "%s/%s", outdir, a1); arm11_jit_map (path); }
		else if (!strcmp (cmd, "pxprof")) { extern void pitrex_prof_dump (const char *); snprintf (path, sizeof path, "%s/%s", outdir, a1); pitrex_prof_dump (path); }
		else if (!strcmp (cmd, "pen")) {         /* pen X Y TOUCH: the light pen (VT_LIGHTPEN) at X, Y (the lines' units), touching or not */
			extern void vt_set_analog (int id, double v);
			char a3[64] = "1"; sscanf (line, "%*s %*s %*s %63s", a3);
			vt_set_analog (24, atof (a1)); vt_set_analog (25, atof (a2)); vt_set_analog (26, atof (a3));
		}
		else if (!strcmp (cmd, "usbkey")) {      /* usbkey MODS KEY (hex HID usages; 0 0: all keys up): a USB keyboard report */
			extern void vt_usb_report (int id, const unsigned char *b);
			unsigned char r[4] = { (unsigned char) strtol (a1, 0, 16), (unsigned char) strtol (a2, 0, 16), 0, 0 };
			vt_usb_report (0x80, r);
		}
		else if (!strcmp (cmd, "usbmouse")) {    /* usbmouse BUTTONS DX [DY]: a USB mouse report */
			extern void vt_usb_report (int id, const unsigned char *b);
			char a3[64] = "0"; sscanf (line, "%*s %*s %*s %63s", a3);
			unsigned char r[4] = { (unsigned char) atoi (a1), (unsigned char) (signed char) atoi (a2), (unsigned char) (signed char) atoi (a3), 0 };
			vt_usb_report (0x81, r);
		}
		else if (!strcmp (cmd, "bot")) { char a3[64] = ""; sscanf (line, "%*s %*s %*s %63s", a3); bot_run (a1, atol (a2), a3[0] ? atoi (a3) : 2); }
		else if (!strcmp (cmd, "regs")) printf ("PC=%04X X=%04X Y=%04X U=%04X S=%04X bank=%d\n", e6809_get_pc () & 0xFFFF, e6809_get_x (), e6809_get_y (), e6809_get_u (), e6809_get_s (), vt_cur_bank ());
		else if (!strcmp (cmd, "recstart")) {   /* must be the first command: replays start at reset */
			recf = fopen (a1, "wb"); uint32_t sum = cart_sum ();
			fwrite (REC_MAGIC, 1, 6, recf); fwrite (&sum, 4, 1, recf);
		}
		else if (!strcmp (cmd, "recplay")) {    /* recplay REC [N]: play N more frames of a --live recording (0 = the rest; the
		                                           same file again continues where it stopped), reporting frames with a lit
		                                           vector over 16000 units long or lit 300+ cycles (a runaway beam) */
			static FILE *rf; static char rname[600]; static long f; char mag[6]; uint32_t sum; unsigned char m[8]; long nmax = a2[0] ? atol (a2) : 0, n = 0;
			frame_budget = getenv ("VT_FRAMEMAX") ? atol (getenv ("VT_FRAMEMAX")) : 33000;   /* (as in --live: the frames end where they did) */
			if (!rf || strcmp (rname, a1)) {
				if (rf) fclose (rf);
				rf = fopen (a1, "rb"); f = 0; snprintf (rname, sizeof rname, "%s", a1);
				if (rf && (fread (mag, 1, 6, rf) != 6 || fread (&sum, 4, 1, rf) != 1)) { fclose (rf); rf = 0; }
			}
			if (!rf) printf ("recplay: cannot read %s\n", a1);
			else {
				while ((!nmax || n < nmax) && fread (m, 1, 4, rf) == 4) {
					if ((m[3] & 0x40) && fread (m + 4, 1, 3, rf) != 3) break;
					apply_input (m); run_frame (); f++; n++; if (getenv ("VT_RECTRACE")) { extern unsigned long long vt_frame_t0; unsigned long xs = 0; for (int i = 0; i < nlast; i++) xs += (unsigned) last[i].x0; printf ("F %ld %d %u %lu\n", f, nlast, (unsigned) vt_frame_t0, xs); }
					int chain = 0;
					for (int i = 0; i < nlast; i++) {
						double dx = (double) last[i].x1 - last[i].x0, dy = (double) last[i].y1 - last[i].y0, d = sqrt (dx * dx + dy * dy);
						if ((last[i].c & 127) && d >= 10 && d <= 40) chain++;
					}
					if (chain > 150) printf ("frame %ld: dot chain, %d short lit moves\n", f, chain);
					for (int i = 0; i < nlast; i++) {
						double dx = (double) last[i].x1 - last[i].x0, dy = (double) last[i].y1 - last[i].y0, d = sqrt (dx * dx + dy * dy);
						if ((d > 16000 || last[i].cyc > 8 * 300) && (last[i].c & 127)) { printf ("frame %ld: line %d of %d, length %.0f, (%ld,%ld)-(%ld,%ld) bright %d cyc %u t0 %u\n", f, i, nlast, d, last[i].x0, last[i].y0, last[i].x1, last[i].y1, last[i].c, last[i].cyc / 8, last[i].t0); break; }
					}
				}
				printf ("recplay: frame %ld\n", f);
			}
		}
		else if (!strcmp (cmd, "chains")) {     /* chains N: run N frames (input as set), counting frames with a dot chain: over
		                                           150 lit moves of 10-40 units (a beam walking across the screen) */
			long nf = atol (a1), hits = 0, first = -1;
			frame_budget = 33000;
			for (long f = 0; f < nf; f++) {
				run_frame ();
				int k = 0;
				for (int i = 0; i < nlast; i++) {
					double dx = (double) last[i].x1 - last[i].x0, dy = (double) last[i].y1 - last[i].y0, d = sqrt (dx * dx + dy * dy);
					if ((last[i].c & 127) && d >= 10 && d <= 40) k++;
				}
				if (k > 150) { hits++; if (first < 0) first = f; }
			}
			printf ("chains: %ld of %ld frames (first at %ld)\n", hits, nf, first);
		}
		else if (!strcmp (cmd, "profon")) { memset (prof, 0, sizeof prof); prof_on = 1; }
		else if (!strcmp (cmd, "profdump")) { snprintf (path, sizeof path, "%s/%s", outdir, a1); FILE *pf = fopen (path, "wb"); fwrite (prof, 1, sizeof prof, pf); fclose (pf); prof_on = 0; }
		else if (!strcmp (cmd, "resetstats")) { busy_sum = 0; busy_max = 0; frames_over = 0; frames = 1; }
		else if (!strcmp (cmd, "expectbank")) {
			int b = e6809_read8 (0xD002) & 0x40 ? (e6809_read8 (0xD000) >> 6) & 1 : 1;
			b = (e6809_read8 (0xD000) >> 6) & 1; /* port B read returns pin level */
			printf ("bank=%d expected=%s %s\n", b, a1, b == atoi (a1) ? "OK" : "FAIL");
			if (b != atoi (a1)) fail = 1;
		}
		else printf ("unknown command %s\n", cmd);
		if (crashed) fail = 1;
	}
	if (wavf) { wav_hdr (wavf, wav_bytes); fclose (wavf); }
	if (recf) fclose (recf);
	stats ();
	return fail;
}
