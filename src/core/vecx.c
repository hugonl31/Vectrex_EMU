#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <string.h>
#include <time.h>
#include "e6809.h"
#include "vecx.h"
#include "e8910.h"

#define einline inline

unsigned char rom[8192];
/* A cartridge with RAM of its own (VT_CARTHW=ram, or found by the title: AnimAction): 8K at $2000-$3FFF, beside the
 * ROM at $0000-$1FFF - AnimAction keeps its animations there (MAME: the "SRAM" cartridge type) */
int vt_cart_ram; unsigned char vt_cram[0x2000];
unsigned char cart[1048576];  /* 32K / 64K (PB6) images, 256K: the 4-bank PB6 + IRQ cart (Vectorblade), 512K: the 512K latch cart, 1M: the multicart */
unsigned long vt_bank_switches = 0;
void vt_line(long x0,long y0,long x1,long y1,unsigned char c,unsigned cyc,unsigned t0);
unsigned long long vt_frame_t0;   /* alg_cycles when this frame's drawing started (vtest sets it) */
unsigned long long vt_piece_t0;   /* alg_cycles when the frame (piece) being collected began: the lines' times count
                                   * from it, and the live frame header sends it (vtest sets it at every cut) */
void vt_step(unsigned icycles);
void vt_bankchange(int from,int to);
static unsigned char ram[1024];

/* the sound chip registers */

unsigned snd_regs[16];
static unsigned snd_select;
static unsigned snd_ioa = 0xff;               /* the AY's port A output latch (register 14 written) */
extern int vt_imager; static void im_motor (int level); static unsigned long long im_pin_until;   /* (the 3D Imager, imager.c) */
extern int vt_lightpen; static unsigned long long lp_pin_until; static void lp_set (int what, double v);   /* (the light pen, imager.c) */

/* the via 6522 registers */

static unsigned via_ora;
static unsigned via_orb;
static unsigned via_ddra;
static unsigned via_ddrb;
static unsigned via_t1on;  /* is timer 1 on? */
static unsigned via_t1int; /* are timer 1 interrupts allowed? */
static unsigned via_t1c;
static unsigned via_t1ll;
static unsigned via_t1lh;
static unsigned via_t1pb7; /* timer 1 controlled version of pb7 */
static unsigned via_t2on;  /* is timer 2 on? */
static unsigned via_t2int; /* are timer 2 interrupts allowed? */
static unsigned via_t2c;
static unsigned via_t2ll;
static unsigned via_sr;
static unsigned sr_flagwait;            /* cycles until the shift register flag (see via_sstep0) */
static unsigned via_srb;   /* number of bits shifted so far */
static unsigned via_src;   /* shift counter */
static unsigned via_srclk;
static unsigned via_srphase;      /* system-clock shifting: every other cycle */
int vt_sr1;
static unsigned via_acr;
static unsigned via_pcr;
static unsigned via_ifr;
static unsigned via_ier;
static unsigned via_ca2;
static unsigned via_cb2h;  /* basic handshake version of cb2 */
static unsigned via_cb2s;  /* version of cb2 controlled by the shift register */

unsigned e6809_get_pc (void);
#include "ds2431.c"
extern int vt_uvm2;                      /* (uvm2_cart.c) */
extern int vt_pitrex;                    /* (pitrex_cart.c) */
#include "sp0256.c"
#include "speakjet.c"
#include "psg.c"

/* PB6 -> EPROM A15 (and the optional DS2431). 3.3K pull-up to VCC: when DDRB bit6 = input, the
 * line is high unless the DS2431 pulls it low. */
static int cart_bank (void)
{
	if (via_ddrb & 0x40) return (via_orb >> 6) & 1;
	return ow_line ();
}
int vt_cart_mirror;
/* VIA accesses outside $D000-$D00F: the chip repeats every 16 bytes, so code that meant a RAM
 * variable at $C8xx but ran with DP=$D0 lands on a VIA register (a direct-page bug) */
unsigned long vt_via_mirror; unsigned vt_via_mirror_pc, vt_via_mirror_addr;
static void via_mirror (unsigned address)
{
	if ((address & 0xfff0) != 0xd000) {
		if (!vt_via_mirror++) { vt_via_mirror_pc = e6809_get_pc () & 0xffff; vt_via_mirror_addr = address & 0xffff; }
	}
}                 /* both 32K halves identical: A15 changes do not matter */


/* The 4-bank cartridge (Malban's Vectorblade: an SST39SF020A, 256K): two bank lines, PB6 and the
 * cartridge port's /IRQ (the VIA's IRQ output: low while an enabled interrupt flag is set - the game
 * sets one with interrupts masked in the CPU to pull it low). Bank = PB6 + 2 x (/IRQ high); every bank
 * is 64K of the chip, of which the cartridge shows $0000-$BFFF (48K). Power-on: bank 3. */
int vt_vb;                          /* set by the loader for images over 64K */
/* Sean Kelly's Multi-Cart (3.0, 1 MB): a latch written at $7FFF picks the 4K page of the flash the
 * cartridge window starts at (games sit on 4K / 8K / 16K / 32K boundaries); power-on: page 0 (the menu) */
int vt_multicart; unsigned vt_mc_page;
/* The 512K latch cartridge (Hugo Bokhorst's v2 board: an SST39SF040, 512K): A15 = PB6, A16-A18 = a 74HC377
 * whose clock is /CART OR A14 - a write to $0000-$3FFF stores the data's bits 0-2 at the end of the
 * cycle (the flash sees the same write first: its address is taken at the start). The window is
 * $0000-$7FFF (/CE = port pin 16), the flash's /WE = R/W. The latch has no reset: power-on = any bank
 * (VT_HBLATCH=n fixes it), the RESET button leaves it. */
int vt_hb512; unsigned vt_hb_latch;
static int vb_bank (void)
{
	return cart_bank () + ((via_ifr & 0x80) ? 0 : 2);
}
int vt_cur_bank (void) { return vt_vb ? vb_bank () : vt_hb512 ? (int) (vt_hb_latch * 2 + cart_bank ()) : cart_bank (); }
/* where a cartridge address sits in the flash */
static unsigned long flash_at (unsigned address)
{
	if (vt_hb512) return ((unsigned long) vt_hb_latch << 16) | ((unsigned long) cart_bank () << 15) | (address & 0x7fff);
	return ((unsigned long) vb_bank () << 16) | address;
}
/* the flash: SST39SF020A / SST39SF040 command sequences (5555=AA, 2AAA=55, then 5555=A0 program / 80 erase / 90 ID
 * entry / F0 exit; erase: then AA, 55 and 30 at a 4K sector or 10 at 5555 for the whole chip). The
 * chip is instant here (a program / erase is done before the next read), so the game's polling sees
 * the result at once. Changes are kept in vt_flash_dirty; the frontend's core saves them. */
static int fl_step, fl_cmd, fl_id;
int vt_flash_dirty;
static void flash_write (unsigned address, unsigned data)
{
	unsigned a = address & 0x7fff;
	unsigned long at = flash_at (address);
	if (fl_cmd == 0xa0) {                               /* byte program: bits can only go 0 */
		cart[at] &= data; fl_cmd = 0; fl_step = 0; vt_flash_dirty = 1; return;
	}
	if (data == 0xf0) { fl_id = 0; fl_step = 0; fl_cmd = 0; return; }
	switch (fl_step) {
	case 0: fl_step = (a == 0x5555 && data == 0xaa) ? 1 : 0; break;
	case 1: fl_step = (a == 0x2aaa && data == 0x55) ? 2 : 0; break;
	case 2:
		fl_step = 0;
		if (a != 0x5555) break;
		if (data == 0xa0) fl_cmd = 0xa0;
		else if (data == 0x90) fl_id = 1;
		else if (data == 0x80) fl_step = 3;
		break;
	case 3: fl_step = (a == 0x5555 && data == 0xaa) ? 4 : 0; break;
	case 4: fl_step = (a == 0x2aaa && data == 0x55) ? 5 : 0; break;
	case 5:
		fl_step = 0;
		if (data == 0x30) { memset (cart + (at & ~0xfffUL), 0xff, 4096); vt_flash_dirty = 1; }
		else if (data == 0x10 && a == 0x5555) { memset (cart, 0xff, sizeof cart); vt_flash_dirty = 1; }
		break;
	}
}

/* analog devices */

static double alg_rshf;   /* zero ref sample and hold, modelled as an RC-charged capacitor (0.01uF) */
#define ZS 256            /* integrator positions are kept in 1/ZS units so fractional offsets drift */
static double ZREF_K = 0.597;  /* per-cycle charge fraction, tau ~1.1 cycles (VIDE); VT_ZREFK overrides.
                                  0.38 reproduces the owner's Vectrex (cage test photo 2026-10-08:
                                  with CalZero 3 the reference ends ~0.25 DAC low, so loops drawn
                                  after ZeroCal drift right and up ~2 px per 1000 cycles; Print_Str
                                  reloads it with 0 each row, so text lines slant by what is left) */
/* analog offset of the integrators against the zero reference, in DAC units (trimmers R333/R335).
 * 0 = an ideal Vectrex (the game's CAL value ~38 is neutral); VT_ZOFS=0.9 models the owner's
 * Vectrex, on which the default CAL value 3 is neutral. */
static double alg_zofs;
/* per-axis integrator offsets in 1/ZS DAC units (VT_HWOFS="x,y" in DAC units): every ramping
 * cycle drifts the beam by this much. The owner's Vectrex (photo of the title cages, 2026-10-08)
 * drifts right and up: a dotted box drawn as one closed loop ends about 2 px right of and 1.5 px
 * above its start, so its top-left corner shows a cross. */
static long alg_xofs, alg_yofs;
static unsigned alg_rdly_on, alg_rdly_off, alg_rdly_off_t1;
static double rdly_frac, rdly_frac_t1;   /* the stop delays' fractions (VT_RAMPOFF) */
/* the frontend's Tube > Line gaps: the integrators' stop delay in cycles (default 7 - the frontend sends it at the start), live - the gaps at
 * line ends and corners grow with it, as they differ between real Vectrexes */
void vt_set_rampoff (double c)
{
	if (c < 0) c = 0; if (c > 40) c = 40;
	alg_rdly_off = alg_rdly_off_t1 = (unsigned) c;
	rdly_frac = rdly_frac_t1 = c - (unsigned) c;
}
static double alg_calshift;
static double alg_wob50, alg_wob100;     /* mains hum (VT_WOBBLE), see alg_wobble */
int vt_wobble_off;                        /* live: input flags bit 1 */
static double alg_ztau, alg_zk, alg_zlev;
/* the beam's path, cycle by cycle (the beam-trace renderer, see vtest's VT_BEAMTRACE): where the spot is after the
 * cycle (game units, as alg_addline's), the beam level 0..1 lighting it and the Z it was set to. Unset: no cost */
void (*vt_beam_hook) (double x, double y, double lev, unsigned char z, unsigned long long t) = 0;
/* the beam-trace mode (vt_set_analog 11, VT_BEAMMODE=1; the renderer's Tube > Beam trace): the frame is sent as the
 * spot's own path instead of the lines the vectoring logic cuts - a piece for every run of cycles where the spot (after
 * the deflection's lag) keeps its speed and direction within BP_TOL (a straight line: one piece, exact; a curve: a piece
 * every few cycles; a rest: a zero-length piece with its cycles), each with its beam energy (the level summed per
 * cycle) and its moment. The renderer spreads each piece's energy along it as the moving spot does, so the pieces join
 * seamlessly and the light is the time the spot spent - no line ends, joins or rest rules */
