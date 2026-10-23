/* uvm2_cart.c - the Ultimate Vectrex Multicart 2 in the cartridge slot (included by vecx.c).
 *
 * A .um2 game is native ARM code on the cartridge's RP2350 (rp2350.c). It holds the 6809 in /HALT for
 * good and becomes the bus master: one access per E period, the address presented while E is low and
 * held through E high, a write latched as E falls, a read sampled just after (the UVMC2 SDK's
 * uvm2_bus.c and its notes, measured on the console). So here, per E period:
 *
 *   E low   ~E (GPIO31) high: the chip runs half a period and presents its next access
 *   E high  ~E low: a read is answered (the VIA, RAM or ROM drive D0-D7 from now until E low is over),
 *           the chip runs the other half
 *   E falls the pins are sampled: a write lands
 *
 * and then the console's own cycle (the VIA, the analog side, the sound chip) runs exactly as for any
 * cartridge. The 6809 does not run at all in this mode.
 *
 * GPIO map (UVMC2): D0-D7 GP0-7, A0-A13 GP8-21, PB6 GP22, /IRQ GP23, A14 GP24, A15 GP25, R/W GP26,
 * /HALT GP27, /NMI GP29, ~E GP31; the SD card GP34-39, the PT8211 audio DAC GP43-45. */
#include "rp2350.h"

int vt_uvm2;                                /* a .um2 game in the slot */
static rp2350_t *uvm2;
static uint32_t uvm2_frac;                  /* (system cycles per E period, 1/256 parts) */
static uint64_t uvm2_ext;                   /* what the console drives (the data bus during a read) */
static int uvm2_rd_valid;
static unsigned long long uvm2_cycles;
unsigned long long uvm2_cycles_pub;
static int uvm2_fault_told;
static unsigned long uvm2_n_halt, uvm2_n_rd, uvm2_n_wr, uvm2_n_via;

static uint8_t uvm2_image[0x90000]; static size_t uvm2_image_n;

/* power on: the chip starts the game afresh (the multicart's firmware would load it from the card) */
void uvm2_power_on (void)
{
	if (!uvm2) return;
	char err[200];
	rp_destroy (uvm2);
	uvm2 = rp_create ();
	if (!uvm2 || rp_load_um2 (uvm2, uvm2_image, uvm2_image_n, err, sizeof err)) {
		fprintf (stderr, "UVMC2: %s\n", uvm2 ? err : "no memory"); vt_uvm2 = 0; return;
	}
	const char *sd = getenv ("VT_UVM2_SD");
	if (sd) rp_set_sd (uvm2, sd);
	uvm2_fault_told = 0; uvm2_rd_valid = 0;
}

int uvm2_open (const char *path)
{
	FILE *f = fopen (path, "rb");
	if (!f) { perror (path); return -1; }
	uint8_t *buf = uvm2_image;
	size_t n = fread (buf, 1, sizeof uvm2_image, f);
	uvm2_image_n = n;
	fclose (f);
	uvm2 = rp_create ();
	char err[200];
	if (!uvm2 || rp_load_um2 (uvm2, buf, n, err, sizeof err)) { fprintf (stderr, "UVMC2: %s\n", uvm2 ? err : "no memory"); return -1; }
	const char *sd = getenv ("VT_UVM2_SD");
	if (sd) rp_set_sd (uvm2, sd);
	vt_uvm2 = 1;
	return 0;
}

static void uvm2_drive (int clk)
{
	uint64_t mask = (1ull << 31) | (1ull << 22) | (1ull << 23) | (1ull << 29);
	uint64_t val = 0;
	if (clk) val |= 1ull << 31;
	if (!(via_ddrb & 0x40) || (via_orb & 0x40)) val |= 1ull << 22;             /* PB6 (pull-up) */
	if (!(via_ifr & 0x80)) val |= 1ull << 23;                                  /* /IRQ */
	val |= 1ull << 29;                                                          /* /NMI */
	if (uvm2_rd_valid) {                                                        /* the data bus, unless the chip drives it */
		uint64_t oe; rp_pins (uvm2, &oe);
		uint64_t dm = 0xffull & ~oe;
		mask |= dm; val |= uvm2_ext & dm;
	}
	rp_set_ext (uvm2, mask, val);
}

