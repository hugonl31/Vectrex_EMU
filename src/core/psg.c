/* psg.c - AY-3-8912 sound for vtest (included by vecx.c).
 *
 * The Vectrex PSG runs from the 1.5 MHz system clock. The chip is stepped at its internal rate
 * (clock / 8 = 187.5 kHz: tone counters, and noise / envelope at clock / 16), each register write
 * is applied at the CPU cycle it happens, and the output is box-filtered down to 44.1 kHz, then a
 * DC-blocking high-pass (the console's AC-coupled amplifier: no clicks when channels switch) and a
 * gentle low-pass (the small speaker). 16-bit signed mono.
 */
#include <stdint.h>

#define PSG_RATE 44100
#define PSG_TICK 187500                      /* chip steps per second (CPU cycles / 8) */

static unsigned char psg_r[16];
static int psg_tcnt[3], psg_tout[3];
static int psg_ncnt, psg_npre, psg_lfsr = 1, psg_nout;
static int psg_ecnt, psg_estep = 15, psg_att, psg_alt, psg_hold, psg_holding, psg_evol;
static unsigned long long psg_t;             /* CPU cycle the chip has been stepped to */
static double psg_acc; static int psg_accn; static long psg_frac;
static double psg_hp_x, psg_hp_y, psg_lp;
static int16_t psg_ring[32768]; static int psg_head, psg_tail;
/* the direct DAC sound: with the multiplexer on its sound input (PB0 = 0, PB1-2 = 11) the DAC's value
 * (port A, signed) reaches the amplifier and is held there when the multiplexer moves on - how games
 * play samples and speech. Mix: the DAC x 6 against the AY's three channels at 10 bits, the balance the
 * MiSTer core measured on a real Vectrex's line-out (Videodr0me, 2026). */
static int psg_dac;
/* the speech module: its 10 kHz samples, interpolated to the chip's rate, mixed as the MiSTer core does
 * (tested there against real hardware): the synthesizer's sample / 16 (+-511) through Sorgelig's audio
 * compressor (a soft curve: x1.2 when quiet, flattening to 255; GPL-2.0-or-later), x1.5 ("Normal"),
 * against the console's (AY + 6 x DAC) / 4 - in our scale the AY's full output = 1.0 = 256 there */
