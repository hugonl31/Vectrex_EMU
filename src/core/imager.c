/* imager.c - the 3D Imager on controller port 2 (included by vecx.c): GCE's goggles (1983) with a spinning disc in front
 * of the eyes - half of it black, half red, green and blue filters - that turns the Vectrex's white lines into colour
 * (and 3D: each eye sees every other picture).
 *
 * The disc's motor is the game's: it switches it with the AY's port A bit 6 (port 2's button 3 line, the port set to
 * output), pulse width modulated, and the disc speeds up, slows down and coasts as a motor with a flywheel does. Once a
 * turn a hole in the disc passes a sensor: port A bit 7 (button 4) goes low for a moment, and the same line pulses the
 * VIA's CA1 - the game times its pictures from it and adjusts the motor until one turn matches its frame. So the disc
 * spins up at power on, the colours run wild until the game has it at speed, and they settle - as on the real goggles.
 *
 * The motor and the disc as MAME models them (Mathis Rosenhauer, BSD-3-Clause: MMI the disc's moment of inertia, DAMPC
 * the damping, the motor's torque falling with the speed: 50 - 1.55 x revolutions a second), and each game's disc: the
 * angles where its colour sectors start, as fractions of a turn (each half turn one eye: blue, then two more sectors).
 * 3D Mine Storm's disc has a thin red and a wide green sector (its mines are green); Narrow Escape's and Crazy
 * Coaster's are about thirds: blue, green, red.
 *
 * Each line gets the colour of the filter in front of the eye at its middle (vt_line_rgb: bits 0-2 red, green, blue;
 * bit 3 the second half turn - the other eye). VT_IMAGER: 1 (a disc of thirds), mine, narrow or coaster.
 *
 * Copyright (c) 2026 Hugo Bokhorst. Part of Vectrex_EMU; see LICENSE.md. */

int vt_imager;                              /* the goggles on port 2 */
unsigned char vt_line_rgb;                  /* the line being added: its colour (0: no goggles) */
static double im_a1 = 0.1631, im_a2 = 0.3305;   /* (the sectors' starts in each half turn) */
static int im_mine;                         /* (3D Mine Storm's colour order: blue, red, green) */
static double im_freq, im_phase;            /* (turns a second; the angle as a fraction of a turn) */
static int im_on;                           /* (the motor powered: port A bit 6 low) */
static unsigned long long im_t, im_due = ~0ull, im_pin_until;
/* MAME's motor and disc (the same numbers - MMI 5, DAMPC -0.2, the torque 50 - 1.55 x speed - but run continuously,
 * not only at the end of each pulse: so a motor held on, as the games do while it spins up, speeds the disc too).
 * Powered: d speed / dt = (50 - 1.55 v) / 5 - 0.2 v / 5 = 10 - 0.35 v (at most 28.6 turns a second, reached with a
 * time constant of ~2.9 s); unpowered it coasts: d speed / dt = -0.04 v. */
static double ims_ph, ims_turn, ims_drive, ims_n1, ims_n2, ims_n3;
#define IM_VMAX  (10.0 / 0.35)
#define IM_KON   0.35
#define IM_KOFF  0.04

static void im_init (void)
{
	const char *z = getenv ("VT_IMAGER");
	vt_imager = z && *z && strcmp (z, "0") != 0;
	im_mine = 0; im_a1 = 0.1631; im_a2 = 0.3305;
	if (z && !strcmp (z, "mine")) { im_mine = 1; im_a1 = 0.1692; im_a2 = 0.2086; }
	else if (z && !strcmp (z, "1")) { im_a1 = 1.0 / 6.0; im_a2 = 1.0 / 3.0; }
	im_freq = 0.0; im_phase = 0.0; im_on = 0;
	im_t = alg_cycles; im_due = ~0ull; im_pin_until = 0;
	ims_ph = ims_turn = ims_drive = ims_n1 = ims_n2 = ims_n3 = 0.0;
}

