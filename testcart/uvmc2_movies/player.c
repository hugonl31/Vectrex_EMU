/* player.c - a UVMC2 (.um2) movie player for Vectrex_EMU: plays the Vectrex vector movies of the PiTrex / VecFever
 * format (BadApple.bin, LaLinea.bin, BlaBla.bin, ...) from the cartridge's SD card, folder movies. Written from
 * scratch, no SDK code: by Hugo Bokhorst with Claude, MIT (README.md). The movies are not part of it.
 *
 * The movie format (worked out from the 6809 VIDEOPLAYER that plays them, MOVIEPLAYER.NIB): 1536-byte chunks, one a
 * frame at 30 frames a second (50000 E cycles). Bytes 0-1023: vector commands; 1024-1535: sound samples for the DAC,
 * one per drawing step ($A5: none). A command byte C:
 *   $80        the beam to the middle (zero)
 *   $20        the end of the frame
 *   else       low nibble n: n moves by one (dy, dx) - [count] [dy] [dx] follow, then count line pairs (0 = 256);
 *              n = 0: [count] follows, then count + 1 line pairs (dy, dx). High nibble: $10 more commands follow,
 *              $F0 the frame goes on in the next chunk, else the frame ends.
 * Each move / line: Y from the DAC through the sample-and-hold, X straight from the DAC, the ramp for 24 cycles.
 *
 * The bus (as demo.c): the RP2350 holds the 6809 in /HALT and makes one access per E period, writing the VIA. The SD
 * card: SPI0 (GP34 SCK, GP35 TX, GP36 RX), its CS on GP39; FAT32 read with long names, a card with or without a
 * partition table. Button 4: the next movie. */
#include <stdint.h>

#define REG(a)          (*(volatile uint32_t *) (a))
#define SIO_IN          REG (0xd0000004u)
#define SIO_OUT         REG (0xd0000010u)
#define SIO_HI_OUT      REG (0xd0000014u)
#define SIO_OE          REG (0xd0000030u)
#define SIO_HI_OE       REG (0xd0000034u)
#define IO_CTRL(n)      REG (0x40028004u + 8u * (n))
#define PAD(n)          REG (0x40038004u + 4u * (n))
#define RESETS_CLR      REG (0x40023000u)
#define CLK_PERI_CTRL   REG (0x40010048u)
#define SPI_CR0         REG (0x40080000u)
#define SPI_CR1         REG (0x40080004u)
#define SPI_DR          REG (0x40080008u)
#define SPI_SR          REG (0x4008000cu)
#define SPI_CPSR        REG (0x40080010u)
#define TIMER_US        REG (0x400b0028u)            /* (TIMERAWL: microseconds, from the 12 MHz crystal) */

#define NE      (1u << 31)
#define RW      (1u << 26)
#define HALT    (1u << 27)
#define DATA    0xffu
#define ADDR    ((0x3fffu << 8) | (1u << 24) | (1u << 25))
#define ORB 0xd000
#define ORA 0xd001
#define DDRB 0xd002
#define DDRA 0xd003
#define ACR 0xd00b
#define PCR 0xd00c
#define IER 0xd00e
#define IDLE 0xc880

/* ------------------------------------------------------------------ the headphone jack (PT8211 on PIO2)
 * A movie with a .pcm file next to it (raw 16-bit mono, 44100 Hz, from the movie's start) plays it through the UVMC2's
 * own audio chip: a PIO program clocks each sample out as a 32-bit stereo word (BCK, WS, DIN on GP45 / 44 / 43, 64 PIO
 * cycles a word); the CPU keeps its FIFO filled between the bus accesses from a ring the SD card fills. */
