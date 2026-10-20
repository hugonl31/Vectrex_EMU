/* sp0256.c - the VecVoice speech accessory: a GI SP0256-AL2 on controller port 2 (included by vecx.c).
 *
 * The synthesizer is a C port of MAME's sp0256.cpp (license BSD-3-Clause, copyright Joseph Zbiciak
 * and Tim Lindner: "By Joe Zbiciak. Ported to MAME by tim lindner"): the microsequencer that reads
 * the allophone ROM, and the 12-pole LPC filter, 10 kHz out of a 3.12 MHz clock. The SPB640 FIFO is
 * left out (the VecVoice has none).
 *
 * The AL2 ROM (2 KB, (c) Microchip Technology Inc.) is not part of the emulator: VT_VECVOICE names
 * the file. Joe Zbiciak distributes it with Microchip's permission (spatula-city.org/~im14u2c/
 * sp0256-al2/, al2.bin) - that file is bit-reversed against MAME's sp0256-al2.bin: both are taken.
 *
 * The VecVoice link (as the MiSTer core models it, Dar / Videodr0me): the Vectrex sends each
 * allophone as a serial byte on the AY's port A bit 4 (port A set to output; idle high), 1.5 MHz /
 * 156 = 9615 baud, LSB first, 1 start bit; bits 0-5 are the allophone. The VecVoice answers on port 2's
 * button lines (port A bits 4-7): 0 = it takes the next allophone, 1 = busy.
 *
 * The VecVox (a SpeakJet on the same link) is in speakjet.c.
 */
#include <stdint.h>

int vt_vecvoice;                                /* 1 = the VecVoice (SP0256), 2 = the VecVox (SpeakJet) */
/* the VecVox speaks with this chip (speakjet.c): */
static int sj_per;                              /* the SpeakJet's pitch as an SP0256 period (samples), 0 = the AL2's own */
static double sj_scale = 1.0;                   /* the SpeakJet's length against the allophone's own */
static int sp_native[64];                       /* each allophone's own length (samples) */
static unsigned char sp_rom[0x10000];           /* the AL2 at $1000 */

#define SP_PER_PAUSE 64
#define SP_PER_NOISE 64

static struct {
	int rpt, cnt;
	uint32_t per, rng;
	int amp;
	int16_t f_coef[6], b_coef[6], z_data[6][2];
	uint8_t r[16];
	int interp;
} sp_f;
static int sp_silent = 1, sp_lrq = 1, sp_ald, sp_pc, sp_stack, sp_halted = 1;
static uint32_t sp_mode, sp_page = 0x1000 << 3;

static uint32_t sp_crc32 (const unsigned char *p, int n)
{
	uint32_t c = 0xffffffffu;
	while (n--) { c ^= *p++; for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xedb88320u & -(c & 1)); }
	return ~c;
}
static unsigned char sp_rev8 (unsigned char v)
{
	v = (unsigned char) ((v & 0xf0) >> 4 | (v & 0x0f) << 4);
	v = (unsigned char) ((v & 0xcc) >> 2 | (v & 0x33) << 2);
	return (unsigned char) ((v & 0xaa) >> 1 | (v & 0x55) << 1);
}
static uint32_t sp_rev32 (uint32_t v)
{
	v = (v & 0xffff0000u) >> 16 | (v & 0x0000ffffu) << 16;
	v = (v & 0xff00ff00u) >> 8 | (v & 0x00ff00ffu) << 8;
	v = (v & 0xf0f0f0f0u) >> 4 | (v & 0x0f0f0f0fu) << 4;
	v = (v & 0xccccccccu) >> 2 | (v & 0x33333333u) << 2;
	return (v & 0xaaaaaaaau) >> 1 | (v & 0x55555555u) << 1;
}

/* load the AL2 ROM: 0 = done, -1 = no file, -2 = not the SP0256-AL2 */
int sp_load (const char *fn)
{
	unsigned char b[2048];
	FILE *f = fopen (fn, "rb");
	if (!f) return -1;
	size_t n = fread (b, 1, sizeof b, f); fclose (f);
	if (n != sizeof b) return -2;
	if (sp_crc32 (b, 2048) == 0xdf8de0b0u) for (int i = 0; i < 2048; i++) b[i] = sp_rev8 (b[i]);   /* (Joe Zbiciak's al2.bin) */
	if (sp_crc32 (b, 2048) != 0xb504ac15u) return -2;                                                /* (MAME's sp0256-al2.bin) */
	memcpy (sp_rom + 0x1000, b, 2048);
	return 0;
}

