#include <ctype.h>
/* vtest - headless scripted Vectrex test harness (vecx core, GPL).
 *
 * Models the Robot Arena cartridge: 64 KiB image, PB6 -> A15 with a pull-up
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
static void beam_swap (void)
{
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
static unsigned long frames;          /* Wait_Recal entries */
static unsigned long long idle_cycles, idle_frame;
static unsigned long long busy_max, busy_sum;
static unsigned long frames_over;     /* frames whose busy time > 30000 */
static int frame_done, in_wait;
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
		extern int vt_uvm2; extern int uvm2_jack44 (int16_t *, int);
		extern int vt_imager;
		if ((vt_speech_sep && vt_vecvoice) || vt_uvm2 || vt_imager) {     /* (two channels: the console, the speech module /
		                                                                     the UVMC2's jack / the 3D Imager's motor) */
			static int16_t sb[4096], st[8192];
			if (vt_uvm2) uvm2_jack44 (sb, ns); else psg_take_speech (sb, ns);
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
static void run_frame (void)
{
	if (recf) {
		unsigned char m[4] = { alg_jch0, alg_jch1, (~snd_regs[14]) & 15, 0 };
		fwrite (m, 1, 4, recf);
	}
	frame_done = 0;
	/* a game that calls Wait_Recal ends its frames itself (the BIOS's timer 2: at most 65535 cycles): the whole
	 * frame, so its picture is never cut in two - a slow game (Armor Attack's helicopter, a 30 fps game) was cut
	 * at frame_budget in the middle of its drawing, and the display could not tell its pictures apart (flicker).
	 * A game without it (Mine Storm's warp) or a UVMC2 / PiTrex bus frame: frame_budget, as before */
	long budget = frame_budget;
	{ extern int vt_uvm2, vt_pitrex;
	  if (!vt_uvm2 && !vt_pitrex && last_recal && total_cycles - last_recal < 100000) budget = 70000; }
	while (!frame_done && budget > 0) { emu_chunk (500); budget -= 500; }
	if (!frame_done) {                  /* no Wait_Recal: show what was drawn and start a new frame */
		extern unsigned long long alg_cycles, vt_frame_t0;
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
	printf (" ram=%08x\n", ram_sum ());
	{ extern void arm11_jit_stats (void); extern int vt_pitrex; if (vt_pitrex) { extern void pitrex_digest (void); arm11_jit_stats (); pitrex_digest (); } }
}

/* the game's title in the cartridge header (the first 64 bytes): does it hold this word? */
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
		FILE *fs = fopen (sav, "rb");
		if (fs) { static unsigned char tmp[524288]; size_t m = fread (tmp, 1, sizeof tmp, fs); fclose (fs);
		          if (m == vt_cart_len) memcpy (cart, tmp, m); }
	}
	{ extern int vt_cart_mirror; vt_cart_mirror = !memcmp (cart, cart + 32768, 32768); }
	{ extern int vt_speech_sep; vt_speech_sep = getenv ("VT_SPEECHOUT") != NULL; }
	{ /* VT_VECVOICE = the SP0256-AL2 ROM file: a speech accessory on controller port 2 - the VecVoice (SP0256),
	   * or with VT_SPEECHDEV=vecvox the VecVox (SpeakJet, spoken with the SP0256) */
	  const char *dev = getenv ("VT_SPEECHDEV"), *z = getenv ("VT_VECVOICE");
	  extern int sp_load (const char *); extern void sp_start (const char *);
	  if (z && *z) { int e = sp_load (z);
	                      if (!e) sp_start (dev);
	                      else fprintf (stderr, "VecVoice: %s\n", e == -1 ? "no SP0256-AL2 ROM file" : "the file is not the SP0256-AL2 ROM"); } }
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