#define PIO2(o)         REG (0x50400000u + (o))
#define RING 16384
static int16_t ring[RING]; static volatile uint32_t ring_w, ring_r;
static int hq;                                       /* (the movie has its .pcm: the good sound) */
static int jack;                                     /* (... played through the headphone jack; else the Vectrex speaker) */
static uint32_t movie_t0;                            /* (the movie's start: microseconds) */
static const uint16_t I2S[8] = { 0x6801, 0x1840, 0x6001, 0xf02e, 0x6001, 0x1044, 0x6801, 0xf82e };
/*   0: out pins,1 side WS   1: jmp x--,0 side WS|BCK   2: out pins,1   3: set x,14 side BCK
 *   4: out pins,1           5: jmp x--,4 side BCK      6: out pins,1 side WS   7: set x,14 side WS|BCK (entry)
 *   (side-set: bit 0 WS, bit 1 BCK; 16 bits a channel, 2 cycles a bit) */
static void jack_stop (void) { PIO2 (0x000) = 0; }
static void jack_init (void)
{
	RESETS_CLR = 1u << 13;                           /* (PIO2 out of reset) */
	for (int n = 43; n <= 45; n++) { IO_CTRL (n) = 8; PAD (n) &= ~(1u << 8); }   /* (FUNCSEL: PIO2) */
	PIO2 (0x000) = 0;                                /* (state machines off) */
	PIO2 (0x168) = 16;                               /* (GPIOBASE: pins 16-47) */
	for (int i = 0; i < 8; i++) PIO2 (0x048 + 4 * i) = I2S[i];
	PIO2 (0x0c8) = (53u << 16) | (38u << 8);         /* (clock divider 53.15: 150 MHz / 64 / 53.15 = 44100 Hz) */
	PIO2 (0x0cc) = (7u << 12) | (0u << 7);           /* (wrap 0 .. 7) */
	PIO2 (0x0d0) = (1u << 17) | (1u << 30);          /* (autopull 32, shift left, the TX FIFO 8 deep) */
	PIO2 (0x0dc) = (27u << 0) | (1u << 20) | (27u << 5) | (3u << 26) | (28u << 10) | (2u << 29);  /* (OUT 43, SET 43-45, side 44-45) */
	PIO2 (0x0d8) = 0xe087;                           /* (set pindirs, 7: the three pins out) */
	PIO2 (0x0d8) = 0x0007;                           /* (jmp 7: the entry) */
	PIO2 (0x000) = 1;                                /* (state machine 0 on) */
}
static inline void jack_feed (void)
{
	while (ring_r != ring_w && !(PIO2 (0x004) & (1u << 16))) {     /* (FSTAT: the TX FIFO not full) */
		uint32_t s = (uint16_t) ring[ring_r & (RING - 1)]; ring_r++;
		PIO2 (0x010) = (s << 16) | s;
	}
}

/* ------------------------------------------------------------------ the bus, and the sound between vectors */
static uint32_t accesses;
static inline uint32_t abits (uint32_t a) { return ((a & 0x3fffu) << 8) | (((a >> 14) & 1u) << 24) | (((a >> 15) & 1u) << 25); }
static uint32_t bus (uint32_t a, int write, uint32_t d)
{
	while (!(SIO_IN & NE)) ;
	if (write) { SIO_OUT = abits (a) | (d & DATA); SIO_OE = ADDR | RW | HALT | DATA; }
	else { SIO_OE = ADDR | RW | HALT; SIO_OUT = abits (a) | RW; }
	while (SIO_IN & NE) ;
	accesses++;
	if (jack) jack_feed ();
	return SIO_IN & DATA;
}
static void wr (uint32_t a, uint32_t d) { bus (a, 1, d); }
static uint32_t rd (uint32_t a) { return bus (a, 0, 0); }