int vt_beam_mode = -1;
#define BP_TOL 0.03                       /* (the speed's share; + BP_TOL0 game units a cycle) */
#define BP_TOL0 0.3
static int bp_open, bp_emitting, bp_shift; static double bp_px, bp_py, bp_x0, bp_y0, bp_x1, bp_y1, bp_vx, bp_vy, bp_cyc, bp_lev;
static unsigned char bp_z; static unsigned long long bp_t0, bp_t1;
/* VT_ANALOGDLY=1: the Vectrex's analog delays (as Malban measured them for VIDE / libretro-vecx):
 * the X integrator gets the DAC value 15 cycles late, Y its sample-and-hold 14 cycles late, the
 * integrators start 12 / stop 15 cycles after RAMP switches, the beam zeroes 5 cycles after /ZERO.
 * Code timed to the cycle on a real Vectrex (Vectorblade's 7-cycle 'smartlist' sprites) needs them. */
static int t1_irqlag = -1, t1_irqwait;   /* VT_T1IRQLAG (see via_sstep0) */
static int alg_adly;
static double beam_off = -1;     /* VT_BEAMOFF (see alg_sstep) */
static int z_delay = -1;          /* VT_ZDELAY (see alg_sstep) */
static int beam_fade = -1, fade_left;   /* VT_BEAMFADE: the beam's faint fade-out after it goes dark (alg_sstep) */
/* the brightness knob at the back (VT_BRIGHT, vt_set_analog 8: -1..1, 0 = a well-set Vectrex). Up, the tube's bias lets
 * the beam glow even while it is blanked: its moves and returns to the middle ("spider web") and its rests are sent as
 * lines of intensity 0 with bit 7 set - the renderer draws them, and turns every line's brightness with the knob */
static double vt_bright = -1;
static double vt_lag;                   /* (the deflection's lag: see below) */
static double ofs_k, zero_tx, zero_ty, vt_lag_y;   /* (see OFS_K) */
/* the integrators' input speed, smoothed (VT_VLAG = tau in cycles; vt_set_analog 10): the DAC's X (and the Y hold's)
 * voltage reaches the integrator switch through a low-pass, so a speed change while the integrators run turns a
 * corner in a curve, while RAMP still starts and stops them at once (the owner's Vectrex, 2026-10-25: testcart/
 * lag_test - lines stop abruptly; testcart/corner_test - corners round off below ~16 cycles of rest) */
static double vt_vlag = -1, vlag_x, vlag_y, vlag_x1, vlag_y1; static int vlag_init, vlag_n;   /* (VT_VLAGN = 2: two stages) */
static double vlag_split_k = 0.06;      /* (a new line piece once the speed changed by this share of it + 2 DAC steps: VT_VLAGSPLIT) */
#define VLAG_TAU 0.0                    /* (off: the combined fit of 2026-10-25 (6.0, X/Y delay 7, start 8, stop 11) rounds the corners as the owner's Vectrex but slants text - the moves between its rows lose distance) */
#define VLAG_N 1
static int dark_on, dark_adding; static long dark_x0, dark_y0, dark_x1, dark_y1, dark_dx, dark_dy;
static double dark_cyc; static unsigned long long dark_t0;

static long fade_x0, fade_y0; static double fade_cyc; static unsigned char fade_col; static unsigned long long fade_t0;   /* on by default (VT_ANALOGDLY=0: off); with it the BIOS text needs no fitted shift: the
                       * delays put it where the owner's photos show it (within 0.1 px) */
static unsigned char dly_x[64], dly_y[64], dly_z[64]; static unsigned dly_p; static double dly_yf[64];
static unsigned dly_nx = 9, dly_ny = 9, dly_nz = 8;     /* VT_XYZDLY=x,y,zero: the S/H and zero delays (9 with the spot's
                                                          * lag LAG_TAU: the combined fit, 2026-10-25; were 15 / 14; zero 8:
                                                          * the ZERO fit with ZERO_TX, 2026-10-26, was 5) */
extern long vt_text_shift; extern double vt_text_shift_w;
static int alg_zideal;           /* VT_ZIDEAL=1: a perfect machine, the zero reference stays at centre */
static unsigned alg_xsh;  /* x sample and hold */
static unsigned alg_ysh;  /* y sample and hold */
/* the Y hold is a capacitor charged through the multiplexer: each cycle it is connected it closes
 * YSH_K of the gap to the DAC. The owner's Vectrex (testcart/sh_test, 2026-10-14): connected 4 cycles
 * after a 191-step jump it was ~1.5 steps short (~0.8 %), from 6 cycles on it had settled: 0.3^4 left
 * per 4 cycles -> 0.7 (VT_YSHK=x; 1 = instant, as vecx) */
static double alg_yshf = 128, ysh_k = -1;
/* the DAC's bit errors (VT_DACBITS=e0,..,e6: each bit's current off by that share of its weight; vt_set_analog 16..22;
 * all 0: an ideal DAC, the old model). The MC1408's ladder: the output is the sum of its bits' currents, so a code's
 * value is sum bit_i 2^i (1 + e_i) - for X straight, for the Y hold and the zero reference what they charge to. The
 * BIOS text goes right at +width and back at -width: two codes with different bits, their errors do not cancel - the
 * slant changes with the text's width (Malban, "PrintStr (3) - DAC"). Fitted on the owner's Vectrex (testcart/beam_test
 * pages 3-4, 2026-10-26): e0 +0.017, e1 +0.007, e2 -0.009, e3..e6 within 0.0015 - half of the measured pattern; the
 * rest is not bit errors (widths with the same bits differ too). Bit 7 sets the scale only (the reference is $80) */
static double dac_e[8]; static int dac_on = -1;
static double dacv (unsigned u)
{
	double v = 0;
	for (int i = 0; i < 8; i++) if ((u >> i) & 1) v += (double) (1 << i) * (1.0 + dac_e[i]);
	return v;
}
static void dac_init (void)
{
	const char *z = getenv ("VT_DACBITS"); dac_on = 0;
	if (z) { double e[7] = {0}; int n = sscanf (z, "%lf,%lf,%lf,%lf,%lf,%lf,%lf", e, e + 1, e + 2, e + 3, e + 4, e + 5, e + 6);
	         for (int i = 0; i < n && i < 7; i++) { dac_e[i] = e[i]; if (e[i] != 0) dac_on = 1; } }
}
#define DACV(u) (dac_on > 0 ? dacv (u) : (double) (u))
/* the frontend's Vectrex unit settings, live (input flags bit 5: an id and a float, see live.c): how one
 * real Vectrex differs from another - 0 the stop delay (cycles), 1 the zero reference's hold (ZREF_K),
 * 2 the zero calibration shift, 3 / 4 the integrators' drift X / Y (DAC units per cycle x ZS), 5 the Y
 * hold's settling, 6 the beam's switch-off delay (cycles), 7 the integrators' offset to the zero reference */
void vt_set_analog (int id, double v)
{
	switch (id) {
	case 0: vt_set_rampoff (v); break;
	case 1: ZREF_K = v < 0.02 ? 0.02 : v > 1 ? 1 : v; break;
	case 2: alg_calshift = v; break;
	case 3: alg_xofs = (long) (v * ZS); break;
	case 4: alg_yofs = (long) (v * ZS); break;
	case 5: ysh_k = v < 0.05 ? 0.05 : v > 1 ? 1 : v; break;
	case 6: beam_off = v < 0 ? 0 : v > 15 ? 15 : v; break;
	case 7: alg_zofs = v; break;
	case 8: vt_bright = v < -1 ? -1 : v > 1 ? 1 : v; break;
	case 9: vt_lag = v < 0 ? 0 : v > 50 ? 50 : v; break;
	case 10: vt_vlag = v < 0 ? 0 : v > 50 ? 50 : v; break;
	case 11: vt_beam_mode = v > 0.5; break;                   /* (the beam-trace mode, see bp_step) */
	case 12: zero_tx = v < 0 ? 0 : v > 50 ? 50 : v; break;    /* (ZERO's RC, X / Y: see OFS_K) */
	case 13: zero_ty = v < 0 ? 0 : v > 50 ? 50 : v; break;
	case 14: ofs_k = v < 0 ? 0 : v > 400 ? 400 : v; break;   /* (the reference's shift, cycles a DAC step) */
	case 15: vt_lag_y = v < 0 ? -1 : v > 50 ? 50 : v; break;  /* (the Y deflection's lag; -1: as X) */
	case 24: case 25: case 26: lp_set (id - 24, v); break;   /* (the light pen: x, y, touching - imager.c) */
	case 16: case 17: case 18: case 19: case 20: case 21: case 22:   /* (the DAC's bit errors e0..e6, see dacv) */
		if (dac_on < 0) dac_init ();
		dac_e[id - 16] = v < -0.05 ? -0.05 : v > 0.05 ? 0.05 : v;
		dac_on = 0; for (int i = 0; i < 7; i++) if (dac_e[i] != 0) dac_on = 1;
		break;
	}
}
static unsigned alg_zsh;  /* z sample and hold */
unsigned alg_jch0;		  /* joystick direction channel 0 */
unsigned alg_jch1;		  /* joystick direction channel 1 */
unsigned alg_jch2;		  /* joystick direction channel 2 */
unsigned alg_jch3;		  /* joystick direction channel 3 */
static unsigned alg_jsh;  /* joystick sample and hold */

static unsigned alg_compare;

static long alg_dx;     /* delta x */
static long alg_dy;     /* delta y */
static long alg_curr_x; /* current x position */
static long alg_curr_y; /* current y position */

enum {
	VECTREX_PDECAY	= 30,      /* phosphor decay rate */

	/* number of 6809 cycles before a frame redraw */

	FCYCLES_INIT    = VECTREX_MHZ / VECTREX_PDECAY,

	/* max number of possible vectors that maybe on the screen at one time.
	 * one only needs VECTREX_MHZ / VECTREX_PDECAY but we need to also store
	 * deleted vectors in a single table
	 */

	VECTOR_CNT		= VECTREX_MHZ / VECTREX_PDECAY,

	VECTOR_HASH     = 65521
};

static unsigned alg_vectoring; /* are we drawing a vector right now? */
/* the deflection's lag (VT_DEFLAG = tau in cycles, VT_DEFLAGN = 1 or 2 stages; vt_set_analog 9): the integrators'
 * outputs drive the yoke through the deflection amplifiers (LM379) and the coils' inductance, so the spot follows
 * the integrator position a few microseconds late, smoothed - the owner's Vectrex (testcart/corner_test, 2026-10-25):
 * corners round off when the beam rests less than ~16 cycles, sharp with a dot from ~24 cycles. The lines are
 * recorded along the spot (beam_x / beam_y); while it has not caught up they are cut every LAG_SPLIT cycles */
static double vt_lag = -1; static int vt_lag_n = 0;
/* measured on the owner's Vectrex with testcart/beam_test (2026-10-25):
 * - the integrators' offset (the zero reference, mux 1) moves the beam directly: an integrator's output is its reference
 *   minus its capacitor's voltage, so the beam sits at the reference times the integrator's RC - OFS_K cycles of
 *   integration per DAC step (photo: 166), X the other way from Y (+offset: left and down). ZERO empties the capacitors:
 *   the beam goes to that point, not to the middle (Malban, "Old News - Fast Move", 2026); changing the reference moves
 *   the beam there and back (through the deflection's lag). VT_OFSK / vt_set_analog 14; 0: the old model (no shift)
 * - ZERO empties the capacitors through the switch (220 ohm): an RC, not a jump - Y faster than X (the photo's lit
 *   sweeps from the corners bend to come in along X). The RAMP's input still flows meanwhile. VT_ZEROTAU=x,y cycles /
 *   vt_set_analog 12, 13; 0: the old jump
 * - the deflection's lag per axis: VT_DEFLAG (X), VT_DEFLAGY (Y, -1: as X) / vt_set_analog 9, 15 */