/* one E period: returns 1 when the chip holds the 6809 */
static int uvm2_cycle (void)
{
	uint32_t hz = rp_sys_hz (uvm2);
	uvm2_frac += (uint32_t) ((uint64_t) hz * 256 / 1500000);
	uint32_t per = uvm2_frac >> 8; uvm2_frac &= 255;
	uint32_t h1 = per / 2, h2 = per - h1;
	/* E low: ~E high; the read data of the last period is still on the bus for a moment, then gone */
	uvm2_drive (1);                                 /* (the read data held through E low: only where the chip does not drive) */
	rp_run (uvm2, h1);
	uvm2_rd_valid = 0;
	/* E high: the access the chip presents */
	uint64_t oe, v = rp_pins (uvm2, &oe);
	int halted = ((oe >> 27) & 1) && !((v >> 27) & 1);
	unsigned addr = (unsigned) (((v >> 8) & 0x3fff) | (((v >> 24) & 1) << 14) | (((v >> 25) & 1) << 15));
	int rw = !((oe >> 26) & 1) || ((v >> 26) & 1);
	if (halted && rw) {
		uvm2_ext = read8 (addr & 0xffff); uvm2_n_rd++;
		uvm2_rd_valid = 1;
	}
	uvm2_drive (0);
	rp_run (uvm2, h2);
	/* E falls: a write lands */
	v = rp_pins (uvm2, &oe);
	halted = ((oe >> 27) & 1) && !((v >> 27) & 1);
	addr = (unsigned) (((v >> 8) & 0x3fff) | (((v >> 24) & 1) << 14) | (((v >> 25) & 1) << 15));
	rw = !((oe >> 26) & 1) || ((v >> 26) & 1);
	if (halted) uvm2_n_halt++;
	{ static long t0 = -2, tn; if (t0 == -2) { const char *z = getenv ("VT_UVM2TRACE"); t0 = -1; if (z) sscanf (z, "%ld,%ld", &t0, &tn); }
	  if (t0 >= 0 && (long) uvm2_cycles >= t0 && (long) uvm2_cycles < t0 + tn)
	  { fprintf (stderr, "%llu %s %04X %02X%s\n", uvm2_cycles, rw ? "R" : "W", addr, (unsigned) (rw ? uvm2_ext : (v & 0xff)), halted ? "" : " (not halted)");
		extern uint64_t rp_sio_mask (rp2350_t *), rp_gpio_out (rp2350_t *);
		if (getenv ("VT_UVM2PINS")) fprintf (stderr, "   sio %012llX out %012llX pins %012llX oe %012llX\n", (unsigned long long) rp_sio_mask (uvm2), (unsigned long long) rp_gpio_out (uvm2), (unsigned long long) v, (unsigned long long) oe); } }
	if (halted && !rw && (addr & 0xc000) == 0xc000) { write8 (addr & 0xffff, (unsigned char) (v & 0xff)); uvm2_n_wr++; if ((addr & 0xf800) == 0xd000) uvm2_n_via++; }
	uvm2_cycles++; uvm2_cycles_pub = uvm2_cycles;
	{ static int lg = -1; if (lg < 0) lg = getenv ("VT_RPLOG") != NULL;
	  if (lg && uvm2_cycles % 1500000 == 0) { extern void rp_debug (rp2350_t *); rp_debug (uvm2); extern unsigned long vt_nlines; fprintf (stderr, "lines %lu  ", vt_nlines); { extern uint32_t rp_gpio_ctrl (rp2350_t *, int); fprintf (stderr, "gpio31 ctrl %08X gpio27 %08X  ", rp_gpio_ctrl (uvm2, 31), rp_gpio_ctrl (uvm2, 27)); } fprintf (stderr, "bus: halted %lu reads %lu writes %lu (VIA %lu)\n", uvm2_n_halt, uvm2_n_rd, uvm2_n_wr, uvm2_n_via); } }
	if (!uvm2_fault_told) {
		const char *fl = rp_fault (uvm2);
		if (fl) { fprintf (stderr, "UVMC2: %s\n", fl); uvm2_fault_told = 1; }
	}
	return halted;
}

/* the headphone jack's samples (stereo 16 bit) since the last call, and their rate */
int uvm2_audio_take (int16_t *buf, int max_frames, int *rate)
{
	if (!uvm2) return 0;
	return rp_audio_take (uvm2, buf, max_frames, rate);
}

/* the headphone jack for the frontend: mono (left + right) at 44100 Hz, n samples (as many as the console's
 * own sound gave for the same time), resampled from the DAC's rate; half scale (headroom, as the speech) */
int uvm2_jack44 (int16_t *out, int n)
{
	static int16_t in[16384]; static int nin; static double pos; static int rate = 32000;
	if (!uvm2) { memset (out, 0, (size_t) n * 2); return 0; }
	int16_t st[4096]; int r = 0;
	int got = rp_audio_take (uvm2, st, 2048, &r);
	if (r > 8000 && r < 96000) rate = r;
	for (int i = 0; i < got && nin < 16384; i++) in[nin++] = (int16_t) (((int) st[2 * i] + st[2 * i + 1]) / 4);
	double step = (double) rate / 44100.0;
	int k;
	for (k = 0; k < n; k++) {
		int i0 = (int) pos;
		if (i0 + 1 >= nin) break;
		double f = pos - i0;
		out[k] = (int16_t) (in[i0] + (in[i0 + 1] - in[i0]) * f);
		pos += step;
	}
	for (int j = k; j < n; j++) out[j] = 0;                    /* (not enough yet: silence) */
	int used = (int) pos; if (used > nin) used = nin;
	memmove (in, in + used, (size_t) (nin - used) * 2); nin -= used; pos -= used;
	if (nin > 8000) { memmove (in, in + nin - 4000, 4000 * 2); nin = 4000; }   /* (never far behind) */
	return k;
}