/* this frame's samples, played evenly over its 50000 cycles (when the DAC is free: between vectors) */
static uint8_t snd[512]; static int snd_n, snd_i; static uint32_t snd_t0, snd_step;   /* (microseconds) */
static void sound_start (const uint8_t *s)
{
	snd_n = 0;
	if (!hq) for (int i = 0; i < 512; i++) if (s[i] != 0xa5) snd[snd_n++] = s[i];
	snd_i = 0; snd_t0 = TIMER_US; snd_step = snd_n ? 33333u / (uint32_t) snd_n : 33333u;
}
/* the good sound through the Vectrex speaker: the .pcm (44100 Hz, 16 bit) at 22050 Hz through the DAC - two samples
 * averaged (a little low pass), 8 bits with a little dither; each sample at its time from the movie's start (a sample
 * due while a vector is drawn goes out right after it; one that is past is skipped: the sound keeps the picture's time) */
#define SPK_HZ 22050u
static uint32_t spk_n, dither = 0x12345678u;
static void speaker_due (void)
{
	uint32_t n = (uint32_t) (((uint64_t) (TIMER_US - movie_t0) * SPK_HZ) / 1000000u);
	if (n < spk_n) return;
	uint32_t p = 2 * n;                              /* (its place in the ring: 44100 Hz samples since the start) */
	if (p + 1 >= ring_w) return;                     /* (not read yet) */
	if (p < ring_r) { spk_n = n + 1; return; }       /* (gone already) */
	int s = (ring[p & (RING - 1)] + ring[(p + 1) & (RING - 1)]) / 2;
	dither = dither * 1664525u + 1013904223u;
	s += (int) ((dither >> 24) & 0xff) - (int) ((dither >> 16) & 0xff);   /* (triangular dither, +-1 step of 256) */
	s >>= 8; if (s > 127) s = 127; if (s < -128) s = -128;
	ring_r = p + 2; spk_n = n + 1;
	wr (ORA, (uint32_t) s & 0xff);
	wr (ORB, 0x86);                                  /* (mux on, select the sound: it takes the DAC) */
	wr (ORB, 0x81);
}
static void sound_due (void)
{
	if (hq) { if (!jack) speaker_due (); return; }
	if (snd_i < snd_n && TIMER_US - snd_t0 >= (uint32_t) snd_i * snd_step) {
		wr (ORA, snd[snd_i++]);
		wr (ORB, 0x86);                              /* (mux on, select the sound: it takes the DAC) */
		wr (ORB, 0x81);
	}
}
static void idle (int n) { while (n-- > 0) { rd (IDLE); } }            /* (the DAC busy: no sound) */
static void idle_snd (int n) { while (n-- > 0) { rd (IDLE); sound_due (); } }

/* ------------------------------------------------------------------ drawing */
static int ramp = 24;                                /* (the ramp, cycles: the movies' scale) */
static int cx, cy;
static void zero (void)
{
	wr (ORA, 0); wr (PCR, 0xcc); idle_snd (10); wr (PCR, 0xce); cx = cy = 0;
}
static void intensity (int z) { wr (ORA, (uint32_t) z & 0xff); wr (ORB, 0x84); wr (ORB, 0x81); }
static void step (int dx, int dy, int lit)
{
	wr (ORA, (uint32_t) dy & 0xff); wr (ORB, 0x80);  /* (Y through its sample-and-hold) */
	idle (12);
	wr (ORB, 0x81); wr (ORA, (uint32_t) dx & 0xff);  /* (X straight from the DAC) */
	idle (12);
	if (lit) wr (PCR, 0xee);
	wr (ORB, 0x01);                                  /* (RAMP) */
	idle (ramp - 2);
	wr (ORB, 0x81);
	idle (10);                                       /* (the integrators run on ~12 cycles: the beam stays on, as the
	                                                  *  6809 player's does - else each short line ends dark: dashes) */
	if (lit) wr (PCR, 0xce);
	sound_due ();
	cx += dx; cy += dy;
}
static void move_to (int x, int y)
{
	while (x != cx || y != cy) {
		int dx = x - cx, dy = y - cy;
		dx = dx > 127 ? 127 : dx < -127 ? -127 : dx; dy = dy > 127 ? 127 : dy < -127 ? -127 : dy;
		step (dx, dy, 0);
	}
}
static void line_to (int x, int y)
{
	int dx = x - cx, dy = y - cy;
	dx = dx > 127 ? 127 : dx < -127 ? -127 : dx; dy = dy > 127 ? 127 : dy < -127 ? -127 : dy;
	step (dx, dy, 1);
}