static double ofs_k = -1, zero_tx = -1, zero_ty = -1, vt_lag_y = -2;
#define OFS_K 166.0
static int zero_n = 1, zero_was; static double zero_x1, zero_y1;
#define ZERO_N 1
#define ZERO_TX 4.5                     /* (2026-10-26: X 4.5, Y 3.3 cycles - the schematic's 220 ohm x 0.01 uF = 3.3; X a */
#define ZERO_TY 3.3                     /*  little slower (beam_test page 2's sweeps bend). beam_test page 2 alone fitted
                                         *  12 / 8, but that zeroes too slowly for games that zero briefly: Vectorblade's
                                         *  title logo split and its raster text garbled; with 6 / 4 and less they draw as
                                         *  on the owner's photos. Page 2 (ZERO with RAMP on) needs something not modelled) */
#define LAG_TAU_Y -1.0
static double beam_x, beam_y, beam_x1, beam_y1, beam_vx, beam_vy;   /* (beam_x1/y1: the first stage of two) */
static int beam_init, lag_moving;
static double piece_bvx, piece_bvy, lag_split_k = 0.1;   /* (VT_DEFLAGSPLIT: a new piece once the spot's speed changed by
                                                          * this share: curves fine enough to look smooth - the renderer joins
                                                          * the pieces of a stroke seamlessly; Vectorblade ~4,800 lines a frame) */
#define LAG_SPLIT 2.0
#define LAG_TAU 6.0                     /* (2026-10-26: 6 - the gap test's photos, testcart/gap_test: the joins' gaps per
                                         * scale as on the owner's Vectrex in X and Y; was 8 - the gaps ~1 cycle too long) */
#define LAG_TAU_WAS 8.0                     /* (the default lag, cycles: the owner's Vectrex, 2026-10-25 - the combined fit of
                                         * testcart/corner_test, lag_test and axis_test with X/Y delay 9, RAMP start 5, stop 7:
                                         * the corners' rounding, the lines' dots, the text upright, the grid's size kept) */
#define LAG_N 1
static long alg_vector_x0;
static long alg_vector_y0;
static long alg_vector_x1;
static long alg_vector_y1;
static long alg_vector_dx;
static long alg_vector_dy;
static unsigned char alg_vector_color;

long vector_draw_cnt;
long vector_erse_cnt;
static vector_t vectors_set[2 * VECTOR_CNT];
vector_t *vectors_draw;
vector_t *vectors_erse;

static long vector_hash[VECTOR_HASH];

static long fcycles;

/* update the snd chips internal registers when via_ora/via_orb changes */

static einline void snd_update (void)
{
	switch (via_orb & 0x18) {
	case 0x00:
		/* the sound chip is disabled */
		break;
	case 0x08:
		/* the sound chip is sending data */
		break;
	case 0x10:
		/* the sound chip is recieving data */

		if (snd_select != 14) {
			snd_regs[snd_select] = via_ora;
			e8910_write(snd_select, via_ora);
			psg_write (snd_select, via_ora);
		} else snd_ioa = via_ora;                       /* (port A's output latch) */
		if (vt_imager && (snd_select == 14 || snd_select == 7))
			im_motor ((snd_regs[7] & 0x40) ? (int) (snd_ioa & 0x40) : 0x40);   /* (the 3D Imager's motor, imager.c) */
		if (vt_vecvoice && (snd_select == 14 || snd_select == 7))
			ser_set ((snd_regs[7] & 0x40) ? (snd_ioa >> 4) & 1 : 1, vx_cyc);   /* (the VecVoice's serial line) */

		break;
	case 0x18:
		/* the sound chip is latching an address */

		if ((via_ora & 0xf0) == 0x00) {
			snd_select = via_ora & 0x0f;
		}

		break;
	}
}

/* update the various analog values when orb is written. */

static einline void alg_update (void)
{
	switch (via_orb & 0x06) {
	case 0x00:
		alg_jsh = alg_jch0;

		if ((via_orb & 0x01) == 0x00) {
			/* demultiplexor is on: the Y hold charges toward the DAC in alg_sstep (VT_YSHK=1: at once) */
			if (ysh_k >= 1.0) { alg_ysh = alg_xsh; alg_yshf = alg_xsh; }
		}

		break;
	case 0x02:
		alg_jsh = alg_jch1;

		if ((via_orb & 0x01) == 0x00) {
			/* demultiplexor is on */
			/* zero reference charges toward the DAC in alg_sstep */
		}

		break;
	case 0x04:
		alg_jsh = alg_jch2;

		if ((via_orb & 0x01) == 0x00) {
			/* demultiplexor is on */

			if (alg_xsh > 0x80) {
				alg_zsh = alg_xsh - 0x80;
			} else {
				alg_zsh = 0;
			}
		}

		break;
	case 0x06:
		/* sound output line */
		alg_jsh = alg_jch3;

		if ((via_orb & 0x01) == 0x00) {
			/* demultiplexor is on: the DAC drives the sound line (held when the mux moves on) */
			psg_dac_set ((int) alg_xsh - 128);
			{ static long n; static int dl = -1; if (dl < 0) dl = getenv ("VT_DACLOG") != NULL; if (dl && !(++n & 1023)) fprintf (stderr, "dac %ld pc=%04X\n", n, e6809_get_pc () & 0xffff); }
		}

		break;
	}

	/* compare the current joystick direction with a reference */

	if (alg_jsh > alg_xsh) {
		alg_compare = 0x20;
	} else {
		alg_compare = 0;
	}

	/* compute the new "deltas" */

	if (dac_on < 0) dac_init ();
	alg_dx = (long) (DACV (alg_xsh) * ZS) - (long) ((alg_rshf + alg_zofs) * ZS) + alg_xofs;
	alg_dy = (long) ((alg_rshf + alg_zofs) * ZS) - (long) (alg_yshf * ZS) + alg_yofs;
}

/* update IRQ and bit-7 of the ifr register after making an adjustment to
 * ifr.
 */

static einline void int_update (void)
{
	if ((via_ifr & 0x7f) & (via_ier & 0x7f)) {
		via_ifr |= 0x80;
	} else {
		via_ifr &= 0x7f;
	}
}

extern unsigned long long alg_cycles;
#include "imager.c"                         /* the 3D Imager on port 2 (VT_IMAGER) */

static int vt_trial;                    /* (a dry run: no writes, VIA reads without side effects) */
/* a read where no chip answers: the last byte on the data bus (see e6809_obus; measured on the owner's
 * Vectrex with testcart/openbus_test 2026-10-14). VT_OPENBUS=hex: a fixed value instead (tests). */
int vt_nocart;                          /* no cartridge in the slot (an empty image) */
static unsigned char open_bus (void)
{
	extern unsigned char e6809_obus;
	static int ob = -2;
	if (ob == -2) { const char *z = getenv ("VT_OPENBUS"); ob = z ? (int) strtol (z, 0, 16) & 0xff : -1; }
	return ob >= 0 ? (unsigned char) ob : e6809_obus;
}

unsigned char read8 (unsigned address)
{
	unsigned char data;

	if (vt_trial && (address & 0xf800) == 0xd000) return 0;

	if ((address & 0xe000) == 0xe000) {
		/* rom */

		data = rom[address & 0x1fff];
	} else if ((address & 0xe000) == 0xc000) {
		if (address & 0x800) {
			/* ram */

			data = ram[address & 0x3ff];
		} else if (address & 0x1000) {
			/* io */
			via_mirror (address);

			switch (address & 0xf) {
			case 0x0:
				/* compare signal is an input so the value does not come from
				 * via_orb.
				 */

				if (via_acr & 0x80) {
					/* timer 1 has control of bit 7 */

					data = (unsigned char) ((via_orb & 0x1f) | (cart_bank () << 6) | via_t1pb7 | alg_compare);
				} else {
					/* bit 7 is being driven by via_orb */

					data = (unsigned char) ((via_orb & 0x9f) | (cart_bank () << 6) | alg_compare);
				}

				break;
			case 0x1:
				/* register 1 also performs handshakes if necessary */

				if ((via_pcr & 0x0e) == 0x08) {
					/* if ca2 is in pulse mode or handshake mode, then it
					 * goes low whenever ira is read.
					 */

					via_ca2 = 0;
				}
				if (via_ifr & 0x02) { via_ifr &= ~0x02u; int_update (); }   /* (CA1's flag, see imager.c) */

				/* fall through */

			case 0xf:
				if ((via_orb & 0x18) == 0x08) {
					/* the snd chip is driving port a */

					data = (unsigned char) snd_regs[snd_select];
					if (vt_imager && snd_select == 14 && alg_cycles < im_pin_until)
						data &= 0x7f;               /* (the 3D Imager's index hole passing: button 4 low) */
					if (vt_lightpen && snd_select == 14 && alg_cycles < lp_pin_until)
						data &= 0x7f;               /* (the light pen seeing the spot: button 4 low) */
					if (vt_vecvoice && snd_select == 14) {
						/* port 2's buttons: the speech accessory's ready line */
						psg_advance (vx_cyc); ser_run (vx_cyc);
						data = (unsigned char) ((data & 0xdf) | (speech_ready () ? 0x00 : 0x20));   /* (the ready line = button 2 (pin 2,
						   as the AtariVox's READY), low = takes the next byte: what VecVoice Demo, VecTalk and
						   VecVox Xmas wait for; the other buttons idle. Games that wait for every button of both
						   controllers to be released (Berzerk) see button 2 held) */
					}
				} else {
					data = (unsigned char) via_ora;
				}

				break;
			case 0x2:
				data = (unsigned char) via_ddrb;
				break;
			case 0x3:
				data = (unsigned char) via_ddra;
				break;
			case 0x4:
				/* T1 low order counter */

				data = (unsigned char) via_t1c;
				via_ifr &= 0xbf; /* remove timer 1 interrupt flag */
				/* (a real 6522 only clears the flag here: the counter runs on - in free-run mode it times
				 * out again and again, PB7 is not touched. vecx used to stop timer 1, which hangs code
				 * that runs T1 free and reads T1C-L each step, e.g. Vectorblade's game screen.) */

				int_update ();

				break;
			case 0x5:
				/* T1 high order counter */

				data = (unsigned char) (via_t1c >> 8);

				break;
			case 0x6:
				/* T1 low order latch */

				data = (unsigned char) via_t1ll;
				break;
			case 0x7:
				/* T1 high order latch */

				data = (unsigned char) via_t1lh;
				break;
			case 0x8:
				/* T2 low order counter */

				data = (unsigned char) via_t2c;
				via_ifr &= 0xdf; /* remove timer 2 interrupt flag */
				{ unsigned pc = e6809_get_pc () & 0xffff; static int n, w = -1; if (w < 0) w = getenv ("VT_WATCH_T2") != NULL;   /* (looked up once) */
				  if (w && pc < 0xE000 && n++ < 20) fprintf (stderr, "T2 low read at PC=%04X addr=%04X\n", pc, address); }

				/* (vecx stopped timer 2 here and dropped its pending flag; a 6522 only clears the flag:
				 * the counter keeps counting and still sets the flag when it runs out - a game reading
				 * T2 low before the end of its frame froze in Wait_Recal; testcart/cputime_test) */

				int_update ();

				break;
			case 0x9:
				/* T2 high order counter */

				data = (unsigned char) (via_t2c >> 8);
				break;
			case 0xa:
				data = (unsigned char) via_sr;
				via_ifr &= 0xfb; /* remove shift register interrupt flag */
				sr_flagwait = 0;
				via_srb = 0;
				via_srclk = 1;

				int_update ();

				break;
			case 0xb:
				data = (unsigned char) via_acr;
				break;
			case 0xc:
				data = (unsigned char) via_pcr;
				break;
			case 0xd:
				/* interrupt flag register */

				data = (unsigned char) via_ifr;
				break;
			case 0xe:
				/* interrupt enable register */

				data = (unsigned char) (via_ier | 0x80);
				break;
			}
		} else {
			/* $C000-$C7FF: neither RAM nor the VIA is selected, the data bus floats (vecx left
			 * `data` unset here: garbage - Mine Storm reads here after some mine fields and drew a
			 * runaway chain of dots for seconds, the owner 2026-10-14). VT_OPENBUS=hex: the value. */
			data = open_bus ();
		}
	} else if (vt_multicart && address < 0x8000) {
		data = cart[(((unsigned long) vt_mc_page << 12) + address) & 0xfffff];
	} else if (vt_vb && address < 0xc000) {
		/* the 4-bank cartridge */
		if (fl_id) data = (address & 1) ? 0xb6 : 0xbf;  /* (SST, SST39SF020A) */
		else data = cart[((unsigned long) vb_bank () << 16) | address];
	} else if (vt_hb512 && address < 0x8000) {
		/* the 512K latch cartridge */
		if (fl_id) data = (address & 1) ? 0xb7 : 0xbf;  /* (SST, SST39SF040) */
		else data = cart[flash_at (address)];
	} else if (vt_cart_ram && (address & 0xe000) == 0x2000) {
		data = vt_cram[address & 0x1fff];               /* (the cartridge's RAM) */
	} else if (address < 0x8000) {
		/* cartridge (none in the slot: nothing drives the bus) */

		data = vt_nocart ? open_bus () : cart[(cart_bank () << 15) | address];
	} else {
		data = open_bus ();                 /* $8000-$BFFF: nothing there */
	}

	return data;
}

