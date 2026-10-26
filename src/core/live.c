/* live.c - interactive play and exact replay for vtest (included by vtest.c).
 *
 *   vtest BIOS CART --live REC.bin
 *       Binary pipe protocol, one game frame per message (frames end at Wait_Recal):
 *       in  (stdin):  4 bytes  jx+128, jy+128, buttons (bit0..3 = buttons 1..4), flags
 *                     flags bit0 = infinite lives (Options |= $80, GameFlags |= GF_CHEAT)
 *                     flags bit1 = display: no mains hum wobble (VT_WOBBLE)
 *                     flags bit2 = the RESET button (RAM kept), bit3 = power on (cold start),
 *                     bit4 (with bit3) = skip the BIOS intro: run it unseen until the cartridge starts
 *                     flags bit6 = 3 more bytes follow: the 2nd-player port (jx2+128, jy2+128, buttons
 *                     bit0..3 = player 2's buttons 1..4); without it player 2 is centred, no buttons
 *                     flags bit7 = quit
 *       out (stdout): "VF" u16 nlines u16 nsamples u32 frame start cycle (low 32 bits), then nlines x (u16 x0 y0 x1 y1, u8 bright, u16 beam energy on the line in 1/8 cycles, u16 start cycle in the frame),
 *                     then nsamples signed 16-bit PCM samples at 44100 Hz (little endian)
 *       Every input message is appended to REC.bin (header + 4 bytes per frame), so the session
 *       can be replayed exactly: the emulator is deterministic and only these inputs reach it.
 *
 *   vtest BIOS CART --replay REC.bin SYMFILE OUT.csv
 *       Replays the recording without a display and writes one telemetry row per frame.
 */
#include <stdint.h>

extern void (*e6809_write8) (unsigned address, unsigned char data);

#define REC_MAGIC "RAREC1"
#define OPT_INF 0x80
#define GF_CHEAT_BIT 0x02

static uint32_t ram_sum (void)
{
	uint32_t s = 2166136261u;
	for (unsigned a = 0xC800; a < 0xCC00; a++) s = (s ^ e6809_read8 (a)) * 16777619u;
	return s;
}

static uint32_t cart_sum (void)
{
	uint32_t s = 2166136261u;
	for (int i = 0; i < 65536; i++) s = (s ^ cart[i]) * 16777619u;
	return s;
}

/* ---------- symbols ---------- */
typedef struct { const char *name; unsigned addr; } sym_t;
static sym_t syms[] = {
	{ "GameState" }, { "Wave" }, { "Lives" }, { "PlrIX" }, { "PlrIY" }, { "PlrAlive" }, { "Score" },
	{ "RequiredLeft" }, { "WaveTime" }, { "WaveTimeMax" }, { "EnemyShots" }, { "EntCount" },
	{ "SavedCount" }, { "Boss" }, { "Options" }, { "GameFlags" }, { "Bullets" }, { "BulletsEnd" },
	{ "Ents" }, { "EntEnd" }, { "Humans" }, { "HumansEnd" }, { 0 }
};
enum { S_STATE, S_WAVE, S_LIVES, S_PX, S_PY, S_ALIVE, S_SCORE, S_REQ, S_WTIME, S_WMAX, S_ESHOTS,
       S_ENTS, S_SAVED, S_BOSS, S_OPTIONS, S_GFLAGS, S_BUL, S_BULEND, S_ENTS0, S_ENTEND, S_HUM, S_HUMEND };
