#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <string.h>
#include "e6809.h"
#include "vecx.h"
#include "e8910.h"

#define einline inline

unsigned char rom[8192];
unsigned char cart[1048576];  /* 32K / 64K (PB6) images, 256K: the 4-bank PB6 + IRQ cart (Vectorblade), 1M: the multicart */
unsigned long vt_bank_switches = 0;
void vt_line(long x0,long y0,long x1,long y1,unsigned char c,unsigned cyc,unsigned t0);
unsigned long long vt_frame_t0;   /* alg_cycles when this frame's drawing started (vtest sets it) */
void vt_step(unsigned icycles);
void vt_bankchange(int from,int to);
static unsigned char ram[1024];

/* the sound chip registers */

unsigned snd_regs[16];
static unsigned snd_select;

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
static int vb_bank (void)
{
	return cart_bank () + ((via_ifr & 0x80) ? 0 : 2);
}
int vt_cur_bank (void) { return vt_vb ? vb_bank () : cart_bank (); }
/* the flash: SST39SF020A command sequences (5555=AA, 2AAA=55, then 5555=A0 program / 80 erase / 90 ID
 * entry / F0 exit; erase: then AA, 55 and 30 at a 4K sector or 10 at 5555 for the whole chip). The
 * chip is instant here (a program / erase is done before the next read), so the game's polling sees
 * the result at once. Changes are kept in vt_flash_dirty; the frontend's core saves them. */
static int fl_step, fl_cmd, fl_id;
int vt_flash_dirty;
static void flash_write (unsigned address, unsigned data)
{
	unsigned a = address & 0x7fff;
	unsigned long base = (unsigned long) vb_bank () << 16;
	if (fl_cmd == 0xa0) {                               /* byte program: bits can only go 0 */
		cart[base + address] &= data; fl_cmd = 0; fl_step = 0; vt_flash_dirty = 1; return;
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
		if (data == 0x30) { memset (cart + ((base + address) & ~0xfffUL), 0xff, 4096); vt_flash_dirty = 1; }
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
static double alg_calshift;
static double alg_wob50, alg_wob100;     /* mains hum (VT_WOBBLE), see alg_wobble */
int vt_wobble_off;                        /* live: input flags bit 1 */
static double alg_ztau, alg_zk, alg_zlev;
/* VT_ANALOGDLY=1: the Vectrex's analog delays (as Malban measured them for VIDE / libretro-vecx):
 * the X integrator gets the DAC value 15 cycles late, Y its sample-and-hold 14 cycles late, the
 * integrators start 12 / stop 15 cycles after RAMP switches, the beam zeroes 5 cycles after /ZERO.
 * Code timed to the cycle on a real Vectrex (Vectorblade's 7-cycle 'smartlist' sprites) needs them. */
static int t1_irqlag = -1, t1_irqwait;   /* VT_T1IRQLAG (see via_sstep0) */
static int alg_adly;
static double beam_off = -1;     /* VT_BEAMOFF (see alg_sstep) */
static int beam_fade = -1, fade_left;   /* VT_BEAMFADE: the beam's faint fade-out after it goes dark (alg_sstep) */
static long fade_x0, fade_y0; static double fade_cyc; static unsigned char fade_col; static unsigned long long fade_t0;   /* on by default (VT_ANALOGDLY=0: off); with it the BIOS text needs no fitted shift: the
                       * delays put it where the owner's photos show it (within 0.1 px) */
static unsigned char dly_x[64], dly_y[64], dly_z[64]; static unsigned dly_p;
static unsigned dly_nx = 15, dly_ny = 14, dly_nz = 5;   /* VT_XYZDLY=x,y,zero: the S/H and zero delays */
extern long vt_text_shift; extern double vt_text_shift_w;
static int alg_zideal;           /* VT_ZIDEAL=1: a perfect machine, the zero reference stays at centre */
static unsigned alg_xsh;  /* x sample and hold */
static unsigned alg_ysh;  /* y sample and hold */
/* the Y hold is a capacitor charged through the multiplexer: each cycle it is connected it closes
 * YSH_K of the gap to the DAC. The owner's Vectrex (testcart/sh_test, 2026-10-14): connected 4 cycles
 * after a 191-step jump it was ~1.5 steps short (~0.8 %), from 6 cycles on it had settled: 0.3^4 left
 * per 4 cycles -> 0.7 (VT_YSHK=x; 1 = instant, as vecx) */
static double alg_yshf = 128, ysh_k = -1;
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
		}

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
		break;
	}

	/* compare the current joystick direction with a reference */

	if (alg_jsh > alg_xsh) {
		alg_compare = 0x20;
	} else {
		alg_compare = 0;
	}

	/* compute the new "deltas" */

	alg_dx = (long) alg_xsh * ZS - (long) ((alg_rshf + alg_zofs) * ZS) + alg_xofs;
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

				/* fall through */

			case 0xf:
				if ((via_orb & 0x18) == 0x08) {
					/* the snd chip is driving port a */

					data = (unsigned char) snd_regs[snd_select];
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
				{ unsigned pc = e6809_get_pc () & 0xffff; static int n;
				  if (getenv ("VT_WATCH_T2") && pc < 0xE000 && n++ < 20) fprintf (stderr, "T2 low read at PC=%04X addr=%04X\n", pc, address); }

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
				{ unsigned pc = e6809_get_pc () & 0xffff; static int n;
				  if (getenv ("VT_WATCH_T2") && pc < 0xE000 && n++ < 20) fprintf (stderr, "T2 high write at PC=%04X addr=%04X\n", pc, address); }

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
	unsigned page = vt_mc_page;                         /* (the multicart's latch is not reset by the button) */
	memcpy (keep, ram, sizeof keep);
	vecx_reset ();
	memcpy (ram, keep, sizeof keep);
	vt_mc_page = page;
}