/* a real 6809 puts a store on the bus in the instruction's last cycle(s), vecx ran it before the
 * instruction's cycles: VIA writes wait in this queue until then. VT_WRITETIME=2 (default): reads and
 * writes in the last cycle (vecx_emu); 1: only the writes (this queue); 0: the old way, both at once */
static int wq_on, vt_wtime = -1; static unsigned wq_n, wq_a[8]; static unsigned char wq_d[8];

void write8 (unsigned address, unsigned char data)
{
	if (vt_trial) return;
	{ static int wa = -2;                       /* VT_WATCH=hex: log the writes to that RAM byte (debugging) */
	  if (wa == -2) { const char *z = getenv ("VT_WATCH"); wa = z ? (int) strtol (z, 0, 16) & 0x3ff : -1; }
	  if (wa >= 0 && (address & 0xe800) == 0xc800 && (address & 0x3ff) == (unsigned) wa) {
		extern unsigned long long alg_cycles;
		printf ("W %04X <- %02X at PC=%04X cyc=%llu\n", address, data, e6809_get_pc () & 0xffff, alg_cycles);
	  } }
	{ static long ta = -2, tb; extern unsigned long long alg_cycles;   /* VT_VIATRACE=a,b: the VIA writes a..b cycles into each frame */
	  if (ta == -2) { const char *z = getenv ("VT_VIATRACE"); ta = -1; if (z) sscanf (z, "%ld,%ld", &ta, &tb); }
	  long rel = (long) (alg_cycles - vt_frame_t0);
	  if (ta >= 0 && (address & 0xf000) == 0xd000 && rel >= ta && rel <= tb)
		printf ("VIA %X <- %02X at PC=%04X t=%ld\n", address & 15, data, e6809_get_pc () & 0xffff, rel); }
	if (wq_on && (address & 0xf000) == 0xd000 && wq_n < 8) {
		if (address & 0x800) ram[address & 0x3ff] = data;
		wq_a[wq_n] = address & ~0x800u; wq_d[wq_n++] = data;
		return;
	}
	if ((address & 0xe000) == 0xe000) {
		/* rom */
	} else if ((address & 0xe000) == 0xc000) {
		/* it is possible for both ram and io to be written at the same! */

		if (address & 0x800) {
			ram[address & 0x3ff] = data;
		}

		if (address & 0x1000) {
			via_mirror (address);
			switch (address & 0xf) {
			case 0x0:
				{ int b0 = cart_bank (); via_orb = data; ow_master ((via_ddrb & 0x40) && !(via_orb & 0x40)); if (cart_bank () != b0 && !vt_cart_mirror) { vt_bank_switches++; vt_bankchange (b0, cart_bank ()); } }

				snd_update ();

				alg_update ();

				if ((via_pcr & 0xe0) == 0x80) {
					/* if cb2 is in pulse mode or handshake mode, then it
					 * goes low whenever orb is written.
					 */

					via_cb2h = 0;
				}

				break;
			case 0x1:
				/* register 1 also performs handshakes if necessary */

				if ((via_pcr & 0x0e) == 0x08) {
					/* if ca2 is in pulse mode or handshake mode, then it
					 * goes low whenever ora is written.
					 */

					via_ca2 = 0;
				}
				if (via_ifr & 0x02) { via_ifr &= ~0x02u; int_update (); }   /* (CA1's flag, see imager.c) */

				/* fall through */

			case 0xf:
				via_ora = data;

				snd_update ();

				/* output of port a feeds directly into the dac which then
				 * feeds the x axis sample and hold.
				 */

				alg_xsh = data ^ 0x80;
				/* the calibrated zero (ZeroCal: reference loaded with CalZero, then a DAC -1 pulse):
				 * VT_CALSHIFT models a Vectrex whose reference ends up as if CalZero were that much
				 * higher. BIOS Reset0Ref / Print_Str load plain 0 without the pulse: unaffected. */
				if (data == 0xff && (via_orb & 0x07) == 0x02 && !alg_zideal)
					alg_rshf += alg_calshift;

				alg_update ();

				break;
			case 0x2:
				{ int b0 = cart_bank (); via_ddrb = data; ow_master ((via_ddrb & 0x40) && !(via_orb & 0x40)); if (cart_bank () != b0 && !vt_cart_mirror) { vt_bank_switches++; vt_bankchange (b0, cart_bank ()); } }
				break;
			case 0x3:
				via_ddra = data;
				break;
			case 0x4:
				/* T1 low order counter */

				via_t1ll = data;

				break;
			case 0x5:
				/* T1 high order counter */

				via_t1lh = data;
				{ static int x1 = -1; if (x1 < 0) { const char *z = getenv ("VT_T1EXTRA"); x1 = z ? atoi (z) : 1; }   /* (1: timer 1 runs N+2 cycles; the 6522 data sheet: N+1.5. The owner's test cart photos 2026-10-12) */
				  via_t1c = ((via_t1lh << 8) | via_t1ll) + x1; }
				via_ifr &= 0xbf; /* remove timer 1 interrupt flag */
				t1_irqwait = 0;

				via_t1on = 1; /* timer 1 starts running */
				via_t1int = 1;
				via_t1pb7 = 0;

				int_update ();

				break;
			case 0x6:
				/* T1 low order latch */

				via_t1ll = data;
				break;
			case 0x7:
				/* T1 high order latch */

				via_t1lh = data;
				break;
			case 0x8:
				/* T2 low order latch */

				via_t2ll = data;
				break;
			case 0x9:
				/* T2 high order latch/counter */

				via_t2c = (data << 8) | via_t2ll;
				via_ifr &= 0xdf;
				{ unsigned pc = e6809_get_pc () & 0xffff; static int n, w = -1; if (w < 0) w = getenv ("VT_WATCH_T2") != NULL;
				  if (w && pc < 0xE000 && n++ < 20) fprintf (stderr, "T2 high write at PC=%04X addr=%04X\n", pc, address);
				  { extern int vt_t2w_new; if (pc < 0xE000 || vt_uvm2 || vt_pitrex) vt_t2w_new = 1; } }   /* (the game's own frame timer: vtest.c) */

				via_t2on = 1; /* timer 2 starts running */
				via_t2int = 1;

				int_update ();

				break;
			case 0xa:
				via_sr = data;
				via_srphase = 1;    /* (system-clock mode: the first bit leaves at the next cycle - one cycle
				                     * later, a pattern that ends right as the beam is zeroed (Vectorblade's
				                     * dots) still lit the jump to the centre: rays the real Vectrex does not show) */
				via_ifr &= 0xfb; /* remove shift register interrupt flag */
				sr_flagwait = 0;
				via_srb = 0;
				via_srclk = 1;

				int_update ();

				break;
			case 0xb:
				via_acr = data;
				break;
			case 0xc:
				via_pcr = data;


				if ((via_pcr & 0x0e) == 0x0c) {
					/* ca2 is outputting low */

					via_ca2 = 0;
				} else {
					/* ca2 is disabled or in pulse mode or is
					 * outputting high.
					 */

					via_ca2 = 1;
				}

				if ((via_pcr & 0xe0) == 0xc0) {
					/* cb2 is outputting low */

					via_cb2h = 0;
				} else {
					/* cb2 is disabled or is in pulse mode or is
					 * outputting high.
					 */

					via_cb2h = 1;
				}

				break;
			case 0xd:
				/* interrupt flag register */

				via_ifr &= ~(data & 0x7f);
				int_update ();

				break;
			case 0xe:
				/* interrupt enable register */

				if (data & 0x80) {
					via_ier |= data & 0x7f;
				} else {
					via_ier &= ~(data & 0x7f);
				}

				int_update ();

				break;
			}
		}
	} else if (vt_multicart && address == 0x7fff) {
		vt_mc_page = data & 0xff;                       /* (the multicart's latch) */
	} else if (vt_vb && address < 0xc000) {
		flash_write (address, data);                    /* (the 4-bank cartridge's flash) */
	} else if (vt_hb512 && address < 0x8000) {
		flash_write (address, data);                    /* (the 512K latch cartridge: the flash, then the latch) */
		if (!(address & 0x4000)) {
			int b0 = vt_cur_bank ();
			vt_hb_latch = data & 7;
			if (vt_cur_bank () != b0) { vt_bank_switches++; vt_bankchange (b0, vt_cur_bank ()); }
		}
	} else if (vt_cart_ram && (address & 0xe000) == 0x2000) {
		vt_cram[address & 0x1fff] = data;               /* (the cartridge's RAM) */
	} else if (address < 0x8000) {
		/* cartridge */
	}
}

/* the RESET button: the 6809, the 6522 and the AY are reset, the RAM keeps its contents (the BIOS
 * then does a warm start: no intro); vecx_reset is power-on (the RAM gets a fresh pattern) */
void vecx_reset (void);
void vecx_reset_button (void)
{
	static unsigned char keep[1024];
	unsigned page = vt_mc_page, ra = vt_hb_latch;      /* (the cartridges' latches are not reset by the button) */
	memcpy (keep, ram, sizeof keep);
	vecx_reset ();
	memcpy (ram, keep, sizeof keep);
	vt_mc_page = page; vt_hb_latch = ra;
}