static void sp_reset (void)
{
	memset (&sp_f, 0, sizeof sp_f);
	sp_f.rpt = -1; sp_f.rng = 1;
	sp_halted = 1; sp_lrq = 1; sp_ald = 0; sp_pc = 0; sp_stack = 0; sp_mode = 0;
	sp_page = 0x1000 << 3; sp_silent = 1;
}

/* the 12-pole filter: one 10 kHz sample (returns 0 when the repeat count ran out first) */
static int16_t sp_limit (int16_t s) { return s > 8191 ? 8191 : s < -8192 ? -8192 : s; }
static int sp_update (int16_t *out)
{
	int do_int = 0;
	uint16_t samp = 0;
	if (sp_f.per) {
		if (sp_f.cnt <= 0) {
			sp_f.cnt += (int) sp_f.per; samp = (uint16_t) sp_f.amp; sp_f.rpt--; do_int = sp_f.interp;
			for (int j = 0; j < 6; j++) sp_f.z_data[j][1] = sp_f.z_data[j][0] = 0;
		} else { samp = 0; sp_f.cnt--; }
	} else {
		if (--sp_f.cnt <= 0) {
			do_int = sp_f.interp; sp_f.cnt = SP_PER_NOISE; sp_f.rpt--;
			for (int j = 0; j < 6; j++) sp_f.z_data[j][0] = sp_f.z_data[j][1] = 0;
		}
		int bit = sp_f.rng & 1;
		sp_f.rng = (sp_f.rng >> 1) ^ (bit ? 0x4001 : 0);
		samp = (uint16_t) (bit ? sp_f.amp : -sp_f.amp);
	}
	if (do_int) {
		sp_f.r[0] = (uint8_t) (sp_f.r[0] + sp_f.r[14]);
		sp_f.r[1] = (uint8_t) (sp_f.r[1] + sp_f.r[15]);
		sp_f.amp = (sp_f.r[0] & 0x1f) << ((sp_f.r[0] & 0xe0) >> 5);
		sp_f.per = sp_f.r[1];
		if (sj_per && sp_f.per) sp_f.per = (uint32_t) sj_per;
	}
	if (sp_f.rpt <= 0) return 0;
	for (int j = 0; j < 6; j++) {
		samp = (uint16_t) (samp + ((sp_f.b_coef[j] * sp_f.z_data[j][1]) >> 9));
		samp = (uint16_t) (samp + ((sp_f.f_coef[j] * sp_f.z_data[j][0]) >> 8));
		sp_f.z_data[j][1] = sp_f.z_data[j][0];
		sp_f.z_data[j][0] = (int16_t) samp;
	}
	*out = (int16_t) (sp_limit ((int16_t) samp) << 2);
	return 1;
}

static void sp_regdec (void)
{
	static const int16_t qtbl[128] = {
		0,   9,   17,  25,  33,  41,  49,  57,  65,  73,  81,  89,  97,  105, 113, 121,
		129, 137, 145, 153, 161, 169, 177, 185, 193, 201, 209, 217, 225, 233, 241, 249,
		257, 265, 273, 281, 289, 297, 301, 305, 309, 313, 317, 321, 325, 329, 333, 337,
		341, 345, 349, 353, 357, 361, 365, 369, 373, 377, 381, 385, 389, 393, 397, 401,
		405, 409, 413, 417, 421, 425, 427, 429, 431, 433, 435, 437, 439, 441, 443, 445,
		447, 449, 451, 453, 455, 457, 459, 461, 463, 465, 467, 469, 471, 473, 475, 477,
		479, 481, 482, 483, 484, 485, 486, 487, 488, 489, 490, 491, 492, 493, 494, 495,
		496, 497, 498, 499, 500, 501, 502, 503, 504, 505, 506, 507, 508, 509, 510, 511 };
	sp_f.amp = (sp_f.r[0] & 0x1f) << ((sp_f.r[0] & 0xe0) >> 5);
	sp_f.cnt = 0;
	sp_f.per = sp_f.r[1];
	for (int i = 0; i < 6; i++) {
		uint8_t b = sp_f.r[2 + 2 * i], f = sp_f.r[3 + 2 * i];
		sp_f.b_coef[i] = (b & 0x80) ? qtbl[0x7f & -b] : -qtbl[b];
		sp_f.f_coef[i] = (f & 0x80) ? qtbl[0x7f & -f] : -qtbl[f];
	}
	sp_f.interp = sp_f.r[14] || sp_f.r[15];
}