static int sp_prev, sp_next; static long sp_phase;
static const unsigned char sp_comp[512] = {
	  0,   1,   2,   3,   4,   6,   7,   8,   9,  11,  12,  13,  14,  15,  17,  18,
	 19,  20,  22,  23,  24,  25,  27,  28,  29,  30,  31,  33,  34,  35,  36,  37,
	 39,  40,  41,  42,  44,  45,  46,  47,  48,  50,  51,  52,  53,  54,  55,  57,
	 58,  59,  60,  61,  63,  64,  65,  66,  67,  68,  70,  71,  72,  73,  74,  75,
	 77,  78,  79,  80,  81,  82,  83,  85,  86,  87,  88,  89,  90,  91,  92,  94,
	 95,  96,  97,  98,  99, 100, 101, 102, 103, 105, 106, 107, 108, 109, 110, 111,
	112, 113, 114, 115, 116, 117, 118, 119, 120, 121, 122, 123, 124, 125, 126, 127,
	128, 129, 130, 131, 132, 133, 134, 135, 136, 137, 138, 139, 140, 141, 142, 143,
	144, 145, 146, 147, 148, 149, 150, 150, 151, 152, 153, 154, 155, 156, 157, 158,
	158, 159, 160, 161, 162, 163, 164, 164, 165, 166, 167, 168, 169, 169, 170, 171,
	172, 173, 173, 174, 175, 176, 177, 177, 178, 179, 180, 180, 181, 182, 183, 183,
	184, 185, 185, 186, 187, 188, 188, 189, 190, 190, 191, 192, 192, 193, 194, 194,
	195, 196, 196, 197, 198, 198, 199, 200, 200, 201, 201, 202, 203, 203, 204, 204,
	205, 206, 206, 207, 207, 208, 208, 209, 210, 210, 211, 211, 212, 212, 213, 213,
	214, 214, 215, 215, 216, 216, 217, 217, 218, 218, 219, 219, 220, 220, 221, 221,
	222, 222, 222, 223, 223, 224, 224, 225, 225, 225, 226, 226, 227, 227, 227, 228,
	228, 229, 229, 229, 230, 230, 230, 231, 231, 231, 232, 232, 233, 233, 233, 234,
	234, 234, 234, 235, 235, 235, 236, 236, 236, 237, 237, 237, 238, 238, 238, 238,
	239, 239, 239, 239, 240, 240, 240, 240, 241, 241, 241, 241, 242, 242, 242, 242,
	243, 243, 243, 243, 243, 244, 244, 244, 244, 244, 245, 245, 245, 245, 245, 246,
	246, 246, 246, 246, 246, 247, 247, 247, 247, 247, 247, 248, 248, 248, 248, 248,
	248, 248, 249, 249, 249, 249, 249, 249, 249, 249, 250, 250, 250, 250, 250, 250,
	250, 250, 250, 251, 251, 251, 251, 251, 251, 251, 251, 251, 251, 251, 252, 252,
	252, 252, 252, 252, 252, 252, 252, 252, 252, 252, 252, 252, 252, 253, 253, 253,
	253, 253, 253, 253, 253, 253, 253, 253, 253, 253, 253, 253, 253, 253, 253, 253,
	253, 253, 254, 254, 254, 254, 254, 254, 254, 254, 254, 254, 254, 254, 254, 254,
	254, 254, 254, 254, 254, 254, 254, 254, 254, 254, 254, 254, 254, 254, 254, 254,
	254, 254, 254, 254, 254, 254, 254, 254, 254, 254, 254, 254, 254, 254, 254, 254,
	254, 254, 254, 254, 254, 254, 254, 254, 254, 254, 254, 254, 254, 254, 254, 254,
	254, 254, 254, 254, 254, 254, 254, 254, 254, 254, 254, 254, 254, 254, 254, 254,
	254, 254, 254, 254, 254, 254, 254, 254, 254, 254, 254, 254, 254, 254, 254, 254,
	254, 254, 254, 254, 254, 254, 254, 254, 254, 254, 254, 254, 254, 254, 254, 255,
};
static double sp_mix (double x)                 /* (x: the synthesizer's int16 output = its sample x 4) */
{
	int v = (int) (x / 64.0), neg = v < 0;
	if (neg) v = -v;
	if (v > 511) v = 511;
	return (neg ? -1 : 1) * sp_comp[v] * 1.5 / 256.0;
}
/* VT_SPEECHOUT=1 (the frontend): the speech module's sound on its own channel - it has its own amplifier
 * and speaker output, so it skips the console's filters and the built-in speaker (psg_take_speech) */
int vt_speech_sep;
static double sp_acc2, sp_hp_x, sp_hp_y;
static int16_t sp_ring[32768]; static int sp_head, sp_tail;
extern int vt_imager; static double im_motor_sample (void);   /* (imager.c: the 3D Imager's motor, on the same channel) */

/* AY output levels (logarithmic DAC), normalised */
static const double psg_vol[16] = { 0.0, 0.0106, 0.0150, 0.0222, 0.0320, 0.0466, 0.0665, 0.1039,
                                    0.1237, 0.1986, 0.2803, 0.3548, 0.4702, 0.6008, 0.7999, 1.0 };

static void psg_env_restart (void)
{
	int s = psg_r[13] & 15;
	psg_att = (s & 4) ? 15 : 0;
	if (!(s & 8)) { psg_hold = 1; psg_alt = psg_att; } else { psg_hold = s & 1; psg_alt = s & 2; }
	psg_estep = 15; psg_holding = 0; psg_ecnt = 0;
	psg_evol = psg_estep ^ psg_att;
}

static void psg_env_step (void)
{
	if (psg_holding) return;
	psg_estep--;
	if (psg_estep < 0) {
		if (psg_hold) { if (psg_alt) psg_att ^= 15; psg_holding = 1; psg_estep = 0; }
		else { if (psg_alt) psg_att ^= 15; psg_estep &= 15; }
	}
	psg_evol = psg_estep ^ psg_att;
}

static void psg_emit (double v)
{
	/* DC blocker (~20 Hz) then a one-pole low-pass (~9 kHz) */
	double hp = v - psg_hp_x + 0.9972 * psg_hp_y;
	psg_hp_x = v; psg_hp_y = hp;
	psg_lp += (hp - psg_lp) * 0.72;
	double s = psg_lp * 34000.0;
	if (s > 32767) s = 32767; if (s < -32768) s = -32768;
	int n = (psg_head + 1) & 32767;
	if (n != psg_tail) { psg_ring[psg_head] = (int16_t) s; psg_head = n; }
}