static int score_mul = 1;
static int load_syms (const char *fn)
{
	FILE *f = fopen (fn, "r");
	if (!f) { perror (fn); return 0; }
	char l[256], name[128]; unsigned v;
	score_mul = 1;
	while (fgets (l, sizeof l, f)) {
		if (sscanf (l, "%127s EQU $%x", name, &v) != 2) continue;
		if (!strcmp (name, "TypeScoreTabs")) score_mul = 10;   /* ROMs that keep the score in tens */
		for (sym_t *s = syms; s->name; s++) if (!strcmp (s->name, name)) s->addr = v;
	}
	fclose (f);
	for (sym_t *s = syms; s->name; s++) if (!s->addr) { fprintf (stderr, "missing symbol %s\n", s->name); return 0; }
	return 1;
}
#define SA(i) (syms[i].addr)
static int rd8 (unsigned a) { return e6809_read8 (a & 0xFFFF); }
static int rds8 (unsigned a) { int v = rd8 (a); return v > 127 ? v - 256 : v; }

/* ---------- skipping the BIOS intro ---------- */
/* flags bit 4 with a power on: the BIOS's start screen (the VECTREX logo and its tune) runs unseen
 * and unheard, as fast as the host can, until the CPU first runs code below $F000 - the cartridge, or
 * Mine Storm in the BIOS ROM - so the game starts as after a normal power on (same RAM, same timers).
 * (The intro and the BIOS title screen last ~13 s.) At most 60 million cycles (40 s). */
static void skip_boot (void)
{
	extern int vt_uvm2, vt_pitrex; if (vt_uvm2 || vt_pitrex) return;   /* (a UVMC2 game, a PiTrex: the 6809 never runs, no intro to skip) */
	extern unsigned long long alg_cycles;
	unsigned long long c0 = alg_cycles;
	static int16_t drop[4096];
	extern int psg_take (int16_t *buf, int max);
	for (long n = 0; (e6809_get_pc () & 0xFFFF) >= 0xF000 && alg_cycles - c0 < 60000000ULL; n++) {
		vecx_emu (1);
		if ((n & 1023) == 0) while (psg_take (drop, 4096) > 0) ;   /* (its sound is not heard) */
	}
	while (psg_take (drop, 4096) > 0) ;
	ncur = 0; nlast = 0;
}

/* ---------- applying one frame of input ---------- */
static unsigned opt_addr, gflags_addr;
static void apply_input (const unsigned char *m)
{
	alg_jch0 = m[0]; alg_jch1 = m[1];
	if (m[3] & 0x40) { alg_jch2 = m[4]; alg_jch3 = m[5]; }           /* the 2nd-player port */
	else { alg_jch2 = alg_jch3 = 128; }
	snd_regs[14] = 0xff & ~((m[2] & 15) | ((m[3] & 0x40) ? (m[6] & 15) << 4 : 0));
	{ extern int vt_wobble_off; vt_wobble_off = (m[3] & 2) != 0; }   /* flags bit 1: no hum wobble */
	if (m[3] & 8) { vecx_reset (); if (m[3] & 0x10) skip_boot (); }   /* flags bit 3: power on (bit 4: no intro) */
	else if (m[3] & 4) { extern void vecx_reset_button (void); vecx_reset_button (); }   /* bit 2: RESET */
	if ((m[3] & 1) && opt_addr) {
		e6809_write8 (opt_addr, e6809_read8 (opt_addr) | OPT_INF);
		e6809_write8 (gflags_addr, e6809_read8 (gflags_addr) | GF_CHEAT_BIT);
	}
}

/* ---------- live ---------- */
static FILE *stdout_bin;
static unsigned char *abuf; static int abuf_n, abuf_cap;
static void live_audio (const unsigned char *b, int n)
{
	if (!abuf) return;
	if (abuf_n + n > abuf_cap) n = abuf_cap - abuf_n;
	memcpy (abuf + abuf_n, b, n); abuf_n += n;
}
static void put16 (unsigned char *p, unsigned v) { p[0] = v; p[1] = v >> 8; }

/* the 4-bank cartridge's flash: saved to <rom>.sav a second after the game last wrote to it, and at the end */
const char *vt_flash_file; size_t vt_cart_len;
static void flash_save (int now)
{
	extern int vt_flash_dirty;
	static int quiet;
	if (!vt_flash_dirty || !vt_flash_file) return;
	if (!now && ++quiet < 50) return;
	FILE *f = fopen (vt_flash_file, "wb");
	if (f) { fwrite (cart, 1, vt_cart_len, f); fclose (f); }
	vt_flash_dirty = 0; quiet = 0;
}