/* the microsequencer's data formats: len 4 | shift 4 | param 4 | delta | field | clear 5 | clear all */
#define CR(l, s, p, d, f, c5, ca) ((l) | (s) << 4 | (p) << 8 | (d) << 12 | (f) << 13 | (c5) << 14 | (ca) << 15)
enum { AM = 0, PR, B0, F0, B1, F1, B2, F2, B3, F3, B4, F4, B5, F5, IA, IP };
static const uint16_t sp_datafmt[] = {
	CR(0,0,0,0,0,0,1),
	CR(8,0,AM,0,0,0,1), CR(8,0,PR,0,0,0,0), CR(8,0,B0,0,0,0,0), CR(8,0,F0,0,0,0,0), CR(8,0,B1,0,0,0,0),
	CR(8,0,F1,0,0,0,0), CR(8,0,B2,0,0,0,0), CR(8,0,F2,0,0,0,0), CR(8,0,B3,0,0,0,0), CR(8,0,F3,0,0,0,0),
	CR(8,0,B4,0,0,0,0), CR(8,0,F4,0,0,0,0), CR(8,0,B5,0,0,0,0), CR(8,0,F5,0,0,0,0), CR(8,0,IA,0,0,0,0),
	CR(8,0,IP,0,0,0,0),
	/* 17 LOAD_4 */
	CR(6,2,AM,0,0,0,1), CR(8,0,PR,0,0,0,0), CR(4,3,B3,0,0,0,0), CR(6,2,F3,0,0,0,0), CR(7,1,B4,0,0,0,0),
	CR(6,2,F4,0,0,0,0), CR(8,0,B5,0,0,0,0), CR(8,0,F5,0,0,0,0),
	CR(6,2,AM,0,0,0,1), CR(8,0,PR,0,0,0,0), CR(6,1,B3,0,0,0,0), CR(7,1,F3,0,0,0,0), CR(8,0,B4,0,0,0,0),
	CR(8,0,F4,0,0,0,0), CR(8,0,B5,0,0,0,0), CR(8,0,F5,0,0,0,0),
	/* 33 SETMSB_6 */
	CR(0,0,0,0,0,1,0), CR(6,2,AM,0,0,0,0), CR(6,2,F3,0,1,0,0), CR(6,2,F4,0,1,0,0), CR(8,0,F5,0,1,0,0),
	CR(0,0,0,0,0,1,0), CR(6,2,AM,0,0,0,0), CR(7,1,F3,0,1,0,0), CR(8,0,F4,0,1,0,0), CR(8,0,F5,0,1,0,0),
	0, 0,
	/* 45 DELTA_9 */
	CR(4,2,AM,1,0,0,0), CR(5,0,PR,1,0,0,0), CR(3,4,B0,1,0,0,0), CR(3,3,F0,1,0,0,0), CR(3,4,B1,1,0,0,0),
	CR(3,3,F1,1,0,0,0), CR(3,4,B2,1,0,0,0), CR(3,3,F2,1,0,0,0), CR(3,3,B3,1,0,0,0), CR(4,2,F3,1,0,0,0),
	CR(4,1,B4,1,0,0,0), CR(4,2,F4,1,0,0,0), CR(5,0,B5,1,0,0,0), CR(5,0,F5,1,0,0,0),
	CR(4,2,AM,1,0,0,0), CR(5,0,PR,1,0,0,0), CR(4,1,B0,1,0,0,0), CR(4,2,F0,1,0,0,0), CR(4,1,B1,1,0,0,0),
	CR(4,2,F1,1,0,0,0), CR(4,1,B2,1,0,0,0), CR(4,2,F2,1,0,0,0), CR(4,1,B3,1,0,0,0), CR(5,1,F3,1,0,0,0),
	CR(5,0,B4,1,0,0,0), CR(5,0,F4,1,0,0,0), CR(5,0,B5,1,0,0,0), CR(5,0,F5,1,0,0,0),
	/* 73 SETMSB_A */
	CR(0,0,0,0,0,1,0), CR(6,2,AM,0,0,0,0), CR(5,3,F0,0,1,0,0), CR(5,3,F1,0,1,0,0), CR(5,3,F2,0,1,0,0),
	CR(0,0,0,0,0,1,0), CR(6,2,AM,0,0,0,0), CR(6,2,F0,0,1,0,0), CR(6,2,F1,0,1,0,0), CR(6,2,F2,0,1,0,0),
	/* 83 LOAD_2 / LOAD_C mode 00 */
	CR(6,2,AM,0,0,0,1), CR(8,0,PR,0,0,0,0), CR(3,4,B0,0,0,0,0), CR(5,3,F0,0,0,0,0), CR(3,4,B1,0,0,0,0),
	CR(5,3,F1,0,0,0,0), CR(3,4,B2,0,0,0,0), CR(5,3,F2,0,0,0,0), CR(4,3,B3,0,0,0,0), CR(6,2,F3,0,0,0,0),
	CR(7,1,B4,0,0,0,0), CR(6,2,F4,0,0,0,0), CR(5,0,IA,0,0,0,0), CR(5,0,IP,0,0,0,0),
	/* 97 mode 10 */
	CR(6,2,AM,0,0,0,1), CR(8,0,PR,0,0,0,0), CR(6,1,B0,0,0,0,0), CR(6,2,F0,0,0,0,0), CR(6,1,B1,0,0,0,0),
	CR(6,2,F1,0,0,0,0), CR(6,1,B2,0,0,0,0), CR(6,2,F2,0,0,0,0), CR(6,1,B3,0,0,0,0), CR(7,1,F3,0,0,0,0),
	CR(8,0,B4,0,0,0,0), CR(8,0,F4,0,0,0,0), CR(5,0,IA,0,0,0,0), CR(5,0,IP,0,0,0,0),
	/* 111 DELTA_D */
	CR(4,2,AM,1,0,0,0), CR(5,0,PR,1,0,0,0), CR(3,3,B3,1,0,0,0), CR(4,2,F3,1,0,0,0), CR(4,1,B4,1,0,0,0),
	CR(4,2,F4,1,0,0,0), CR(5,0,B5,1,0,0,0), CR(5,0,F5,1,0,0,0),
	CR(4,2,AM,1,0,0,0), CR(5,0,PR,1,0,0,0), CR(4,1,B3,1,0,0,0), CR(5,1,F3,1,0,0,0), CR(5,0,B4,1,0,0,0),
	CR(5,0,F4,1,0,0,0), CR(5,0,B5,1,0,0,0), CR(5,0,F5,1,0,0,0),
	/* 127 LOAD_E */
	CR(6,2,AM,0,0,0,0), CR(8,0,PR,0,0,0,0),
	/* 129 LOAD_2 / LOAD_C mode 01 */
	CR(6,2,AM,0,0,0,1), CR(8,0,PR,0,0,0,0), CR(3,4,B0,0,0,0,0), CR(5,3,F0,0,0,0,0), CR(3,4,B1,0,0,0,0),
	CR(5,3,F1,0,0,0,0), CR(3,4,B2,0,0,0,0), CR(5,3,F2,0,0,0,0), CR(4,3,B3,0,0,0,0), CR(6,2,F3,0,0,0,0),
	CR(7,1,B4,0,0,0,0), CR(6,2,F4,0,0,0,0), CR(8,0,B5,0,0,0,0), CR(8,0,F5,0,0,0,0), CR(5,0,IA,0,0,0,0),
	CR(5,0,IP,0,0,0,0),
	/* 145 mode 11 */
	CR(6,2,AM,0,0,0,1), CR(8,0,PR,0,0,0,0), CR(6,1,B0,0,0,0,0), CR(6,2,F0,0,0,0,0), CR(6,1,B1,0,0,0,0),
	CR(6,2,F1,0,0,0,0), CR(6,1,B2,0,0,0,0), CR(6,2,F2,0,0,0,0), CR(6,1,B3,0,0,0,0), CR(7,1,F3,0,0,0,0),
	CR(8,0,B4,0,0,0,0), CR(8,0,F4,0,0,0,0), CR(8,0,B5,0,0,0,0), CR(8,0,F5,0,0,0,0), CR(5,0,IA,0,0,0,0),
	CR(5,0,IP,0,0,0,0),
	/* 161 SETMSB_3 / SETMSB_5 */
	CR(0,0,0,0,0,1,0), CR(6,2,AM,0,0,0,0), CR(8,0,PR,0,0,0,0), CR(5,3,F0,0,1,0,0), CR(5,3,F1,0,1,0,0),
	CR(5,3,F2,0,1,0,0), CR(5,0,IA,0,0,0,0), CR(5,0,IP,0,0,0,0),
	CR(0,0,0,0,0,1,0), CR(6,2,AM,0,0,0,0), CR(8,0,PR,0,0,0,0), CR(6,2,F0,0,1,0,0), CR(6,2,F1,0,1,0,0),
	CR(6,2,F2,0,1,0,0), CR(5,0,IA,0,0,0,0), CR(5,0,IP,0,0,0,0),
};
static const int16_t sp_df_idx[16 * 8] = {
	-1, -1,  -1, -1,  -1, -1,  -1, -1,          /* 0000 */
	-1, -1,  -1, -1,  -1, -1,  -1, -1,          /* 1000 */
	17, 22,  17, 24,  25, 30,  25, 32,          /* 0100 */
	83, 94,  129,142, 97, 108, 145,158,         /* 1100 */
	83, 96,  129,144, 97, 110, 145,160,         /* 0010 */
	73, 77,  74, 77,  78, 82,  79, 82,          /* 1010 */
	33, 36,  34, 37,  38, 41,  39, 42,          /* 0110 */
	127,128, 127,128, 127,128, 127,128,         /* 1110 */
	1,  14,  1,  16,  1,  14,  1,  16,          /* 0001 */
	45, 56,  45, 58,  59, 70,  59, 72,          /* 1001 */
	161,166, 162,166, 169,174, 170,174,         /* 0101 */
	111,116, 111,118, 119,124, 119,126,         /* 1101 */
	161,168, 162,168, 169,176, 170,176,         /* 0011 */
	-1, -1,  -1, -1,  -1, -1,  -1, -1,          /* 1011 */
	-1, -1,  -1, -1,  -1, -1,  -1, -1,          /* 0111 */
	0,  0,   0,  0,   0,  0,   0,  0            /* 1111 */
};

