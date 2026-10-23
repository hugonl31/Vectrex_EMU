/* demo.c - a small UVMC2 (.um2) demo for Vectrex_EMU: a rotating 3D cube computed on the cartridge's RP2350 and a
 * title, drawn on the Vectrex by the chip itself. Written from scratch (no UVMC2 SDK code); by Hugo Bokhorst with
 * Claude, free to use and share (MIT, see the LICENSE note in README.md).
 *
 * How a .um2 program draws: the RP2350 holds the 6809 in /HALT and becomes the bus master. Each E period it presents
 * one access - while ~E (GP31) is high (E low) it puts the address, R/W and, for a write, the data on the pins; while
 * ~E is low (E high) a read's data is valid; a write lands as ~E rises again. So the program writes the VIA's
 * registers itself, as the BIOS's line routines do: the DAC (port A), the multiplexer and RAMP (port B), /ZERO and
 * /BLANK (CA2 / CB2 through the PCR). One bus access = one E cycle = 2/3 us: delays are counted in accesses.
 *
 * Pins (UVMC2): D0-D7 GP0-7, A0-A13 GP8-21, A14 GP24, A15 GP25, R/W GP26, /HALT GP27, ~E GP31. */
#include <stdint.h>
#include "sintab.h"                                 /* (sin_q14[256], made by make_um2.py) */

#define REG(a)          (*(volatile uint32_t *) (a))
#define SIO_IN          REG (0xd0000004u)
#define SIO_OUT         REG (0xd0000010u)
#define SIO_OE          REG (0xd0000030u)
#define IO_CTRL(n)      REG (0x40028004u + 8u * (n))
#define PAD(n)          REG (0x40038004u + 4u * (n))

#define NE              (1u << 31)                   /* ~E */
#define RW              (1u << 26)
#define HALT            (1u << 27)
#define DATA            0xffu
#define ADDR            ((0x3fffu << 8) | (1u << 24) | (1u << 25))

/* the VIA */
#define ORB 0xd000
#define ORA 0xd001
#define DDRB 0xd002
#define DDRA 0xd003
#define ACR 0xd00b
#define PCR 0xd00c
#define IER 0xd00e
#define IDLE 0xc880                                  /* (a RAM byte: a harmless read for a delay) */

static uint32_t accesses;                           /* (bus accesses = E cycles: the frame's clock) */

static inline uint32_t abits (uint32_t a)
{
	return ((a & 0x3fffu) << 8) | (((a >> 14) & 1u) << 24) | (((a >> 15) & 1u) << 25);
}
/* one bus access: wait for this E period's start (~E high), present it, wait for E high (~E low) */
static uint32_t bus (uint32_t a, int write, uint32_t d)
{
	while (!(SIO_IN & NE)) ;
	if (write) {
		SIO_OUT = abits (a) | (d & DATA);            /* (/HALT low: bit 27 clear) */
		SIO_OE = ADDR | RW | HALT | DATA;
	} else {
		SIO_OE = ADDR | RW | HALT;
		SIO_OUT = abits (a) | RW;
	}
	while (SIO_IN & NE) ;
	accesses++;
	return SIO_IN & DATA;
}
static void wr (uint32_t a, uint32_t d) { bus (a, 1, d); }
static void wait (int n) { while (n-- > 0) bus (IDLE, 0, 0); }

/* ---- drawing, as the BIOS does it: the beam position is DAC value x ramp time */
#define T_STEP 96                                    /* (ramp cycles per move: DAC 127 x 96 ~ the screen's half width) */
static void zero (void)                              /* the beam to the middle, the integrators reset */
{
	wr (ORA, 0);
	wr (PCR, 0xcc);                                  /* (/ZERO low, beam off) */
	wait (8);
	wr (PCR, 0xce);
}
static void intensity (int z)
{
	wr (ORA, (uint32_t) z & 0xff);
	wr (ORB, 0x84);                                  /* (mux on, select Z, RAMP off) */
	wr (ORB, 0x81);
}
/* the beam moves by (dx, dy) DAC units for T_STEP cycles, lit or not */
static void step (int dx, int dy, int lit)
{
	/* (the analog side lags, as on the console: the Y sample-and-hold settles over ~14 cycles, the X integrator
	 * sees a new DAC value ~15 cycles late, and the integrators run on ~12 cycles after RAMP goes off - so wait
	 * for each before the next change, as the BIOS's timing does) */
	wr (ORA, (uint32_t) dy & 0xff);
	wr (ORB, 0x80);                                  /* (mux on, select Y: the Y hold takes dy) */
	wait (16);
	wr (ORB, 0x81);                                  /* (mux off) */
	wr (ORA, (uint32_t) dx & 0xff);                  /* (X: straight from the DAC) */
	wait (16);
	if (lit) wr (PCR, 0xee);                         /* (beam on) */
	wr (ORB, 0x01);                                  /* (RAMP on) */
	wait (T_STEP - 4);
	wr (ORB, 0x81);                                  /* (RAMP off) */
	wait (12);                                       /* (the integrators stop - the beam still on: the line's end lit) */
	if (lit) wr (PCR, 0xce);
	wait (2);
}
static int cx, cy;                                   /* (where the beam is, DAC units since the last zero) */
static void move_to (int x, int y)
{
	while (x != cx || y != cy) {                     /* (steps of at most 127) */
		int dx = x - cx, dy = y - cy;
		if (dx > 127) dx = 127; if (dx < -127) dx = -127;
		if (dy > 127) dy = 127; if (dy < -127) dy = -127;
		step (dx, dy, 0); cx += dx; cy += dy;
	}
}
static void line_to (int x, int y)
{
	int dx = x - cx, dy = y - cy;
	if (dx > 127) dx = 127; if (dx < -127) dx = -127;
	if (dy > 127) dy = 127; if (dy < -127) dy = -127;
	step (dx, dy, 1); cx += dx; cy += dy;
}
static void from_middle (void) { zero (); cx = cy = 0; }