/* a stroke font on a 4 x 6 grid: each glyph points "xy", "|" lifts the pen */
static const char *glyph (char c)
{
	static const char *const L[26] = { "00 04 26 44 40|03 43", "00 06 36 45 44 33 03|33 42 41 30 00", "46 06 00 40",
		"00 06 36 45 41 30 00", "46 06 00 40|03 33", "46 06 00|03 33", "46 06 00 40 43 23", "00 06|40 46|03 43",
		"06 46|26 20|00 40", "46 40 00 02", "00 06|46 03 40", "06 00 40", "00 06 23 46 40", "00 06 40 46",
		"00 06 46 40 00", "00 06 46 43 03", "00 06 46 42 20 00|22 40", "00 06 46 43 03 40", "46 06 03 43 40 00",
		"06 46|26 20", "06 00 40 46", "06 20 46", "06 00 23 40 46", "00 46|06 40", "06 23 46|23 20", "06 46 00 40" };
	static const char *const D[10] = { "00 06 46 40 00", "15 26 20|00 40", "06 46 43 03 00 40", "06 46 40 00|03 43",
		"06 03 43|46 40", "46 06 03 43 40 00", "46 06 00 40 43 03", "06 46 20", "00 06 46 40 00|03 43", "40 46 06 03 43" };
	if (c >= 'a' && c <= 'z') c -= 32;
	if (c >= 'A' && c <= 'Z') return L[c - 'A'];
	if (c >= '0' && c <= '9') return D[c - '0'];
	if (c == '.') return "00 01";
	if (c == '-' || c == '_') return "03 43";
	if (c == '/') return "00 46";
	if (c == '!') return "26 22|20 21";
	return "";
}
static int text_width (const char *s) { int n = 0; while (*s++) n++; return n * 6 - 2; }
static void text (const char *s, int x0, int y0, int k)
{
	for (; *s; s++, x0 += 6 * k) {
		const char *g = glyph (*s); int pen = 0;
		while (*g) {
			if (*g == '|') { pen = 0; g++; continue; }
			if (*g == ' ') { g++; continue; }
			int x = x0 + (g[0] - '0') * k, y = y0 + (g[1] - '0') * k;
			if (pen) line_to (x, y); else move_to (x, y);
			pen = 1; g += 2;
		}
	}
}
static void text_c (const char *s, int y, int k) { text (s, -text_width (s) * k / 2, y, k); }

/* ------------------------------------------------------------------ the buttons (the AY's port A, register 14) */
static unsigned buttons (void)
{
	wr (ORA, 0x0e); wr (ORB, 0x99); wr (ORB, 0x81);  /* (latch the register number) */
	wr (DDRA, 0x00); wr (ORB, 0x89);                 /* (read it) */
	unsigned v = rd (ORA);
	wr (ORB, 0x81); wr (DDRA, 0xff);
	return ~v & 0x0f;                                /* (player 1's buttons 1-4, pressed = 1) */
}