static uint32_t sp_getb (int len)
{
	uint32_t d0 = sp_rom[(sp_pc >> 3) & 0xffff], d1 = sp_rom[((sp_pc + 8) >> 3) & 0xffff];
	uint32_t data = ((d1 << 8) | d0) >> (sp_pc & 7);
	sp_pc += len;
	return data & ((1u << len) - 1);
}

static void sp_micro (void)
{
	while (sp_f.rpt <= 0) {
		if (sp_halted && !sp_lrq) {
			sp_pc = sp_ald | (0x1000 << 3);
			sp_halted = 0; sp_lrq = 1; sp_ald = 0;
			memset (sp_f.r, 0, sizeof sp_f.r);
		}
		if (sp_halted) {
			sp_f.rpt = 1; sp_lrq = 1; sp_ald = 0;
			memset (sp_f.r, 0, sizeof sp_f.r);
			return;
		}
		uint8_t immed4 = (uint8_t) sp_getb (4), opcode = (uint8_t) sp_getb (4);
		int repeat = 0, ctrl_xfer = 0;
		switch (opcode) {
		case 0x0:                                       /* RTS / SETPAGE */
			if (immed4) sp_page = sp_rev32 (immed4) >> 13;
			else {
				int btrg = sp_stack; sp_stack = 0;
				if (!btrg) { sp_halted = 1; sp_pc = 0; } else sp_pc = btrg;
				ctrl_xfer = 1;
			}
			break;
		case 0xE: case 0xD: {                           /* JMP, JSR */
			uint32_t btrg = sp_page | (sp_rev32 (immed4) >> 17) | (sp_rev32 (sp_getb (8)) >> 21);
			ctrl_xfer = 1;
			if (opcode == 0xD) sp_stack = (sp_pc + 7) & ~7;
			sp_pc = (int) btrg;
			break;
		}
		case 0x1:                                       /* SETMODE */
			sp_mode = ((immed4 & 8) >> 2) | (immed4 & 4) | ((immed4 & 3) << 4);
			break;
		default:
			repeat = immed4 | (sp_mode & 0x30);
			break;
		}
		if (opcode != 1) sp_mode &= 0xF;
		if (ctrl_xfer) continue;
		if (!repeat) continue;
		sp_f.rpt = repeat + 1;
		int i = (opcode << 3) | (sp_mode & 6);
		int idx0 = sp_df_idx[i], idx1 = sp_df_idx[i + 1];
		if (idx0 < 0 || idx1 < idx0) { sp_halted = 1; continue; }
		for (i = idx0; i <= idx1; i++) {
			uint16_t cr = sp_datafmt[i];
			int len = cr & 15, shf = (cr >> 4) & 15, prm = (cr >> 8) & 15;
			int8_t value;
			if (cr & 0x8000) { memset (sp_f.r, 0, sizeof sp_f.r); sp_silent = 1; }
			if (cr & 0x4000) sp_f.r[B5] = sp_f.r[F5] = 0;
			if (!len) continue;
			value = (int8_t) sp_getb (len);
			if ((cr & 0x1000) && (value & (1 << (len - 1)))) value = (int8_t) (value | (-1 << len));
			if (shf) value = (int8_t) (value << shf);
			sp_silent = 0;
			if (cr & 0x2000) { sp_f.r[prm] &= (uint8_t) ~(~0u << shf); sp_f.r[prm] |= (uint8_t) value; continue; }
			if (cr & 0x1000) { sp_f.r[prm] = (uint8_t) (sp_f.r[prm] + value); continue; }
			sp_f.r[prm] = (uint8_t) value;
		}
		if (opcode == 0xF) { sp_silent = 1; sp_f.r[1] = SP_PER_PAUSE; }
		sp_regdec ();
		if (vt_vecvoice == 2) {
			/* the VecVox: the frame's own length in samples, stretched; voiced frames at the SpeakJet's pitch */
			double n = (double) (sp_f.rpt - 1) * (sp_f.per ? sp_f.per : 64) * sj_scale;
			if (sp_f.per && !sp_silent && sj_per) sp_f.per = (uint32_t) sj_per;
			int unit = sp_f.per ? (int) sp_f.per : 64;
			int r = (int) (n / unit + 0.5);
			sp_f.rpt = (r < 1 ? 1 : r) + 1;
		}
		break;
	}
}