void vecx_reset (void)
{
	im_init ();                             /* (the 3D Imager: its disc standing still) */
	lp_init ();                             /* (the light pen) */
	{ extern void uvm2_power_on (void); if (vt_uvm2) uvm2_power_on (); }
	{ extern void pitrex_power_on (void); if (vt_pitrex) pitrex_power_on (); }   /* (a PiTrex: its program starts afresh) */   /* (a UVMC2: the game starts afresh) */
	{ extern unsigned char e6809_obus_idle; e6809_obus_idle = rom[0x1fff]; }   /* (the BIOS byte at $FFFF: the idle cycles' bus) */
	vt_mc_page = 0; fl_step = fl_cmd = fl_id = 0;          /* (power-on: the multicart's menu page, the flash idle) */
	{ const char *z = getenv ("VT_HBLATCH"); static unsigned n; vt_hb_latch = z ? (unsigned) atoi (z) & 7 : (unsigned) (time (0) + n++ * 5) & 7; }   /* (the 512K cartridge's latch: whatever it powers up with) */
	unsigned r;
	static int ow_ready;
	if (!ow_ready) { ow_ready = 1; ow_init (); const char *z = getenv ("VT_ZOFS"); if (z) alg_zofs = atof (z); alg_zideal = getenv ("VT_ZIDEAL") != NULL; vt_sr1 = getenv ("VT_SR1") != NULL; z = getenv ("VT_WOBBLE"); if (z) sscanf (z, "%lf,%lf", &alg_wob50, &alg_wob100); z = getenv ("VT_ZTAU"); if (z) { alg_ztau = atof (z); alg_zk = 1.0 - exp (-1.0 / alg_ztau); } z = getenv ("VT_CALSHIFT"); if (z) alg_calshift = atof (z); z = getenv ("VT_ZREFK"); if (z) ZREF_K = atof (z); z = getenv ("VT_RAMPDLY"); if (z) { alg_rdly_off_t1 = 0; if (sscanf (z, "%u,%u,%u", &alg_rdly_on, &alg_rdly_off, &alg_rdly_off_t1) < 3) alg_rdly_off_t1 = alg_rdly_off; } z = getenv ("VT_RAMPOFF"); if (z) { double a = 0, b = -1; sscanf (z, "%lf,%lf", &a, &b); if (b < 0) b = a; alg_rdly_on = alg_rdly_on ? alg_rdly_on : 12; alg_rdly_off = (unsigned) a; rdly_frac = a - (unsigned) a; alg_rdly_off_t1 = (unsigned) b; rdly_frac_t1 = b - (unsigned) b; } z = getenv ("VT_XYZDLY"); if (z) sscanf (z, "%u,%u,%u", &dly_nx, &dly_ny, &dly_nz); z = getenv ("VT_ANALOGDLY"); alg_adly = !(z && atoi (z) == 0); if (alg_adly && !getenv ("VT_RAMPDLY") && !getenv ("VT_RAMPOFF")) { alg_rdly_on = 5; alg_rdly_off = 7; alg_rdly_off_t1 = 7; rdly_frac = rdly_frac_t1 = 0.0; }   /* (2026-10-25, the combined fit with the spot's lag LAG_TAU: start 5, stop 7 cycles. Before: stop 12.3 cycles with timer 1 one cycle longer (VT_T1EXTRA=1); Malban: 15. Fitted to the owner's test cart photos 2026-10-12 (11.8 fitted the lines best, 12.3 gives Mine Storm's ship its ~15 % short lines): line ends dark only at scales $0B-$0C / $12-$13 as on the tube; Mine Storm's mothership interior closed, ship lines ~25 % short) */ if (alg_adly) { if (!getenv ("VT_TEXTSHIFT")) vt_text_shift = 0; if (!getenv ("VT_TEXTSHIFTW")) vt_text_shift_w = 0; } z = getenv ("VT_HWOFS"); if (z) { double ox = 0, oy = 0; sscanf (z, "%lf,%lf", &ox, &oy); alg_xofs = (long) (ox * ZS); alg_yofs = (long) (oy * ZS); } }

	/* ram */

	for (r = 0; r < 1024; r++) {
		ram[r] = r & 0xff;
	}
	/* a real Vectrex powers up with its RAM in a random state; the BIOS keeps the random number seed
	 * (Vec_Random_Seed, $C87E/$C87F) as it finds it, so Mine Storm's first starfield differs each time.
	 * VT_POWERSEED=n (the frontend sets it): a different odd seed at every power-on from n (the MiSTer
	 * core's way, Mathis Rosenhauer's idea); unset: the fixed pattern above (repeatable tests). */
	{ const char *z = getenv ("VT_POWERSEED"); static unsigned long n;
	  if (z) { unsigned long x = strtoul (z, 0, 10) + 0x9E3779B9UL * ++n; x ^= x >> 15; x *= 0x2C1B3C6DUL; x ^= x >> 12;
	           ram[0x7e] = (unsigned char) (x | 1); ram[0x7f] = (unsigned char) ((x >> 8) | 1); } }

	for (r = 0; r < 16; r++) {
		snd_regs[r] = 0;
		e8910_write(r, 0);
	}

	/* input buttons */

	snd_regs[14] = 0xff;
	e8910_write(14, 0xff);

	snd_select = 0; snd_ioa = 0xff;
	sp_reset (); sj_reset (); ser_line = 1; ser_div = ser_cnt = 0; ser_t = vx_cyc;

	via_ora = 0;
	via_orb = 0;
	via_ddra = 0;
	via_ddrb = 0;
	via_t1on = 0;
	via_t1int = 0;
	via_t1c = 0;
	via_t1ll = 0;
	via_t1lh = 0;
	via_t1pb7 = 0x80;
	via_t2on = 0;
	via_t2int = 0;
	via_t2c = 0;
	via_t2ll = 0;
	via_sr = 0;
	via_srb = 8;
	via_src = 0;
	via_srclk = 0;
	via_acr = 0;
	via_pcr = 0;
	via_ifr = 0;
	via_ier = 0;
	via_ca2 = 1;
	via_cb2h = 1;
	via_cb2s = 0;

	alg_rshf = 128;
	alg_xsh = 128;
	alg_ysh = 128; alg_yshf = 128;
	alg_zsh = 0;
	alg_jch0 = 128;
	alg_jch1 = 128;
	alg_jch2 = 128;
	alg_jch3 = 128;
	alg_jsh = 128;

	alg_compare = 0; /* check this */

	alg_dx = 0;
	alg_dy = 0;
	alg_curr_x = (ALG_MAX_X*ZS) / 2;
	alg_curr_y = (ALG_MAX_Y*ZS) / 2;

	alg_vectoring = 0;
	beam_init = 0;                      /* (the spot starts where the integrators are) */

	vector_draw_cnt = 0;
	vector_erse_cnt = 0;
	vectors_draw = vectors_set;
	vectors_erse = vectors_set + VECTOR_CNT;

	fcycles = FCYCLES_INIT;

	e6809_read8 = read8;
	e6809_write8 = write8;

	e6809_reset ();
}

/* perform a single cycle worth of via emulation.
 * via_sstep0 is the first postion of the emulation.
 */

/* VT_T1IRQLAG=n: in one-shot mode the timer 1 flag (IFR bit 6, what the CPU polls) is set n cycles after
 * PB7 went high (the ramp stops on PB7) */
static int t1_reload = -1; static unsigned t1_reloadnow;
static einline void via_sstep0 (void)
{
	unsigned t2shift;

	if (t1_irqlag < 0) { const char *z = getenv ("VT_T1IRQLAG"); t1_irqlag = z ? atoi (z) : 0; }
	if (t1_irqwait && !--t1_irqwait) { via_ifr |= 0x40; int_update (); }
	if (sr_flagwait && !--sr_flagwait) { via_ifr |= 0x04; int_update (); }
	if (t1_reload < 0) { const char *z = getenv ("VT_T1RELOAD"); t1_reload = z ? atoi (z) : 2; }

	if (via_t1on && t1_reloadnow) {             /* (one-shot: the latch back in, a cycle after $FFFF) */
		t1_reloadnow = 0;
		via_t1c = (via_t1lh << 8) | via_t1ll;
	} else if (via_t1on) {
		via_t1c--;

		if ((via_t1c & 0xffff) == 0xffff) {
			/* counter just rolled over */

			if (via_acr & 0x40) {
				/* continuous interrupt mode */

				via_ifr |= 0x40;
				int_update ();
				via_t1pb7 = 0x80 - via_t1pb7;

				/* reload counter */

				via_t1c = (via_t1lh << 8) | via_t1ll;
			} else {
				/* one shot mode: the 6522 reloads the counter from the latch here too (only the
				 * flag and PB7 do not repeat) - the owner's Vectrex, testcart/cputime_test
				 * 2026-10-14: a 4-cycle timer read 04 some 25 cycles after it started (vecx: $FFEC).
				 * VT_T1RELOAD: 0 = keep counting down (vecx), 1 = reload at once, 2 = $FFFF for a
				 * cycle first (default) */

				if (t1_reload == 1) via_t1c = (via_t1lh << 8) | via_t1ll;
				else if (t1_reload == 2) t1_reloadnow = 1;
				if (via_t1int) {
					via_t1pb7 = 0x80;
					via_t1int = 0;
					if (t1_irqlag) t1_irqwait = t1_irqlag;   /* (the flag follows later: see t1_irqlag) */
					else { via_ifr |= 0x40; int_update (); }
				}
			}
		}
	}

	if (via_t2on && (via_acr & 0x20) == 0x00) {
		via_t2c--;

		if ((via_t2c & 0xffff) == 0xffff) {
			/* one shot mode */

			if (via_t2int) {
				via_ifr |= 0x20;
				int_update ();
				via_t2int = 0;
			}
		}
	}

	/* shift counter */

	via_src--;

	if ((via_src & 0xff) == 0xff) {
		via_src = via_t2ll;

		if (via_srclk) {
			t2shift = 1;
			via_srclk = 0;
		} else {
			t2shift = 0;
			via_srclk = 1;
		}
	} else {
		t2shift = 0;
	}

	if (via_srb < 8) {
		switch (via_acr & 0x1c) {
		case 0x00:
			/* disabled */
			break;
		case 0x04:
			/* shift in under control of t2 */

			if (t2shift) {
				/* shifting in 0s since cb2 is always an output */

				via_sr <<= 1;
				via_srb++;
			}

			break;
		case 0x08:
			/* shift in under system clk control */

			via_sr <<= 1;
			via_srb++;

			break;
		case 0x0c:
			/* shift in under cb1 control */
			break;
		case 0x10:
			/* shift out under t2 control (free run) */

			if (t2shift) {
				via_cb2s = (via_sr >> 7) & 1;

				via_sr <<= 1;
				via_sr |= via_cb2s;
			}

			break;
		case 0x14:
			/* shift out under t2 control */

			if (t2shift) {
				via_cb2s = (via_sr >> 7) & 1;

				via_sr <<= 1;
				via_sr |= via_cb2s;
				via_srb++;
			}

			break;
		case 0x18:
			/* shift out under system clock control: on the 6522 one bit per TWO phi2 cycles (CB1
			 * runs at phi2/2). vecx shifted every cycle, which drew the BIOS raster characters half
			 * as wide (8 of the 18 cycles per character lit instead of 16) and the patterned lines
			 * with half-length dots; the owner's photos (2026-10-08) show the 2-cycle rate.
			 * VT_SR1=1 restores the old behaviour. */
			if (!vt_sr1 && (via_srphase ^= 1))
				break;
			via_cb2s = (via_sr >> 7) & 1;

			via_sr <<= 1;
			via_sr |= via_cb2s;
			via_srb++;

			break;
		case 0x1c:
			/* shift out under cb1 control */
			break;
		}

		if (via_srb == 8) {
			/* system-clock mode: the flag at the end of the 8th bit (2 cycles after it leaves) -
			 * the owner's Vectrex, testcart/cputime_test 2026-10-14 (vecx: as the 8th bit left) */
			if ((via_acr & 0x1c) == 0x18 && !vt_sr1) sr_flagwait = 2;
			else { via_ifr |= 0x04; int_update (); }
		}
	}
}

