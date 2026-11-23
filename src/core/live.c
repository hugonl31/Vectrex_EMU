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
 *                     settings id 0x7E (flags bit5): run-ahead, 1 = one game frame (0 = off): see ra_step
 *       out (stdout): "VF" u16 nlines u16 nsamples u32 frame start cycle (low 32 bits), u32 the state's checksum (the
 *                     console's RAM and cycle count, a UVMC2's / PiTrex's CPU state: online play compares it), then nlines x (u16 x0 y0 x1 y1 (+ 8000: LIVE_OFS), u8 bright, u16 beam energy on the line in 1/8 cycles, u16 start cycle in the frame),
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
	{ extern int vt_uvm2_intro; extern void uvm2_intro_end (void); if (vt_uvm2_intro) { uvm2_intro_end (); return; } }   /* (a UVMC2: straight into the game) */
	extern int vt_uvm2, vt_pitrex; if (vt_uvm2 || vt_pitrex) return;   /* (a PiTrex: the 6809 never runs, no intro to skip) */
	extern unsigned long long alg_cycles;
	unsigned long long c0 = alg_cycles;
	static int16_t drop[4096];
	extern int psg_take (int16_t *buf, int max);
	for (long n = 0; (e6809_get_pc () & 0xFFFF) >= 0xF000 && alg_cycles - c0 < 60000000ULL; n++) {
		vecx_emu (1);
		if ((n & 1023) == 0) while (psg_take (drop, 4096) > 0) ;   /* (its sound is not heard) */
	}
	while (psg_take (drop, 4096) > 0) ;
	{ extern int psg_take_speech (int16_t *buf, int max); extern int vt_imager;   /* (the second channel: nothing queued) */
	  if (vt_imager) for (int k = 0; k < 64; k++) psg_take_speech (drop, 4096); }
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
#define LIVE_OFS 8000                       /* (the lines' coordinates are sent + this: they may be below 0) */
#define LIVE_C(v) ((v) + LIVE_OFS < 0 ? 0 : (v) + LIVE_OFS > 65535 ? 65535 : (v) + LIVE_OFS)
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

/* ---------- run-ahead ----------
 * With it on, each message runs the game frame for real (its sound sent, its state kept), saves the whole state, runs
 * one more frame unseen with the same input and sends that frame's picture (its sound dropped), then loads the state
 * back. The picture shown is one game frame ahead of the game's state: the game's own frame of input delay is hidden,
 * so a press shows ~20 ms sooner - closer to a real Vectrex, which draws while the game runs (an emulator shows each
 * frame after it is done). The real frames, their sound, the recording and the state's checksum are the same as
 * without it. Only 6809 games built with VX_STATES (build_core.bat): the core says "STAT: runahead ..." on stderr. */