/* ---- the title: a little stroke font on a 4 x 6 grid (pairs of points: a line each) */
static const signed char G_U[] = { 0,6, 0,0,  0,0, 4,0,  4,0, 4,6, -1 };
static const signed char G_V[] = { 0,6, 2,0,  2,0, 4,6, -1 };
static const signed char G_M[] = { 0,0, 0,6,  0,6, 2,3,  2,3, 4,6,  4,6, 4,0, -1 };
static const signed char G_C[] = { 4,6, 0,6,  0,6, 0,0,  0,0, 4,0, -1 };
static const signed char G_2[] = { 0,6, 4,6,  4,6, 4,3,  4,3, 0,3,  0,3, 0,0,  0,0, 4,0, -1 };
static const signed char G_D[] = { 0,0, 0,6,  0,6, 3,6,  3,6, 4,5,  4,5, 4,1,  4,1, 3,0,  3,0, 0,0, -1 };
static const signed char G_E[] = { 4,6, 0,6,  0,6, 0,0,  0,0, 4,0,  0,3, 3,3, -1 };
static const signed char G_O[] = { 0,0, 0,6,  0,6, 4,6,  4,6, 4,0,  4,0, 0,0, -1 };
static const signed char *const TITLE[] = { G_U, G_V, G_M, G_C, G_2, 0, G_D, G_E, G_M, G_O };
static void title (int x0, int y0, int k)
{
	intensity (0x5f);
	from_middle ();
	for (unsigned i = 0; i < sizeof TITLE / sizeof TITLE[0]; i++, x0 += 6 * k) {
		const signed char *g = TITLE[i];
		if (!g) continue;
		for (; g[0] >= 0; g += 4) {
			move_to (x0 + g[0] * k, y0 + g[1] * k);
			line_to (x0 + g[2] * k, y0 + g[3] * k);
		}
	}
}

/* ---- the cube */
static const signed char CUBE[8][3] = { {-1,-1,-1},{1,-1,-1},{1,1,-1},{-1,1,-1},{-1,-1,1},{1,-1,1},{1,1,1},{-1,1,1} };
static const unsigned char EDGES[12][2] = { {0,1},{1,2},{2,3},{3,0},{4,5},{5,6},{6,7},{7,4},{0,4},{1,5},{2,6},{3,7} };
static int sn (unsigned a) { return sin_q14[a & 255]; }
static int cs (unsigned a) { return sin_q14[(a + 64) & 255]; }
static void cube (unsigned ax, unsigned ay, unsigned az)
{
	int px[8], py[8];
	for (int i = 0; i < 8; i++) {
		int x = CUBE[i][0] * 16384, y = CUBE[i][1] * 16384, z = CUBE[i][2] * 16384, t;
		t = (y * cs (ax) - z * sn (ax)) >> 14; z = (y * sn (ax) + z * cs (ax)) >> 14; y = t;     /* (about X) */
		t = (x * cs (ay) + z * sn (ay)) >> 14; z = (-x * sn (ay) + z * cs (ay)) >> 14; x = t;    /* (about Y) */
		t = (x * cs (az) - y * sn (az)) >> 14; y = (x * sn (az) + y * cs (az)) >> 14; x = t;     /* (about Z) */
		int d = 4 * 16384 - z;                       /* (perspective: the eye 4 units away) */
		px[i] = (x * 150) / d; py[i] = (y * 150) / d - 10;
	}
	intensity (0x7f);
	for (int e = 0; e < 12; e++) {
		from_middle ();
		move_to (px[EDGES[e][0]], py[EDGES[e][0]]);
		line_to (px[EDGES[e][1]], py[EDGES[e][1]]);
	}
}

void main_loop (void)
{
	/* the pins: the bus as SIO outputs, /HALT low for good (the 6809 stops) */
	for (int n = 0; n < 32; n++) {
		IO_CTRL (n) = 5;                             /* (FUNCSEL: SIO) */
		PAD (n) = (PAD (n) & ~(1u << 8)) | (1u << 6);    /* (pad: isolation off, input on) */
	}
	SIO_OUT = abits (IDLE) | RW;                     /* (a read; /HALT low) */
	SIO_OE = ADDR | RW | HALT;
	for (volatile int i = 0; i < 20000; i++) ;       /* (the 6809 finishes its instruction and halts) */
	/* the VIA, as the BIOS sets it up (RAMP by hand: ACR bit 7 off) */
	wr (DDRA, 0xff); wr (DDRB, 0x9f); wr (ACR, 0x00); wr (IER, 0x7f); wr (PCR, 0xce); wr (ORB, 0x81);
	unsigned f = 0;
	for (;;) {
		uint32_t start = accesses;
		cube (f, f * 2 / 3, f / 3);
		title (-90, 95, 4);
		f++;
		while (accesses - start < 30000) wait (1);   /* (50 frames a second: 30000 E cycles each) */
	}
}

/* ---- start-up: the vector table, the stack, .bss cleared */
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