/* perform the second part of the via emulation */

static einline void via_sstep1 (void)
{
	if ((via_pcr & 0x0e) == 0x0a) {
		/* if ca2 is in pulse mode, then make sure
		 * it gets restored to '1' after the pulse.
		 */

		via_ca2 = 1;
	}

	if ((via_pcr & 0xe0) == 0xa0) {
		/* if cb2 is in pulse mode, then make sure
		 * it gets restored to '1' after the pulse.
		 */

		via_cb2h = 1;
	}
}

/* lines drawn by the BIOS Print_Str ($F495-$F510) appear further left on a real Vectrex:
 * vt_text_shift + vt_text_shift_w * Vec_Text_Width units (160 units = 1 px). Fitted, with the
 * 2-cycle shift register, to the owner's photos (2026-10-08): the name entry letter (width 70)
 * centred in its box, the countdown digit (width 120) 0.7 px right of the ship. */
double vt_text_shift_w = 4.6;   /* (5.6 before the shift register started a cycle sooner, 2026-10-11) */
long vt_text_shift = 602;
static double alg_vector_cyc;    /* beam energy on the current line: the beam level summed per cycle */
/* VT_WOBBLE=a50,a100 (game px): mains hum. The Vectrex's transformer field and its unfiltered
 * supply ripple push the beam by a 50 Hz and a 100 Hz sine. The mains is not locked to the 1.5 MHz
 * crystal, so objects drawn at different moments of a frame get different offsets and the pattern
 * drifts slowly. Fitted to the owner's video (2026-10-08, 50 Hz mains, not recapped): ship vs text
 * ~0.25-0.3 px apart frame to frame, text slant varying +-0.15 px, single rows not independently. */

unsigned long long alg_cycles;
static unsigned long long alg_vector_t0;
static void alg_wobble (unsigned long long t, long *dx, long *dy)
{
	double s = t / 1500000.0;
	double a = 2 * 3.14159265358979 * 50.03 * s;     /* mains a little off the crystal: slow drift */
	*dx = (long) (160.0 * (alg_wob50 * sin (a) + alg_wob100 * sin (2 * a + 0.7)));
	*dy = (long) (160.0 * (alg_wob50 * 0.8 * sin (a + 1.9) + alg_wob100 * cos (2 * a + 0.3)));
}
static einline void alg_addline (long x0, long y0, long x1, long y1, unsigned char color);
/* the beam-trace mode's piece so far to the frame (through alg_addline: the text shift, the wobble, its moment) */
static void bp_flush (void)
{
	if (!bp_open) return;
	double c = alg_vector_cyc; unsigned long long t = alg_vector_t0, now = alg_cycles;
	alg_vector_cyc = bp_cyc; alg_vector_t0 = bp_t0; alg_cycles = bp_t1; bp_emitting = 1;
	alg_addline ((long) bp_x0, (long) bp_y0, (long) bp_x1, (long) bp_y1, bp_z);
	bp_emitting = 0; alg_vector_cyc = c; alg_vector_t0 = t; alg_cycles = now;
	bp_open = 0;
}
/* one cycle of the spot (after the lag: beam_x / beam_y, ZS units): the cycle's light goes from where the spot was to
 * where it is now; the same piece while the speed holds (BP_TOL), the beam level and the Z the same */
static void bp_step (unsigned sig_blank)
{
	double lev = (sig_blank && (alg_zsh & 0x80) == 0 && (alg_zsh & 0x7f)) ? alg_zlev : 0.0;
	int inb = beam_x >= 0 && beam_x < (ALG_MAX_X*ZS) && beam_y >= 0 && beam_y < (ALG_MAX_Y*ZS);
	double x = beam_x, y = beam_y, vx = x - bp_px, vy = y - bp_py;
	if (lev > 0.0 && inb) {
		unsigned pc = e6809_get_pc () & 0xFFFF; int sh = pc >= 0xF495 && pc < 0xF511;
		if (bp_open && (unsigned char) alg_zsh == bp_z && fabs (lev - bp_lev) < 0.02 && sh == bp_shift &&
		    fabs (vx - bp_vx) + fabs (vy - bp_vy) <= BP_TOL * (fabs (bp_vx) + fabs (bp_vy)) + BP_TOL0 * ZS) {
			bp_x1 = x; bp_y1 = y; bp_cyc += lev; bp_t1 = alg_cycles;
		} else {
			bp_flush ();
			bp_open = 1; bp_x0 = bp_px; bp_y0 = bp_py; bp_x1 = x; bp_y1 = y; bp_vx = vx; bp_vy = vy;
			bp_cyc = lev; bp_lev = lev; bp_z = (unsigned char) alg_zsh; bp_shift = sh;
			bp_t0 = alg_cycles - 1; bp_t1 = alg_cycles;
		}
	} else bp_flush ();
	bp_px = x; bp_py = y;
}
static einline void alg_addline (long x0, long y0, long x1, long y1, unsigned char color)
{
	unsigned long key;
	long index;

	if (vt_beam_mode > 0 && !bp_emitting && !dark_adding) return;   /* (the beam-trace mode: the pieces, see bp_step) */
	x0 /= ZS; y0 /= ZS; x1 /= ZS; y1 /= ZS;
	{ unsigned pc = e6809_get_pc () & 0xFFFF;
	  if (bp_emitting ? bp_shift : (pc >= 0xF495 && pc < 0xF511)) { long sh = vt_text_shift + (long) (vt_text_shift_w * ram[0x2B]); x0 -= sh; x1 -= sh; } }
	if ((alg_wob50 != 0 || alg_wob100 != 0) && !vt_wobble_off) {
		long ax, ay, bx, by;
		alg_wobble (alg_vector_t0, &ax, &ay); alg_wobble (alg_cycles, &bx, &by);
		x0 += ax; y0 -= ay; x1 += bx; y1 -= by;          /* (screen y grows downwards) */
	}
	vt_line_rgb = vt_imager ? im_color (alg_vector_t0 + (unsigned long long) (alg_vector_cyc * 0.5)) : 0;   /* (the 3D Imager) */
	{ unsigned long long tf = alg_vector_t0 > vt_piece_t0 ? alg_vector_t0 - vt_piece_t0 : 0;
	  vt_line (x0, y0, x1, y1, color, (unsigned) (alg_vector_cyc * 8.0 + 0.5),   /* 1/8 cycles */
		   tf > 65535 ? 65535 : (unsigned) tf); }                              /* start in frame */

	key = (unsigned long) x0;
	key = key * 31 + (unsigned long) y0;
	key = key * 31 + (unsigned long) x1;
	key = key * 31 + (unsigned long) y1;
	key %= VECTOR_HASH;

	/* first check if the line to be drawn is in the current draw list.
	 * if it is, then it is not added again.
	 */

	index = vector_hash[key];

	if (index >= 0 && index < vector_draw_cnt &&
		x0 == vectors_draw[index].x0 &&
		y0 == vectors_draw[index].y0 &&
		x1 == vectors_draw[index].x1 &&
		y1 == vectors_draw[index].y1) {
		vectors_draw[index].color = color;
	} else {
		/* missed on the draw list, now check if the line to be drawn is in
		 * the erase list ... if it is, "invalidate" it on the erase list.
		 */

		if (index >= 0 && index < vector_erse_cnt &&
			x0 == vectors_erse[index].x0 &&
			y0 == vectors_erse[index].y0 &&
			x1 == vectors_erse[index].x1 &&
			y1 == vectors_erse[index].y1) {
			vectors_erse[index].color = VECTREX_COLORS;
		}

		vectors_draw[vector_draw_cnt].x0 = x0;
		vectors_draw[vector_draw_cnt].y0 = y0;
		vectors_draw[vector_draw_cnt].x1 = x1;
		vectors_draw[vector_draw_cnt].y1 = y1;
		vectors_draw[vector_draw_cnt].color = color;
		vector_hash[key] = vector_draw_cnt;
		vector_draw_cnt++;
	}
}

/* perform a single cycle worth of analog emulation */