static int ra_want;                         /* (settings id 0x7E) */
#ifdef VX_STATES
size_t vxs_init (void); void vxs_exclude (const void *p, size_t n); void vxs_save (unsigned char *o); void vxs_load (const unsigned char *s);
static unsigned char *ra_buf; static size_t ra_raw; static int ra_state = -1;   /* (-1: not set up, 0: none, 1: ready) */
static int ra_ready (void)
{
	if (ra_state >= 0) return ra_state;
	extern int vt_uvm2_cart, vt_pitrex;
	ra_state = 0;
	if (vt_uvm2_cart || vt_pitrex) { fprintf (stderr, "STAT: runahead unsupported (UVMC2 / PiTrex)\n"); return 0; }
	{ extern int vt_veclink; if (vt_veclink) { fprintf (stderr, "STAT: runahead unsupported (VecLink)\n"); return 0; } }   /* (the
	                                                   other console can not run a frame again with this one) */
	vxs_exclude (cur, sizeof cur); vxs_exclude (last, sizeof last); vxs_exclude (prof, sizeof prof);
	vxs_exclude (acc, sizeof acc);                  /* (the lines: only their used part is saved; the profile, the shots'
	                                                   picture: tests) */
	ra_raw = vxs_init ();
	ra_buf = malloc (ra_raw + 2 * sizeof cur + (bcur ? sizeof (beam_t) * MAXB : 0) + vt_cart_len);
	if (!ra_buf) { fprintf (stderr, "STAT: runahead unsupported (memory)\n"); return 0; }
	ra_state = 1;
	fprintf (stderr, "STAT: runahead ready, %zu bytes a save\n", ra_raw);
	return 1;
}
static void ra_save (void)
{
	unsigned char *o = ra_buf;
	vxs_save (o); o += ra_raw;
	memcpy (o, cur, sizeof (line_t) * ncur); o += sizeof (line_t) * ncur;    /* (ncur, nlast, nbcur: in the sections) */
	memcpy (o, last, sizeof (line_t) * nlast); o += sizeof (line_t) * nlast;
	if (bcur) { memcpy (o, bcur, sizeof (beam_t) * nbcur); o += sizeof (beam_t) * nbcur; }
	if (vt_cart_len) memcpy (o, cart, vt_cart_len);                          /* (a flash cartridge: the game writes to it) */
}
static void ra_load (void)
{
	const unsigned char *s = ra_buf;
	vxs_load (s); s += ra_raw;
	memcpy (cur, s, sizeof (line_t) * ncur); s += sizeof (line_t) * ncur;
	memcpy (last, s, sizeof (line_t) * nlast); s += sizeof (line_t) * nlast;
	if (bcur) { memcpy (bcur, s, sizeof (beam_t) * nbcur); s += sizeof (beam_t) * nbcur; }
	if (vt_cart_len) memcpy (cart, s, vt_cart_len);
}
/* (online rollback, netplay.py: a ring of saved frames - settings 0x7C save the state into slot k before this frame,
 *  0x7D load slot k before it; 0x7B a quiet frame: run, its lines and sound not sent - a frame run again) */
#define RB_SLOTS 24
static unsigned char *rb_buf[RB_SLOTS];
static int rb_save (int k)
{
	if (k < 0 || k >= RB_SLOTS || ra_ready () != 1) return 0;
	if (!rb_buf[k]) rb_buf[k] = malloc (ra_raw + 2 * sizeof cur + (bcur ? sizeof (beam_t) * MAXB : 0) + vt_cart_len); if (!rb_buf[k]) return 0;
	unsigned char *keep = ra_buf; ra_buf = rb_buf[k]; ra_save (); ra_buf = keep; return 1;
}
static int rb_load (int k)
{
	if (k < 0 || k >= RB_SLOTS || !rb_buf[k] || ra_ready () != 1) return 0;
	unsigned char *keep = ra_buf; ra_buf = rb_buf[k]; ra_load (); ra_buf = keep; return 1;
}
#else
static int ra_ready (void) { static int told; if (!told) { told = 1; fprintf (stderr, "STAT: runahead unsupported (this build)\n"); } return 0; }
static void ra_save (void) { }
static void ra_load (void) { }
static int rb_save (int k) { (void) k; return 0; }
static int rb_load (int k) { (void) k; return 0; }
#endif

/* ---------- line streaming (online, netplay.py - the apps' netvx.c format): a card game played on one side only. The
 * host's core adds each frame's lines and sound, small (settings 0x7A on): after the frame's record "VS", u32 length, the
 * frame (u32 number, u64 start, u64 end cycles, u16 lines, u16 samples; each line a flags byte (bit 0 it starts where the
 * last ended, 1 its brightness as the last's, 2 its colour as the last's, 3 its start where the last ended) and the rest
 * as zig-zag differences in 7-bit bytes; the sound 8-bit mu-law, 22 kHz). Player 2's core runs no game: a message
 * 0x90 brings such a frame (u32 length, the frame) and it sends it back as an ordinary frame record */
