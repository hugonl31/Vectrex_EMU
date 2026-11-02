/* uvm2_probe - Vectrex_EMU's Optimize test for UVMC2 games (system/uvm2_probe.um2, pctest.py uvm2_probe): built from
 * the UVMC2 timing test with -DPROBE - one heavy frame over and over: 200 short lines and 30 sweeps on the bus, and a
 * fixed amount of game-logic work on core 0 (integer maths over a table, as a game's own code), no SD log, no input.
 * Optimize runs it for 200 frames per mode and scales by factors measured against VecDoom, Galaga and Moon Cresta.
 * Build: tools/uvm2_probe/build.sh (WSL, the UVMC2 starter kit).
 *
 * The timing test it comes from: */
/* uvmc2 timing test - how fast a real UVMC2 draws, to make Vectrex_EMU's UVMC2 emulation match the hardware.
 *
 * A scripted animation, the same on every run, no input needed. Six load phases of 10 seconds each, in a loop:
 *   A  50 lines          B  200 lines          C  400 lines
 *   D  60 shift-register sweeps (rows of a bit pattern, as raster text is drawn)
 *   E  60 dotted lines (the VIA shift register pattern along a ramp)
 *   F  24 long lines across the whole screen
 * On screen: the phase, frames per second, the slowest frame, strokes the SDK dropped, the fullest frame's bus cycles
 * and seconds since power-on (by the cartridge's own timer: compare with a stopwatch). On the SD card,
 * uvmc2test/timing.txt: one line per finished phase.
 * (c) 2026 Hugo Bokhorst. MIT licence. */
#include <stdint.h>
#include <vpy.h>
#include <vectrex/vectrexInterface.h>
#include "uvm2_bus.h"
#include "uvm2_sd.h"

int v_directSweepSR (int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint8_t b, const unsigned char *pattern, int n, int step);

#define TIMER_US   (*(volatile uint32_t *) 0x400B0028u)   /* RP2350 TIMER0 TIMERAWL: microseconds */
#define PHASE_US   10000000u
#define LOG_PATH   "uvmc2test/timing.txt"

typedef struct { char id; const char *name; int n; } phase_t;
static const phase_t PH[] = {
	{ 'A', "50 LINES", 50 }, { 'B', "200 LINES", 200 }, { 'C', "400 LINES", 400 },
	{ 'D', "60 SWEEPS", 60 }, { 'E', "60 DOTTED", 60 }, { 'F', "24 LONG", 24 },
};
#define NPH ((int) (sizeof PH / sizeof PH[0]))

/* integer sine, 256 steps a turn, +-16384 */
static int32_t isin (int a)
{
	static int16_t t[65]; static int ok;
	if (!ok) {   /* a quarter wave by the angle-addition recurrence: no floating point, no libm */
		int64_t c = 16384, s = 0, cd = 16379, sd = 402;   /* cos/sin of 2 pi / 256 x 16384 */
		for (int i = 0; i <= 64; i++) { t[i] = (int16_t) s; int64_t c2 = (c * cd - s * sd) >> 14; s = (s * cd + c * sd) >> 14; c = c2; }
		t[64] = 16384; ok = 1;
	}
	a &= 255;
	if (a < 64) return t[a];
	if (a < 128) return t[128 - a];
	if (a < 192) return -t[a - 128];
	return -t[256 - a];
}
#define ICOS(a) isin ((a) + 64)

static uint32_t frame_no;
static uint32_t sum_exec, sum_input, sum_rest, n_core1;   /* (core 1's own timing, per phase: the SDK's uvm2_stats) */