/* ------------------------------------------------------------------ the SD card (SPI mode) */
static int sd_hc;
static uint8_t spi (uint8_t b)
{
	SPI_DR = b;
	while (!(SPI_SR & 4)) ;                          /* (RNE) */
	return (uint8_t) SPI_DR;
}
static void cs (int on) { if (on) SIO_HI_OUT &= ~(1u << 7); else SIO_HI_OUT |= 1u << 7; }   /* (GP39) */
static uint8_t sd_cmd (int cmd, uint32_t arg, uint8_t crc)
{
	cs (1); spi (0xff);
	spi ((uint8_t) (0x40 | cmd)); spi ((uint8_t) (arg >> 24)); spi ((uint8_t) (arg >> 16)); spi ((uint8_t) (arg >> 8));
	spi ((uint8_t) arg); spi (crc);
	uint8_t r = 0xff;
	for (int i = 0; i < 16 && (r & 0x80); i++) r = spi (0xff);
	return r;
}
static int sd_init (void)
{
	RESETS_CLR = 1u << 18;                           /* (SPI0 out of reset) */
	CLK_PERI_CTRL = 1u << 11;                        /* (clk_peri on, from clk_sys) */
	for (int n = 34; n <= 36; n++) { IO_CTRL (n) = 1; PAD (n) = (PAD (n) & ~(1u << 8)) | (1u << 6) | (n == 36 ? 1u << 3 : 0); }
	IO_CTRL (39) = 5; PAD (39) &= ~(1u << 8);
	SIO_HI_OE |= 1u << 7; cs (0);
	SPI_CR1 = 0; SPI_CR0 = 7 | (1u << 8); SPI_CPSR = 254; SPI_CR1 = 2;   /* (8 bits, ~300 kHz to start) */
	for (int i = 0; i < 10; i++) spi (0xff);
	if (sd_cmd (0, 0, 0x95) != 1) { cs (0); return 0; }
	uint8_t r = sd_cmd (8, 0x1aa, 0x87);
	int v2 = r == 1;
	if (v2) for (int i = 0; i < 4; i++) spi (0xff);
	for (int i = 0; i < 20000; i++) {
		sd_cmd (55, 0, 0x65);
		if ((r = sd_cmd (41, v2 ? 0x40000000u : 0, 0x77)) == 0) break;
	}
	if (r) { cs (0); return 0; }
	sd_hc = 0;
	if (v2 && sd_cmd (58, 0, 0xfd) == 0) { uint8_t o = spi (0xff); spi (0xff); spi (0xff); spi (0xff); sd_hc = (o & 0x40) != 0; }
	cs (0); spi (0xff);
	SPI_CR1 = 0; SPI_CR0 = 7; SPI_CPSR = 6; SPI_CR1 = 2;  /* (25 MHz) */
	return 1;
}
/* a block; with tick: one bus access (and the sound) between the bytes, so the picture and sound go on */
static int sd_read (uint32_t lba, uint8_t *buf, int tick)
{
	if (sd_cmd (17, sd_hc ? lba : lba * 512, 0xff) != 0) { cs (0); return 0; }
	uint8_t t = 0xff;
	for (int i = 0; i < 100000 && t == 0xff; i++) { t = spi (0xff); if (tick) { rd (IDLE); sound_due (); } }
	if (t != 0xfe) { cs (0); return 0; }
	for (int i = 0; i < 512; i++) { buf[i] = spi (0xff); if (tick && (i & 1)) { rd (IDLE); sound_due (); } }
	spi (0xff); spi (0xff); cs (0); spi (0xff);
	return 1;
}