static int st_out; static uint32_t st_fno;
static unsigned char *st_vput (unsigned char *o, unsigned long long v) { while (v >= 0x80) { *o++ = (unsigned char) (v | 0x80); v >>= 7; } *o++ = (unsigned char) v; return o; }
static unsigned char *st_zput (unsigned char *o, long long v) { return st_vput (o, v < 0 ? ((unsigned long long) (-(v + 1)) << 1) | 1 : (unsigned long long) v << 1); }
static const unsigned char *st_vget (const unsigned char *i, const unsigned char *e, unsigned long long *v)
{ unsigned long long r = 0; int sh = 0; while (i < e) { const unsigned char c = *i++; r |= (unsigned long long) (c & 0x7f) << sh; if (!(c & 0x80)) { *v = r; return i; } sh += 7; if (sh > 63) break; } *v = 0; return NULL; }
static const unsigned char *st_zget (const unsigned char *i, const unsigned char *e, long long *v)
{ unsigned long long u; i = st_vget (i, e, &u); *v = (u & 1) ? -(long long) (u >> 1) - 1 : (long long) (u >> 1); return i; }
static unsigned char st_mu_enc (int x)
{
	const int sign = x < 0 ? 0x80 : 0; if (x < 0) x = -x; if (x > 32635) x = 32635; x += 132;
	int e = 7; for (int m = 0x4000; !(x & m) && e > 0; m >>= 1) e--;
	return (unsigned char) ~(sign | (e << 4) | ((x >> (e + 3)) & 15));
}
static int st_mu_dec (unsigned char u) { u = (unsigned char) ~u; const int e = (u >> 4) & 7, m = u & 15, x = (((m << 3) + 132) << e) - 132; return u & 0x80 ? -x : x; }
static void st_put32 (unsigned char *p, uint32_t v) { p[0] = (unsigned char) v; p[1] = (unsigned char) (v >> 8); p[2] = (unsigned char) (v >> 16); p[3] = (unsigned char) (v >> 24); }
static size_t st_pack (unsigned char *o0, unsigned long long p0, int nl, int two)
{
	unsigned char *o = o0; const unsigned long long tc = total_cycles; const int16_t *a = (const int16_t *) abuf;
	const int stride = two ? 4 : 2, na = abuf_n / 2 / stride;   /* (every second sample of the first channel: 22 kHz) */
	st_put32 (o, st_fno++); memcpy (o + 4, &p0, 8); memcpy (o + 12, &tc, 8); put16 (o + 20, (unsigned) nl); put16 (o + 22, (unsigned) na); o += 24;
	long px = 0, py = 0; unsigned char pc = 0, prgb = 0; unsigned long long pend = 0;
	for (int i = 0; i < nl; i++) { const line_t *l = &last[i];
		const int cont = l->x0 == px && l->y0 == py, samec = l->c == pc, samergb = l->rgb == prgb, sameT = (unsigned long long) l->t0 == pend;
		*o++ = (unsigned char) (cont | samec << 1 | samergb << 2 | sameT << 3);
		if (!cont) { o = st_zput (o, (long long) l->x0 - px); o = st_zput (o, (long long) l->y0 - py); }
		o = st_zput (o, (long long) l->x1 - l->x0); o = st_zput (o, (long long) l->y1 - l->y0);
		if (!samec) *o++ = l->c; if (!samergb) *o++ = l->rgb;
		o = st_vput (o, l->cyc); if (!sameT) o = st_zput (o, (long long) l->t0 - (long long) pend);
		px = l->x1; py = l->y1; pc = l->c; prgb = l->rgb; pend = (unsigned long long) l->t0 + l->cyc; }
	for (int k = 0; k < na; k++) *o++ = st_mu_enc (a[k * stride]);
	return (size_t) (o - o0);
}
static int st_unpack (const unsigned char *r, size_t rn, unsigned long long *p0)   /* (into last / nlast, abuf: 44 kHz mono) */
{
	if (rn < 24) return 0; const unsigned char *e = r + rn;
	memcpy (p0, r + 4, 8); const int n = r[20] | r[21] << 8, na = r[22] | r[23] << 8;
	if (n > MAXL || na > 4000) return 0;
	const unsigned char *i = r + 24; long px = 0, py = 0; unsigned char pc = 0, prgb = 0; unsigned long long pend = 0;
	for (int k = 0; k < n; k++) { line_t *l = &last[k]; long long v; unsigned long long u;
		if (!i || i >= e) return 0; const unsigned char f = *i++;
		if (f & 1) { l->x0 = px; l->y0 = py; } else { i = st_zget (i, e, &v); if (!i) return 0; l->x0 = px + (long) v; i = st_zget (i, e, &v); if (!i) return 0; l->y0 = py + (long) v; }
		i = st_zget (i, e, &v); if (!i) return 0; l->x1 = l->x0 + (long) v; i = st_zget (i, e, &v); if (!i) return 0; l->y1 = l->y0 + (long) v;
		if (f & 2) l->c = pc; else { if (i >= e) return 0; l->c = *i++; }
		if (f & 4) l->rgb = prgb; else { if (i >= e) return 0; l->rgb = *i++; }
		i = st_vget (i, e, &u); if (!i) return 0; l->cyc = (unsigned) u;
		if (f & 8) l->t0 = (unsigned) pend; else { i = st_zget (i, e, &v); if (!i) return 0; l->t0 = (unsigned) ((long long) pend + v); }
		px = l->x1; py = l->y1; pc = l->c; prgb = l->rgb; pend = (unsigned long long) l->t0 + l->cyc; }
	if (!i || i + na > e) return 0;
	nlast = n;
	if (abuf_cap < na * 4) return 0;
	int16_t *a = (int16_t *) abuf; for (int k = 0; k < na; k++) a[2 * k] = a[2 * k + 1] = (int16_t) st_mu_dec (i[k]);
	abuf_n = na * 4;
	return 1;
}
static int live_main (const char *recfn, const char *symfn)
{
	FILE *rec = fopen (recfn, "wb");
	if (!rec) { perror (recfn); return 2; }
	uint32_t sum = cart_sum ();
	fwrite (REC_MAGIC, 1, 6, rec); fwrite (&sum, 4, 1, rec);
	if (symfn && load_syms (symfn)) { opt_addr = SA (S_OPTIONS); gflags_addr = SA (S_GFLAGS); }
	abuf_cap = 32768; abuf = malloc (abuf_cap);
	{ extern int vt_uvm2_cart, vt_pitrex; frame_budget = getenv ("VT_FRAMEMAX") ? atol (getenv ("VT_FRAMEMAX")) : (vt_uvm2_cart || vt_pitrex) ? 30000 : 33000; }   /* (a UVMC2 game: its 50 Hz bus frame) */
	static unsigned char *out; if (!out) out = malloc (16 + 30000 * 14 + 2 + 32768);   /* (30000: the beam-trace mode's pieces; 14: a
	                                                   3D Imager colour each. On the heap: not in a save, run-ahead) */
	{ extern void vecx_veclink_open (void); vecx_veclink_open (); }   /* (VT_VECLINK: the cable to another console, veclink.c) */
	unsigned char m[8];
	long nframes = 0;
	unsigned long long piece0 = 0;
	while (fread (m, 1, 4, stdin) == 4) {
		int rb_s = -1, rb_l = -1, quiet = 0;         /* (online rollback: this message's slot to save / load, a quiet frame) */
		if (m[3] == 0x90) {                         /* (line streaming: the host's frame - no game run here) */
			unsigned char lb[4]; if (fread (lb, 1, 4, stdin) != 4) break;
			const size_t ln = (size_t) lb[0] | (size_t) lb[1] << 8 | (size_t) lb[2] << 16 | (size_t) lb[3] << 24;
			static unsigned char *sb; static size_t sbn; if (ln > sbn) { unsigned char *q = realloc (sb, ln); if (!q) break; sb = q; sbn = ln; }
			if (ln && fread (sb, 1, ln, stdin) != ln) break;
			unsigned long long p0 = 0; if (!st_unpack (sb, ln, &p0)) { nlast = 0; abuf_n = 0; }
			const int n = nlast > 30000 ? 30000 : nlast; unsigned char *p = out;
			*p++ = 'V'; *p++ = 'F'; put16 (p, n); p += 2; put16 (p, abuf_n / 2); p += 2;
			put16 (p, (unsigned) p0 & 0xffff); put16 (p + 2, (unsigned) (p0 >> 16) & 0xffff); p += 4; put16 (p, 0); put16 (p + 2, 0); p += 4;
			for (int i = 0; i < n; i++) { line_t *l = &last[i];
				put16 (p, LIVE_C (l->x0)); put16 (p + 2, LIVE_C (l->y0)); put16 (p + 4, LIVE_C (l->x1)); put16 (p + 6, LIVE_C (l->y1));
				p[8] = l->c; put16 (p + 9, l->cyc > 65535 ? 65535 : l->cyc); put16 (p + 11, l->t0); p += 13; }
			memcpy (p, abuf, abuf_n); p += abuf_n;
			fwrite (out, 1, p - out, stdout_bin); fflush (stdout_bin);
			continue; }
		if (m[3] & 0x80) break;
		if ((m[3] & 0x40) && fread (m + 4, 1, 3, stdin) != 3) break;   /* (player 2) */
		if (m[3] & 0x20) {                      /* (bit 5: settings follow - a count, then each an id and 4 bytes: a float for
							   vt_set_analog, or ids 0x80 / 0x81 a USB keyboard / mouse report for a UVMC2 (usbhost.c)) */
			unsigned char cnt, g[5]; float f; extern void vt_set_analog (int id, double v); extern void vt_usb_report (int id, const unsigned char *b);
			if (fread (&cnt, 1, 1, stdin) != 1) break;
			for (int k = 0; k < cnt; k++) {
				if (fread (g, 1, 5, stdin) != 5) { cnt = 0; break; }
				if (g[0] == 0x7E) { memcpy (&f, g + 1, 4); ra_want = f > 0.5f; if (ra_want) ra_ready (); }   /* (run-ahead) */
				else if (g[0] == 0x7C) { memcpy (&f, g + 1, 4); rb_s = (int) f; }      /* (rollback: save into this slot) */
				else if (g[0] == 0x7D) { memcpy (&f, g + 1, 4); rb_l = (int) f; }      /* (rollback: load this slot) */
				else if (g[0] == 0x7B) { memcpy (&f, g + 1, 4); quiet = f > 0.5f; }   /* (a frame run again: not sent) */
				else if (g[0] == 0x7A) { memcpy (&f, g + 1, 4); st_out = f > 0.5f; }   /* (line streaming: each frame's lines out too) */
				else if (g[0] >= 0x80) vt_usb_report (g[0], g + 1);
				else { memcpy (&f, g + 1, 4); vt_set_analog (g[0], f); }
			}
			m[3] &= ~0x20;                      /* (recordings keep their format) */
		}
		if (rb_l >= 0 && !rb_load (rb_l)) fprintf (stderr, "STAT: rollback load %d failed\n", rb_l);
		if (rb_s >= 0 && !rb_save (rb_s)) fprintf (stderr, "STAT: rollback save %d failed\n", rb_s);
		if (!quiet) fwrite (m, 1, (m[3] & 0x40) ? 7 : 4, rec);
		nframes++;
		apply_input (m);
		abuf_n = 0;
		{ extern unsigned long long vt_piece_t0; piece0 = vt_piece_t0; }   /* (this frame's lines count from its start) */
		run_frame ();
		uint32_t s;                                     /* the state's checksum (the real frame's, with run-ahead too) */
		{ extern int vt_uvm2, vt_pitrex; extern uint32_t uvm2_sig (void), pitrex_sig (void);
		  s = ram_sum () ^ (uint32_t) total_cycles * 2654435761u;
		  if (vt_uvm2) s ^= uvm2_sig (); else if (vt_pitrex) s ^= pitrex_sig (); }
		int ra = ra_want && ra_ready () == 1, abuf_real = abuf_n;
		if (ra) {                                       /* run-ahead: the next frame unseen, its picture sent (see above) */
			ra_save ();
			{ extern unsigned long long vt_piece_t0; piece0 = vt_piece_t0; }
			run_frame ();
			abuf_n = abuf_real;                         /* (its sound dropped) */
		}
		int n = nlast > 30000 ? 30000 : nlast;
		if (quiet) { n = 0; abuf_n = 0; }               /* (a frame run again: its checksum only) */
		{ extern int vt_veclink, vt_slice_frame; extern long vl_slice; static int nolines = -1;
		  if (nolines < 0) nolines = getenv ("VT_NOLINES") != NULL;
		  if (nolines || (vt_veclink && vl_slice > 0 && !vt_slice_frame)) n = 0; }   /* (the console nobody watches -
		                                           online VecLink's other one: no picture; a slice with no new game frame:
		                                           nothing new to show, the last frame is not sent twice) */
		unsigned char *p = out;
		{ extern int vt_speech_sep, vt_vecvoice; extern int vt_uvm2_cart; extern int vt_imager; int two = (vt_speech_sep && vt_vecvoice) || vt_uvm2_cart || vt_imager;   /* (bit 15: two channels -
		  the console and the speech module, interleaved) */
		  extern int vt_imager, vt_piece_cut; const unsigned cut = quiet ? 0 : (unsigned) vt_piece_cut << 13;   /* (bits 13-14: how the piece
		  ended, vtest.c vt_piece_cut - the frontend joins a cut one to the rest of its frame) */
		  *p++ = 'V'; *p++ = vt_imager ? 'C' : 'F'; put16 (p, n); p += 2;   /* ('VC': each line's colour follows the lines) */ put16 (p, (two ? (abuf_n / 4) | 0x8000 : abuf_n / 2) | cut); p += 2; }   /* 16-bit samples */
		{ unsigned t = (unsigned) piece0;               /* the frame's start (absolute cycles): its lines' times count from it */
		  put16 (p, t & 0xffff); put16 (p + 2, t >> 16); p += 4; }
		put16 (p, s & 0xffff); put16 (p + 2, s >> 16); p += 4;   /* the state's checksum */
		for (int i = 0; i < n; i++) {
			line_t *l = &last[i];
			/* (+ LIVE_OFS: the beam goes past the canvas, vecx.c ALG_CLIP; the frontend takes it off) */
			put16 (p, LIVE_C (l->x0)); put16 (p + 2, LIVE_C (l->y0));
			put16 (p + 4, LIVE_C (l->x1)); put16 (p + 6, LIVE_C (l->y1));
			p[8] = l->c; put16 (p + 9, l->cyc > 65535 ? 65535 : l->cyc); put16 (p + 11, l->t0); p += 13;
		}
		{ extern int vt_imager; extern double vt_imager_speed (void);
		  if (vt_imager) {                                  /* (the 3D Imager's colours, then its disc's speed: 1/100 turns a second) */
			for (int i = 0; i < n; i++) *p++ = last[i].rgb;
			double v = vt_imager_speed () * 100.0; put16 (p, v < 0 ? 0 : v > 65535 ? 65535 : (unsigned) v); p += 2;
		  } }
		memcpy (p, abuf, abuf_n); p += abuf_n;
		fwrite (out, 1, p - out, stdout_bin);
		if (st_out) {                                   /* (line streaming: the frame for player 2, small - see above) */
			static unsigned char *sp; if (!sp) sp = malloc (64 + MAXL * 26 + 8000);
			extern int vt_speech_sep, vt_vecvoice, vt_uvm2_cart, vt_imager; const int two = (vt_speech_sep && vt_vecvoice) || vt_uvm2_cart || vt_imager;
			if (sp) { const size_t k = st_pack (sp + 6, piece0, nlast > 30000 ? 30000 : nlast, two); sp[0] = 'V'; sp[1] = 'S'; st_put32 (sp + 2, (uint32_t) k); fwrite (sp, 1, 6 + k, stdout_bin); } }
		fflush (stdout_bin);
		if (ra) ra_load ();                             /* (back to the real frame's end) */
		flash_save (0);
	}
	flash_save (1);
	{ extern void vecx_veclink_close (void); vecx_veclink_close (); }   /* (the cable: this console gone, its wires high) */
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