static einline void alg_sstep (void)
{
	long sig_dx, sig_dy;
	unsigned sig_ramp;
	unsigned sig_blank;

	if (vt_imager) im_tick ();              /* (the 3D Imager's disc: its index hole) */
	if (vt_lightpen) lp_tick ();            /* (the light pen: the lit spot under it) */
	if ((via_acr & 0x10) == 0x10) {
		sig_blank = via_cb2s;
	} else {
		sig_blank = via_cb2h;
	}
	/* VT_BEAMOFF=c: the beam goes dark c cycles after /BLANK switches it off (default 0.5; the
	 * owner's photos 2026-10-12: the dark line ends of Mine Storm's ship and mines are about half a
	 * cycle shorter than without it). Whole cycles: the beam stays lit; the fraction: a moving line
	 * is drawn that much further (alg_addline below). */
	if (beam_off < 0) { const char *z = getenv ("VT_BEAMOFF"); beam_off = z ? atof (z) : 0.5; if (beam_off > 15) beam_off = 15; }
	/* VT_ZDELAY=c: the beam's on and off both reach the tube c cycles late - the Z amplifier delays /BLANK as the
	 * deflection amplifiers delay X and Y (the spot's lag, LAG_TAU), so a line is lit along its own path to its end
	 * (tried for the owner's DAC test photos, 2026-10-26: every long line reaches the reference placed by a move; ours
	 * reach 98-98.5 %, within the photos' scatter). Measured 2026-10-26 (TODO.md, Clean Sweep): the real wall ends sit
	 * ~0.4 cycle further along their sweeps, below this switch's whole-cycle step - a test switch, 0 (off) */
	if (z_delay < 0) { const char *z = getenv ("VT_ZDELAY"); z_delay = z ? atoi (z) : 0; if (z_delay > 30) z_delay = 30; }
	if (z_delay > 0) { static unsigned zpos; static unsigned char zh[32];
		zh[zpos++ & 31] = sig_blank ? 1 : 0;
		sig_blank = zh[(zpos - 1 - z_delay) & 31]; }
	{ static unsigned bpos; static unsigned char bh[16]; int boff = (int) beam_off;
	  if (boff) {
		bh[bpos++ & 15] = sig_blank ? 1 : 0;
		for (int k = 1; k <= boff; k++) if (bh[(bpos - 1 - k) & 15]) sig_blank = 1;
	  } }
	/* VT_ZTAU=t: the beam switches on almost at once but fades out with a time constant of t cycles
	 * at full intensity, shorter when dimmer (the owner's photos, 2026-10-08: at intensity 127 the
	 * a big title's dashes, single pixels too, are ~3.5 cycles longer; the intensity-80 small
	 * text only a little). alg_zlev = the beam level 0..1, summed per line as its energy. */
	if (alg_ztau > 0) {
		/* fast rise (0.7 cycles), the fall slows with the intensity: tau * (Z/127)^1.5 */
		if (sig_blank) alg_zlev += (1.0 - alg_zlev) * 0.76;
		else {
			double t = alg_ztau * pow ((alg_zsh & 0x7f) / 127.0, 1.5);
			alg_zlev = t > 0.05 ? alg_zlev * exp (-1.0 / t) : 0.0;
		}
		sig_blank = alg_zlev > 0.2;      /* (visible) */
	} else
		alg_zlev = sig_blank ? 1.0 : 0.0;

	if (ysh_k < 0) { const char *z = getenv ("VT_YSHK"); ysh_k = z ? atof (z) : 0.7; }
	if ((via_orb & 0x07) == 0x00 && ysh_k < 1.0) {
		/* mux enabled, select 0: the Y hold charges toward the DAC value (see alg_yshf) */
		alg_yshf += (DACV (alg_xsh) - alg_yshf) * ysh_k;
		alg_ysh = (unsigned) (alg_yshf + 0.5);
		alg_dy = (long) ((alg_rshf + alg_zofs) * ZS) - (long) (alg_yshf * ZS) + alg_yofs;
	}
	if ((via_orb & 0x07) == 0x02) {
		/* mux enabled, select 1: the zero reference capacitor charges toward the DAC value */
		if (!alg_zideal) alg_rshf += (DACV (alg_xsh) - alg_rshf) * ZREF_K;
		alg_dx = (long) (DACV (alg_xsh) * ZS) - (long) ((alg_rshf + alg_zofs) * ZS) + alg_xofs;
		alg_dy = (long) ((alg_rshf + alg_zofs) * ZS) - (long) (alg_yshf * ZS) + alg_yofs;
	}

	long adx = alg_dx, ady = alg_dy;                    /* (the integrators' inputs now) */
	unsigned zero_now = via_ca2;
	if (alg_adly) {
		dly_x[dly_p & 63] = (unsigned char) alg_xsh; dly_y[dly_p & 63] = (unsigned char) alg_ysh;
		dly_yf[dly_p & 63] = alg_yshf;
		dly_z[dly_p & 63] = (unsigned char) via_ca2; dly_p++;
		unsigned xd = dly_x[(dly_p - 1 - dly_nx) & 63], yd = dly_y[(dly_p - 1 - dly_ny) & 63];
		zero_now = dly_z[(dly_p - 1 - dly_nz) & 63];
		if (dac_on > 0) {                                   /* (the DAC's bit errors: the hold's value as it charged) */
			adx = (long) (dacv (xd) * ZS) - (long) ((alg_rshf + alg_zofs) * ZS) + alg_xofs;
			ady = (long) ((alg_rshf + alg_zofs) * ZS) - (long) (dly_yf[(dly_p - 1 - dly_ny) & 63] * ZS) + alg_yofs;
		} else {
		adx = (long) xd * ZS - (long) ((alg_rshf + alg_zofs) * ZS) + alg_xofs;
		ady = (long) ((alg_rshf + alg_zofs) * ZS) - (long) yd * ZS + alg_yofs;
		}
	}
	if (vt_vlag < 0) { const char *z = getenv ("VT_VLAG"); vt_vlag = z ? atof (z) : VLAG_TAU; z = getenv ("VT_VLAGN"); vlag_n = z ? atoi (z) : VLAG_N;
	                   z = getenv ("VT_VLAGSPLIT"); if (z) vlag_split_k = atof (z); }
	if (vt_vlag > 0.05) {                               /* the speed, smoothed (see vt_vlag) */
		if (!vlag_init) { vlag_init = 1; vlag_x = vlag_x1 = adx; vlag_y = vlag_y1 = ady; }
		if (vlag_n == 2) {                              /* (two stages of tau / 2) */
			double k = 1.0 - exp (-2.0 / vt_vlag);
			vlag_x1 += (adx - vlag_x1) * k; vlag_y1 += (ady - vlag_y1) * k;
			vlag_x += (vlag_x1 - vlag_x) * k; vlag_y += (vlag_y1 - vlag_y) * k;
		} else {
			double k = 1.0 - exp (-1.0 / vt_vlag);
			vlag_x += (adx - vlag_x) * k; vlag_y += (ady - vlag_y) * k;
		}
		adx = (long) (vlag_x + (vlag_x >= 0 ? 0.5 : -0.5)); ady = (long) (vlag_y + (vlag_y >= 0 ? 0.5 : -0.5));
	}
	if (via_acr & 0x80) {
		sig_ramp = via_t1pb7;
	} else {
		sig_ramp = via_orb & 0x80;
	}
	/* VT_RAMPDLY=on,off: the integrators start/stop that many cycles after the RAMP line
	 * switches (the analog switch and its driver lag on a real Vectrex); the history runs on while
	 * the integrators are zeroed */
	/* The stop delay can differ by what drives PB7: timer 1 (ACR b7) or a port B write
	 * (VT_RAMPDLY=on,off_portB,off_timer1). Default 14 for both (Malban: 15): closest to the owner's
	 * real Mine Storm (2026-10-12) - the ship's lines ~20 % short, the mothership's corners open now and
	 * then (on the tube less visibly). */
	double ramp_part = 1.0;                             /* (the last, fractional cycle of a stop delay) */
	{ static unsigned char hist[64]; static unsigned hp;
	  hist[hp++ & 63] = (sig_ramp ? 1 : 0) | ((via_acr & 0x80) ? 2 : 0);
	  if (alg_rdly_on | alg_rdly_off) {
		/* a RAMP pulse [a, b) moves the integrators during [a + on, b + off): 'on' whole cycles, 'off'
		 * whole cycles plus a fraction (the last, partial cycle at that share of the speed). off may be
		 * shorter than on. The stop delay follows what drove PB7 (h & 2: timer 1). */
		#define RAMP_ACT(k) (!(hist[(hp - 1 - (k)) & 63] & 1))
		unsigned act = 0, src = hist[(hp - 1 - alg_rdly_on) & 63] & 2;
		unsigned oi = src ? alg_rdly_off_t1 : alg_rdly_off; double fr = src ? rdly_frac_t1 : rdly_frac;
		if (oi >= alg_rdly_on) {
			for (unsigned k = alg_rdly_on; k <= oi && !act; k++) if (RAMP_ACT (k)) act = 1;
		} else
			act = RAMP_ACT (alg_rdly_on) && RAMP_ACT (oi);
		if (!act && fr > 0 && RAMP_ACT (oi + 1) && !RAMP_ACT (oi) && (oi + 1 >= alg_rdly_on || RAMP_ACT (alg_rdly_on))) {
			act = 1; ramp_part = fr;
		}
		#undef RAMP_ACT
		sig_ramp = act ? 0 : 0x80;
	  } }

	if (zero_tx < 0) { const char *z = getenv ("VT_ZERON"); zero_n = z ? atoi (z) : ZERO_N; z = getenv ("VT_ZEROTAU"); zero_tx = ZERO_TX; zero_ty = ZERO_TY;
	                   if (z) { double a = 0, b = -1; sscanf (z, "%lf,%lf", &a, &b); zero_tx = a; zero_ty = b < 0 ? a : b; } }
	if (zero_now == 0) {
		/* ZERO: the integrators' capacitors emptied - at once (zero_tx / zero_ty 0: the old model), or as an RC with
		 * the RAMP's input still flowing (see OFS_K) */
		if (zero_tx > 0.05 || zero_ty > 0.05) {
			int n = zero_n == 2 ? 2 : 1;                         /* (VT_ZERON=2: two stages, each tau / 2) */
			double kx = zero_tx > 0.05 ? 1.0 - exp (-(double) n / zero_tx) : 1.0, ky = zero_ty > 0.05 ? 1.0 - exp (-(double) n / zero_ty) : 1.0;
			long rx = sig_ramp == 0 ? (ramp_part < 1.0 ? (long) (adx * ramp_part) : adx) : 0;
			long ry = sig_ramp == 0 ? (ramp_part < 1.0 ? (long) (ady * ramp_part) : ady) : 0;
			double cx = (ALG_MAX_X*ZS) / 2, cy = (ALG_MAX_Y*ZS) / 2;
			if (n == 2) {
				if (!zero_was) { zero_x1 = alg_curr_x; zero_y1 = alg_curr_y; }
				zero_x1 += (cx - zero_x1) * kx; zero_y1 += (cy - zero_y1) * ky;
				cx = zero_x1; cy = zero_y1;
			}
			sig_dx = (long) ((cx - alg_curr_x) * kx) + rx;
			sig_dy = (long) ((cy - alg_curr_y) * ky) + ry;
		} else {
			sig_dx = (ALG_MAX_X*ZS) / 2 - alg_curr_x;
			sig_dy = (ALG_MAX_Y*ZS) / 2 - alg_curr_y;
		}
	} else {
		if (sig_ramp == 0) {
			sig_dx = ramp_part < 1.0 ? (long) (adx * ramp_part) : adx;
			sig_dy = ramp_part < 1.0 ? (long) (ady * ramp_part) : ady;
		} else {
			sig_dx = 0;
			sig_dy = 0;
		}
	}

	zero_was = zero_now == 0;
	if (!beam_init) { beam_init = 1; beam_x = beam_x1 = alg_curr_x; beam_y = beam_y1 = alg_curr_y; }
	if (alg_vectoring == 0) {
		if (sig_blank == 1 &&
			beam_x >= 0 && beam_x < (ALG_MAX_X*ZS) &&
			beam_y >= 0 && beam_y < (ALG_MAX_Y*ZS)) {

			/* start a new vector */

			alg_vectoring = 1;
			alg_vector_x0 = (long) beam_x;
			alg_vector_y0 = (long) beam_y;
			piece_bvx = beam_vx; piece_bvy = beam_vy;
			alg_vector_x1 = (long) beam_x;
			alg_vector_y1 = (long) beam_y;
			alg_vector_dx = sig_dx;
			alg_vector_dy = sig_dy;
			alg_vector_color = (unsigned char) alg_zsh;
			alg_vector_cyc = 0;
			alg_vector_t0 = alg_cycles;
		}
	} else {
		/* already drawing a vector ... check if we need to turn it off */

		if (sig_blank == 0) {
			/* blank just went on, vectoring turns off, and we've got a
			 * new line.
			 */

			alg_vectoring = 0;

			{ double fr = beam_off - (int) beam_off;         /* (still moving: lit a fraction further) */
			  long ex = 0, ey = 0;
			  if (fr > 0 && zero_now && sig_ramp == 0) { ex = (long) (fr * sig_dx); ey = (long) (fr * sig_dy); }
			  alg_addline (alg_vector_x0, alg_vector_y0,
						 alg_vector_x1 + ex, alg_vector_y1 + ey,
						 alg_vector_color);
			  /* VT_BEAMFADE=n (default 0 - owner: gave all text a shadow to the right): the spot does not go black at once - for n more cycles the
			   * moving beam leaves a faint trace (sent with intensity bit 7 set; the renderer draws it
			   * at BEAM_FADE of a line's brightness), so a line's dark end is softened, not hidden */
			  if (beam_fade < 0) { const char *z = getenv ("VT_BEAMFADE"); beam_fade = z ? atoi (z) : 0; }
			  if (beam_fade > 0 && zero_now && sig_ramp == 0 && alg_vector_color < 128) {
				fade_left = beam_fade; fade_x0 = alg_vector_x1 + ex; fade_y0 = alg_vector_y1 + ey;
				fade_col = alg_vector_color; fade_cyc = 0; fade_t0 = alg_cycles;
			  } }
		} else if ((vt_vlag > 0.05 ? (labs (sig_dx - alg_vector_dx) + labs (sig_dy - alg_vector_dy) >   /* (the speed smoothed: a */
				    (long) ((labs (alg_vector_dx) + labs (alg_vector_dy)) * vlag_split_k) + 2 * ZS)   /*  new piece once it changed */
				   : (sig_dx != alg_vector_dx || sig_dy != alg_vector_dy)) ||                  /*  a little: a curve in chords) */
				   (unsigned char) alg_zsh != alg_vector_color ||
				   (lag_moving && alg_vector_cyc >= LAG_SPLIT && fabs (beam_vx - piece_bvx) + fabs (beam_vy - piece_bvy) >
				    lag_split_k * (fabs (piece_bvx) + fabs (piece_bvy)) + 2 * ZS) ||   /* (the spot catching up: a curve, in chords) */
				   (vt_lag > 0.05 && fabs (beam_vx) + fabs (beam_vy) < 0.25 * ZS &&     /* (the spot has come to a stop: its */
				    fabs (piece_bvx) + fabs (piece_bvy) >= 0.5 * ZS)) {                 /*  rest a piece of its own - a spot) */

			/* the parameters of the vectoring processing has changed.
			 * so end the current line.
			 */

			alg_addline (alg_vector_x0, alg_vector_y0,
						 alg_vector_x1, alg_vector_y1,
						 alg_vector_color);

			/* we continue vectoring with a new set of parameters if the
			 * current point is not out of limits.
			 */

			if (beam_x >= 0 && beam_x < (ALG_MAX_X*ZS) &&
				beam_y >= 0 && beam_y < (ALG_MAX_Y*ZS)) {
				alg_vector_x0 = (long) beam_x;
				alg_vector_y0 = (long) beam_y;
				piece_bvx = beam_vx; piece_bvy = beam_vy;
				alg_vector_x1 = (long) beam_x;
				alg_vector_y1 = (long) beam_y;
				alg_vector_dx = sig_dx;
				alg_vector_dy = sig_dy;
				alg_vector_color = (unsigned char) alg_zsh;
				alg_vector_cyc = 0;
				alg_vector_t0 = alg_cycles;
			} else {
				alg_vectoring = 0;
			}
		}
	}

	if (vt_bright < 0) { const char *z = getenv ("VT_BRIGHT"); vt_bright = z ? atof (z) : 0; }
	if (vt_bright > 0 || dark_on) {
		int dark = !sig_blank && vt_bright > 0;                /* (only up: down, the blanked beam is darker still) */
		int inb = alg_curr_x >= 0 && alg_curr_x < (ALG_MAX_X*ZS) && alg_curr_y >= 0 && alg_curr_y < (ALG_MAX_Y*ZS);
		if (dark_on && (!dark || sig_dx != dark_dx || sig_dy != dark_dy || dark_cyc >= 1500)) {   /* (a long rest: in pieces, each frame sees it) */
			double c = alg_vector_cyc; unsigned long long t = alg_vector_t0;
			alg_vector_cyc = dark_cyc; alg_vector_t0 = dark_t0; dark_adding = 1;
			alg_addline (dark_x0, dark_y0, dark_x1, dark_y1, 0x80);   /* (the web: the renderer's to show) */
			dark_adding = 0; alg_vector_cyc = c; alg_vector_t0 = t;
			dark_on = 0;
		}
		if (dark && !dark_on && inb) {
			dark_on = 1; dark_x0 = dark_x1 = alg_curr_x; dark_y0 = dark_y1 = alg_curr_y;
			dark_dx = sig_dx; dark_dy = sig_dy; dark_cyc = 0; dark_t0 = alg_cycles;
		}
	}

	alg_curr_x += sig_dx;
	alg_curr_y += sig_dy;
	alg_cycles++;
	if (vt_lag < 0) { const char *z = getenv ("VT_DEFLAG"); vt_lag = z ? atof (z) : LAG_TAU; z = getenv ("VT_DEFLAGN"); vt_lag_n = z ? atoi (z) : LAG_N; z = getenv ("VT_DEFLAGSPLIT"); if (z) lag_split_k = atof (z); }
	if (vt_lag_y < -1.5) { const char *z = getenv ("VT_DEFLAGY"); vt_lag_y = z ? atof (z) : LAG_TAU_Y; }
	if (ofs_k < 0) { const char *z = getenv ("VT_OFSK"); ofs_k = z ? atof (z) : OFS_K; }
	{ double ox = beam_x, oy = beam_y;
	  /* where the integrators put the beam: their sum plus the reference's own shift (OFS_K; a perfect machine - VT_ZIDEAL
	   * - none) */
	  double sh = alg_zideal ? 0.0 : ofs_k * (alg_rshf - 128.0) * ZS;
	  double tx = alg_curr_x - sh, ty = alg_curr_y + sh;
	  double lx = vt_lag, ly = vt_lag_y >= 0 ? vt_lag_y : vt_lag;
	  if (lx > 0.05 || ly > 0.05) {
		int n = vt_lag_n == 2 ? 2 : 1;
		double kx = lx > 0.05 ? 1.0 - exp (-(double) n / lx) : 1.0;   /* (each stage tau / n) */
		double ky = ly > 0.05 ? 1.0 - exp (-(double) n / ly) : 1.0;
		if (n == 2) {
			beam_x1 += (tx - beam_x1) * kx; beam_y1 += (ty - beam_y1) * ky;
			beam_x += (beam_x1 - beam_x) * kx; beam_y += (beam_y1 - beam_y) * ky;
		} else {
			beam_x += (tx - beam_x) * kx; beam_y += (ty - beam_y) * ky;
		}
	  } else {
		beam_x = beam_x1 = tx; beam_y = beam_y1 = ty;
	  }
	  beam_vx = beam_x - ox; beam_vy = beam_y - oy;
	  /* still catching up: its speed off the integrators' by more than 3 % (and 0.02 units a cycle) */
	  { double ex = beam_vx - sig_dx, ey = beam_vy - sig_dy, v = fabs ((double) sig_dx) + fabs ((double) sig_dy);
	    lag_moving = fabs (ex) + fabs (ey) > 0.03 * v + 0.02 * ZS; } }
	if (vt_beam_mode < 0) { const char *z = getenv ("VT_BEAMMODE"); vt_beam_mode = z && atoi (z) > 0; }
	if (vt_beam_mode > 0) bp_step (sig_blank);
	if (vt_beam_hook) {
		double hx = beam_x / ZS, hy = beam_y / ZS;
		unsigned pc = e6809_get_pc () & 0xFFFF;                 /* (the text's shift, as alg_addline's) */
		if (pc >= 0xF495 && pc < 0xF511) hx -= vt_text_shift + (long) (vt_text_shift_w * ram[0x2B]);
		vt_beam_hook (hx, hy, (sig_blank && (alg_zsh & 0x80) == 0) ? alg_zlev : 0.0, (unsigned char) alg_zsh, alg_cycles);
	}
	if (dark_on) {
		if (alg_curr_x >= 0 && alg_curr_x < (ALG_MAX_X*ZS) && alg_curr_y >= 0 && alg_curr_y < (ALG_MAX_Y*ZS)) {
			dark_x1 = alg_curr_x; dark_y1 = alg_curr_y; dark_cyc += 1.0;
		}
	}

	if (fade_left) {                                    /* the fading trace (see VT_BEAMFADE) */
		int moving = sig_blank == 0 && zero_now && sig_ramp == 0;
		if (moving) { fade_cyc += 1.0; fade_left--; }
		if (!moving || !fade_left) {
			fade_left = 0;
			if (fade_x0 != alg_curr_x || fade_y0 != alg_curr_y) {
				double c = alg_vector_cyc; unsigned long long t = alg_vector_t0;
				alg_vector_cyc = fade_cyc; alg_vector_t0 = fade_t0;
				alg_addline (fade_x0, fade_y0, alg_curr_x, alg_curr_y, fade_col | 0x80);
				alg_vector_cyc = c; alg_vector_t0 = t;
			}
		}
	}

	if (alg_vectoring == 1 &&
		beam_x >= 0 && beam_x < (ALG_MAX_X*ZS) &&
		beam_y >= 0 && beam_y < (ALG_MAX_Y*ZS)) {

		/* we're vectoring ... current point is still within limits so
		 * extend the current vector (along the spot: see vt_lag).
		 */

		alg_vector_x1 = (long) beam_x;
		alg_vector_y1 = (long) beam_y;
		alg_vector_cyc += alg_zlev;
	}
}