/* the disc's speed and angle now: the motor's pull (or the coasting) since the last time, solved exactly */
static void im_move (void)
{
	if (alg_cycles > im_t) {
		double dt = (double) (alg_cycles - im_t) / 1500000.0, v0 = im_freq;
		if (im_on) {
			double e = exp (-IM_KON * dt);
			im_freq = IM_VMAX + (v0 - IM_VMAX) * e;
			im_phase += IM_VMAX * dt + (v0 - IM_VMAX) * (1.0 - e) / IM_KON;
		} else {
			double e = exp (-IM_KOFF * dt);
			im_freq = v0 * e;
			im_phase += v0 * (1.0 - e) / IM_KOFF;
		}
	}
	im_t = alg_cycles;
	if (im_phase >= 1.0 && im_due == ~0ull) im_phase -= floor (im_phase);   /* (no index planned: too slow for one) */
}
/* the cycle the index hole passes next (at today's speed: it changes little in one turn - it is checked again then) */
static void im_plan (void)
{
	if (im_freq > 1.0) {
		double left = im_phase < 1.0 ? 1.0 - im_phase : 0.0;
		im_due = alg_cycles + (unsigned long long) (left / im_freq * 1500000.0) + 1;
	} else im_due = ~0ull;                  /* (standing, or nearly: no index) */
}
/* the hole passes the sensor: button 4 low for a degree of the turn, and CA1 pulsed */
static void im_index (void)
{
	static int lg = -1; static unsigned long long lt;
	im_move ();
	if (im_phase < 0.999) { im_plan (); return; }   /* (not there yet: the speed changed - wait on) */
	if (lg < 0) lg = getenv ("VT_IMLOG") != NULL;
	if (lg && alg_cycles - lt >= 1500000ull) { lt = alg_cycles; fprintf (stderr, "imager: %.2f turns a second\n", im_freq); }
	im_phase -= floor (im_phase + 0.001);
	if (im_phase < 0.0) im_phase = 0.0;
	im_pin_until = alg_cycles + (unsigned long long) (1500000.0 / (im_freq * 360.0)) + 1;
	via_ifr |= 0x02;                        /* (CA1: whichever edge it waits for - the pulse has both) */
	int_update ();
	im_plan ();
}
static einline void im_tick (void)
{
	if (alg_cycles >= im_due) im_index ();
}
/* port A bit 6: the motor, powered while low (the port an output driving 0; as an input it floats high: off) */
static void im_motor (int level)
{
	int on = level == 0;
	if (on == im_on) return;
	im_move ();
	im_on = on;
	im_plan ();                             /* (the next index at the speed it has now) */
}
/* the filter in front of the eyes at cycle tc: red 1, green 2, blue 4; +8 in the second half turn */
static unsigned char im_color (unsigned long long tc)
{
	double p = im_phase + (tc > im_t ? im_freq * (double) (tc - im_t) / 1500000.0 : 0.0);
	p -= floor (p);
	int half = p >= 0.5;
	double f = p - 0.5 * half;
	unsigned char c = f < im_a1 ? 4 : f < im_a2 ? (im_mine ? 1 : 2) : (im_mine ? 2 : 1);
	return (unsigned char) (c | (half ? 8 : 0));
}

/* the disc's speed now (turns a second): live.c sends it with each frame (the frontend's spin-up note) */
double vt_imager_speed (void)
{
	im_move ();
	return im_freq;
}

/* The motor's sound, on the second audio channel (psg.c: 44100 samples a second - the frontend mixes it beside
 * the Vectrex's, not through its knob or speaker: it is on the player's head). A small brushed DC motor - the
 * kind in cassette players and toys - turning ~5 times faster than the disc (a belt or a pinion: such motors run
 * at 6000-8000 rpm, the disc at ~1560). Its sound:
 * - the whine: the current's ripple at each commutation, 6 a turn with 3 poles and 2 brushes (~780 Hz at speed),
 *   stronger while the game powers the motor - its pulses, as the game regulates the speed, are heard as such;
 * - the brushes' hiss: noise (~0.9-4.5 kHz) in bursts at each commutation;
 * - the air the disc pushes: a low rush growing with the square of its speed, swelling once a turn (its
 *   imbalance). All of it silent while the disc stands, rising with it as it spins up, falling as it coasts. */
#define IM_GEAR 5.0
static unsigned ims_seed = 22222u;
static double im_motor_sample (void)
{
	const double dt = 1.0 / 44100.0, PI2 = 6.283185307179586;
	if (!vt_imager) return 0.0;
	im_move ();
	double v = im_freq > 0.0 ? im_freq : 0.0, s = v / IM_VMAX;
	ims_drive += ((im_on ? 1.0 : 0.0) - ims_drive) * 0.02;            /* (the winding's current: ~1 ms) */
	ims_ph += v * IM_GEAR * 6.0 * dt; ims_ph -= floor (ims_ph);       /* (the commutations) */
	ims_turn += v * dt; ims_turn -= floor (ims_turn);                  /* (the disc) */
	double p = ims_ph;
	double whine = sin (PI2 * p) + 0.45 * sin (2 * PI2 * p) + 0.25 * sin (3 * PI2 * p) + 0.12 * sin (5 * PI2 * p);
	ims_seed = ims_seed * 1664525u + 1013904223u;
	double wn = (double) (ims_seed >> 9) / 4194304.0 - 1.0;
	ims_n1 += (wn - ims_n1) * 0.45;                                    /* (low pass ~4.5 kHz) */
	double hp = ims_n1 - ims_n2; ims_n2 += (ims_n1 - ims_n2) * 0.12;   /* (high pass ~0.9 kHz) */
	double brush = hp * (p < 0.15 ? 1.0 : 0.55);
	ims_n3 += (wn - ims_n3) * 0.03;                                    /* (low pass ~210 Hz) */
	double air = ims_n3 * 3.0;
	double out = whine * s * (0.08 + 0.20 * ims_drive)
	           + brush * s * (0.10 + 0.30 * ims_drive)
	           + air * s * s * 0.9;
	return 0.2 * out * (1.0 + 0.10 * sin (PI2 * ims_turn));   /* (quiet: a motor by the ears, under the game's sound) */
}