/* ------------------------------------------------------------------ FAT32 */
static uint8_t sec[512];
static uint32_t part, fat0, data0, spc, root;
static uint32_t u16 (const uint8_t *p) { return (uint32_t) p[0] | ((uint32_t) p[1] << 8); }
static uint32_t u32 (const uint8_t *p) { return u16 (p) | (u16 (p + 2) << 16); }
static int fat_mount (void)
{
	if (!sd_read (0, sec, 0)) return 0;
	part = 0;
	if (!(sec[82] == 'F' && sec[83] == 'A' && sec[84] == 'T' && sec[85] == '3' && sec[86] == '2')) {
		part = u32 (sec + 0x1c6);                    /* (a partition table: the first partition) */
		if (!sd_read (part, sec, 0)) return 0;
		if (!(sec[82] == 'F' && sec[86] == '2')) return 0;
	}
	spc = sec[13];
	fat0 = part + u16 (sec + 14);
	data0 = fat0 + sec[16] * u32 (sec + 36);
	root = u32 (sec + 44);
	return spc != 0;
}
static uint32_t clus_lba (uint32_t c) { return data0 + (c - 2) * spc; }
static uint32_t fat_next (uint32_t c)
{
	static uint32_t cached = 0xffffffffu;
	uint32_t s = fat0 + c / 128;
	if (s != cached) { if (!sd_read (s, sec, 0)) return 0x0fffffff; cached = s; }
	return u32 (sec + (c % 128) * 4) & 0x0fffffff;
}
/* the entries of a folder, with their long names */
typedef struct { char name[40]; uint32_t clus, size; int dir; } ent_t;
static int fat_list (uint32_t dclus, ent_t *out, int max)
{
	int n = 0; char lfn[40]; int lfn_ok = 0;
	static uint8_t blk[512];
	for (uint32_t c = dclus; c >= 2 && c < 0x0ffffff8; c = fat_next (c)) {
		for (uint32_t s = 0; s < spc; s++) {
			if (!sd_read (clus_lba (c) + s, blk, 0)) return n;
			for (int e = 0; e < 512; e += 32) {
				const uint8_t *d = blk + e;
				if (d[0] == 0) return n;
				if (d[0] == 0xe5) { lfn_ok = 0; continue; }
				if (d[11] == 0x0f) {                 /* (a long name part: 13 characters) */
					int seq = (d[0] & 0x1f) - 1;
					if (d[0] & 0x40) { for (int i = 0; i < 40; i++) lfn[i] = 0; lfn_ok = 1; }
					static const int at[13] = { 1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30 };
					for (int i = 0; i < 13; i++) {
						int k = seq * 13 + i; uint32_t ch = u16 (d + at[i]);
						if (k < 39 && ch != 0 && ch != 0xffff) lfn[k] = ch < 128 ? (char) ch : '_';
					}
					continue;
				}
				if (d[11] & 0x08) { lfn_ok = 0; continue; }     /* (the volume label) */
				if (n < max) {
					ent_t *o = &out[n++];
					if (lfn_ok && lfn[0]) { for (int i = 0; i < 40; i++) o->name[i] = lfn[i]; }
					else {                               /* (the 8.3 name) */
						int k = 0;
						for (int i = 0; i < 8 && d[i] != ' '; i++) o->name[k++] = (char) d[i];
						if (d[8] != ' ') { o->name[k++] = '.'; for (int i = 8; i < 11 && d[i] != ' '; i++) o->name[k++] = (char) d[i]; }
						o->name[k] = 0;
					}
					o->clus = (u16 (d + 20) << 16) | u16 (d + 26); o->size = u32 (d + 28); o->dir = (d[11] & 0x10) != 0;
				}
				lfn_ok = 0;
			}
		}
	}
	return n;
}
static int same (const char *a, const char *b)
{
	for (; *a && *b; a++, b++) { char x = *a, y = *b; if (x >= 'a' && x <= 'z') x -= 32; if (y >= 'a' && y <= 'z') y -= 32; if (x != y) return 0; }
	return *a == *b;
}
static int ends_bin (const char *s)
{
	int n = 0; while (s[n]) n++;
	return n > 4 && same (s + n - 4, ".BIN");
}