void vecx_reset (void)
{
	{ extern unsigned char e6809_obus_idle; e6809_obus_idle = rom[0x1fff]; }   /* (the BIOS byte at $FFFF: the idle cycles' bus) */
	vt_mc_page = 0; fl_step = fl_cmd = fl_id = 0;          /* (power-on: the multicart's menu page, the flash idle) */
	unsigned r;
	static int ow_ready;
	if (!ow_ready) { ow_ready = 1; ow_init (); const char *z = getenv ("VT_ZOFS"); if (z) alg_zofs = atof (z); alg_zideal = getenv ("VT_ZIDEAL") != NULL; vt_sr1 = getenv ("VT_SR1") != NULL; z = getenv ("VT_WOBBLE"); if (z) sscanf (z, "%lf,%lf", &alg_wob50, &alg_wob100); z = getenv ("VT_ZTAU"); if (z) { alg_ztau = atof (z); alg_zk = 1.0 - exp (-1.0 / alg_ztau); } z = getenv ("VT_CALSHIFT"); if (z) alg_calshift = atof (z); z = getenv ("VT_ZREFK"); if (z) ZREF_K = atof (z); z = getenv ("VT_RAMPDLY"); if (z) { alg_rdly_off_t1 = 0; if (sscanf (z, "%u,%u,%u", &alg_rdly_on, &alg_rdly_off, &alg_rdly_off_t1) < 3) alg_rdly_off_t1 = alg_rdly_off; } z = getenv ("VT_RAMPOFF"); if (z) { double a = 0, b = -1; sscanf (z, "%lf,%lf", &a, &b); if (b < 0) b = a; alg_rdly_on = alg_rdly_on ? alg_rdly_on : 12; alg_rdly_off = (unsigned) a; rdly_frac = a - (unsigned) a; alg_rdly_off_t1 = (unsigned) b; rdly_frac_t1 = b - (unsigned) b; } z = getenv ("VT_XYZDLY"); if (z) sscanf (z, "%u,%u,%u", &dly_nx, &dly_ny, &dly_nz); z = getenv ("VT_ANALOGDLY"); alg_adly = !(z && atoi (z) == 0); if (alg_adly && !getenv ("VT_RAMPDLY") && !getenv ("VT_RAMPOFF")) { alg_rdly_on = 12; alg_rdly_off = 12; alg_rdly_off_t1 = 12; rdly_frac = rdly_frac_t1 = 0.3; }   /* (stop 12.3 cycles with timer 1 one cycle longer (VT_T1EXTRA=1); Malban: 15. Fitted to the owner's test cart photos 2026-10-12 (11.8 fitted the lines best, 12.3 gives Mine Storm's ship its ~15 % short lines): line ends dark only at scales $0B-$0C / $12-$13 as on the tube; Robot Arena's bowties closed; Mine Storm's mothership interior closed, ship lines ~25 % short) */ if (alg_adly) { if (!getenv ("VT_TEXTSHIFT")) vt_text_shift = 0; if (!getenv ("VT_TEXTSHIFTW")) vt_text_shift_w = 0; } z = getenv ("VT_HWOFS"); if (z) { double ox = 0, oy = 0; sscanf (z, "%lf,%lf", &ox, &oy); alg_xofs = (long) (ox * ZS); alg_yofs = (long) (oy * ZS); } }

	/* ram */

	for (r = 0; r < 1024; r++) {
		ram[r] = r & 0xff;
	}

	for (r = 0; r < 16; r++) {
		snd_regs[r] = 0;
		e8910_write(r, 0);
	}

	/* input buttons */

	snd_regs[14] = 0xff;
	e8910_write(14, 0xff);

	snd_select = 0;

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
static einline void alg_addline (long x0, long y0, long x1, long y1, unsigned char color)
{
	unsigned long key;
	long index;

	x0 /= ZS; y0 /= ZS; x1 /= ZS; y1 /= ZS;
	{ unsigned pc = e6809_get_pc () & 0xFFFF;
	  if (pc >= 0xF495 && pc < 0xF511) { long sh = vt_text_shift + (long) (vt_text_shift_w * ram[0x2B]); x0 -= sh; x1 -= sh; } }
	if ((alg_wob50 != 0 || alg_wob100 != 0) && !vt_wobble_off) {
		long ax, ay, bx, by;
		alg_wobble (alg_vector_t0, &ax, &ay); alg_wobble (alg_cycles, &bx, &by);
		x0 += ax; y0 -= ay; x1 += bx; y1 -= by;          /* (screen y grows downwards) */
	}
	{ unsigned long long tf = alg_vector_t0 > vt_frame_t0 ? alg_vector_t0 - vt_frame_t0 : 0;
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
	{ static unsigned bpos; static unsigned char bh[16]; int boff = (int) beam_off;
	  if (boff) {
		bh[bpos++ & 15] = sig_blank ? 1 : 0;
		for (int k = 1; k <= boff; k++) if (bh[(bpos - 1 - k) & 15]) sig_blank = 1;
	  } }
	/* VT_ZTAU=t: the beam switches on almost at once but fades out with a time constant of t cycles
	 * at full intensity, shorter when dimmer (the owner's photos, 2026-10-08: at intensity 127 the
	 * ROBOT ARENA title's dashes, single pixels too, are ~3.5 cycles longer; the intensity-80 small
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
		alg_yshf += ((double) alg_xsh - alg_yshf) * ysh_k;
		alg_ysh = (unsigned) (alg_yshf + 0.5);
		alg_dy = (long) ((alg_rshf + alg_zofs) * ZS) - (long) (alg_yshf * ZS) + alg_yofs;
	}
	if ((via_orb & 0x07) == 0x02) {
		/* mux enabled, select 1: the zero reference capacitor charges toward the DAC value */
		if (!alg_zideal) alg_rshf += ((double) alg_xsh - alg_rshf) * ZREF_K;
		alg_dx = (long) alg_xsh * ZS - (long) ((alg_rshf + alg_zofs) * ZS) + alg_xofs;
		alg_dy = (long) ((alg_rshf + alg_zofs) * ZS) - (long) (alg_yshf * ZS) + alg_yofs;
	}

	long adx = alg_dx, ady = alg_dy;                    /* (the integrators' inputs now) */
	unsigned zero_now = via_ca2;
	if (alg_adly) {
		dly_x[dly_p & 63] = (unsigned char) alg_xsh; dly_y[dly_p & 63] = (unsigned char) alg_ysh;
		dly_z[dly_p & 63] = (unsigned char) via_ca2; dly_p++;
		unsigned xd = dly_x[(dly_p - 1 - dly_nx) & 63], yd = dly_y[(dly_p - 1 - dly_ny) & 63];
		zero_now = dly_z[(dly_p - 1 - dly_nz) & 63];
		adx = (long) xd * ZS - (long) ((alg_rshf + alg_zofs) * ZS) + alg_xofs;
		ady = (long) ((alg_rshf + alg_zofs) * ZS) - (long) yd * ZS + alg_yofs;
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

	if (zero_now == 0) {
		/* need to force the current point to the 'orgin' so just
		 * calculate distance to origin and use that as dx,dy.
		 */

		sig_dx = (ALG_MAX_X*ZS) / 2 - alg_curr_x;
		sig_dy = (ALG_MAX_Y*ZS) / 2 - alg_curr_y;
	} else {
		if (sig_ramp == 0) {
			sig_dx = ramp_part < 1.0 ? (long) (adx * ramp_part) : adx;
			sig_dy = ramp_part < 1.0 ? (long) (ady * ramp_part) : ady;
		} else {
			sig_dx = 0;
			sig_dy = 0;
		}
	}

	if (alg_vectoring == 0) {
		if (sig_blank == 1 &&
			alg_curr_x >= 0 && alg_curr_x < (ALG_MAX_X*ZS) &&
			alg_curr_y >= 0 && alg_curr_y < (ALG_MAX_Y*ZS)) {

			/* start a new vector */

			alg_vectoring = 1;
			alg_vector_x0 = alg_curr_x;
			alg_vector_y0 = alg_curr_y;
			alg_vector_x1 = alg_curr_x;
			alg_vector_y1 = alg_curr_y;
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
		} else if (sig_dx != alg_vector_dx ||
				   sig_dy != alg_vector_dy ||
				   (unsigned char) alg_zsh != alg_vector_color) {

			/* the parameters of the vectoring processing has changed.
			 * so end the current line.
			 */

			alg_addline (alg_vector_x0, alg_vector_y0,
						 alg_vector_x1, alg_vector_y1,
						 alg_vector_color);

			/* we continue vectoring with a new set of parameters if the
			 * current point is not out of limits.
			 */

			if (alg_curr_x >= 0 && alg_curr_x < (ALG_MAX_X*ZS) &&
				alg_curr_y >= 0 && alg_curr_y < (ALG_MAX_Y*ZS)) {
				alg_vector_x0 = alg_curr_x;
				alg_vector_y0 = alg_curr_y;
				alg_vector_x1 = alg_curr_x;
				alg_vector_y1 = alg_curr_y;
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

	alg_curr_x += sig_dx;
	alg_curr_y += sig_dy;
	alg_cycles++;

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
		alg_curr_x >= 0 && alg_curr_x < (ALG_MAX_X*ZS) &&
		alg_curr_y >= 0 && alg_curr_y < (ALG_MAX_Y*ZS)) {

		/* we're vectoring ... current point is still within limits so
		 * extend the current vector.
		 */

		alg_vector_x1 = alg_curr_x;
		alg_vector_y1 = alg_curr_y;
		alg_vector_cyc += alg_zlev;
	}
}

void vecx_emu (long cycles)
{
	unsigned c, icycles;

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