#ifdef PROBE
#ifndef PROBE_WORK
#define PROBE_WORK 400000                    /* (VecDoom-like: core 0 busy most of the frame) */
#endif
static uint32_t probe_tab[1024], probe_sum;
static void probe_work (void)                /* (a game's own code: integer maths over a table, the same every frame) */
{
	uint32_t h = frame_no * 2654435761u;
	for (int i = 0; i < PROBE_WORK; i++) {
		uint32_t k = (h >> 22) & 1023;
		h = (h ^ probe_tab[k]) * 2246822519u + (uint32_t) i;
		probe_tab[k] = h >> 3;
	}
	probe_sum += h;
}
#endif
static void draw_phase (int p)
{
	int f = (int) frame_no;
	switch (PH[p].id) {
	case 'A': case 'B': case 'C': {          /* spokes of a turning star, each a short line */
		int n = PH[p].n;
		for (int i = 0; i < n; i++) {
			int a = (i * 256 / n + f) & 255, r0 = 3000 + (i % 7) * 900, r1 = r0 + 2200;
			int32_t c = ICOS (a), s = isin (a);
			v_directDraw32 (c * r0 / 16384, s * r0 / 16384 - 2000, c * r1 / 16384, s * r1 / 16384 - 2000, 90);
		}
		break; }
	case 'D': {                              /* rows of a moving checker pattern */
		static unsigned char pat[12];
		for (int r = 0; r < 60; r++) {
			for (int k = 0; k < 12; k++) pat[k] = (unsigned char) (((r + k + f / 4) & 1) ? 0xAA : 0x55);
			int32_t y = 9000 - r * 260, x = -9000 + (isin (f + r * 4) >> 6);
			v_directSweepSR (x, y, x + 16000, y, 90, pat, 12, 18);
		}
		break; }
	case 'E': {                              /* dotted spokes */
		static const unsigned char dots[8] = { 0x88, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88 };
		for (int i = 0; i < 60; i++) {
			int a = (i * 256 / 60 - f) & 255;
			int32_t c = ICOS (a), s = isin (a);
			v_directSweepSR (c * 1500 / 16384, s * 1500 / 16384 - 2000, c * 9000 / 16384, s * 9000 / 16384 - 2000, 90, dots, 8, 16);
		}
		break; }
	case 'F': {                              /* long lines, edge to edge, turning */
		for (int i = 0; i < 24; i++) {
			int a = (i * 256 / 48 + f / 2) & 255;
			int32_t c = ICOS (a), s = isin (a);
			v_directDraw32 (-c * 15000 / 16384, -s * 15000 / 16384 - 2000, c * 15000 / 16384, s * 15000 / 16384 - 2000, 70);
		}
		break; }
	}
}

static char *num (char *p, uint32_t v)
{
	char t[12]; int n = 0;
	do t[n++] = (char) ('0' + v % 10); while ((v /= 10) && n < 11);
	while (n) *p++ = t[--n];
	return p;
}
static char *str (char *p, const char *s) { while (*s) *p++ = *s++; return p; }

static char logbuf[16000];
static int loglen, sd_ok = -1;

static void log_phase (uint32_t t, int p, uint32_t frames, uint32_t min_fps, uint32_t max_us, uint32_t drops, uint32_t bus)
{
	char line[200], *q = line;
	if (sd_ok < 0) sd_ok = uvm2_sd_init () != 0;
	if (!sd_ok) return;
	if (!loglen)
		loglen = (int) (str (logbuf, "UVMC2 TIMING TEST 2026-11-01. One line per 10 s phase: frames in the phase, the lowest\r\n"
		                             "frames per second, the slowest frame (ms), strokes dropped, the fullest frame's bus cycles\r\n"
		                             "(30000 = one 50 Hz frame), seconds since power-on by the cartridge's timer; core 1's\r\n"
		                             "average time per frame replaying the list (exec), reading the pads (input) and the rest,\r\n"
		                             "and the SDK's frame periods so far (min, max, frames over 20.5 ms).\r\n") - logbuf);
	q = str (q, "t="); q = num (q, t / 1000000u);
	q = str (q, "s phase="); *q++ = PH[p].id;
	q = str (q, " ("); q = str (q, PH[p].name);
	q = str (q, ") frames="); q = num (q, frames);
	q = str (q, " minfps="); q = num (q, min_fps);
	q = str (q, " max="); q = num (q, (max_us + 999) / 1000);
	q = str (q, "ms drop="); q = num (q, drops);
	q = str (q, " bus="); q = num (q, bus);
	if (n_core1) {
		q = str (q, " exec="); q = num (q, sum_exec / n_core1);
		q = str (q, "us input="); q = num (q, sum_input / n_core1);
		q = str (q, "us rest="); q = num (q, sum_rest / n_core1); q = str (q, "us");
	}
	q = str (q, " period="); q = num (q, uvm2_stats.us_frame_min);
	q = str (q, "-"); q = num (q, uvm2_stats.us_frame_max);
	q = str (q, "us slow="); q = num (q, uvm2_stats.slow_frames);
	q = str (q, "\r\n");
	int n = (int) (q - line);
	if (loglen + n >= (int) sizeof logbuf) return;
	for (int i = 0; i < n; i++) logbuf[loglen + i] = line[i];
	loglen += n;
	uvm2_sd_write (LOG_PATH, (const unsigned char *) logbuf, (uint32_t) loglen);
}