static int live_main (const char *recfn, const char *symfn)
{
	FILE *rec = fopen (recfn, "wb");
	if (!rec) { perror (recfn); return 2; }
	uint32_t sum = cart_sum ();
	fwrite (REC_MAGIC, 1, 6, rec); fwrite (&sum, 4, 1, rec);
	if (symfn && load_syms (symfn)) { opt_addr = SA (S_OPTIONS); gflags_addr = SA (S_GFLAGS); }
	abuf_cap = 32768; abuf = malloc (abuf_cap);
	{ extern int vt_uvm2, vt_pitrex; frame_budget = getenv ("VT_FRAMEMAX") ? atol (getenv ("VT_FRAMEMAX")) : (vt_uvm2 || vt_pitrex) ? 30000 : 33000; }   /* (a UVMC2 game: its 50 Hz bus frame) */
	static unsigned char out[10 + 30000 * 13 + 32768];   /* (30000: the beam-trace mode's pieces) */
	unsigned char m[8];
	long nframes = 0;
	unsigned long long piece0 = 0;
	while (fread (m, 1, 4, stdin) == 4) {
		if (m[3] & 0x80) break;
		if ((m[3] & 0x40) && fread (m + 4, 1, 3, stdin) != 3) break;   /* (player 2) */
		if (m[3] & 0x20) {                      /* (bit 5: settings follow - a count, then each an id and 4 bytes: a float for
							   vt_set_analog, or ids 0x80 / 0x81 a USB keyboard / mouse report for a UVMC2 (usbhost.c)) */
			unsigned char cnt, g[5]; float f; extern void vt_set_analog (int id, double v); extern void vt_usb_report (int id, const unsigned char *b);
			if (fread (&cnt, 1, 1, stdin) != 1) break;
			for (int k = 0; k < cnt; k++) {
				if (fread (g, 1, 5, stdin) != 5) { cnt = 0; break; }
				if (g[0] >= 0x80) vt_usb_report (g[0], g + 1);
				else { memcpy (&f, g + 1, 4); vt_set_analog (g[0], f); }
			}
			m[3] &= ~0x20;                      /* (recordings keep their format) */
		}
		fwrite (m, 1, (m[3] & 0x40) ? 7 : 4, rec);
		nframes++;
		apply_input (m);
		abuf_n = 0;
		{ extern unsigned long long vt_piece_t0; piece0 = vt_piece_t0; }   /* (this frame's lines count from its start) */
		run_frame ();
		int n = nlast > 30000 ? 30000 : nlast;
		unsigned char *p = out;
		{ extern int vt_speech_sep, vt_vecvoice; extern int vt_uvm2; int two = (vt_speech_sep && vt_vecvoice) || vt_uvm2;   /* (bit 15: two channels -
		  the console and the speech module, interleaved) */
		  *p++ = 'V'; *p++ = 'F'; put16 (p, n); p += 2; put16 (p, two ? (abuf_n / 4) | 0x8000 : abuf_n / 2); p += 2; }   /* 16-bit samples */
		{ unsigned t = (unsigned) piece0;               /* the frame's start (absolute cycles): its lines' times count from it */
		  put16 (p, t & 0xffff); put16 (p + 2, t >> 16); p += 4; }
		for (int i = 0; i < n; i++) {
			line_t *l = &last[i];
			put16 (p, l->x0 < 0 ? 0 : l->x0); put16 (p + 2, l->y0 < 0 ? 0 : l->y0);
			put16 (p + 4, l->x1 < 0 ? 0 : l->x1); put16 (p + 6, l->y1 < 0 ? 0 : l->y1);
			p[8] = l->c; put16 (p + 9, l->cyc > 65535 ? 65535 : l->cyc); put16 (p + 11, l->t0); p += 13;
		}
		memcpy (p, abuf, abuf_n); p += abuf_n;
		fwrite (out, 1, p - out, stdout_bin);
		fflush (stdout_bin);
		flash_save (0);
	}
	flash_save (1);
	fclose (rec);
	fprintf (stderr, "live: %ld frames, %08x RAM checksum\n", nframes, ram_sum ());
	return 0;
}