/* ------------------------------------------------------------------ playing a movie */
#define MAXCL 16384
static uint32_t chain[MAXCL]; static int nchain;
static uint32_t pchain[MAXCL]; static int npchain; static uint32_t pcm_blocks, pcm_next;
/* the ring topped up from the .pcm file to at least `want` samples ahead (whole blocks: 256 samples each) */
static void pcm_fill (uint32_t want, int tick)
{
	static uint8_t b[512];
	while (hq && pcm_next < pcm_blocks && ring_w - ring_r + 256 <= RING && ring_w - ring_r < want) {
		uint32_t ci = pcm_next / spc;
		if (ci >= (uint32_t) npchain || !sd_read (clus_lba (pchain[ci]) + pcm_next % spc, b, tick)) { pcm_next = pcm_blocks; return; }
		pcm_next++;
		for (int i = 0; i < 256; i++) ring[(ring_w + (uint32_t) i) & (RING - 1)] = (int16_t) (b[2 * i] | (b[2 * i + 1] << 8));
		ring_w += 256;
	}
}
static uint8_t chunk[2][1536];
static int read_chunk (uint32_t k, uint8_t *buf, int tick)
{
	for (int b = 0; b < 3; b++) {
		uint32_t blk = k * 3 + (uint32_t) b, ci = blk / spc;
		if (ci >= (uint32_t) nchain) return 0;
		if (!sd_read (clus_lba (chain[ci]) + blk % spc, buf + 512 * b, tick)) return 0;
	}
	return 1;
}
/* one frame from chunk *k (and on, $F0); returns 0 at the movie's end; the next chunk is read meanwhile */
/* (the frames are timed by the microsecond timer from the movie's start - frame k at k x 1/30 s: counting bus accesses
 * ran slow, a cycle is missed whenever the work between two accesses takes longer than half of it) */
static int frame (uint32_t *k, int *cur, uint32_t nchunks)
{
	wr (0xc890, *k >> 8); wr (0xc891, *k & 255);     /* (the frame number in the console's RAM: for tests) */
	const uint8_t *p = chunk[*cur], *end = p + 1024;
	sound_start (p + 1024);
	zero (); intensity (0x3f);
	int more = 1;
	while (more && p < end) {
		uint8_t c = *p++;
		if (c == 0x80) { zero (); continue; }
		if (c == 0x20) break;
		int n = c & 15, lines;
		if (n) {
			if (p + 3 > end) break;
			lines = p[0] ? p[0] : 256;
			for (int i = 0; i < n; i++) step ((int8_t) p[2], (int8_t) p[1], 0);
			p += 3;
		} else {
			if (p + 1 > end) break;
			lines = p[0] + 1; p += 1;
		}
		for (int i = 0; i < lines && p + 2 <= end; i++, p += 2) step ((int8_t) p[1], (int8_t) p[0], 1);
		int f = c & 0xf0;
		if (f == 0x10) continue;
		if (f == 0xf0) {                             /* (the frame goes on in the next chunk) */
			if (++*k >= nchunks || !read_chunk (*k, chunk[*cur], 1)) return 0;
			p = chunk[*cur]; end = p + 1024; sound_start (p + 1024);
			continue;
		}
		more = 0;
	}
	/* the next chunk, between the bus accesses, then the rest of the frame's 50000 cycles */
	if (++*k >= nchunks) return 0;
	int nx = *cur ^ 1;
	if (!read_chunk (*k, chunk[nx], 1)) return 0;
	*cur = nx;
	pcm_fill (6000, 1);                              /* (the jack's sound: ~4 frames ahead) */
	uint32_t due = movie_t0 + (uint32_t) (((uint64_t) *k * 1000000u) / 30u);
	while ((int32_t) (TIMER_US - due) < 0) idle_snd (1);
	return 1;
}
static void message (const char *a, const char *b, int frames)
{
	for (int f = 0; f < frames; f++) {
		uint32_t t0 = TIMER_US;
		zero (); intensity (0x5f); ramp = 64;
		text_c (a, 40, 5); zero ();
		if (b) text_c (b, -30, 3);
		ramp = 24;
		while (TIMER_US - t0 < 20000u) idle (1);
	}
}
static void title_of (const char *file, char *out)
{
	int i = 0;
	for (; file[i] && file[i] != '.' && i < 30; i++) out[i] = file[i];
	out[i] = 0;
}