/* ------------------------------------------------------------------ the light pen */
/* GCE's light pen (1983, Art Master, AnimAction, Melody Master), also on controller port 2: a photodiode in its tip.
 * When the lit spot passes under it, it pulls the same line as the Imager's index - port A bit 7 (button 4), which
 * also pulses the VIA's CA1 - and the game, which knows where it is drawing at that moment, knows where the pen is.
 * Here the pen is the PC's mouse (the frontend sends its place and whether it touches the screen: vt_set_analog 24 /
 * 25 / 26), and the spot is the emulator's own - where the beam really is, with its lag, each cycle: the pen fires
 * when the lit spot comes within LP_R of it, once each time it passes (the spot out of its sight a few cycles: armed
 * again - so it sees each dot of Art Master's tracking circle, not only the first). VT_LIGHTPEN=1 */
int vt_lightpen;
static double lp_x = -1e12, lp_y = -1e12;  /* (the pen's place, in the integrators' units) */
static int lp_down, lp_in;
static unsigned long long lp_pin_until;
static double lp_r = 450.0 * ZS;            /* (the patch of tube it sees: its lens, the spot, the gap to the glass -
                                               ~1.8 mm; VT_LPR in the lines' units. Wider, Art Master's cursor
                                               trails further behind the pen; narrower, it is harder to pick up) */
#define LP_R   lp_r
#define LP_GAP 12                           /* (dark under the pen this many cycles: armed for the next pass - the
                                               photodiode's amplifier passes changes, not steady light) */
static unsigned long long lp_dark;          /* (since when the pen sees no lit spot) */
#define LP_PULSE 40                         /* (its pulse: ~27 us of the photodiode's amplifier) */
static int lp_zmin = -1;                    /* (the faintest beam it sees: VT_LPZMIN, default 1 - any light) */

static void lp_init (void)
{
	const char *z = getenv ("VT_LIGHTPEN");
	vt_lightpen = z && *z && strcmp (z, "0") != 0;
	lp_in = 0; lp_pin_until = 0;
	z = getenv ("VT_LPZMIN"); lp_zmin = z ? atoi (z) : 1;
	z = getenv ("VT_LPR"); if (z) lp_r = atof (z) * ZS;
}
static void lp_set (int what, double v)     /* (the frontend: 0 x, 1 y - the line's units - 2 touching) */
{
	if (what == 0) lp_x = v * ZS;
	else if (what == 1) lp_y = v * ZS;
	else { lp_down = v > 0.5; if (!lp_down) lp_in = 0; }
}
/* every cycle: the lit spot under the pen? */
static einline void lp_tick (void)
{
	if (!lp_down) return;
	double dx = beam_x - lp_x, dy = beam_y - lp_y, d2 = dx * dx + dy * dy;
	int lit = alg_zlev > 0.2 && (alg_zsh & 0x80) == 0 && (alg_zsh & 0x7f) >= lp_zmin;
	int seen = lit && d2 < LP_R * LP_R;
	if (lp_in) {
		if (seen) lp_dark = alg_cycles;
		else if (alg_cycles - lp_dark >= LP_GAP) lp_in = 0;
		return;
	}
	if (seen) {
		static int lg = -1;
		if (lg < 0) lg = getenv ("VT_LPLOG") != NULL;
		if (lg) fprintf (stderr, "pen: seen at cycle %llu, beam %.0f, %.0f (pen %.0f, %.0f) Z %u\n", alg_cycles, beam_x / ZS, beam_y / ZS, lp_x / ZS, lp_y / ZS, alg_zsh & 0x7f);
		lp_in = 1; lp_dark = alg_cycles;
		lp_pin_until = alg_cycles + LP_PULSE;
		via_ifr |= 0x02;                    /* (CA1) */
		int_update ();
	}
}
