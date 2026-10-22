/* core_api.c - the Vectrex core (src/core, the vecx core, GPL) as a library for the Android app.
 *
 * The desktop program runs the core as its own process (vtest --live) and talks to it over a pipe;
 * here the same per-frame step is called directly. vtest.c is compiled into this file unchanged
 * (its main renamed), so the frame loop, the BIOS-intro skip, the input flags and the cartridge
 * set-up are exactly the desktop core's. BIOS and cartridge come in as bytes (the app reads them
 * through Android's content URIs), never as file paths. */
#define main vtest_main
#include "vtest.c"
#undef main
#include "core_api.h"

static int vx_ready;

/* the cartridge set-up of vtest's main(), from bytes: the board by the image's size (or VT_CARTHW),
 * the flash cartridges' saved state from sav_path */
int vx_load (const unsigned char *bios, size_t bios_len, const unsigned char *data, size_t n, const char *sav_path)
{
	if (bios_len < 8192) return -1;
	memcpy (rom, bios, 8192);
	memset (cart, 0xff, sizeof cart);
	if (n > sizeof cart) n = sizeof cart;
	if (n) memcpy (cart, data, n);
	{ extern int vt_nocart; vt_nocart = (n == 0); }
	if (n <= 32768) memcpy (cart + 32768, cart, 32768);
	const char *hw = getenv ("VT_CARTHW");
	int kind = n == 524288 ? 'r' : n > 262144 ? 'm' : n > 65536 ? 'v' : 'p';
	if (hw && !strcmp (hw, "pb6")) kind = 'p';
	else if (hw && !strcmp (hw, "vb")) kind = 'v';
	else if (hw && !strcmp (hw, "hb512")) kind = 'r';
	else if (hw && !strcmp (hw, "multi")) kind = 'm';
	else if (hw && atoi (hw) == 0 && isdigit ((unsigned char) hw[0])) kind = 0;
	if (kind == 0) {
		memcpy (cart + 32768, cart, 32768); memset (cart + 65536, 0xff, sizeof cart - 65536);
	} else if (kind == 'm') {
		extern int vt_multicart; vt_multicart = 1;
	} else if (kind == 'v' || kind == 'r') {
		extern int vt_vb, vt_hb512;
		if (kind == 'v') { vt_vb = 1; vt_cart_len = n > 65536 ? n : 262144; }
		else { vt_hb512 = 1; vt_cart_len = 524288; }
		static char sav[4096];
		if (sav_path && *sav_path) {
			snprintf (sav, sizeof sav, "%s", sav_path); vt_flash_file = sav;
			FILE *fs = fopen (sav, "rb");
			if (fs) { static unsigned char tmp[524288]; size_t m = fread (tmp, 1, sizeof tmp, fs); fclose (fs);
			          if (m == vt_cart_len) memcpy (cart, tmp, m); }
		}
	}
	{ extern int vt_cart_mirror; vt_cart_mirror = !memcmp (cart, cart + 32768, 32768); }
	{ extern int vt_speech_sep; vt_speech_sep = getenv ("VT_SPEECHOUT") != NULL; }
	{ const char *dev = getenv ("VT_SPEECHDEV"), *z = getenv ("VT_VECVOICE");
	  extern int sp_load (const char *); extern void sp_start (const char *);
	  if (z && *z && !sp_load (z)) sp_start (dev); }
	{ extern long vt_text_shift; extern double vt_text_shift_w; if (getenv ("VT_TEXTSHIFT")) vt_text_shift = atol (getenv ("VT_TEXTSHIFT")); if (getenv ("VT_TEXTSHIFTW")) vt_text_shift_w = atof (getenv ("VT_TEXTSHIFTW")); }
	if (getenv ("VT_COMMON")) common_base = (unsigned) strtol (getenv ("VT_COMMON"), 0, 16);
	e8910_init_headless ();
	vecx_reset ();
	alg_jch0 = alg_jch1 = 128;
	/* as live_main */
	if (!abuf) { abuf_cap = 32768; abuf = malloc (abuf_cap); }
	frame_budget = getenv ("VT_FRAMEMAX") ? atol (getenv ("VT_FRAMEMAX")) : 33000;
	vx_ready = 1;
	return 0;
}

/* one game frame, as one --live message: m = jx+128, jy+128, buttons, flags[, jx2+128, jy2+128, buttons2]
 * (flags bit 6: the 2nd port's 3 bytes are there); rampoff >= 0: Tube > Line gaps, the stop delay in cycles */
void vx_frame (const unsigned char *msg, double rampoff, vx_frame_t *out)
{
	unsigned char m[8] = { 0 };
	memcpy (m, msg, (msg[3] & 0x40) ? 7 : 4);
	if (rampoff >= 0) { extern void vt_set_rampoff (double c); vt_set_rampoff ((unsigned) (rampoff * 100 + 0.5) / 100.0); }
	apply_input (m);
	abuf_n = 0;
	run_frame ();
	int n = nlast > VX_MAX_LINES ? VX_MAX_LINES : nlast;
	for (int i = 0; i < n; i++) {                     /* (as live_main sends them: u16, clamped) */
		line_t *l = &last[i]; vx_line_t *o = &out->lines[i];
		o->x0 = l->x0 < 0 ? 0 : l->x0 > 65535 ? 65535 : (uint16_t) l->x0;
		o->y0 = l->y0 < 0 ? 0 : l->y0 > 65535 ? 65535 : (uint16_t) l->y0;
		o->x1 = l->x1 < 0 ? 0 : l->x1 > 65535 ? 65535 : (uint16_t) l->x1;
		o->y1 = l->y1 < 0 ? 0 : l->y1 > 65535 ? 65535 : (uint16_t) l->y1;
		o->bright = l->c; o->energy = l->cyc > 65535 ? 65535 : (uint16_t) l->cyc; o->t0 = (uint16_t) l->t0;
	}
	out->nlines = n;
	extern int vt_speech_sep, vt_vecvoice;
	out->two_channels = vt_speech_sep && vt_vecvoice;
	out->nsamples = out->two_channels ? abuf_n / 4 : abuf_n / 2;
	out->pcm = (const int16_t *) abuf;
	extern unsigned long long vt_frame_t0;
	out->frame_t0 = (uint32_t) vt_frame_t0;
	flash_save (0);
}

void vx_close (void)
{
	if (vx_ready) flash_save (1);
}