int main (void)
{
	vpy_init ();
	uint32_t t0 = TIMER_US, last = t0, sec0 = t0, ph0 = t0;
	uint32_t frames = 0, fps = 0, sec_frames = 0, max_us = 0, ph_max = 0, drops = 0, ph_drops = 0, bus = 0, ph_bus = 0;
	uint32_t ph_frames = 0, ph_min = 999, skip = 0;
	int p = 0;
	char l1[40], l2[40], l3[40], *q;
	for (;;) {
		/* the readout: phase, rate, slowest frame, drops, bus, seconds */
		q = str (l1, "PHASE "); *q++ = PH[p].id; *q++ = ' '; q = str (q, PH[p].name); *q = 0;
		q = str (l2, "FPS "); q = num (q, fps); q = str (q, " MAX "); q = num (q, (max_us + 999) / 1000);
		q = str (q, "MS DROP "); q = num (q, drops); *q = 0;
		q = str (l3, "BUS "); q = num (q, bus); q = str (q, " TIME "); q = num (q, (last - t0) / 1000000u); *q = 0;
		vpy_set_text_size (7);
		#ifdef PROBE
		vpy_print_text (-96, 116, "VECTREX_EMU UVMC2 PROBE");
#else
		vpy_print_text (-96, 116, "UVMC2 TIMING TEST");
#endif
		vpy_print_text (-96, -86, l1);
		vpy_print_text (-96, -98, l2);
		vpy_print_text (-96, -110, l3);
		vpy_flush ();                                /* the readout first: in an overfull frame the load is what drops */
#ifdef PROBE
		probe_work ();
		draw_phase (1);                              /* (200 short lines) */
		{ static unsigned char pat[12];              /* (30 sweeps, as phase D) */
		  for (int r = 0; r < 30; r++) {
			for (int k = 0; k < 12; k++) pat[k] = (unsigned char) (((r + k + (int) frame_no / 4) & 1) ? 0xAA : 0x55);
			int32_t y = 9000 - r * 520, x = -9000 + (isin ((int) frame_no + r * 4) >> 6);
			v_directSweepSR (x, y, x + 16000, y, 90, pat, 12, 18);
		  } }
		(void) p;
#else
		draw_phase (p);
#endif
		vpy_wait_recal ();

		uint32_t now = TIMER_US, len = now - last;
		last = now;
		frame_no++;
		frames++; sec_frames++; ph_frames++;
		if (skip) skip--;
		else if (len > ph_max) ph_max = len;
		ph_drops += uvm2_stats.dropped;
		if (uvm2_stats.bus_cycles > ph_bus) ph_bus = uvm2_stats.bus_cycles;
		sum_exec += uvm2_stats.us_exec; sum_input += uvm2_stats.us_input; sum_rest += uvm2_stats.us_rest; n_core1++;
		if (now - sec0 >= 1000000u) {                /* the readout's numbers: the last whole second */
			fps = sec_frames; sec_frames = 0; sec0 += 1000000u;
			if (now - sec0 >= 1000000u) sec0 = now;
			if (fps < ph_min && ph_frames > fps) ph_min = fps;
			max_us = ph_max; drops = ph_drops; bus = ph_bus;
		}
#ifdef PROBE
		if (0) {                                     /* (the probe: one load, no phases, no SD log) */
#else
		if (now - ph0 >= PHASE_US) {                 /* phase over: log it, next phase */
#endif
			log_phase (now - t0, p, ph_frames, ph_min, ph_max, ph_drops, ph_bus);
			p = (p + 1) % NPH;
			sum_exec = sum_input = sum_rest = n_core1 = 0;
			ph0 = now; ph_frames = 0; ph_min = 999; ph_max = 0; ph_drops = 0; ph_bus = 0;
			skip = 2;                               /* (the SD write: not counted) */
		}
	}
	return 0;
}