void main_loop (void)
{
	for (int n = 0; n < 32; n++) { IO_CTRL (n) = 5; PAD (n) = (PAD (n) & ~(1u << 8)) | (1u << 6); }
	SIO_OUT = abits (IDLE) | RW; SIO_OE = ADDR | RW | HALT;
	for (volatile int i = 0; i < 20000; i++) ;
	wr (DDRA, 0xff); wr (DDRB, 0x9f); wr (ACR, 0x00); wr (IER, 0x7f); wr (PCR, 0xce); wr (ORB, 0x81);
	static ent_t ents[64], movies[64];
	if (!sd_init () || !fat_mount ()) for (;;) message ("NO SD CARD", "UVMC2 MOVIE PLAYER", 50);
	int ne = fat_list (root, ents, 64), nm = 0;
	uint32_t mdir = root;
	for (int i = 0; i < ne; i++) if (ents[i].dir && same (ents[i].name, "movies")) mdir = ents[i].clus;
	if (mdir != root) ne = fat_list (mdir, ents, 64);
	for (int i = 0; i < ne; i++) if (!ents[i].dir && ends_bin (ents[i].name) && ents[i].size >= 1536) movies[nm++] = ents[i];
	if (!nm) for (;;) message ("NO MOVIES", "PUT THEM IN SD/MOVIES", 50);
	if (ne > 64) ne = 64;
	for (int m = 0;; m = (m + 1) % nm) {
		char t[32]; title_of (movies[m].name, t);
		message (t, "BUTTON 3: SPEAKER / JACK", 100);
		nchain = 0;
		for (uint32_t c = movies[m].clus; c >= 2 && c < 0x0ffffff8 && nchain < MAXCL; c = fat_next (c)) chain[nchain++] = c;
		/* its sound for the jack: NAME.pcm in the same folder */
		hq = 0; jack_stop (); npchain = 0; pcm_blocks = pcm_next = 0; ring_w = ring_r = 0; spk_n = 0;
		for (int i = 0; i < ne; i++) {
			char a[32], b[32]; title_of (ents[i].name, a);
			int n = 0; while (ents[i].name[n]) n++;
			if (!ents[i].dir && n > 4 && same (ents[i].name + n - 4, ".PCM") && same (a, t)) {
				for (uint32_t c = ents[i].clus; c >= 2 && c < 0x0ffffff8 && npchain < MAXCL; c = fat_next (c)) pchain[npchain++] = c;
				pcm_blocks = ents[i].size / 512; hq = 1; (void) b;
			}
		}
		if (hq) { pcm_fill (RING - 512, 0); if (jack) jack_init (); }
		uint32_t nchunks = movies[m].size / 1536, k = 0; int cur = 0;
		if (!read_chunk (0, chunk[0], 0)) continue;
		unsigned last = 0;
		movie_t0 = TIMER_US;
		while (frame (&k, &cur, nchunks)) {
			pcm_fill (6000, 1);
			unsigned b = buttons ();
			if ((b & 8) && !(last & 8)) break;       /* (button 4: the next movie) */
			if ((b & 4) && !(last & 4) && hq) {      /* (button 3: the speaker or the jack - from the sound now) */
				jack = !jack;
				if (jack) { ring_r = 2 * (uint32_t) (((uint64_t) (TIMER_US - movie_t0) * SPK_HZ) / 1000000u); jack_init (); }
				else { jack_stop (); spk_n = 0; }
			}
			last = b;
		}
	}
}

extern uint32_t __bss_start__, __bss_end__, __stack_top__;
void reset_handler (void)
{
	for (uint32_t *p = &__bss_start__; p < &__bss_end__; p++) *p = 0;
	main_loop ();
}
void fault_handler (void) { for (;;) ; }
__attribute__ ((section (".vectors"), used)) const void *const vectors[16] = {
	&__stack_top__, (void *) reset_handler, (void *) fault_handler, (void *) fault_handler, (void *) fault_handler,
	(void *) fault_handler, (void *) fault_handler, 0, 0, 0, 0, (void *) fault_handler, 0, 0,
	(void *) fault_handler, (void *) fault_handler };