/* the next 10 kHz sample */
static int16_t sp_sample (void)
{
	int16_t s = 0;
	for (int tries = 0; tries < 4; tries++) {
		if (sp_f.rpt <= 0) sp_micro ();
		if (sp_silent && sp_f.rpt <= 0) return 0;
		if (sp_halted) { sp_f.rpt = 0; return 0; }
		if (sp_update (&s)) return s;
	}
	return 0;
}

/* an allophone from the serial link (dropped while one is waiting, as on the chip) */
static void sp_ald_w (unsigned a)
{
	if (!sp_lrq) return;
	sp_lrq = 0;
	sp_ald = (int) (a & 0x3f) << 4;
}


static int sp_busy (void) { return !sp_halted || !sp_lrq; }

static void speech_byte (unsigned b);           /* (speakjet.c: the byte to the device plugged in) */

/* ---------- the serial link (the MiSTer core's receiver: the baud counter restarts at every edge) ---------- */
static int ser_line = 1, ser_div, ser_cnt;
static unsigned ser_shift;
static unsigned long long ser_t;                /* CPU cycle the receiver has been stepped to */
static void ser_run (unsigned long long t)
{
	while (ser_t < t) {
		/* the next event in this level: a sample at 78, the byte check at 96, the wrap at 155 */
		int next = ser_div < 78 ? 78 : ser_div < 96 ? 96 : 156;
		unsigned long long left = t - ser_t;
		if ((unsigned long long) (next - ser_div) > left) { ser_div += (int) left; ser_t = t; break; }
		ser_t += (unsigned long long) (next - ser_div);
		ser_div = next;
		if (ser_div == 78) {
			ser_shift = (ser_shift >> 1) | (ser_line ? 0x80 : 0);
			int c = ser_cnt;
			if (c == 0 && !ser_line) ser_cnt = 1;
			if (c > 0) ser_cnt = c + 1;
			if (c == 10) ser_cnt = 0;
		} else if (ser_div == 96) {
			if (ser_cnt == 9) speech_byte (ser_shift);
		} else ser_div = 0;
	}
}
/* the line (port A bit 4 when port A drives, else high) at the current CPU cycle */
static void ser_set (int level, unsigned long long t)
{
	ser_run (t);
	if (level != ser_line) { ser_line = level; ser_div = 0; }
}