/* ---------- replay + telemetry ---------- */
static long bcd3 (unsigned a)
{
	long v = 0;
	for (int i = 0; i < 3; i++) { int b = rd8 (a + i); v = v * 100 + (b >> 4) * 10 + (b & 15); }
	return v;
}
static int replay_main (const char *recfn, const char *symfn, const char *csvfn)
{
	if (!load_syms (symfn)) return 2;
	opt_addr = SA (S_OPTIONS); gflags_addr = SA (S_GFLAGS);
	FILE *rec = fopen (recfn, "rb");
	if (!rec) { perror (recfn); return 2; }
	char mag[6]; uint32_t sum;
	if (fread (mag, 1, 6, rec) != 6 || memcmp (mag, REC_MAGIC, 6) || fread (&sum, 4, 1, rec) != 1) {
		fprintf (stderr, "not a recording\n"); return 2;
	}
	if (sum != cart_sum ()) fprintf (stderr, "WARNING: the cartridge image differs from the recorded one\n");
	FILE *csv = fopen (csvfn, "w");
	if (!csv) { perror (csvfn); return 2; }
	fprintf (csv, "frame,state,wave,lives,px,py,alive,score,req,wtime,wmax,eshots,ents,saved,"
	              "boss,bossx,bossy,bosshp,bosscore,options,jx,jy,btn,newshots,bullets,"
	              "near_d,near_t,shot_d,shot_t,humans,hum_d,busy,cyc,tgt_dx,tgt_dy,tgt_t\n");
	/* robot lives: one row per robot when it leaves the pool (killed, or the wave ended) */
	char entfn[600]; snprintf (entfn, sizeof entfn, "%s.ents.csv", csvfn);
	FILE *ecsv = fopen (entfn, "w");
	fprintf (ecsv, "frame,wave,type,born,active,life,wall,dist,state,req\n");
	int et[64] = { 0 }; long eborn[64], eact[64]; int ewall[64];
	unsigned char m[8];
	int prevbul[16]; memset (prevbul, 0xff, sizeof prevbul);
	int nb = (SA (S_BULEND) - SA (S_BUL)) / 4; if (nb > 16) nb = 16;
	long f = 0;
	unsigned long long busy_prev = busy_sum, cyc_prev = total_cycles;
	while (fread (m, 1, 4, rec) == 4) {
		if ((m[3] & 0x40) && fread (m + 4, 1, 3, rec) != 3) break;     /* (player 2) */
		apply_input (m);
		run_frame ();
		f++;
		/* player bullets: count slots that became used this frame */
		int newshots = 0, live = 0;
		for (int i = 0; i < nb; i++) {
			int d = rd8 (SA (S_BUL) + i * 4);
			if (d != 0xff) { live++; if (prevbul[i] == 0xff) newshots++; }
			prevbul[i] = d;
		}
		int px = rds8 (SA (S_PX)), py = rds8 (SA (S_PY));
		/* nearest robot / shot (types >= 14 are projectiles; 9 = electrode, 2 = hulk) */
		unsigned end = (rd8 (SA (S_ENTEND)) << 8) | rd8 (SA (S_ENTEND) + 1);
		double nd = 999, sd = 999, td = 999; int nt = 0, st = 0, tt = 0, tdx = 0, tdy = 0;
		for (unsigned e = SA (S_ENTS0); e < end && e < SA (S_ENTS0) + 8 * 64; e += 8) {
			int t = rd8 (e); if (!t || (rd8 (e + 1) & 0x80)) continue;
			double d = hypot (rds8 (e + 2) - px, rds8 (e + 4) - py);
			if (t >= 14) { if (d < sd) { sd = d; st = t; } }
			else {
				if (d < nd) { nd = d; nt = t; }
				if (t != 2 && t != 9 && d < td) { td = d; tt = t; tdx = rds8 (e + 2) - px; tdy = rds8 (e + 4) - py; }
			}
		}
		{
			int gst = rd8 (SA (S_STATE)), wave = rd8 (SA (S_WAVE)), req = rd8 (SA (S_REQ));
			for (int k = 0; k < 64; k++) {
				unsigned e = SA (S_ENTS0) + k * 8;
				int t = e < end ? rd8 (e) : 0;
				if (t >= 14) t = 0;                     /* projectiles are not tracked */
				if (et[k] && t != et[k]) {
					int ex = rds8 (e + 2), ey = rds8 (e + 4);
					fprintf (ecsv, "%ld,%d,%d,%ld,%ld,%ld,%d,%.1f,%d,%d\n", f, wave, et[k], eborn[k], eact[k],
					         eact[k] >= 0 ? f - eact[k] : -1, ewall[k], hypot (ex - px, ey - py), gst, req);
				}
				if (t && t != et[k]) { eborn[k] = f; eact[k] = -1; ewall[k] = 0; }
				if (t) {
					int fl = rd8 (e + 1);
					if (eact[k] < 0 && !(fl & 0x80)) eact[k] = f;
					if (eact[k] >= 0 && f - eact[k] < 100) {   /* first 2 s: frames hugging a wall */
						int ex = rds8 (e + 2), ey = rds8 (e + 4);
						if (ex < -88 || ex > 88 || ey < -114 || ey > 100) ewall[k]++;
					}
				}
				et[k] = t;
			}
		}
		int boss = rd8 (SA (S_BOSS));
		int bx = rds8 (SA (S_BOSS) + 2), by = rds8 (SA (S_BOSS) + 3);
		if (boss) { double d = hypot (bx - px, by - py); if (d < nd) { nd = d; nt = 0; } if (d < td) { td = d; tt = 0; tdx = bx - px; tdy = by - py; } }
		int hum = 0; double hd = 999;
		for (unsigned h = SA (S_HUM); h < SA (S_HUMEND); h += 6) {
			if (!rd8 (h)) continue;
			hum++;
			double d = hypot (rds8 (h + 1) - px, rds8 (h + 2) - py); if (d < hd) hd = d;
		}
		unsigned long long busy = busy_sum - busy_prev, cyc = total_cycles - cyc_prev; busy_prev = busy_sum; cyc_prev = total_cycles;
		fprintf (csv, "%ld,%d,%d,%d,%d,%d,%d,%ld,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,"
		              "%.1f,%d,%.1f,%d,%d,%.1f,%llu,%llu,%d,%d,%d\n",
		         f, rd8 (SA (S_STATE)), rd8 (SA (S_WAVE)), rd8 (SA (S_LIVES)), px, py, rd8 (SA (S_ALIVE)),
		         score_mul * bcd3 (SA (S_SCORE)), rd8 (SA (S_REQ)), rd8 (SA (S_WTIME)), rd8 (SA (S_WMAX)),
		         rd8 (SA (S_ESHOTS)), rd8 (SA (S_ENTS)), rd8 (SA (S_SAVED)),
		         boss, bx, by, rd8 (SA (S_BOSS) + 5), rd8 (SA (S_BOSS) + 14), rd8 (SA (S_OPTIONS)),
		         (int) m[0] - 128, (int) m[1] - 128, m[2] & 15, newshots, live,
		         nd, nt, sd, st, hum, hd, busy, cyc, td < 999 ? tdx : 0, td < 999 ? tdy : 0, td < 999 ? tt : -1);
	}
	fclose (csv); fclose (rec); fclose (ecsv);
	fprintf (stderr, "replayed %ld frames, %08x RAM checksum\n", f, ram_sum ());
	return 0;
}