#include "uvm2_cart.c"
#include "pitrex_cart.c"

void vecx_emu (long cycles)
{
	unsigned c, icycles;

	if (vt_uvm2 || vt_pitrex) {             /* the UVMC2 / the PiTrex drives the bus; the 6809 is held (uvm2_cart.c, pitrex_cart.c) */
		while (cycles > 0) {
			if (vt_pitrex) pitrex_cycle (); else uvm2_cycle ();
			via_sstep0 ();
			alg_sstep ();
			via_sstep1 ();
			vt_step (1);
			vx_cyc++;
			cycles--;
			fcycles--;
			if (fcycles < 0) {
				vector_t *tmp;
				fcycles += FCYCLES_INIT;
				vector_erse_cnt = vector_draw_cnt;
				vector_draw_cnt = 0;
				tmp = vectors_erse; vectors_erse = vectors_draw; vectors_draw = tmp;
			}
		}
		return;
	}

	while (cycles > 0) {
		if (vt_wtime < 0) { const char *z = getenv ("VT_WRITETIME"); vt_wtime = z ? atoi (z) : 2; }
		if (vt_wtime >= 2) {
			/* reads and writes both in the instruction's last cycle, as on a 6809: a dry run gives
			 * its length (which never depends on what it reads from the VIA), the VIA and the analog
			 * side run up to that last cycle, then the instruction runs for real, then the last cycle */
			unsigned st[10], irq = via_ifr & 0x80;
			extern void e6809_save (unsigned *), e6809_restore (const unsigned *);
			e6809_save (st);
			vt_trial = 1; unsigned n = e6809_sstep (irq, 0); vt_trial = 0;
			e6809_restore (st);
			for (c = 0; c + 1 < n; c++) {
				via_sstep0 ();
				alg_sstep ();
				via_sstep1 ();
			}
			icycles = e6809_sstep (irq, 0);
			for (c = n ? n - 1 : 0; c < icycles; c++) {
				via_sstep0 ();
				alg_sstep ();
				via_sstep1 ();
			}
			if (icycles < n) icycles = n;
			vt_step (icycles);
			vx_cyc += icycles;
			cycles -= (long) icycles;
			fcycles -= (long) icycles;
			if (fcycles < 0) {
				vector_t *tmp;
				fcycles += FCYCLES_INIT;
				vector_erse_cnt = vector_draw_cnt;
				vector_draw_cnt = 0;
				tmp = vectors_erse;
				vectors_erse = vectors_draw;
				vectors_draw = tmp;
			}
			continue;
		}
		wq_on = vt_wtime; wq_n = 0;
		icycles = e6809_sstep (via_ifr & 0x80, 0);
		wq_on = 0;

		for (c = 0; c < icycles; c++) {
			/* the queued writes: the last one in the last cycle, one before it a cycle earlier */
			if (wq_n) for (unsigned j = 0; j < wq_n; j++)
				if (c == (icycles > wq_n - j ? icycles - (wq_n - j) : 0)) write8 (wq_a[j], wq_d[j]);
			via_sstep0 ();
			alg_sstep ();
			via_sstep1 ();
		}

		vt_step (icycles);
		vx_cyc += icycles;
		cycles -= (long) icycles;

		fcycles -= (long) icycles;

		if (fcycles < 0) {
			vector_t *tmp;

			fcycles += FCYCLES_INIT;


			/* everything that was drawn during this pass now now enters
			 * the erase list for the next pass.
			 */

			vector_erse_cnt = vector_draw_cnt;
			vector_draw_cnt = 0;

			tmp = vectors_erse;
			vectors_erse = vectors_draw;
			vectors_draw = tmp;
		}
	}
}
