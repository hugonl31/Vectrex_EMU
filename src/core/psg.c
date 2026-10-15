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
	psg_acc += out; psg_accn++;
	psg_frac += PSG_RATE;
	if (psg_frac >= PSG_TICK) {
		psg_frac -= PSG_TICK;
		psg_emit (psg_acc / psg_accn / 3.0);
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

/* vtest: step to now and take up to max samples */
int psg_take (int16_t *buf, int max)
{
	psg_advance (vx_cyc);
	int n = 0;
	while (psg_tail != psg_head && n < max) { buf[n++] = psg_ring[psg_tail]; psg_tail = (psg_tail + 1) & 32767; }
	return n;
}