static void psg_tick (void)
{
	unsigned char *r = psg_r;
	for (int c = 0; c < 3; c++) {
		int p = r[c * 2] | ((r[c * 2 + 1] & 15) << 8);
		if (p == 0) p = 1;
		if (++psg_tcnt[c] >= p) { psg_tcnt[c] = 0; psg_tout[c] ^= 1; }
	}
	psg_npre ^= 1;
	if (!psg_npre) {                               /* clock / 16 */
		int np = r[6] & 31; if (np == 0) np = 1;
		if (++psg_ncnt >= np) {
			psg_ncnt = 0;
			psg_lfsr = (psg_lfsr >> 1) | (((psg_lfsr ^ (psg_lfsr >> 3)) & 1) << 16);
			psg_nout = psg_lfsr & 1;
		}
		int ep = r[11] | (r[12] << 8); if (ep == 0) ep = 1;
		if (++psg_ecnt >= ep) { psg_ecnt = 0; psg_env_step (); }
	}
	double out = 0;
	for (int c = 0; c < 3; c++) {
		int tone = psg_tout[c] | ((r[7] >> c) & 1);
		int noise = psg_nout | ((r[7] >> (c + 3)) & 1);
		if (tone & noise) {
			int v = r[8 + c] & 0x10 ? psg_evol : r[8 + c] & 15;
			out += psg_vol[v];
		}
	}
	out = out / 3.0 + psg_dac * (6.0 / 1023.0);
	if (vt_vecvoice) {
		sp_phase += speech_rate ();
		if (sp_phase >= PSG_TICK) { sp_phase -= PSG_TICK; sp_prev = sp_next; sp_next = speech_sample (); }
		double spv = sp_mix (sp_prev + (sp_next - sp_prev) * (double) sp_phase / PSG_TICK);
		if (vt_speech_sep) sp_acc2 += spv; else out += spv;
	}
	psg_acc += out; psg_accn++;
	psg_frac += PSG_RATE;
	if (psg_frac >= PSG_TICK) {
		psg_frac -= PSG_TICK;
		if (vt_speech_sep) {                     /* (the module's output: only its coupling capacitor, ~20 Hz) */
			double v = sp_acc2 / psg_accn, hp = v - sp_hp_x + 0.9972 * sp_hp_y;
			sp_hp_x = v; sp_hp_y = hp;
			double s = hp * 17000.0;             /* (half the console's scale: MiSTer's mix goes up to 2x its
			                                        full level - the frontend doubles it into a soft limit) */
			if (s > 32767) s = 32767; if (s < -32768) s = -32768;
			int n2 = (sp_head + 1) & 32767;
			if (n2 != sp_tail) { sp_ring[sp_head] = (int16_t) s; sp_head = n2; }
			sp_acc2 = 0;
		}
		if (vt_imager && !vt_vecvoice) {         /* (the goggles' motor: its own sound, not the Vectrex's speaker) */
			double s = im_motor_sample () * 16000.0;
			if (s > 32767) s = 32767; if (s < -32768) s = -32768;
			int n2 = (sp_head + 1) & 32767;
			if (n2 != sp_tail) { sp_ring[sp_head] = (int16_t) s; sp_head = n2; }
		}
		psg_emit (psg_acc / psg_accn);
		psg_acc = 0; psg_accn = 0;
	}
}

/* step the chip up to CPU cycle t */
static void psg_advance (unsigned long long t)
{
	while (psg_t + 8 <= t) { psg_tick (); psg_t += 8; }
}

/* a register write at the current CPU cycle */
static void psg_write (int reg, int val)
{
	psg_advance (vx_cyc);
	psg_r[reg & 15] = val;
	if ((reg & 15) == 13) psg_env_restart ();
}

/* the DAC reaches the sound line (signed -128..127) at the current CPU cycle */
static void psg_dac_set (int v)
{
	if (v == psg_dac) return;
	psg_advance (vx_cyc);
	psg_dac = v;
}

/* the speech module's channel: as many samples as psg_take just gave (the two rings run in step) */
int psg_take_speech (int16_t *buf, int max)
{
	int n = 0;
	while (sp_tail != sp_head && n < max) { buf[n++] = sp_ring[sp_tail]; sp_tail = (sp_tail + 1) & 32767; }
	while (n < max) buf[n++] = 0;
	return n;
}

/* vtest: step to now and take up to max samples */
int psg_take (int16_t *buf, int max)
{
	psg_advance (vx_cyc);
	int n = 0;
	while (psg_tail != psg_head && n < max) { buf[n++] = psg_ring[psg_tail]; psg_tail = (psg_tail + 1) & 32767; }
	return n;
}
