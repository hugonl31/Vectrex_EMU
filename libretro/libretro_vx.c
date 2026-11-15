#ifndef _WIN32
#define _GNU_SOURCE                             /* (dladdr: the core pins itself, see retro_init) */
#endif
#ifdef _WIN32
__declspec(dllimport) unsigned long __stdcall GetTickCount (void);   /* (v0.25: the boot token) */
#define VX_TICKS() ((unsigned) GetTickCount ())
#else
#include <time.h>
static unsigned VX_TICKS (void) { struct timespec t; clock_gettime (CLOCK_MONOTONIC, &t); return (unsigned) (t.tv_sec * 1000u + t.tv_nsec / 1000000); }
#endif
/* libretro_vx.c - Vectrex_EMU's core as a libretro core (for EmuVR's RetroArch 1.7.5 and any other RetroArch).
 *
 * The emulator is vtest.c's, unchanged: its main() runs once with an empty script (it loads the BIOS and the
 * cartridge and resets), then retro_run runs one frame at a time as the --live mode does, and draws the frame's lines.
 *
 * The picture is the overlay sheet's area of the tube (162 x 207 mm: 0.924 of the picture height, centred - as the
 * emulator lays its overlays), 624 x 800, so an overlay covers it exactly. Overlays: a PNG named like the game (its
 * name without (...) parts, or its letters and digits) in <system>/vectrex_emu/overlays, or in an "Overlays" folder
 * next to the game. SELECT switches the overlay off and on.
 *
 * The 3D Imager (3D Mine Storm, 3D Narrow Escape, 3D Crazy Coaster, Rocket Sledge): colour, or VR side by side -
 * each line goes to the eye whose half of the disc was in front when it was drawn, and each eye keeps the lines of its
 * own last half turn (the eye's persistence, as with the real goggles); "swap eyes" if the depth comes out inverted.
 * The light pen (Art Master, Melody Master, AnimAction): RetroArch's light gun (EmuVR: the hand's ray).
 *
 * v0.3 (for VR): a thin beam (each line drawn by its distance, a Gaussian spot, not summed along the line), the glow
 * off by default, a 2x picture option (read when a game starts), the 3D Imager's eye shift, and the Vectrex's own
 * character from the emulator's front end (vecrender.py / vectrex_emu.py): the mains hum wobble, the buzz, and the
 * small built-in speaker.
 *
 * v0.9 (the vectors as in Vectrex_EMU, seen up close): the line renderer of vecrender.py ported to the CPU. Each piece
 * of the beam's path a capsule with a Gaussian cross-section, its width and brightness from the measured tube
 * (TUBE_LIGHT, the narrower spot below intensity 63), fainter the faster it was swept (BEAM_LINE, BEAM_SPEED); the
 * pieces of one stroke cut at their joints along the bisector, so a joint is lit once; the resting spots (the beam
 * slower than REST_SPEED: corners, dots, stars) round spots brighter than the lines by the time they rested (BEAM_DOTMAX,
 * BEAM_DOTC), wider and blooming when long, with a faint halo (Corner glow); the dwells inside strokes found as
 * dwell_rests does; brightness bending (the picture grows a little with the beam's load); the hum wobble applied
 * after the tube's map, as vecrender does. The afterglow is the measured one (PHOS_A, at the owner's time scale 0.6):
 * what is no longer lit glows away in two parts. The screen rate option (90 Hz by default, for the Valve Index): each
 * retro_run is one refresh, the emulator runs whole game frames until the emulated time has caught up, and the sound
 * goes out evenly through a queue. Smooth motion (vectrex_emu.py smooth_frame): each refresh shows the moment one game
 * frame and one refresh back, its lines between the two game frames around it where they match line for line. A 3D
 * Imager game is not mixed between frames (its two half turns' pictures are shown as they are). The buffers are made at
 * the picture's size, one colour channel unless the game is a 3D Imager game.
 *
 * v0.11 (the sound as Vectrex_EMU's): checked against Vectrex_EMU's own pipeline (its core in --live mode, each game
 * frame through vectrex_emu.py CrtSound), the same game frames and button presses: the output is the same to the sample
 * at 44.1 kHz, 16 bits, the same in both channels (Vectrex_EMU plays mono), with nothing dropped or repeated between
 * refreshes, at 90 Hz and 50 Hz, for Mine Storm II, Armor Attack, Berzerk, Spike, 3D Mine Storm (the Imager's motor) and
 * B.E.T.H. (speech). New: the speech module. The SP0256-AL2 ROM (sp0256-al2.bin or al2.bin in <system>/vectrex_emu, the
 * system folder or next to the game) is loaded whenever it is there, its sound on a channel of its own through the
 * module's own path (the full-range speakers' bass, beside the Vectrex's speaker), and the games that talk get a VecVoice
 * or VecVox in port 2 by their title or file name (vectrex_emu.py port2_needed); before, those games were silent. The
 * buzz takes each line as Vectrex_EMU gets it from its core (at most 30000 lines, the times 16 bits). Test hooks:
 * VXPOC_ADUMP (each game frame's raw sound and lines) and VXPOC_AQLOG (the sound queue per refresh).
 *
 * v0.12 (the phosphor glow as Vectrex_EMU's): the Glow option is now Vectrex_EMU's bloom, the soft light round the lines
 * and brighter round the corner dots, ported from vecrender.py: the picture the phosphor shows filtered down softly, a
 * Gaussian of 1.12 mm on the glass (its size on the owner's screen), added at Vectrex_EMU's strength in the White
 * balance's blue tint, before the knee to 8 bits (see glow_blur). Off by default, as most VR headsets' lenses already
 * bloom; "strong" doubles it. With it off the picture is the same as v0.11's.
 *
 * v0.13 (save states, for RetroArch's run-ahead): the emulator's own files are compiled with all their data in two
 * sections of their own (state\state_*.c: .vxd initialised, .vxb zeroed; Vectrex_EMU's source unchanged), and so are
 * vtest.c and the core's game-side variables here (the sound queue and its filters, the 3D Imager's eye buffers, the
 * emulated time, SELECT); a save copies those sections (less the big buffers that are not the game's state: the display
 * vector lists, the cartridge image, the UVMC2 / PiTrex images, the speech ROM), the lines in progress and the last
 * frame's, and the game frames kept for Smooth motion. Not saved: the picture (the phosphor, the glow), so the
 * afterglow runs on once per shown refresh. RetroArch's run-ahead runs hidden refreshes with the video off: those draw
 * no picture. UVMC2 and PiTrex games have no save states (their processors are not in the sections).
 *
 * v0.14 (the rest of Vectrex_EMU's default look): the Tube geometry - the owner's measured tube (tubemap.py, his trace
 * of the Test Cartridge grid on the real tube: the grid not perfectly straight), the straight part plus the measured
 * imperfections, lines longer than TUBE_SEG bent in pieces (vecrender.py tube_map / _tube_lines); Smooth bends - a stroke's
 * bent pieces cut along a centripetal Catmull-Rom curve (vecrender.py smooth_bends); the White balance - the beam's light
 * at 12000 K, bluish as the real Vectrex (vecrender.py white_gain / cold: the glow's tint, the bright core per
 * white_core_k). The geometry and the white balance on by default, Smooth bends off (the owner: a VR headset hardly shows it); each an option. The light pen follows the map back (a few steps).
 *
 * v0.15 (switching on and off, vectrex_emu.py crt_on / crt_off / crt_fx): on - the tube warms up, its light from dark in
 * 0.3 .. 5 s by how long it was off (15 s off: 2.5 s; a first start: 5 s), and a still warm tube's picture bulges out and
 * settles (its high voltage builds up abruptly); off - the deflection collapses: mostly a zap (one smooth bright freehand
 * stroke, 2-8 cm, anywhere on the screen), now and then the picture shrinking into the centre in 0.05 s and stopping at
 * 30 %; what the beam drew then fades in the phosphor. The power recordings (on.wav / off.wav in <system>/vectrex_emu,
 * from Vectrex_EMU's sounds) play with it, not through the volume. Switching on is every game start (the time it was off:
 * a stamp in the temp folder at each unload); switching off needs the front end to say so: the file named by
 * VECTREX_POWER ("<number> <1 on / 0 off>", a new number for each switch) - EmuVR's VectrexStereo mod writes it and holds
 * EmuVR's own switch-off back until the zap has faded. While off: no game, silence (and the off sound), the afterglow.
 *
 * Copyright (c) 2026 Hugo Bokhorst. Part of Vectrex_EMU; see LICENSE.md. */
/* (2026-11-15, the RetroArch core for Android / Linux: the save-state sections either Visual C++'s data_seg / bss_seg or
 * clang's section pragma - src/core/state/state_core.c bounds both) */
#if defined(_MSC_VER)
#define VXS_ON __pragma (data_seg (".vxd$m")) __pragma (bss_seg (".vxb$m"))
#define VXS_OFF __pragma (data_seg ()) __pragma (bss_seg ())
#elif defined(__clang__)
#define VXS_ON _Pragma ("clang section data=\"vxd\" bss=\"vxb\"")
#define VXS_OFF _Pragma ("clang section data=\"\" bss=\"\"")
#else
#error "the save-state sections need Visual C++ or clang"
#endif
VXS_ON
#define main vtest_main
#include "vtest.c"
#undef main
VXS_OFF
#include "libretro.h"
#ifdef _WIN32
#define SEP "\\"                           /* (the folder separator in paths) */
#define vx_putenv(s) _putenv (s)
#define vx_stricmp _stricmp
#else
#define SEP "/"
#include <strings.h>
#include <sys/stat.h>
#include <dirent.h>
#include <dlfcn.h>
#define vx_stricmp strcasecmp
static void vx_putenv (const char *s)       /* (as Windows' _putenv: "NAME=" takes NAME away - the core asks getenv != NULL) */
{
	char k[256]; const char *e = strchr (s, '='); size_t n = e ? (size_t) (e - s) : strlen (s); if (n >= sizeof k) return;
	memcpy (k, s, n); k[n] = 0;
	if (e && e[1]) setenv (k, e + 1, 1); else unsetenv (k);
}
#endif
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#include "stb_image.h"

#define PW 624                          /* one eye's picture: the overlay sheet, 162 x 207 mm */
#define PH 800
#define SHEET_H (0.924 * ALG_MAX_Y)     /* the sheet in emulator units (vecrender.py ART_TUBE_K) */
#define SHEET_W (SHEET_H * 162.0 / 207.0)
#define SHEET_X0 (ALG_MAX_X / 2.0 - SHEET_W / 2.0)
#define SHEET_Y0 (ALG_MAX_Y / 2.0 - SHEET_H / 2.0)
/* v0.6: the tube's straight map (vecrender.py TUBE_LIN = tubemap.LIN, measured from the owner's trace of the Test
 * Cartridge grid on a real tube): x = a u + b, y = c v + d in half picture heights round the canvas's middle. Without
 * it the picture came out ~1.4x too wide and ~1.27x too tall on the glass (the emulator has always used it) */
#define LIN_A 0.694396
#define LIN_B (-0.002300)
#define LIN_C 0.789907
#define LIN_D 0.002146
#define TU (ALG_MAX_Y / 2.0)
#define MAPX(x) (ALG_MAX_X / 2.0 + (LIN_A * (((x) - ALG_MAX_X / 2.0) / TU) + LIN_B) * TU)
#define MAPY(y) (ALG_MAX_Y / 2.0 + (LIN_C * (((y) - ALG_MAX_Y / 2.0) / TU) + LIN_D) * TU)
#define UNMAPX(X) (ALG_MAX_X / 2.0 + ((((X) - ALG_MAX_X / 2.0) / TU - LIN_B) / LIN_A) * TU)
#define UNMAPY(Y) (ALG_MAX_Y / 2.0 + ((((Y) - ALG_MAX_Y / 2.0) / TU - LIN_D) / LIN_C) * TU)
/* v0.14: the Tube geometry (vecrender.py tube_map, k 1: the owner's tube): the straight map plus the measured
 * imperfections (tubemap_tab.h, from tubemap.py), bilinear between the table's points */
#include "tubemap_tab.h"
#ifdef _OPENMP
#include <omp.h>
#endif
#define TUBE_SEG 1200.0                     /* (lines longer than this, emulator units, are bent in pieces) */
static int opt_geom = 1;
#include "unit.c"
static void tube_pt (double x, double y, double *X, double *Y)
{
	const double XM = ALG_MAX_X / 2.0, YM = ALG_MAX_Y / 2.0;
	double u = (x - XM) / TU, v = (y - YM) / TU;
	if (uw_on) unit_wp (&u, &v);                 /* (this Vectrex's own picture shape: unit.c) */
	double Xu = LIN_A * u + LIN_B, Yv = LIN_C * v + LIN_D;
	if (opt_geom) {
		const double SU = (TM_NU - 1) / (TM_U1 - TM_U0), SV = (TM_NV - 1) / (TM_V1 - TM_V0);
		double fu = (u - TM_U0) * SU, fv = (v - TM_V0) * SV;
		if (fu < 0) fu = 0; if (fu > TM_NU - 1.000001) fu = TM_NU - 1.000001;
		if (fv < 0) fv = 0; if (fv > TM_NV - 1.000001) fv = TM_NV - 1.000001;
		const int i = (int) fu, j = (int) fv, k = j * TM_NU + i; const double a = fu - i, b = fv - j;
		const short *DX = TM_D, *DY = TM_D + TM_NU * TM_NV;
		Xu += ((DX[k] * (1 - a) + DX[k + 1] * a) * (1 - b) + (DX[k + TM_NU] * (1 - a) + DX[k + TM_NU + 1] * a) * b) * TM_Q;
		Yv += ((DY[k] * (1 - a) + DY[k + 1] * a) * (1 - b) + (DY[k + TM_NU] * (1 - a) + DY[k + TM_NU + 1] * a) * b) * TM_Q;
	}
	*X = XM + Xu * TU; *Y = YM + Yv * TU;
}
static void tube_inv (double X, double Y, double *x, double *y)   /* (the light pen: the map undone, vecrender's 8 steps) */
{
	double px = UNMAPX (X), py = UNMAPY (Y);
	for (int k = 0; k < 8; k++) { double fx, fy; tube_pt (px, py, &fx, &fy); px += (X - fx) / LIN_A; py += (Y - fy) / LIN_C; }
	*x = px; *y = py;
}
static uint32_t *fb;
static float *lacc[3], *shown[3];
static double pf_t[6], pf_g[3]; static long pf_n; static int pf_on = -1;
/* v0.18 (overnight optimisation, 2026-11-05): each row's span that can hold light - the lines splatted into each lit
 * buffer, the afterglow, the 8-bit picture - so the phosphor pass skips the black (a Vectrex picture is mostly black)
 * and the rows / columns outside the glass window that is sent (no glow: nothing there is ever seen). Same pixels. */
#define SP_NONE (1 << 30)
static int *sp_l0[2], *sp_l1[2], *sp_g0, *sp_g1, *sp_f0, *sp_f1, *cur_l0, *cur_l1;
static void spans_reset (int h, int fb_dirty)
{
	for (int y = 0; y < h; y++) { sp_l0[0][y] = sp_l0[1][y] = sp_g0[y] = SP_NONE; sp_l1[0][y] = sp_l1[1][y] = sp_g1[y] = -1;
		sp_f0[y] = fb_dirty ? 0 : SP_NONE; sp_f1[y] = fb_dirty ? (1 << 29) : -1; }
}
static int NC = 1;                          /* (colour channels drawn: 3 for a 3D Imager game, else 1) */
static int S = 1, FW, FH, EW;               /* the scale (1 or 2), the frame's size, one eye's width */
/* the picture sent: the whole overlay sheet (162 x 207 mm) or only the glass's window (145.7 x 188.5 mm, the owner's
 * trace of the real tube: vecrender.py GLASS_LUT) - what a screen model shows behind its bezel, at its real size */
static int opt_glass = 1, CW, CH, CX0, CY0, OW, OH; static uint32_t *ob;
static retro_environment_t env_cb;
static retro_video_refresh_t video_cb;
static retro_audio_sample_batch_t audio_cb;
static retro_input_poll_t poll_cb;
static retro_input_state_t input_cb;
static int opt_overlay = 0, opt_imager = 2, opt_swap = 0, opt_glow = 0, opt_after = 1;
static int opt_res = 1, opt_wob = 50, opt_buzz = 1, opt_spk = 1, opt_shift = 0; static double opt_sigma = 0.55;
static int opt_rate = 60, opt_smooth = 1, opt_halo = 1, opt_bend = 1, opt_dwell = 1;   /* (v0.9) */
static int opt_dots = 1, opt_speedb = 1; static float opt_trail = 0.6f, opt_trail_k = 1.0f, opt_bright = 1.0f;   /* (v0.10: every look switchable, for slower PCs) */
static int opt_sbend = 0; static double opt_white = 12000;   /* (v0.14; opt_geom with the tube map above) */
static int opt_power = 1;                    /* (v0.15: the switch-on / switch-off effects) */
/* (1.0, 2026-11-15) the overlay as Vectrex_EMU's (vecrender.py ART_SHADER): its clear filters colour the lines in their
 * own hue at the tint strength, its opaque print lit by the room at the print brightness; the overlay's button; the
 * rotation (RetroArch turns the picture, the stick turned with it) */
static float opt_ov_tint = 1.0f, opt_ov_print = 1.0f; static int opt_ov_btn = 1, opt_rot, opt_rot_stick = 1;
/* (1.0, the owner's second test 2026-11-15) the picture turned by the core itself (RetroArch then sizes it right: its
 * own rotation kept the upright picture's shape - squashed), the buttons turned with the stick; the picture's size as
 * the pixels on the screen's short side (the picture fills it exactly - integer scale 1x, the sharpest lines); the area
 * automatic (the whole sheet with an overlay, the glass window without); the room's light (vecrender.py: the filters
 * x its square, the print x it, the bare glass grey x its cube); the trail's brightness with and without an overlay */
static int opt_size = 800, opt_area = 0;
static int size_auto = 1, scr_short, scr_new;   /* (Picture size auto: the screen's short side - measured at the GPU's start) */
static const char *optv (const char *key);
static void scr_file (char *o, size_t n)
{ const char *sv = NULL; env_cb (RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY, &sv); if (sv && *sv) snprintf (o, n, "%s" SEP "vectrex_emu_screen.txt", sv); else o[0] = 0; }
static void scr_load (void)
{ char f[1200]; scr_file (f, sizeof f); FILE *t = f[0] ? fopen (f, "r") : NULL; int v = 0; if (t) { if (fscanf (t, "%d", &v) != 1) v = 0; fclose (t); }
  if (v >= 240 && v <= 4320) scr_short = v; }
uint32_t *dk_cb;
static int dk_pop, dk_y, dk_shown;   /* (the pop-up deck: over the picture's bottom, dk_y its top in the block; shown now) */
static int opt_deck, opt_shape, dk_on, DKH, TW, TH, BX, BY, dk_dirty_set = 1, vx_paused, geo_deck = -1, geo_shape = -1;   /* (the control deck:
                                                  deck.c - the output TW x TH, the picture at BX, BY, the deck DKH high below it) */
static float vx_vol = 1.0f;                      /* (the volume knob: deck.c) */   /* (opt_area: 0 auto, 1 glass window, 2 overlay sheet) */
static float opt_room = 1.0f, opt_trail_k0 = 1.5f, glass_grey;
static int paint_all;
/* (1.1: the quality profile - vecrender.py's tiers: Ultra, High, Medium, Low, Basic; auto: Ultra with the GPU, Medium on
 *  the CPU only) and what the options set to auto take from it; the GPU renderer asked for */
static int opt_profile, opt_renderer, raw_glow = -1, raw_smooth = -1, raw_sbend = -1, raw_glass = -1, raw_ceiling = -1,
           raw_case = -1, raw_crefl = -1, raw_cwear = -1, opt_room_glass;
#ifndef VX_EMUVR
static float g_lamp_x, g_lamp_y;                 /* (the lamp's position: gpu_gl.c) */
#endif
/* what the overlay shows on the GPU (gpu_ovfx.c; fx_apply: the options and the profile) */
static struct { float glow, ovl, wear, fine, medium, large, scrib, glint, reach; int scr, seed; } ovfx;
static float k_ovl = -1, k_ovglow = -1; static int raw_scr = -1, opt_par = 1, raw_par = -1, raw_geom = -1, raw_passes = -1, raw_dith = -1, raw_groom = -1;
typedef struct { unsigned char passes, curve, glass, groom, light, lov, aglow, scr, par, crefl, sbend, dith, half, lh; float ps; } prow;   /* (a profile's switches) */
static int fx_passes = 2, fx_dith = 1, fx_half, fx_lh; static float fx_ps = 1.0f;   /* (the profile's: the glow passes, dither, phosphor scale,
                                                                          half rate, the lines at the phosphor's size - GPU) */   /* (the overlay's ceiling light, plastic glow, scratches, parallax) */
static float glass_grey_v, k_ceiling = -1, k_crefl = -1, k_cwear = -1;   /* (the strengths: -1 auto - the profile's on / off at 1x) */                       /* (this refresh: every pixel has colour - an overlay painted in, or the glass grey) */
static int geo_glass = -1, geo_rot = -1, geo_size = -1; static unsigned av_maxw, av_maxh; static uint32_t *rb; static size_t rb_n;
VXS_ON
static int ov_on = 1, sel_held;          /* (v0.13: saved - SELECT's overlay switch is game-side input) */
VXS_OFF
static int stereo, lightpen;
static unsigned char *ov; static int ovw, ovh;   /* the overlay: RGBA */
/* v0.7 the overlay as a sheet of its own (EmuVR + the VectrexStereo mod v0.8+): the mod sets VECTREX_SHEET to a file
 * path in EmuVR's environment (RetroArch inherits it). Then the overlay is not painted into the picture: the core
 * writes "<overlay png>|<1 shown / 0 off>" there (an empty file: no overlay) and the mod hangs the PNG as a
 * see-through sheet in front of the glass - so the picture moves behind it as the head moves, as on a real Vectrex */
static char ov_path[1600]; static const char *sheet_file; static int sheet_written = -1;
/* v0.19 overlays you put on by hand (EmuVR + VectrexStereo v0.16: an overlay is an object you take from the game list
 * and press onto the screen, like a cartridge): the mod writes "<number> <1 one on the screen / 0 none>" to
 * VECTREX_OVERLAY (a new number each change). One put on hides the game's own (automatic) overlay; SELECT brings the
 * automatic one back and the mod then takes the hand-placed one off - the last action wins. The picture gets the
 * sheet's gain while either is in front of it. */
static const char *ovl_file; static int ovl_seq_seen = -1, ovl_manual, ovl_poll_n;
static void ovl_poll (void)
{
	if (!ovl_file || (ovl_poll_n++ % 6)) return;
	FILE *f = fopen (ovl_file, "r"); int seq, man; if (!f) return;
	if (fscanf (f, "%d %d", &seq, &man) == 2 && seq != ovl_seq_seen) {
		/* v0.24: 2 = the game's own overlay object is on the screen (the automatic overlay, placed by the mod or by hand):
		 * auto stays on (and is remembered); taken off again (SELECT or by hand) auto goes off - unless this game has none */
		/* v0.25 (owner 2026-11-06): what is on the screen only - 0 none, 1 one put on by hand (a world object: SELECT
		 * leaves it alone), 2 the automatic one (the mod's, changes with the games; SELECT takes it off) */
		ovl_seq_seen = seq; ovl_manual = man;
	}
	fclose (f);
}
/* v0.20 (owner 2026-11-06: one overlay at a time, as on a real Vectrex): SELECT with a hand-placed overlay on asks the
 * mod to take it off (a count on the sheet file's second line, "sel <n>") and leaves the automatic one off; with none on
 * it shows the game's own (the Automatic overlay option on), with that on it hides it */
static int sel_count, sel_written = -1; static unsigned boot_id, boot_written;
static char look[256], look_written[256];   /* (v0.26: the overlay look, see read_look) */
/* v0.21 (owner 2026-11-06: no overlay on, a new cartridge in - it stays off until SELECT): the automatic overlay's
 * on / off is kept across games (RetroArch starts anew for each) in TEMP\vectrex_emu_auto_overlay.txt, EmuVR only */
static int auto_saved = -1;
static char g_sys[1024];
static const char *tmp_dir (void)           /* (Windows: TEMP; Android: no TEMP - the system folder) */
{
	const char *t = getenv ("TEMP"); if (!t || !*t) t = getenv ("TMPDIR"); if (!t || !*t) t = g_sys[0] ? g_sys : ".";
	return t;
}
static void auto_path (char *o, size_t n) { snprintf (o, n, "%s" SEP "vectrex_emu_auto_overlay.txt", tmp_dir ()); }
static void auto_keep (void)
{
	if (!sheet_file || ov_on == auto_saved) return; auto_saved = ov_on;
	char p[1100]; auto_path (p, sizeof p); FILE *f = fopen (p, "w"); if (f) { fprintf (f, "%d\n", ov_on); fclose (f); }
}
static void auto_load (void)
{
	if (!sheet_file || !opt_overlay) return;
	char p[1100]; auto_path (p, sizeof p); FILE *f = fopen (p, "r"); int v;
	if (f) { if (fscanf (f, "%d", &v) == 1) ov_on = v != 0; fclose (f); }
	auto_saved = ov_on;
}
static void sheet_write (void);
static void sheet_write (void)
{
	if (!sheet_file) return;
	int st = ov && ov_path[0] && !stereo ? (ov_on ? 1 : 0) : -2;
	if (st == sheet_written && sel_count == sel_written && boot_id == boot_written && !strcmp (look, look_written)) return;
	sheet_written = st; sel_written = sel_count; boot_written = boot_id; snprintf (look_written, sizeof look_written, "%s", look);
	FILE *f = fopen (sheet_file, "w"); if (!f) return;
	if (st != -2) fprintf (f, "%s|%d\n", ov_path, st); else fprintf (f, "\n");
	fprintf (f, "sel %d\nboot %u\nlook %s\n", sel_count, boot_id, look);
	fclose (f);
}
static char tmpdir[1024];
/* v0.23 (owner 2026-11-06: a cartridge pulled or put in while on boots at once, as on a real Vectrex - no RetroArch
 * restart, no warm-up): the mod writes "<number> <cartridge file>" to VECTREX_CART; the running core loads it (Vectrex_EMU's
 * own loader, vtest main), resets and goes on - the overlay follows. Plain 6809 cartridges only: the mod restarts
 * RetroArch for UVMC2 / PiTrex / flash / 3D Imager games. */
static char g_bios[1600], g_dir[1024]; static const char *cart_file; static int cart_seq = -1, cart_poll_n;
static void cart_swap (const char *path);
static void emu_wait_ext (void);
static int vx_ok (void);
static int sp_ok, dk_p2;   /* (deck.c) */
/* (RetroArch's netplay: one Vectrex on both machines, kept the same by rewinding with save states. Seen by the reason
 *  RetroArch gives a save - GET_SAVESTATE_CONTEXT 72, ROLLBACK_NETPLAY 3; while it runs, nothing one side does alone may
 *  change the emulation: the ROM browser, eject, RESET, pause, the deck's power and port 2, the USB pad read directly) */
static long net_seen = -1000000, net_runs;
static int net_on (void) { return net_runs - net_seen < 180; }
static void net_mark (void) { int c = 0; if (env_cb (72 | 0x10000, &c) && c == 3) net_seen = net_runs; }
static char sw_next[1100], cur_game[1100]; static int sw_eject, ej_out; static int game_switch_now (const char *path);   /* (the game switch: below) */
static int unit_addon;
static const char *optv (const char *key);
static int btn_id (const char *key)                /* (a function's RetroPad button, the option: -1 none) */
{
	static const char *const N[] = { "L", "R", "L2", "R2", "L3", "R3", "SELECT", NULL };
	static const int I[] = { RETRO_DEVICE_ID_JOYPAD_L, RETRO_DEVICE_ID_JOYPAD_R, RETRO_DEVICE_ID_JOYPAD_L2, RETRO_DEVICE_ID_JOYPAD_R2,
		RETRO_DEVICE_ID_JOYPAD_L3, RETRO_DEVICE_ID_JOYPAD_R3, RETRO_DEVICE_ID_JOYPAD_SELECT };
	const char *v = optv (key); for (int i = 0; N[i]; i++) if (!strcmp (v, N[i])) return I[i];
	return -1;
}
static void input_names (void);
static int km_on, km_dirty, km_n, km_btn; static unsigned char km_mods;   /* (usbkm.c) */
static void RETRO_CALLCONV km_key (bool down, unsigned keycode, uint32_t character, uint16_t mods);
static const char *optv (const char *key);
static const char *unit_preset (void)               /* (the Unit option: owner, ideal, random N, cranky N) */
{
	const char *u = optv ("vectrex_emu_unit");
	return !strncmp (u, "ideal", 5) ? "ideal" : !strncmp (u, "random", 6) || !strncmp (u, "cranky", 6) ? u : "owner";
}
static int opt_async = 1, emu_primed;   /* (1.1: Threaded emulation - UVMC2 / PiTrex, see emu_start) */
static int unser_seen, runs_since_unser;   /* (v0.27: run-ahead - see retro_run) */
static void cart_poll (void)
{
	if (!cart_file || (cart_poll_n++ % 6)) return;
	FILE *f = fopen (cart_file, "r"); int seq; char p[1100]; if (!f) return;
	if (fscanf (f, "%d %1023[^\r\n]", &seq, p) == 2 && seq != cart_seq) { int first = cart_seq < 0; cart_seq = seq;
		/* v0.27 (owner 2026-11-06: the reset knob did nothing in VR): the knob's press comes from the mod this way too */
		if (!first && !strcmp (p, "RESET")) { extern void vecx_reset_button (void); vecx_reset_button (); fprintf (stderr, "[vectrex_emu] reset knob\n"); }
		else if (!first) cart_swap (p); }
	fclose (f);
}

/* (2026-11-15) two flavours from this one source: VX_EMUVR - EmuVR's (the VectrexStereo mod draws the overlay, VR
 * headsets, 90 Hz), else RetroArch's on a screen (Android, Linux, Windows): the overlay painted in, 60 Hz, no VR options.
 * The first value of each is its default. */
#ifdef VX_EMUVR
#define VX_RATE_DEF 90
#define VX_TRAIL_DEF 1.0
static struct retro_variable vars[] = {
	{ "vectrex_emu_overlay", "Automatic overlay (the game's own put on at each game - SELECT takes it off / puts it on); off|on" },
	{ "vectrex_emu_screen_light", "Screen light in EmuVR (all of the picture alike - the trail ends up at this / the dots level); 2.4x|1x|1.5x|2x|2.8x|3x|4x" },
	{ "vectrex_emu_dot_level", "Screen light in EmuVR: what is the brightest (the beam's dots and corners at 2 - lines below them by their speed); 2|1.5|2.5|3|4" },
	{ "vectrex_emu_tube_grey", "Tube face in room light (Vectrex_EMU's phosphor layer - grey where light falls on it, black in the dark - EmuVR: 0.08); 0.32|0.08|0.2|0.25|0.4|0.5" },
	{ "vectrex_emu_case_refl", "Bezel walls: the picture mirrored in them (Vectrex_EMU's case reflections); x1|x0|x0.5|x1.5|x2" },
	{ "vectrex_emu_case_wear", "Bezel walls: wear (Vectrex_EMU's case scratches); x1|x0|x0.5|x1.5|x2" },
	{ "vectrex_emu_scr", "Overlay: scratches; on|off" },
	{ "vectrex_emu_scr_wear", "Overlay: wear - how much the sheet was handled (all scratches); x0.25|x0|x0.1|x0.5|x0.75|x1|x1.25|x1.5|x2" },
	{ "vectrex_emu_scr_fine", "Overlay: fine scratches (the matte buff); x1|x0|x0.1|x0.25|x0.5|x0.75|x1.25|x1.5|x2" },
	{ "vectrex_emu_scr_medium", "Overlay: medium scratches; x1|x0|x0.1|x0.25|x0.5|x0.75|x1.25|x1.5|x2" },
	{ "vectrex_emu_scr_large", "Overlay: damage (deep, through the colour); x0.1|x0|x0.25|x0.5|x0.75|x1|x1.25|x1.5|x2" },
	{ "vectrex_emu_scr_scrib", "Overlay: scribbles (the share of curly scratches); x1|x0|x0.1|x0.25|x0.5|x0.75|x1.25|x1.5|x2" },
	{ "vectrex_emu_scr_lamps", "Overlay: scratches catch the room's lamps (near their reflection); on|off" },
	{ "vectrex_emu_scr_glint", "Overlay: glint strength (how strongly the scratches catch the light); x1|x0|x0.1|x0.25|x0.5|x0.75|x1.25|x1.5|x2" },
	{ "vectrex_emu_scr_reach", "Overlay: glint reach (left: close to the lines, right: farther out); x1|x0|x0.1|x0.25|x0.5|x0.75|x1.25|x1.5|x2" },
	{ "vectrex_emu_art_glow", "Overlay: plastic glow; on|off" },
	{ "vectrex_emu_art_glow_k", "Overlay: plastic glow strength; x0.2|x0|x0.1|x0.25|x0.5|x0.75|x1|x1.5|x2" },
	{ "vectrex_emu_imager", "3D Imager; vr side by side|colour|off" },
	{ "vectrex_emu_swap_eyes", "3D Imager: swap eyes; off|on" },
	{ "vectrex_emu_eye_shift", "3D Imager: right eye's picture left (-) or right (+), pixels; 0|-10|-20|-30|-40|-50|-60|-70|-80|-90|-100|-110|-120|-130|-140|-150|-160|10|20|30|40|50|60|70|80|90|100|110|120|130|140|150|160" },
	{ "vectrex_emu_beam", "Beam width; thin|medium|wide" },
	{ "vectrex_emu_area", "Picture area (from the next game start); glass window 145.7x188.5 mm|overlay sheet 162x207 mm" },
	{ "vectrex_emu_res", "Picture size (from the next game start); 1x 624x800|2x 1248x1600|3x 1872x2400|4x 2496x3200" },
	{ "vectrex_emu_rate", "Screen rate (from the next game start); 90 Hz|72 Hz|80 Hz|120 Hz|144 Hz|60 Hz|50 Hz (game)" },
	{ "vectrex_emu_smooth", "Smooth motion (not at 50 Hz); on|off" },
	{ "vectrex_emu_corner_glow", "Corner glow; on|off" },
	{ "vectrex_emu_bending", "Brightness bending; on|off" },
	{ "vectrex_emu_geometry", "Tube geometry (the owner's measured tube); on|off" },
	{ "vectrex_emu_smooth_bends", "Smooth curves at bends (fine detail, off: VR hardly shows it); off|on" },
	{ "vectrex_emu_white", "White balance; 12000 K (real Vectrex)|9300 K|16000 K|6504 K (neutral)" },
	{ "vectrex_emu_power_fx", "Switching on / off (tube warm-up, switch-off zap); on|off" },
	{ "vectrex_emu_glow", "Phosphor glow (bloom); off|on|strong" },
	{ "vectrex_emu_afterglow", "Phosphor trail (afterglow); on|long (real Vectrex)|short|off" },
	{ "vectrex_emu_brightness", "Brightness (VR: the screen behind its glass looks dimmer than a monitor); 1x (measured)|1.25x|1.5x|1.75x|2x" },
	{ "vectrex_emu_trail_bright", "Phosphor trail brightness (VR headsets lose faint light); 1x (real)|0.5x|0.75x|1.5x|2x|3x|4x" },
	{ "vectrex_emu_corner_dots", "Corner dots (the beam resting); on|off" },
	{ "vectrex_emu_dwell", "Dwell spots inside strokes; on|off" },
	{ "vectrex_emu_speed_bright", "Fast sweeps fainter; on|off" },
	{ "vectrex_emu_wobble", "Mains hum wobble; 50 Hz|60 Hz|off" },
	{ "vectrex_emu_buzz", "Vectrex buzz; on|off" },
	{ "vectrex_emu_speaker", "Vectrex speaker sound; on|off" },
	{ NULL, NULL } };
#else
#define VX_RATE_DEF 60
#define VX_TRAIL_DEF 1.5                    /* (the trail: the owner's 1.5x on black - now the same everywhere) */
/* (1.0: the options in submenus - RetroArch's core options v2, its categories; older frontends get the same list flat,
 *  each name led by its submenu's) */
struct vx_opt { const char *key, *cat, *desc, *info, *vals; };
static const struct vx_opt OPTS[] = {
	{ "vectrex_emu_size", "video", "Picture size",
	  "Auto: the screen's height, as RetroArch shows it (the sharpest lines, with Integer Scale on). Or a height of its own.",
	  "auto|800 px|480 px|540 px|600 px|720 px|768 px|900 px|960 px|1080 px|1200 px|1440 px|1600 px|2160 px" },
	{ "vectrex_emu_rotate", "video", "Rotation (tate)", "Turns the picture for playing with the device held upright.",
	  "90 (turn the device right)|off|270 (turn the device left)|180" },
	{ "vectrex_emu_profile", "video", "Quality profile",
	  "Sets the options left on auto. Auto: the best this device runs at full speed, measured once (GPU; CPU only: Medium).",
	  "auto|ultra|high|medium|low|basic" },
	{ "vectrex_emu_deck", "video", "Control deck", "The desktop's deck below the picture (mouse or touch). Slim: one low row. Pop-up: over the picture, tap its bottom or the arrow.", "off|full|slim|pop-up" },
	{ "vectrex_emu_shape", "video", "Screen shape", "With the control deck: the picture and the deck fitted to a 4:3 or a 16:9 screen.", "4:3|16:9" },
	{ "vectrex_emu_rate", "video", "Screen rate", "50 Hz needs a screen that can run at 50 Hz.",
	  "60 Hz|50 Hz (game)|72 Hz|90 Hz|120 Hz|144 Hz" },
	{ "vectrex_emu_smooth", "video", "Smooth motion", "Lines move smoothly between game frames.", "auto|on|off" },
	{ "vectrex_emu_area", "video", "Picture area",
	  "Auto: the whole sheet while an overlay or the case is shown, else the glass window (a larger picture).",
	  "auto|glass window|overlay sheet" },
	{ "vectrex_emu_renderer", "video", "Renderer (next game start)", "GPU adds the glass and case effects (video driver gl / glcore).",
	  "auto|GPU|CPU only" },
	{ "vectrex_emu_power_fx", "video", "Switching on / off", "Tube warm-up and switch-off effects.", "on|off" },
	{ "vectrex_emu_btn_reset", "controls", "RESET button", "A RetroPad button for the Vectrex's RESET button.", "off|L|R|L2|R2|L3|R3" },
	{ "vectrex_emu_btn_browser", "controls", "ROM browser button", "A RetroPad button that opens and closes the ROM browser.", "L3|SELECT|off|L|R|L2|R2|R3" },
	{ "vectrex_emu_btn_pause", "controls", "Pause button", "A RetroPad button that pauses and resumes the game.", "off|L|R|L2|R2|L3|R3" },
	{ "vectrex_emu_btn_eject", "controls", "Eject button", "A RetroPad button: the cartridge out (Mine Storm), again: back in.", "off|L|R|L2|R2|L3|R3" },
	{ "vectrex_emu_usb_hid", "controls", "USB keyboard and mouse (UVMC2, PiTrex)", "RetroArch's keyboard and mouse as the cartridge's USB devices. Keys: Game Focus (Scroll Lock).", "on|off" },
	{ "vectrex_emu_rotate_stick", "controls", "Controls turned with the picture", "In tate the stick and buttons keep their places.", "on|off" },
	{ "vectrex_emu_overlay", "overlay", "Overlay", "The game's overlay. START switches it (any button: Quick Menu > Controls).", "on|off" },
	{ "vectrex_emu_ov_tint", "overlay", "Tint strength", "How strongly the overlay colours the lines.", "1x|0.5x|0.75x|1.25x|1.5x|2x|0x" },
	{ "vectrex_emu_ov_print", "overlay", "Printed art brightness", "The overlay's opaque printed parts.", "1x|1.5x|2x|3x|4x|6x|0.5x|0.25x|0x" },
	{ "vectrex_emu_parallax", "overlay", "Parallax", "The picture a little higher than the sheet, as seen from the usual height.", "auto|on|off" },
	{ "vectrex_emu_ov_glow", "overlay", "Plastic glow", "The plastic spreads the light softly around bright lines (GPU).", "auto|0x|0.25x|0.5x|0.75x|1x|1.25x|1.5x|1.75x|2x" },
	{ "vectrex_emu_scratches", "overlay", "Scratches", "The sheet's scratches catch the light (GPU).", "auto|on|off" },
	{ "vectrex_emu_scr_wear", "overlay", "Scratches: wear", "How much the sheet was handled (all of them).", "1x|0x|0.25x|0.5x|0.75x|1.25x|1.5x|1.75x|2x" },
	{ "vectrex_emu_scr_fine", "overlay", "Fine scratches", "Steps 1-3: the matte buff.", "1x|0x|0.25x|0.5x|0.75x|1.25x|1.5x|1.75x|2x" },
	{ "vectrex_emu_scr_medium", "overlay", "Medium scratches", "Steps 4-7.", "1x|0x|0.25x|0.5x|0.75x|1.25x|1.5x|1.75x|2x" },
	{ "vectrex_emu_scr_damage", "overlay", "Damage", "Steps 8-10: deep, through the colour.", "0.1x|0x|0.25x|0.5x|0.75x|1x|1.5x|2x" },
	{ "vectrex_emu_scr_scrib", "overlay", "Scratches: scribbles", "The share of curly ones.", "1x|0x|0.25x|0.5x|0.75x|1.25x|1.5x|1.75x|2x" },
	{ "vectrex_emu_scr_glint", "overlay", "Scratches: glint strength", NULL, "1x|0x|0.25x|0.5x|0.75x|1.25x|1.5x|1.75x|2x" },
	{ "vectrex_emu_scr_reach", "overlay", "Scratches: glint reach", "Low: close to the lines. High: farther out.", "1x|0x|0.25x|0.5x|0.75x|1.25x|1.5x|1.75x|2x" },
	{ "vectrex_emu_scr_seed", "overlay", "Scratch pattern", "Another random set of scratches.", "1|2|3|4|5|6|7|8|9|10" },
	{ "vectrex_emu_buzz", "audio", "Vectrex buzz", NULL, "on|off" },
	{ "vectrex_emu_speaker", "audio", "Vectrex speaker sound", NULL, "on|off" },
	{ "vectrex_emu_glow", "tube", "Phosphor glow (bloom)", NULL, "auto|on|strong|off" },
	{ "vectrex_emu_afterglow", "tube", "Phosphor trail (afterglow)", NULL, "on|long (real Vectrex)|short|off" },
	{ "vectrex_emu_trail_bright", "tube", "Phosphor trail brightness", NULL, "1x|0.5x|0.75x|1.25x|1.5x|2x|3x" },
	{ "vectrex_emu_white", "tube", "White balance", NULL, "12000 K (real Vectrex)|9300 K|16000 K|6504 K (neutral)" },
	{ "vectrex_emu_brightness", "tube", "Brightness", NULL, "1x (measured)|1.25x|1.5x|1.75x|2x" },
	{ "vectrex_emu_geometry", "tube", "Tube geometry", "A real tube's slight curves.", "auto|on|off" },
	{ "vectrex_emu_glass_edge", "tube", "Glass edge", "The tube glass's rounded outline.", "auto|on|off" },
	{ "vectrex_emu_smooth_bends", "tube", "Smooth curves at bends", NULL, "auto|on|off" },
	{ "vectrex_emu_passes", "tube", "Second glow pass", "A wider glow (GPU).", "auto|on|off" },
	{ "vectrex_emu_dither", "tube", "Dither", "No bands in soft glows (GPU).", "auto|on|off" },
	{ "vectrex_emu_corner_glow", "tube", "Corner glow", NULL, "on|off" },
	{ "vectrex_emu_corner_dots", "tube", "Corner dots", NULL, "on|off" },
	{ "vectrex_emu_beam", "tube", "Beam width", NULL, "thin|medium|wide" },
	{ "vectrex_emu_dwell", "tube", "Dwell spots inside strokes", NULL, "on|off" },
	{ "vectrex_emu_speed_bright", "tube", "Fast sweeps fainter", NULL, "on|off" },
	{ "vectrex_emu_wobble", "tube", "Mains hum wobble", NULL, "50 Hz|60 Hz|off" },
	{ "vectrex_emu_bending", "tube", "Brightness bending", NULL, "on|off" },
	{ "vectrex_emu_case", "case", "Plastic case", "The case round the glass, without an overlay.", "auto|on|off" },
	{ "vectrex_emu_case_refl", "case", "Case reflections", "The lines mirrored in the case's walls: their strength.", "auto|0x|0.25x|0.5x|0.75x|1x|1.25x|1.5x|1.75x|2x" },
	{ "vectrex_emu_case_wear", "case", "Case scratches and wear", NULL, "auto|0x|0.25x|0.5x|0.75x|1x|1.25x|1.5x|1.75x|2x" },
	{ "vectrex_emu_room", "case", "Room light", "Lights the overlay.", "1x (evening)|off (dark room)|0.5x|0.75x|1.25x|1.5x|1.75x|2x" },
	{ "vectrex_emu_room_glass", "case", "Glass catches room light", "The bare glass grey in the room's light (no overlay). Off: pure black.", "auto|on|off" },
	{ "vectrex_emu_ceiling", "case", "Ceiling light reflection", "A ceiling light mirrored in the glass: its strength.", "auto|0x|0.25x|0.5x|0.75x|1x|1.25x|1.5x|1.75x|2x" },
	{ "vectrex_emu_ceiling_ov", "case", "Ceiling light on the overlay", "Its reflection on the sheet: strength (GPU).", "auto|0x|0.25x|0.5x|0.75x|1x|1.25x|1.5x|1.75x|2x" },
	{ "vectrex_emu_lamp_x", "case", "Lamp position: left / right", NULL, "0%|10%|20%|30%|40%|50%|60%|70%|80%|90%|100%" },
	{ "vectrex_emu_lamp_y", "case", "Lamp position: high / low", NULL, "27%|0%|10%|20%|30%|40%|50%|60%|70%|80%|90%|100%" },
	{ "vectrex_emu_skip_intro", "system", "Skip the Vectrex intro", "Games start straight away (next game start).", "off|on" },
	{ "vectrex_emu_port2", "system", "Port 2 device (next game start)", "Auto: the game's own (3D Imager, light pen, VecVoice, VecVox).",
	  "auto|none|3D Imager|light pen|VecVoice|VecVox" },
	{ "vectrex_emu_imager", "system", "3D Imager", NULL, "colour|off" },
	{ "vectrex_emu_cart_hw", "system", "Cartridge hardware", "Auto: by the ROM's size (next game start).",
	  "auto|off (plain 32 KB)|pb6 (64 KB)|vb (Vectorblade 4 x 48 KB)|hb512 (512 KB flash)|multi (Multi-Cart)|ram (8 KB RAM)" },
	{ "vectrex_emu_uvm2_mode", "system", "UVMC2 mode", "Fast: the same game, less CPU. Exact: as the cartridge, cycle for cycle. Dev: fast, with an ideal beam.", "fast|exact|dev" },
	{ "vectrex_emu_threaded", "system", "Threaded emulation (UVMC2, PiTrex)", "Emulates while drawing: full speed on handhelds, one frame more input delay.", "on|off" },
	{ "vectrex_emu_unit", "unit", "Unit", "Which Vectrex: the owner's (measured), an ideal one, a random or a cranky one (each number its own).", "owner's Vectrex (measured)|ideal|random 1|random 2|random 3|random 4|random 5|random 6|random 7|random 8|random 9|random 10|cranky 1|cranky 2|cranky 3|cranky 4|cranky 5|cranky 6|cranky 7|cranky 8|cranky 9|cranky 10" },
	{ "vectrex_emu_unit_knob", "unit", NULL, NULL, "@" },
	{ "vectrex_emu_unit_sizex", "unit", NULL, NULL, "@" },
	{ "vectrex_emu_unit_sizey", "unit", NULL, NULL, "@" },
	{ "vectrex_emu_unit_hv", "unit", NULL, NULL, "@" },
	{ "vectrex_emu_unit_cenx", "unit", NULL, NULL, "@" },
	{ "vectrex_emu_unit_ceny", "unit", NULL, NULL, "@" },
	{ "vectrex_emu_unit_rot", "unit", NULL, NULL, "@" },
	{ "vectrex_emu_unit_skew", "unit", NULL, NULL, "@" },
	{ "vectrex_emu_unit_keyx", "unit", NULL, NULL, "@" },
	{ "vectrex_emu_unit_keyy", "unit", NULL, NULL, "@" },
	{ "vectrex_emu_unit_pinx", "unit", NULL, NULL, "@" },
	{ "vectrex_emu_unit_piny", "unit", NULL, NULL, "@" },
	{ "vectrex_emu_unit_linx", "unit", NULL, NULL, "@" },
	{ "vectrex_emu_unit_liny", "unit", NULL, NULL, "@" },
	{ "vectrex_emu_unit_dacofs", "circuit", NULL, NULL, "@" },
	{ "vectrex_emu_unit_drift_x", "circuit", NULL, NULL, "@" },
	{ "vectrex_emu_unit_drift_y", "circuit", NULL, NULL, "@" },
	{ "vectrex_emu_unit_ring", "circuit", NULL, NULL, "@" },
	{ "vectrex_emu_unit_zhold", "circuit", NULL, NULL, "@" },
	{ "vectrex_emu_unit_zcal", "circuit", NULL, NULL, "@" },
	{ "vectrex_emu_unit_zofs", "circuit", NULL, NULL, "@" },
	{ "vectrex_emu_unit_ysettle", "circuit", NULL, NULL, "@" },
	{ "vectrex_emu_unit_beamoff", "circuit", NULL, NULL, "@" },
	{ "vectrex_emu_unit_zerotx", "circuit", NULL, NULL, "@" },
	{ "vectrex_emu_unit_zeroty", "circuit", NULL, NULL, "@" },
	{ "vectrex_emu_unit_ofsk", "circuit", NULL, NULL, "@" },
	{ "vectrex_emu_unit_lagx", "circuit", NULL, NULL, "@" },
	{ "vectrex_emu_unit_lagy", "circuit", NULL, NULL, "@" },
	{ "vectrex_emu_unit_dac0", "circuit", NULL, NULL, "@" },
	{ "vectrex_emu_unit_dac1", "circuit", NULL, NULL, "@" },
	{ "vectrex_emu_unit_dac2", "circuit", NULL, NULL, "@" },
	{ "vectrex_emu_unit_dac3", "circuit", NULL, NULL, "@" },
	{ "vectrex_emu_unit_gap", "circuit", NULL, NULL, "@" },
	{ NULL, NULL, NULL, NULL, NULL } };
static const char *const OPT_CATS[][3] = {   /* (RetroArch's own kind of menus, the most used first - the owner, 2026-11-15) */
	{ "video", "Video", "Picture size, rotation, screen shape, control deck, screen rate, quality" },
	{ "controls", "Controls", "Buttons for the ROM browser, pause, RESET and eject; tate; USB keyboard and mouse" },
	{ "overlay", "Overlay", "The sheet: tint, print, plastic and scratches, parallax" },
	{ "audio", "Audio", "The Vectrex's sound" },
	{ "tube", "Tube", "Phosphor, glow and beam, the tube's shape and glass" },
	{ "case", "Case & Room light", "The plastic round the glass, the room's light, the ceiling light" },
	{ "system", "System", "Port 2, the cartridge hardware, UVMC2 and PiTrex, the intro" },
	{ "unit", "Vectrex Unit", "Which Vectrex; the knob, the pots and the yoke; the picture's shape" },
	{ "circuit", "Vectrex Circuit", "The service trim pots; its parts: zero reference, beam, zeroing, lag, DAC" } };
#define NOPT (sizeof OPTS / sizeof OPTS[0])
static struct retro_variable vars[NOPT];
/* (libretro.h's core options v2 - not in this core's older header: the same layout) */
struct vx_opt_value { const char *value, *label; };
struct vx_opt_cat { const char *key, *desc, *info; };
struct vx_opt_def { const char *key, *desc, *desc_categorized, *info, *info_categorized, *category_key; struct vx_opt_value values[128]; const char *default_value; };
struct vx_opts_v2 { struct vx_opt_cat *categories; struct vx_opt_def *definitions; };
static void opts_set (retro_environment_t cb)
{
	static char vbuf[262144]; static struct vx_opt_def defs[NOPT]; static struct vx_opt_cat cats[10]; size_t u = 0;
	static const char *vals[NOPT], *desc[NOPT], *info[NOPT];
	for (size_t i = 0; OPTS[i].key; i++) {          /* (the Vectrex unit's rows: their names and values from unit.c) */
		vals[i] = OPTS[i].vals; desc[i] = OPTS[i].desc; info[i] = OPTS[i].info;
		if (OPTS[i].vals[0] == '@') { const unit_par *p = &UNIT_P[unit_idx (OPTS[i].key + 17)];
			vals[i] = vbuf + u; u += unit_vals (p, vbuf + u, sizeof vbuf - u) + 1; desc[i] = p->label;
			char *f = vbuf + u; u += (size_t) snprintf (f, sizeof vbuf - u, "%s%sAuto: the Unit's.", p->info ? p->info : "", p->info ? " " : "") + 1; info[i] = f; } }
	for (size_t i = 0; OPTS[i].key; i++) {          /* (the flat list: "Submenu: name; values") */
		const char *cn = ""; for (int c = 0; c < 9; c++) if (!strcmp (OPT_CATS[c][0], OPTS[i].cat)) cn = OPT_CATS[c][1];
		vars[i].key = OPTS[i].key; vars[i].value = vbuf + u;
		u += (size_t) snprintf (vbuf + u, sizeof vbuf - u, "%s: %s; %s", cn, desc[i], vals[i]) + 1; }
	unsigned ver = 0;
	if (!cb (52 /* GET_CORE_OPTIONS_VERSION */, &ver) || ver < 2) { cb (RETRO_ENVIRONMENT_SET_VARIABLES, vars); return; }
	for (int c = 0; c < 9; c++) cats[c] = (struct vx_opt_cat) { OPT_CATS[c][0], OPT_CATS[c][1], OPT_CATS[c][2] };
	for (size_t i = 0; OPTS[i].key; i++) {
		struct vx_opt_def *d = &defs[i]; memset (d, 0, sizeof *d);
		d->key = OPTS[i].key; d->category_key = OPTS[i].cat;
		/* (the name with its submenu, for a frontend that shows no submenus; the name alone inside one) */
		{ char *dn = vbuf + u; const char *v = vars[i].value; size_t L = (size_t) (strchr (v, ';') - v);
		  memcpy (dn, v, L); dn[L] = 0; u += L + 1; d->desc = dn; }
		d->desc_categorized = desc[i]; d->info = info[i]; d->info_categorized = info[i];
		char *q = vbuf + u; u += (size_t) snprintf (q, sizeof vbuf - u, "%s", vals[i]) + 1;
		int n = 0; for (char *t = q; t && n < 127; ) { char *b = strchr (t, '|'); if (b) *b = 0; d->values[n++].value = t; t = b ? b + 1 : NULL; }
		d->default_value = d->values[0].value; }
	struct vx_opts_v2 o = { cats, defs };
	cb (67 /* SET_CORE_OPTIONS_V2 */, &o);
}
#endif

static int opt_is (const char *key, const char *val)
{
	struct retro_variable v = { key, NULL };
	return env_cb (RETRO_ENVIRONMENT_GET_VARIABLE, &v) && v.value && !strcmp (v.value, val);
}
static int rate_set;                        /* (the screen rate: read at each game's start - RetroArch keeps the rate it
                                               was told; the RetroArch flavour tells it a new one too) */
static int opt_overlay_seen = -1, ov_made;
void retro_get_system_av_info (struct retro_system_av_info *a);
static void white_make (void);
static float light_g = 2.0f, light_f = 1.2f, light_p = 4.0f;   /* (v0.32: the screen light in EmuVR and its faint parts, see pixel_out) */
/* v0.26 (owner 2026-11-06): the overlay's scratches and plastic glow as Vectrex_EMU's sliders (the EmuVR look by
 * default) - drawn by the mod, so only passed on: the sheet file's line "look <values>" */
static const char *optv (const char *key) { struct retro_variable v = { key, NULL }; return env_cb (RETRO_ENVIRONMENT_GET_VARIABLE, &v) && v.value ? v.value : ""; }
static double optx (const char *key, double d) { const char *v = optv (key); return v[0] == 'x' ? atof (v + 1) : v[0] >= '0' && v[0] <= '9' ? atof (v) : d; }
static double optn (const char *key, double d) { const char *v = optv (key); return v[0] >= '0' && v[0] <= '9' ? atof (v) : d; }
static void read_look (void)
{
	snprintf (look, sizeof look, "scr=%d wear=%g fine=%g medium=%g large=%g scrib=%g glint=%g reach=%g glow=%d glowk=%g lamps=%d light=%g faint=%g tube=%g crefl=%g cwear=%g",
		strcmp (optv ("vectrex_emu_scr"), "off") != 0, optx ("vectrex_emu_scr_wear", 0.25), optx ("vectrex_emu_scr_fine", 1),
		optx ("vectrex_emu_scr_medium", 1), optx ("vectrex_emu_scr_large", 0.1), optx ("vectrex_emu_scr_scrib", 1),
		optx ("vectrex_emu_scr_glint", 1), optx ("vectrex_emu_scr_reach", 1), strcmp (optv ("vectrex_emu_art_glow"), "off") != 0,
		optx ("vectrex_emu_art_glow_k", 0.2), strcmp (optv ("vectrex_emu_scr_lamps"), "off") != 0, atof (optv ("vectrex_emu_screen_light")[0] ? optv ("vectrex_emu_screen_light") : "2.4"), optx ("vectrex_emu_faint_light", 1.2), optn ("vectrex_emu_tube_grey", 0.32), optx ("vectrex_emu_case_refl", 1), optx ("vectrex_emu_case_wear", 1));
	light_g = (float) atof (optv ("vectrex_emu_screen_light")[0] ? optv ("vectrex_emu_screen_light") : "2.4"); light_f = 1.0f; light_p = (float) optn ("vectrex_emu_dot_level", 2);
	if (!(light_p >= 1.0f && light_p <= 8.0f)) light_p = 2.0f;
	if (!(light_g >= 0.5f && light_g <= 8.0f)) light_g = 2.4f;   /* (v0.33: an unreadable value - the default; v0.32 had a ';' in its description: 0) */
	light_f = light_g / light_p;   /* (v0.35: the trail's level on screen: x light / dots level) */
	{ char *lp = strstr (look, " light="); if (lp) snprintf (lp, sizeof look - (lp - look), " light=%g faint=%g tube=%g crefl=%g cwear=%g", light_g, light_f, optn ("vectrex_emu_tube_grey", 0.32), optx ("vectrex_emu_case_refl", 1), optx ("vectrex_emu_case_wear", 1)); }
}
#define TRI(k) (opt_is (k, "on") ? 1 : opt_is (k, "off") ? 0 : -1)
static void read_options (void)
{
#ifdef VX_EMUVR
	read_look ();   /* (the EmuVR mod's look: its options only there) */
#endif
	if (!rate_set) { struct retro_variable v = { "vectrex_emu_rate", NULL };
	  int r = env_cb (RETRO_ENVIRONMENT_GET_VARIABLE, &v) && v.value ? atoi (v.value) : VX_RATE_DEF; opt_rate = r >= 30 && r <= 360 ? r : VX_RATE_DEF; }
	opt_overlay = opt_is ("vectrex_emu_overlay", "on");   /* (v0.25: off by default) */
	opt_imager = opt_is ("vectrex_emu_imager", "off") ? 0 : opt_is ("vectrex_emu_imager", "colour") ? 1 : 2;
#ifdef VX_EMUVR
	opt_swap = opt_is ("vectrex_emu_swap_eyes", "on");
	opt_glass = !opt_is ("vectrex_emu_area", "overlay sheet 162x207 mm");
#endif
	opt_glow = opt_is ("vectrex_emu_glow", "on") ? 1 : opt_is ("vectrex_emu_glow", "strong") ? 2 : 0;
	raw_glow = opt_is ("vectrex_emu_glow", "auto") ? -1 : opt_glow;
	opt_sigma = opt_is ("vectrex_emu_beam", "wide") ? 1.1 : opt_is ("vectrex_emu_beam", "medium") ? 0.8 : 0.55;
#ifdef VX_EMUVR
	opt_res = opt_is ("vectrex_emu_res", "4x 2496x3200") ? 4 : opt_is ("vectrex_emu_res", "3x 1872x2400") ? 3 : opt_is ("vectrex_emu_res", "2x 1248x1600") ? 2 : 1;
#endif
	opt_wob = opt_is ("vectrex_emu_wobble", "off") ? 0 : opt_is ("vectrex_emu_wobble", "60 Hz") ? 60 : 50;
	opt_buzz = !opt_is ("vectrex_emu_buzz", "off"); opt_spk = !opt_is ("vectrex_emu_speaker", "off");
#ifdef VX_EMUVR
	{ struct retro_variable v = { "vectrex_emu_eye_shift", NULL }; opt_shift = env_cb (RETRO_ENVIRONMENT_GET_VARIABLE, &v) && v.value ? atoi (v.value) : 0; }
#endif
	opt_after = !opt_is ("vectrex_emu_afterglow", "off");
	opt_trail = opt_is ("vectrex_emu_afterglow", "long (real Vectrex)") ? 1.0f : opt_is ("vectrex_emu_afterglow", "short") ? 0.3f : 0.6f;
	{ struct retro_variable v = { "vectrex_emu_trail_bright", NULL }; double k = env_cb (RETRO_ENVIRONMENT_GET_VARIABLE, &v) && v.value ? atof (v.value) * VX_TRAIL_DEF : VX_TRAIL_DEF;   /* (1x: the default) */
	  opt_trail_k = k >= 0.25 && k <= 6.0 ? (float) k : (float) VX_TRAIL_DEF; }   /* (v0.22: also dimmer, 0.5x / 0.75x - the owner) */
	{ struct retro_variable v = { "vectrex_emu_brightness", NULL }; double k = env_cb (RETRO_ENVIRONMENT_GET_VARIABLE, &v) && v.value ? atof (v.value) : 1.0;
	  opt_bright = k >= 1.0 && k <= 2.0 ? (float) k : 1.0f; }
	opt_dots = !opt_is ("vectrex_emu_corner_dots", "off"); opt_dwell = !opt_is ("vectrex_emu_dwell", "off"); opt_speedb = !opt_is ("vectrex_emu_speed_bright", "off");
	opt_smooth = !opt_is ("vectrex_emu_smooth", "off"); raw_smooth = opt_is ("vectrex_emu_smooth", "auto") ? -1 : opt_smooth; opt_halo = !opt_is ("vectrex_emu_corner_glow", "off"); opt_bend = !opt_is ("vectrex_emu_bending", "off");
	raw_geom = TRI ("vectrex_emu_geometry"); opt_geom = raw_geom != 0;
	raw_passes = TRI ("vectrex_emu_passes"); raw_dith = TRI ("vectrex_emu_dither"); opt_sbend = opt_is ("vectrex_emu_smooth_bends", "on");
	raw_sbend = opt_is ("vectrex_emu_smooth_bends", "auto") ? -1 : opt_sbend;
	{ const char *pv = optv ("vectrex_emu_profile"); opt_profile = !strcmp (pv, "ultra") ? 1 : !strcmp (pv, "high") ? 2 : !strcmp (pv, "medium") ? 3
	                                                             : !strcmp (pv, "low") ? 4 : !strcmp (pv, "basic") ? 5 : 0; }
	raw_glass = TRI ("vectrex_emu_glass_edge"); raw_case = TRI ("vectrex_emu_case");
#define STR(k) (opt_is (k, "auto") || !optv (k)[0] ? -1.0f : (float) atof (optv (k)))   /* (a strength: auto -1, else 0..2) */
	k_ovl = STR ("vectrex_emu_ceiling_ov"); k_ovglow = STR ("vectrex_emu_ov_glow"); raw_scr = TRI ("vectrex_emu_scratches");
	raw_par = TRI ("vectrex_emu_parallax"); opt_par = raw_par != 0;
	{ float *const d[7] = { &ovfx.wear, &ovfx.fine, &ovfx.medium, &ovfx.large, &ovfx.scrib, &ovfx.glint, &ovfx.reach };
	  static const char *const kk[7] = { "vectrex_emu_scr_wear", "vectrex_emu_scr_fine", "vectrex_emu_scr_medium", "vectrex_emu_scr_damage",
		"vectrex_emu_scr_scrib", "vectrex_emu_scr_glint", "vectrex_emu_scr_reach" };
	  for (int i = 0; i < 7; i++) { float v = (float) optx (kk[i], i == 3 ? 0.1 : 1.0); *d[i] = v >= 0 && v <= 2 ? v : (i == 3 ? 0.1f : 1.0f); }
	  int sd = atoi (optv ("vectrex_emu_scr_seed")); ovfx.seed = sd >= 1 && sd <= 10 ? sd : 1; }
	k_ceiling = STR ("vectrex_emu_ceiling"); k_crefl = STR ("vectrex_emu_case_refl"); k_cwear = STR ("vectrex_emu_case_wear");
	raw_ceiling = k_ceiling < 0 ? -1 : k_ceiling > 0; raw_crefl = k_crefl < 0 ? -1 : k_crefl > 0; raw_cwear = k_cwear < 0 ? -1 : k_cwear > 0;
#ifndef VX_EMUVR
	{ const char *lx = optv ("vectrex_emu_lamp_x"), *ly = optv ("vectrex_emu_lamp_y"); g_lamp_x = lx[0] ? (float) atof (lx) / 100.0f : 0.0f; g_lamp_y = ly[0] ? (float) atof (ly) / 100.0f : 0.273f;
	  if (!strcmp (ly, "27%")) g_lamp_y = 0.273f; }
#endif
	raw_groom = TRI ("vectrex_emu_room_glass"); opt_room_glass = raw_groom > 0;
	if (unit_known) { unit_make (unit_preset (), optv); if (vx_ok ()) { emu_wait_ext (); unit_live (unit_addon); } }   /* (the unit, while running) */
	opt_async = !opt_is ("vectrex_emu_threaded", "off");
	opt_white = opt_is ("vectrex_emu_white", "9300 K") ? 9300 : opt_is ("vectrex_emu_white", "16000 K") ? 16000 : opt_is ("vectrex_emu_white", "6504 K (neutral)") ? 6504 : 12000;
	white_make ();
	opt_power = !opt_is ("vectrex_emu_power_fx", "off");
	opt_trail_k0 = opt_trail_k;                 /* (EmuVR: one trail brightness; the RetroArch flavour reads its own below) */
	{ int was = opt_overlay_seen; opt_overlay_seen = opt_overlay; if (was != opt_overlay) ov_on = opt_overlay; }   /* (only when the option itself changed:
	                                              a change of another option keeps the button's choice) */
#ifdef VX_EMUVR
	opt_ov_btn = 2;                             /* (EmuVR: SELECT, as before) */
#else
	opt_ov_btn = 1;                             /* (the RetroPad's START, named "Overlay off / on": RetroArch's Controls menu
	                                               puts it on any button - the owner, 2026-11-15) */
	{ float t = (float) optx ("vectrex_emu_ov_tint", 1), pr = (float) optx ("vectrex_emu_ov_print", 1);
	  const char *rv = optv ("vectrex_emu_room"); float rm = !strncmp (rv, "off", 3) ? 0.0f : rv[0] ? (float) atof (rv) : 1.0f;
	  if (!(rm >= 0 && rm <= 4)) rm = 1.0f;
	  if (t != opt_ov_tint || pr != opt_ov_print || rm != opt_room) { opt_ov_tint = t; opt_ov_print = pr; opt_room = rm; ov_made = 0; }
	  /* (vecrender.py: the bare glass grey x the light level - (room / twice the default) cubed, 1 at most) */
	  { float ll = rm / 2.0f; ll = ll > 1 ? 1 : ll; glass_grey_v = 0.20f * ll * ll * ll; glass_grey = opt_room_glass ? glass_grey_v : 0.0f; }   /* (fx_apply: whether it shows) */ }
	/* (the core turns the picture itself: RetroArch's own rotation kept the upright picture's shape on the screen) */
	opt_rot = opt_is ("vectrex_emu_rotate", "90 (turn the device right)") ? 1 : opt_is ("vectrex_emu_rotate", "180") ? 2
	        : opt_is ("vectrex_emu_rotate", "270 (turn the device left)") ? 3 : 0;
	opt_rot_stick = !opt_is ("vectrex_emu_rotate_stick", "off");
	{ const char *zv = optv ("vectrex_emu_size"); int z = atoi (zv); size_auto = !zv[0] || !strcmp (zv, "auto");
	  if (size_auto) { if (!scr_short) scr_load (); z = scr_short; }   /* (the screen's short side: gpu_gl.c scr_detect, kept) */
	  opt_size = z >= 240 && z <= 2400 ? z : 800; }
	opt_area = opt_is ("vectrex_emu_area", "glass window") ? 1 : opt_is ("vectrex_emu_area", "overlay sheet") ? 2 : 0;
	opt_deck = opt_is ("vectrex_emu_deck", "full") || opt_is ("vectrex_emu_deck", "on") ? 1 : opt_is ("vectrex_emu_deck", "slim") ? 2 : opt_is ("vectrex_emu_deck", "pop-up") ? 3 : 0; opt_shape = opt_is ("vectrex_emu_shape", "16:9");
	opt_trail_k0 = opt_trail_k;                 /* (1.1: one trail brightness - the light added as light: no other one needed) */
	/* the screen rate changed while a game runs: RetroArch told the new timing (it was only read at a game's start) */
	if (rate_set) { struct retro_variable v = { "vectrex_emu_rate", NULL };
	  int r = env_cb (RETRO_ENVIRONMENT_GET_VARIABLE, &v) && v.value ? atoi (v.value) : VX_RATE_DEF; r = r >= 30 && r <= 360 ? r : VX_RATE_DEF;
	  if (r != opt_rate) { opt_rate = r; struct retro_system_av_info av; retro_get_system_av_info (&av); env_cb (RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO, &av); } }
#endif
}

void retro_set_environment (retro_environment_t cb)
{
	env_cb = cb;
#ifdef VX_EMUVR
	cb (RETRO_ENVIRONMENT_SET_VARIABLES, vars);
#else
	opts_set (cb);
#endif
	bool nogame = false; cb (RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME, &nogame);
}
void retro_set_video_refresh (retro_video_refresh_t cb) { video_cb = cb; }
void retro_set_audio_sample (retro_audio_sample_t cb) { (void) cb; }
void retro_set_audio_sample_batch (retro_audio_sample_batch_t cb) { audio_cb = cb; }
void retro_set_input_poll (retro_input_poll_t cb) { poll_cb = cb; }
void retro_set_input_state (retro_input_state_t cb) { input_cb = cb; }
unsigned retro_api_version (void) { return RETRO_API_VERSION; }
/* v0.16: the core stays loaded until RetroArch ends (pinned): RetroArch unloading it while OpenMP's worker threads (the
 * banded drawing, since v0.8) still live crashed RetroArch on exit */
#ifdef _WIN32
__declspec(dllimport) int __stdcall GetModuleHandleExA (unsigned long, const char *, void **);
__declspec(dllimport) int __stdcall SetEnvironmentVariableA (const char *, const char *);
#endif
#if defined(_WIN32)
#include <dbghelp.h>
/* (test: VXPOC_CRASHLOG=file - a crash's address in the core written there: the module's base, the offset) */
static LONG WINAPI vx_crash (EXCEPTION_POINTERS *ep)
{
	const char *f = getenv ("VXPOC_CRASHLOG"); FILE *o = f ? fopen (f, "a") : NULL;
	if (o) { HMODULE m = NULL; GetModuleHandleExA (GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR) ep->ExceptionRecord->ExceptionAddress, &m);
		char mn[260] = ""; if (m) GetModuleFileNameA (m, mn, sizeof mn);
		fprintf (o, "code %08lx at %p module %s offset %llx\n", (unsigned long) ep->ExceptionRecord->ExceptionCode, ep->ExceptionRecord->ExceptionAddress, mn,
		         (unsigned long long) ((char *) ep->ExceptionRecord->ExceptionAddress - (char *) m));
		/* (the callers: dbghelp's StackWalk64 from the fault's context) */
		HMODULE dh = LoadLibraryA ("dbghelp.dll");
		typedef BOOL (WINAPI *SW) (DWORD, HANDLE, HANDLE, LPSTACKFRAME64, PVOID, PREAD_PROCESS_MEMORY_ROUTINE64, PFUNCTION_TABLE_ACCESS_ROUTINE64, PGET_MODULE_BASE_ROUTINE64, PTRANSLATE_ADDRESS_ROUTINE64);
		typedef BOOL (WINAPI *SI) (HANDLE, PCSTR, BOOL);
		SW sw = dh ? (SW) GetProcAddress (dh, "StackWalk64") : NULL; SI si = dh ? (SI) GetProcAddress (dh, "SymInitialize") : NULL;
		PFUNCTION_TABLE_ACCESS_ROUTINE64 fta = dh ? (PFUNCTION_TABLE_ACCESS_ROUTINE64) GetProcAddress (dh, "SymFunctionTableAccess64") : NULL;
		PGET_MODULE_BASE_ROUTINE64 gmb = dh ? (PGET_MODULE_BASE_ROUTINE64) GetProcAddress (dh, "SymGetModuleBase64") : NULL;
		if (sw && si && fta && gmb) { HANDLE pr = GetCurrentProcess (); si (pr, NULL, TRUE);
			CONTEXT cx = *ep->ContextRecord; STACKFRAME64 sf; memset (&sf, 0, sizeof sf);
			sf.AddrPC.Offset = cx.Rip; sf.AddrPC.Mode = AddrModeFlat; sf.AddrFrame.Offset = cx.Rbp; sf.AddrFrame.Mode = AddrModeFlat; sf.AddrStack.Offset = cx.Rsp; sf.AddrStack.Mode = AddrModeFlat;
			for (int k = 0; k < 16 && sw (IMAGE_FILE_MACHINE_AMD64, pr, GetCurrentThread (), &sf, &cx, NULL, fta, gmb, NULL); k++) {
				HMODULE mm = NULL; GetModuleHandleExA (GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR) (uintptr_t) sf.AddrPC.Offset, &mm);
				char n2[260] = ""; if (mm) GetModuleFileNameA (mm, n2, sizeof n2); const char *b = strrchr (n2, '\\');
				fprintf (o, "  %s +%llx\n", b ? b + 1 : n2, (unsigned long long) (sf.AddrPC.Offset - (uintptr_t) mm)); } }
		fclose (o); }
	return EXCEPTION_CONTINUE_SEARCH;
}
#endif
void retro_init (void)
{
#if defined(_WIN32)
	if (getenv ("VXPOC_CRASHLOG")) AddVectoredExceptionHandler (1, vx_crash);
	if (getenv ("VXPOC_STDERR")) { freopen (getenv ("VXPOC_STDERR"), "a", stderr); setvbuf (stderr, NULL, _IONBF, 0); }   /* (test: the core's messages to a file) */
#endif
	const char *e = getenv ("VECTREX_SHEET"); sheet_file = e && *e ? e : NULL;
	e = getenv ("VECTREX_OVERLAY"); ovl_file = e && *e ? e : NULL;
#ifndef _WIN32
	/* (Android / Linux: OpenMP's idle threads sleep (set before its first parallel loop: the runtime reads it then), all
	 *  but two of the cores draw (2 to 4: a phone's big cores; the rest for RetroArch and the system), and the core stays loaded -
	 *  RetroArch unloading it while OpenMP's threads live would crash, as on Windows (v0.16)) */
	if (!getenv ("OMP_WAIT_POLICY")) setenv ("OMP_WAIT_POLICY", "PASSIVE", 1);
#ifdef _OPENMP
	if (!getenv ("OMP_NUM_THREADS")) { long n = sysconf (_SC_NPROCESSORS_ONLN) - 2; omp_set_num_threads ((int) (n < 2 ? 2 : n > 4 ? 4 : n)); }   /* (the Retroid Pocket Nova, 2026-11-15: 1 thread 23 ms, 2 12.2, 3 8.4, 4 7.2, 6 8.4 - the small cores slow it) */
#endif
	{ Dl_info di; if (dladdr ((void *) retro_init, &di) && di.dli_fname) dlopen (di.dli_fname, RTLD_NOW | RTLD_NODELETE); }
#else
	/* v0.17: OpenMP's idle threads sleep instead of spinning (owner 2026-11-05: all 16 cores at 100 % in EmuVR - the
	 * drawing threads spun between frames: 13.9 cores busy, 4.6 with this), set before the first parallel drawing */
	{ char ow[32]; if (!GetEnvironmentVariableA ("OMP_WAIT_POLICY", ow, sizeof ow)) { _putenv ("OMP_WAIT_POLICY=PASSIVE"); SetEnvironmentVariableA ("OMP_WAIT_POLICY", "PASSIVE");
		/* (vcomp reads it through the shared C runtime, ucrtbase, which copied the environment when it started: set there too) */
		HMODULE u = LoadLibraryA ("ucrtbase.dll"); int (*up) (const char *) = u ? (int (*) (const char *)) (void *) GetProcAddress (u, "_putenv") : NULL;
		if (up) up ("OMP_WAIT_POLICY=PASSIVE"); } }
	/* v0.18: the drawing threads - half the PC's (at most 8; RetroArch, EmuVR and SteamVR need the rest): measured
	 * 2026-11-05 on 16 threads, 8 draw as fast as 16 (0.79 against 0.77 ms) at half the CPU (0.51 against 1.10 cores) */
	{ char nt[16]; if (!GetEnvironmentVariableA ("OMP_NUM_THREADS", nt, sizeof nt)) {
		SYSTEM_INFO si; GetSystemInfo (&si); int n = (int) si.dwNumberOfProcessors / 2; n = n < 2 ? 2 : n > 8 ? 8 : n;
		omp_set_num_threads (n); } }
	{ const char *lp = getenv ("VXPOC_OMPLOG"); FILE *f = lp ? fopen (lp, "a") : NULL;   /* (test: is vcomp already there?) */
	  if (f) { char v[64] = ""; GetEnvironmentVariableA ("OMP_WAIT_POLICY", v, sizeof v);
	           fprintf (f, "retro_init: OMP_WAIT_POLICY=%s, vcomp loaded %d%c", v, GetModuleHandleA ("vcomp140.dll") != NULL, 10); fclose (f); } }
	/* (the OpenMP runtime, vcomp140, is delay-loaded - build.bat /DELAYLOAD - so it starts after this and reads it) */
	void *h; GetModuleHandleExA (0x4 | 0x1, (const char *) (void *) retro_init, &h);   /* (FROM_ADDRESS | PIN) */
#endif
}
void retro_deinit (void) { if (sheet_file) { FILE *f = fopen (sheet_file, "w"); if (f) fclose (f); } }
void retro_get_system_info (struct retro_system_info *i)
{
	memset (i, 0, sizeof *i);
	i->library_name = "Vectrex_EMU";
#ifdef VX_EMUVR
	i->library_version = "0.36";
#else
	i->library_version = "1.1";                 /* (the RetroArch flavour's own line: libretro_port_plan.md 5.6) */
#endif
	i->valid_extensions = "vec|bin|gam|um2|img|pit|v4e"; i->need_fullpath = true; i->block_extract = false;
}
/* the picture: the glass window or the whole sheet (opt_area; auto: the sheet while an overlay is painted in) */
static int area_glass (void)
{
#ifdef VX_EMUVR
	return opt_glass;
#else
	extern int gpu_frame_shown (void);
	return opt_area == 1 ? 1 : opt_area == 2 ? 0 : !(opt_deck == 1 || opt_deck == 2) && !(ov && ov_on && !sheet_file) && !gpu_frame_shown ();
#endif
}
/* one eye's frame (the sheet, ew x fh) and the area sent from it (cw x ch) - the RetroArch flavour: the area's side that
 * lies along the screen's short side (its height; turned 90 / 270: its width) opt_size pixels exactly */
static void geom_calc (int glass, int *ew, int *fh, int *cw, int *ch)
{
#ifdef VX_EMUVR
	*ew = PW * opt_res; *fh = PH * opt_res;
	*cw = glass ? (int) (*ew * 145.7 / 162.0 + 0.5) : *ew; *ch = glass ? (int) (*fh * 188.5 / 207.0 + 0.5) : *fh;
#else
	const int odd = opt_rot & 1; double k = opt_size / (glass ? (odd ? 145.7 : 188.5) : (odd ? 162.0 : 207.0));   /* (px per mm) */
	if ((opt_deck == 1 || opt_deck == 2) && !odd && !opt_shape && !glass) k /= 1.0 + (opt_deck == 2 ? 0.046 : 0.08134) / 0.924;   /* (4:3 with the deck:
	                                              picture + deck = the screen's height) */
	*ew = (int) (162.0 * k + 0.5); *fh = (int) (207.0 * k + 0.5);
	*cw = glass ? (int) (145.7 * k + 0.5) : *ew; *ch = glass ? (int) (188.5 * k + 0.5) : *fh;
	if (*ew > 4 * PW) { *ew = 4 * PW; if (*cw > *ew) *cw = *ew; }   /* (glow_blur's row: 4 x 624 at most) */
#endif
}
static void av_fill (struct retro_system_av_info *a)
{
	memset (a, 0, sizeof *a);
#ifdef VX_EMUVR
	a->geometry.base_width = OW ? OW : PW; a->geometry.base_height = OH ? OH : PH;
	a->geometry.max_width = OW ? OW : PW * 2; a->geometry.max_height = OH ? OH : PH;
	a->geometry.aspect_ratio = (float) (stereo ? 2.0 * PW / PH : (double) PW / PH);
#else
	const int odd = opt_rot & 1, ow = OW ? OW : PW, oh = OH ? OH : PH;
	a->geometry.base_width = odd ? oh : ow; a->geometry.base_height = odd ? ow : oh;   /* (as sent: turned) */
	a->geometry.aspect_ratio = (float) a->geometry.base_width / (float) a->geometry.base_height;   /* (square pixels) */
	unsigned m = 0;                             /* (the largest either area gives, either way round: a toggle needs no new video) */
	for (int gl = 0; gl < 2; gl++) { int ew, fh, cw, ch; geom_calc (gl, &ew, &fh, &cw, &ch);
		unsigned w = (unsigned) (stereo ? 2 * cw : cw); if (w > m) m = w; if ((unsigned) ch > m) m = (unsigned) ch; }
	if (a->geometry.base_width > m) m = a->geometry.base_width; if (a->geometry.base_height > m) m = a->geometry.base_height;
	a->geometry.max_width = a->geometry.max_height = m;
#endif
	a->timing.fps = (double) opt_rate; a->timing.sample_rate = 44100.0;
}
void retro_get_system_av_info (struct retro_system_av_info *a) { av_fill (a); av_maxw = a->geometry.max_width; av_maxh = a->geometry.max_height; }
/* (a new size, area or rotation while a game runs: RetroArch told - a new picture size within the one announced, else
 *  the video set up anew) */
static void geo_tell (void)
{
	struct retro_system_av_info av; av_fill (&av);
	if (av.geometry.max_width > av_maxw || av.geometry.max_height > av_maxh) retro_get_system_av_info (&av), env_cb (RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO, &av);
	else env_cb (RETRO_ENVIRONMENT_SET_GEOMETRY, &av.geometry);
}
void retro_set_controller_port_device (unsigned p, unsigned d) { (void) p; (void) d; }
void retro_reset (void) { extern void vecx_reset_button (void); emu_wait_ext (); vecx_reset_button (); }

/* ---- the overlay by the game's name (vectrex_emu.py find_overlay: the name, its base without (...), its letters) */
static void base_of (const char *s, char *out, size_t n)
{
	const char *b = s; for (const char *p = s; *p; p++) if (*p == '/' || *p == '\\') b = p + 1;
	snprintf (out, n, "%s", b);
	char *e = strrchr (out, '.'); if (e && strlen (e) <= 5) *e = 0;
	for (char *p = out; *p; p++) if (*p == '_') *p = ' ';
	for (;;) {                                  /* (the trailing (...) and [...] parts off) */
		size_t L = strlen (out); while (L && out[L - 1] == ' ') out[--L] = 0;
		if (L && (out[L - 1] == ')' || out[L - 1] == ']')) {
			char open = out[L - 1] == ')' ? '(' : '['; char *o = strrchr (out, open);
			if (o) { *o = 0; continue; } }
		break; }
}
static void letters (const char *s, char *out, size_t n)
{
	size_t k = 0; for (; *s && k + 1 < n; s++) if (isalnum ((unsigned char) *s)) out[k++] = (char) tolower ((unsigned char) *s);
	out[k] = 0;
}
#ifndef VX_EMUVR
#include "overlay_art.c"                    /* (1.1: the desktop's overlays as they are - MiSTer .art and PNG, vart.py) */
#define OV_EXT(x) ((x) && (!vx_stricmp (x, ".png") || !vx_stricmp (x, ".art")))
#else
#define OV_EXT(x) ((x) && !vx_stricmp (x, ".png"))   /* (EmuVR: the mod hangs the PNG itself) */
#endif
static int try_overlay_dir (const char *dir, const char *rom)
{
	char want[512], wantl[512]; base_of (rom, want, sizeof want); letters (want, wantl, sizeof wantl);
	char best[1600] = ""; int bestk = 0; size_t bestlen = 99999;
#ifdef _WIN32
	char pat[1200]; snprintf (pat, sizeof pat, "%s\\*.*", dir);
	WIN32_FIND_DATAA fd; HANDLE h = FindFirstFileA (pat, &fd);
	if (h == INVALID_HANDLE_VALUE) return 0;
	do { const char *fn = fd.cFileName, *x = strrchr (fn, '.');
		if (!OV_EXT (x)) continue;
#else
	DIR *dh = opendir (dir); struct dirent *de;
	if (!dh) return 0;
	while ((de = readdir (dh))) { const char *fn = de->d_name, *x = strrchr (fn, '.');
		if (!OV_EXT (x)) continue;
#endif
		char b[512], bl[512]; base_of (fn, b, sizeof b); letters (b, bl, sizeof bl);
		char full[512]; snprintf (full, sizeof full, "%s", fn); char *e = strrchr (full, '.'); if (e) *e = 0;
		int k = !vx_stricmp (full, want) ? 3 : !vx_stricmp (b, want) ? 2 : (bl[0] && !strcmp (bl, wantl)) ? 1 : 0;
		size_t L = strlen (fn);
		if (k > bestk || (k && k == bestk && L < bestlen)) { bestk = k; bestlen = L; snprintf (best, sizeof best, "%s" SEP "%s", dir, fn); }
#ifdef _WIN32
	} while (FindNextFileA (h, &fd));
	FindClose (h);
#else
	}
	closedir (dh);
#endif
	if (!bestk) return 0;
#ifdef VX_EMUVR
	int n; ov = stbi_load (best, &ovw, &ovh, &n, 4);
#else
	ov = ov_file_load (best, &ovw, &ovh);       /* (as the desktop: .art or .png, cut to the sheet, its soft edge) */
#endif
	snprintf (ov_path, sizeof ov_path, "%s", best); ov_made = 0;
	fprintf (stderr, "[vectrex_emu] overlay %s\n", best);
	return ov != NULL;
}

bool retro_load_game (const struct retro_game_info *g);   /* (at the end: it sets up the picture's buffers) */
static bool game_open (const char *gpath, const char *sys, int first);
static int memmem_title (const unsigned char *h, size_t n, const char *t);
static const char *speech_dev (const unsigned char *h, size_t n, const char *path);
/* the game's set-up from its title: port 2 (the 3D Imager, the light pen), the speech module - for a load and (v0.23)
 * a cartridge swapped while running; returns whether it is a 3D Imager game */
static void vx_mkdir (const char *d)
{
#ifdef _WIN32
	extern int _mkdir (const char *); _mkdir (d);
#else
	mkdir (d, 0777);
#endif
}
static int vx_ncpu (void)
{
#ifdef _WIN32
	SYSTEM_INFO si; GetSystemInfo (&si); return (int) si.dwNumberOfProcessors;
#else
	return (int) sysconf (_SC_NPROCESSORS_ONLN);
#endif
}
#include "sdcard.c"                         /* (1.1: a folder as the UVMC2's / PiTrex's SD card - the desktop's fatimg.py) */
static char sd_img[1900], sd_folder[1800];     /* (the card in use: its image, its folder - written back at the end) */
/* (1.1) a save's file: RetroArch's saves folder (else the game's), <game file><ext>; a save next to the ROM and none
 * there yet: copied over as the start */
static void save_path (const char *rom, const char *ext, char *out, size_t n)
{
	const char *sv = NULL; env_cb (RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY, &sv);
	const char *b = rom; for (const char *q = rom; *q; q++) if (*q == '/' || *q == '\\') b = q + 1;
	if (sv && *sv) snprintf (out, n, "%s" SEP "%s%s", sv, b, ext); else snprintf (out, n, "%s%s", rom, ext);
	FILE *t = fopen (out, "rb"); if (t) { fclose (t); return; }
	char old[1900]; snprintf (old, sizeof old, "%s%s", rom, ext);
	FILE *a = fopen (old, "rb"); if (!a) return;
	FILE *c = fopen (out, "wb"); char buf[16384]; size_t k;
	if (c) { while ((k = fread (buf, 1, sizeof buf, a)) > 0) fwrite (buf, 1, k, c); fclose (c); fprintf (stderr, "[vectrex_emu] save %s: from %s\n", out, old); }
	fclose (a);
}
/* (1.1) a flash cartridge (Vectorblade, the 512K cartridge): its saves in RetroArch's saves folder - vtest loaded the
 * ROM folder's; this one, when there, is the newer. Written a second after the game last wrote (live.c flash_save) */
static void flash_to_saves (const char *rom)
{
	extern const char *vt_flash_file; extern size_t vt_cart_len; static char fs[1900];
	if (!vt_flash_file) return;
	save_path (rom, ".sav", fs, sizeof fs);
	FILE *f = fopen (fs, "rb");
	if (f) { unsigned char *tmp = malloc (vt_cart_len); size_t m = tmp ? fread (tmp, 1, vt_cart_len, f) : 0; fclose (f);
	         if (m == vt_cart_len) memcpy (cart, tmp, m);
	         free (tmp); }
	vt_flash_file = fs; fprintf (stderr, "[vectrex_emu] flash saves %s\n", fs);
}
/* (1.1) VecFever's .v4e cartridges (vectrex_emu.py v4e_rom, v4e.py): unpacked into <saves>/vectrex_emu_v4e/<name>.bin,
 * the plain image the core runs (its saves beside the others); type 4 has a DS2431 save chip */
static int v4e_ds;
static uint32_t crc32_up (uint32_t c, const unsigned char *p, size_t n)
{ c = ~c; while (n--) { c ^= *p++; for (int k = 0; k < 8; k++) c = c & 1 ? (c >> 1) ^ 0xEDB88320u : c >> 1; } return ~c; }
static long v4e_bank (const unsigned char *s, size_t n, unsigned char *o)   /* (send2vf's dzip: -> the bytes out, -1 damaged) */
{
	size_t i = 0; long m = 0;
	while (i < n) {
		unsigned b = s[i];
		if (b < 0x80) { long k = 128 - (long) b; if (i + 1 + (size_t) k > n || m + k > 0x8000) return -1; memcpy (o + m, s + i + 1, (size_t) k); m += k; i += 1 + (size_t) k; continue; }
		if (i + 1 >= n) return -1;
		unsigned c = s[i + 1]; long off, len;
		if (c < 0x80) { off = (long) b - 256; len = 128 - (long) c; i += 2; }
		else { if (i + 2 >= n) return -1; unsigned d = s[i + 2]; off = (long) (((b & 0x7f) << 7) | (c & 0x7f)) - 16384; len = d >= 128 ? 384 - (long) d : 128 - (long) d; i += 3; }
		long p = m + off; if (p < 0 || m + len > 0x8000) return -1;
		for (long k = 0; k < len; k++) o[m + k] = o[p + k];
		m += len; }
	return m;
}
static const char *v4e_open (const char *path)          /* (the image to run; NULL: not loadable - said why) */
{
	static char out[1900]; v4e_ds = 0;
	const char *e = strrchr (path, '.'); if (!e || vx_stricmp (e, ".v4e")) return path;
	FILE *f = fopen (path, "rb"); if (!f) return NULL;
	fseek (f, 0, SEEK_END); long n = ftell (f); fseek (f, 0, SEEK_SET);
	unsigned char *d = n > 8 && n < (8L << 20) ? malloc ((size_t) n) : NULL, *rom = malloc (64 * 0x8000 + 0x10000);
	const char *bad = NULL; long rn = 0; int ct = -1;
	if (!d || !rom || fread (d, 1, (size_t) n, f) != (size_t) n) bad = "unreadable";
	fclose (f);
	if (!bad && memcmp (d, "V4EB", 4)) bad = "not a VecFever .v4e file";
	int nb = 0;
	if (!bad) { ct = d[4] << 8 | d[5]; nb = d[6] << 8 | d[7];
		if (nb < 1 || nb > 64 || n < 8 + 6 * nb) bad = "a damaged header";
		else if (ct != 0 && ct != 3 && ct != 4 && ct != 5 && ct != 8 && ct != 10) bad = "a cartridge type this emulator does not have"; }
	long pos = 8 + 6L * nb;
	for (int k = 0; !bad && k < nb; k++) {
		const unsigned char *h = d + 8 + 6 * k; uint32_t crc = (uint32_t) h[0] << 24 | h[1] << 16 | h[2] << 8 | h[3]; long sz = h[4] << 8 | h[5];
		if (pos + sz > n) { bad = "a bank cut short"; break; }
		if (crc32_up (0x56344542u, d + pos, (size_t) sz) != crc) { bad = "a wrong checksum (a damaged file)"; break; }
		long m = sz == 0x8000 ? (memcpy (rom + rn, d + pos, 0x8000), 0x8000) : v4e_bank (d + pos, (size_t) sz, rom + rn);
		if (m < 0) { bad = "a damaged bank"; break; }
		rn += m; pos += sz; }
	unsigned char *img = NULL; long in = rn;
	if (!bad && ct == 8) {                                  /* (2 x 48 KB, banks by PB6: the IRQ line does not switch) */
		if (rn != 3 * 0x8000) bad = "a 2 x 48 KB cartridge of the wrong size";
		else { img = malloc (4 * 0x10000); for (int q = 0; q < 4; q++) { unsigned char *b0 = img + q * 0x10000; int k = q & 1;
			memcpy (b0, rom + k * 0x8000, 0x8000); memcpy (b0 + 0x8000, rom + 0x10000 + k * 0x4000, 0x4000); memset (b0 + 0xC000, 0xff, 0x4000); } in = 4 * 0x10000; } }
	else if (!bad && ct == 10) {                            /* (4 x 48 KB, Vectorblade: PB6 and IRQ - send2vf's upper halves put back) */
		if (rn != 6 * 0x8000) bad = "a 4 x 48 KB cartridge of the wrong size";
		else { static const int HI[4] = { 0x0000, 0x8000, 0x4000, 0xC000 }; img = malloc (4 * 0x10000);
			for (int q = 0; q < 4; q++) { unsigned char *b0 = img + q * 0x10000;
				memcpy (b0, rom + q * 0x8000, 0x8000); memcpy (b0 + 0x8000, rom + 4 * 0x8000 + HI[q], 0x4000); memset (b0 + 0xC000, 0xff, 0x4000); }
			in = 4 * 0x10000; } }
	if (!bad) {
		const char *sv = NULL; env_cb (RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY, &sv);
		char dd[1700]; snprintf (dd, sizeof dd, "%s" SEP "vectrex_emu_v4e", sv && *sv ? sv : g_dir); vx_mkdir (dd);
		const char *b = path; for (const char *q = path; *q; q++) if (*q == '/' || *q == '\\') b = q + 1;
		char nm[600]; snprintf (nm, sizeof nm, "%s", b); { char *x = strrchr (nm, '.'); if (x) *x = 0; }
		snprintf (out, sizeof out, "%s" SEP "%s.bin", dd, nm);
		FILE *o = fopen (out, "wb"); if (o) { fwrite (img ? img : rom, 1, (size_t) in, o); fclose (o); } else bad = "the image could not be written";
		v4e_ds = ct == 4; }
	free (d); free (rom); free (img);
	if (bad) { fprintf (stderr, "[vectrex_emu] %s: %s\n", path, bad); return NULL; }
	fprintf (stderr, "[vectrex_emu] VecFever .v4e (type %d) unpacked: %s\n", ct, out);
	return out;
}
static int game_env (const char *path, const char *sys, const char *dir)
{
	int im_out = 0;
	/* port 2 by the game's title (vectrex_emu.py port2_needed): the 3D Imager's games, the light pen's */
	{ FILE *c = fopen (path, "rb"); unsigned char h[96] = { 0 }; if (c) { fread (h, 1, 96, c); fclose (c); }
	  int im = memmem_title (h, 96, "3D MINE") || memmem_title (h, 96, "NARROW") || memmem_title (h, 96, "CRAZY COASTER") || memmem_title (h, 96, "ROCKET SLEDGE");
	  if (im) vx_putenv (memmem_title (h, 96, "3D MINE") ? "VT_IMAGER=mine" : memmem_title (h, 96, "NARROW") ? "VT_IMAGER=narrow" : "VT_IMAGER=coaster");
	  else vx_putenv ("VT_IMAGER=");
	  im_out = im;
	  lightpen = (memmem_title (h, 96, "ART") && memmem_title (h, 96, "MASTER")) || memmem_title (h, 96, "MELODY") || memmem_title (h, 96, "ANIMACTION");
	  /* (1.1, the desktop's Config > Ports: port 2's device - auto (the game's own, above), none, or one by name) */
	  const char *p2 = optv ("vectrex_emu_port2"); int p2_none = !strcmp (p2, "none"), p2_im = !strcmp (p2, "3D Imager"), p2_lp = !strcmp (p2, "light pen");
	  const char *p2_sp = !strcmp (p2, "VecVoice") ? "vecvoice" : !strcmp (p2, "VecVox") ? "vecvox" : NULL;
	  if (p2_none || p2_lp || p2_sp) { im = 0; vx_putenv ("VT_IMAGER="); }
	  if (p2_im && !im) { const char *b = path; for (const char *q = path; *q; q++) if (*q == '/' || *q == '\\') b = q + 1;   /* (the disc by
	                                              the name, as the desktop's imager_disc) */
		char lb[512]; snprintf (lb, sizeof lb, "%s", b); for (char *q = lb; *q; q++) *q = (char) tolower ((unsigned char) *q);
		im = 1; vx_putenv (strstr (lb, "mine") && strstr (lb, "3d") ? "VT_IMAGER=mine" : strstr (lb, "coaster") ? "VT_IMAGER=coaster" : "VT_IMAGER=narrow"); }
	  if (p2_none || p2_im || p2_sp) lightpen = 0;
	  if (p2_lp) lightpen = 1;
	  im_out = im;
	  vx_putenv (lightpen ? "VT_LIGHTPEN=1" : "VT_LIGHTPEN=");
	  /* v0.11: the speech ROM (sp0256-al2.bin or al2.bin, in <system>/vectrex_emu, the system folder or next to the game)
	   * loaded whenever it is there, the module's sound on a channel of its own (vectrex_emu.py Core: VT_VECVOICE,
	   * VT_SPEECHOUT), and a VecVoice / VecVox in port 2 for the games that talk */
	  extern int vt_vecvoice; vt_vecvoice = 0;   /* (the last game's module out) */
	  static char spr[1700], e1[1800]; spr[0] = 0;
	  const char *spn[] = { "vectrex_emu" SEP "sp0256-al2.bin", "vectrex_emu" SEP "al2.bin", "sp0256-al2.bin", "al2.bin", NULL };
	  const char *spd[] = { sys ? sys : dir, dir, NULL };
	  for (int d = 0; spd[d] && !spr[0]; d++) for (int k = 0; spn[k] && !spr[0]; k++) {
		char p[1700]; snprintf (p, sizeof p, "%s" SEP "%s", spd[d], spn[k]);
		FILE *t = fopen (p, "rb"); if (t) { fclose (t); strcpy (spr, p); } }
	  const char *dev = p2_none ? NULL : p2_sp ? (spr[0] ? p2_sp : NULL) : spr[0] && !im && !lightpen ? speech_dev (h, sizeof h, path) : NULL;
	  if (spr[0]) { snprintf (e1, sizeof e1, "VT_VECVOICE=%s", spr); vx_putenv (e1); vx_putenv ("VT_SPEECHOUT=1"); }
	  else { vx_putenv ("VT_VECVOICE="); vx_putenv ("VT_SPEECHOUT="); }
	  vx_putenv (dev ? (strcmp (dev, "vecvox") ? "VT_SPEECHDEV=vecvoice" : "VT_SPEECHDEV=vecvox") : "VT_SPEECHDEV=");
	  if (dev) fprintf (stderr, "[vectrex_emu] speech module in port 2: %s\n", dev);
#ifndef VX_EMUVR
	  sp_ok = spr[0] != 0; dk_p2 = lightpen ? 4 : im_out ? 3 : dev ? (strcmp (dev, "vecvox") ? 2 : 1) : 0; dk_dirty_set = 1;
#endif
	}
	/* (1.1: the desktop Vectrex_EMU's default Vectrex unit - vectrex_emu.py UNIT_PRESETS['owner'], the analog parts as
	 *  measured on the owner's console; without them the core ran as an ideal machine: Last Human's calibration then
	 *  needed ZERO REF +36. Read at the first reset) */
	{ const char *e = strrchr (path, '.');
	  unit_addon = e && (!vx_stricmp (e, ".um2") || !vx_stricmp (e, ".img") || !vx_stricmp (e, ".pit"));
	  unit_make (unit_preset (), optv); unit_env (unit_addon);
	  km_on = unit_addon && !opt_is ("vectrex_emu_usb_hid", "off"); km_n = 0; km_mods = 0; km_dirty = km_on; km_btn = -1;
	  vx_putenv (km_on ? "VT_USBHID=1" : "VT_USBHID=");   /* (the desktop's usb_hid: a UVMC2 / PiTrex gets the keyboard and mouse) */
	  if (km_on) { static struct retro_keyboard_callback kc = { km_key }; env_cb (RETRO_ENVIRONMENT_SET_KEYBOARD_CALLBACK, &kc); } }   /* (unit.c: the desktop's Config > Unit / Circuit) */
	/* (1.1, as the desktop's Core: the cartridge hardware - auto by the ROM's size, off a plain 32 KB, or a board; a UVMC2's
	 *  mode; its core 0 on a host thread of its own with 4 or more processors) */
	{ const char *ch = optv ("vectrex_emu_cart_hw"); static char e[64];
	  if (!ch[0] || !strncmp (ch, "auto", 4)) snprintf (e, sizeof e, "VT_CARTHW=1");
	  else if (!strncmp (ch, "off", 3)) snprintf (e, sizeof e, "VT_CARTHW=0");
	  else { int k = 0; while (ch[k] && ch[k] != ' ' && k < 20) k++; snprintf (e, sizeof e, "VT_CARTHW=%.*s", k, ch); }
	  vx_putenv (e); }
	{ const char *um = optv ("vectrex_emu_uvm2_mode"); if (!um[0]) um = "fast";
	  vx_putenv (!strcmp (um, "fast") ? "VT_UVM2_FAST=1" : "VT_UVM2_FAST="); vx_putenv (!strcmp (um, "dev") ? "VT_UVM2_DEV=1" : "VT_UVM2_DEV="); }
	{ const char *x = strrchr (path, '.'); int on = x && !vx_stricmp (x, ".um2") && vx_ncpu () >= 4 && !opt_async;   /* (with Threaded
	                                              emulation the emulation has a thread already: the UVMC2's core 0 lock-step thread
	                                              beside it and the drawing ran slower - the Nova, 2026-11-15: 7.9 ms a refresh without, 8.9 with) */
	  { const char *t = getenv ("VX_UVM2_THREADS"); if (t) on = on && atoi (t) > 0; }   /* (test: the UVMC2's core 0 thread off) */
	  vx_putenv (on ? "VT_THREADS=1" : "VT_THREADS="); }
	/* (1.1: Last Human's save chip - a DS2431 on PB6: its calibration, difficulty and high scores - in RetroArch's saves
	 *  folder; a save next to the ROM (the desktop's) is the start) */
	{ static char ds[1900]; ds[0] = 0; unsigned char h[96] = { 0 };
	  { FILE *c = fopen (path, "rb"); if (c) { if (fread (h, 1, 96, c)) {} fclose (c); } }
	  if (v4e_ds || (!memcmp (h, "g GCE", 5) && memmem_title (h, 96, "LAST") && memmem_title (h, 96, "HUMAN"))) save_path (path, ".ds2431", ds, sizeof ds);
	  { static char e[1950]; snprintf (e, sizeof e, "VT_DS2431=%s", ds); vx_putenv (e); }
	  extern void vt_ds2431_reload (void); vt_ds2431_reload ();
	  if (ds[0]) fprintf (stderr, "[vectrex_emu] save chip %s\n", ds); }
	/* (1.1: the SD card of a PiTrex program (.img / .pit) or a UVMC2 game (.um2) - a FAT image, next to the game or in
	 *  <system>/vectrex_emu: pitrex_sd.img / uvmc2_sd.img, as the desktop Vectrex_EMU builds them from its card folders) */
	{ const char *e = strrchr (path, '.'); int px = e && (!vx_stricmp (e, ".img") || !vx_stricmp (e, ".pit")), um = e && !vx_stricmp (e, ".um2");
	  sd_img[0] = sd_folder[0] = 0;
	  if (px || um) { /* (1.1, as the desktop: the card is a folder - <system>/vectrex_emu/uvmc2 or pitrex - built into an image in
	                     RetroArch's saves folder before the game and what the game wrote copied back after it) */
		char fo[1800]; snprintf (fo, sizeof fo, "%s" SEP "vectrex_emu" SEP "%s", sys ? sys : dir, px ? "pitrex" : "uvmc2"); sdc_st st;
		if (!sdc_stat (fo, &st) && (st.st_mode & S_IFMT) == S_IFDIR) {
			const char *sv = NULL; env_cb (RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY, &sv);
			char cd[1700]; snprintf (cd, sizeof cd, "%s" SEP "vectrex_emu_cards", sv && *sv ? sv : dir); vx_mkdir (cd);
			snprintf (sd_img, sizeof sd_img, "%s" SEP "%s", cd, px ? "pitrex_sd.img" : "uvmc2_sd.img");
			if (sdc_prepare (fo, sd_img)) { static char e[2000]; snprintf (sd_folder, sizeof sd_folder, "%s", fo);
				snprintf (e, sizeof e, "%s=%s", px ? "VT_PITREX_SD" : "VT_UVM2_SD", sd_img); vx_putenv (e); }
			else sd_img[0] = 0; } }
	  if ((px || um) && !sd_folder[0]) { static char sd[1800]; const char *nm = px ? "pitrex_sd.img" : "uvmc2_sd.img"; const char *dd[] = { dir, sys, NULL }; sd[0] = 0;
		for (int d = 0; dd[d] && !sd[0]; d++) { char q[1700]; snprintf (q, sizeof q, d ? "%s" SEP "vectrex_emu" SEP "%s" : "%s" SEP "%s", dd[d], nm);
			FILE *t = fopen (q, "rb"); if (t) { fclose (t); snprintf (sd, sizeof sd, "%s=%s", px ? "VT_PITREX_SD" : "VT_UVM2_SD", q); } }
		if (sd[0]) { vx_putenv (sd); fprintf (stderr, "[vectrex_emu] SD card %s\n", strchr (sd, '=') + 1); }
		else fprintf (stderr, "[vectrex_emu] no SD card (%s in <system>/vectrex_emu)\n", nm); } }
	return im_out;
}

bool retro_load_game_special (unsigned t, const struct retro_game_info *i, size_t n) { (void) t; (void) i; (void) n; return false; }
static long vx_hidden, vx_shown;          /* (test: refreshes run hidden / shown - run-ahead's hidden runs) */
static void pw_stamp_path (char *o, size_t n) { snprintf (o, n, "%s" SEP "vectrex_emu_power_off.txt", tmp_dir ()); }
void retro_unload_game (void)
{
	emu_wait_ext (); emu_primed = 0;
	if (sd_img[0] && sd_folder[0]) sdc_sync_back (sd_img, sd_folder);   /* (1.1: what the game wrote on its card, into the folder) */
	flash_save (1);                             /* (1.1: the flash cartridge's last writes) */
	{ char sp[1100]; pw_stamp_path (sp, sizeof sp); FILE *f = fopen (sp, "w"); if (f) { fprintf (f, "%lld", (long long) time (NULL)); fclose (f); } }
	{ const char *lp = getenv ("VXPOC_RALOG"); FILE *f = lp ? fopen (lp, "a") : NULL;   /* (test: was run-ahead on?) */
	  if (f) { fprintf (f, "shown %ld hidden %ld%c", vx_shown, vx_hidden, 10); fclose (f); } }
	if (pf_on > 0 && pf_n) fprintf (stderr, "[vectrex_emu] per shown refresh (%ld): input %.3f, emulator %.3f, lines %.3f, phosphor + picture %.3f, sending %.3f ms%c",
		pf_n, pf_t[0] / pf_n * 1e3, pf_t[1] / pf_n * 1e3, pf_t[2] / pf_n * 1e3, pf_t[3] / pf_n * 1e3, pf_t[4] / pf_n * 1e3, 10);
	if (pf_on > 0 && pf_n) fprintf (stderr, "[vectrex_emu]   of which: lines + afterglow %.3f, glow blur %.3f, glow added + picture %.3f ms%c", pf_g[0] / pf_n * 1e3, pf_g[1] / pf_n * 1e3, pf_g[2] / pf_n * 1e3, 10);
	if (ov) stbi_image_free (ov); ov = NULL; ov_path[0] = 0; sheet_write ();
}
unsigned retro_get_region (void) { return RETRO_REGION_PAL; }
void *retro_get_memory_data (unsigned id) { (void) id; return NULL; }
size_t retro_get_memory_size (unsigned id) { (void) id; return 0; }
/* (v0.13: the save states are at the end, before a game starts) */
void retro_cheat_reset (void) {}
void retro_cheat_set (unsigned i, bool e, const char *c) { (void) i; (void) e; (void) c; }

/* v0.16 (owner 2026-11-05): the Vectrex USB Controller (Arduino, VID 2341 PID 8037: 2 axes, buttons 1-4) read here
 * directly, Windows' joystick API (winmm), and merged into player 1. EmuVR's RetroArch takes its pads from EmuVR (the VR
 * controllers and XInput pads, sent as commands), so a DirectInput stick never reached the game through RetroArch.
 * The same dead zone as Vectrex_EMU's for it (0.2, the rest of the travel scaled to full). */
#ifdef _WIN32
#pragma comment (lib, "winmm.lib")
typedef struct { unsigned short wMid, wPid; char szPname[32]; unsigned wXmin, wXmax, wYmin, wYmax, wZmin, wZmax, wNumButtons,
	wPeriodMin, wPeriodMax, wRmin, wRmax, wUmin, wUmax, wVmin, wVmax, wCaps, wMaxAxes, wNumAxes, wMaxButtons; char szRegKey[32], szOEMVxD[260]; } vx_joycaps;
typedef struct { unsigned long dwSize, dwFlags, dwXpos, dwYpos, dwZpos, dwRpos, dwUpos, dwVpos, dwButtons, dwButtonNumber, dwPOV, dwReserved1, dwReserved2; } vx_joyinfo;
__declspec(dllimport) unsigned __stdcall joyGetNumDevs (void);
__declspec(dllimport) unsigned __stdcall joyGetDevCapsA (size_t, vx_joycaps *, unsigned);
__declspec(dllimport) unsigned __stdcall joyGetPosEx (unsigned, vx_joyinfo *);
static int usb_id = -2, usb_scan; static vx_joycaps usb_caps;
static double usb_axis (unsigned long v, unsigned lo, unsigned hi)
{
	double t = hi > lo ? ((double) v - lo) / (hi - lo) * 2.0 - 1.0 : 0.0, a = fabs (t);
	if (a <= 0.2) return 0.0; a = (a - 0.2) / 0.8; if (a > 1) a = 1; return t < 0 ? -a : a;
}
/* 1 when the stick is there: *x, *y -128..127 (y up +), *b the buttons' bits */
static int usb_pad (int *x, int *y, unsigned *b)
{
	if (usb_id < 0 && usb_scan-- <= 0) {               /* (looked for at the start and every ~2 s while it is not there) */
		usb_scan = 180; usb_id = -1; unsigned n = joyGetNumDevs ();
		for (unsigned i = 0; i < n && i < 16; i++) { vx_joycaps c; memset (&c, 0, sizeof c);
			if (joyGetDevCapsA (i, &c, sizeof c) != 0) continue;
			vx_joyinfo j; memset (&j, 0, sizeof j); j.dwSize = sizeof j; j.dwFlags = 0xFF;
			if ((c.wMid == 0x2341 && c.wPid == 0x8037) && joyGetPosEx (i, &j) == 0) { usb_id = (int) i; usb_caps = c;
				fprintf (stderr, "[vectrex_emu] Vectrex USB Controller: joystick %u%c", i, 10); break; } }
	}
	if (usb_id < 0) return 0;
	vx_joyinfo j; memset (&j, 0, sizeof j); j.dwSize = sizeof j; j.dwFlags = 0xFF;
	if (joyGetPosEx ((unsigned) usb_id, &j) != 0) { usb_id = -1; usb_scan = 180; return 0; }   /* (unplugged) */
	*x = (int) (usb_axis (j.dwXpos, usb_caps.wXmin, usb_caps.wXmax) * 127.0);
	*y = (int) (-usb_axis (j.dwYpos, usb_caps.wYmin, usb_caps.wYmax) * 127.0);
	*b = (unsigned) (j.dwButtons & 15);
	return 1;
}
#else
static int usb_pad (int *x, int *y, unsigned *b) { (void) x; (void) y; (void) b; return 0; }
#endif

/* the player's controller: stick (analog, else the d-pad) and the four buttons (Y X B A = 1 2 3 4, left to right) */
static void pad (unsigned port, unsigned char *jx, unsigned char *jy, unsigned char *bt)
{
	int x = input_cb (port, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_X) / 256;
	int y = -input_cb (port, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_Y) / 256;
	/* (the right stick: the same - held in tate it is the one under the thumb; the stronger of the two) */
	{ const int rx = input_cb (port, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_X) / 256,
	            ry = -input_cb (port, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_Y) / 256;
	  if (rx * rx + ry * ry > x * x + y * y) { x = rx; y = ry; } }
	if (x > 127) x = 127; if (y > 127) y = 127; if (x < -128) x = -128; if (y < -128) y = -128;
	if (input_cb (port, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT)) x = -127;
	if (input_cb (port, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT)) x = 127;
	if (input_cb (port, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP)) y = 127;
	if (input_cb (port, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN)) y = -127;
	unsigned ub = 0; int ux = 0, uy = 0;
	if (port == 0 && !net_on () && usb_pad (&ux, &uy, &ub)) { if (ux) x = ux; if (uy) y = uy; }   /* (not in netplay: RetroArch's input only) */   /* (the Vectrex USB Controller, player 1) */
	if (opt_rot_stick && opt_rot > 0) {          /* (the picture turned 90 / 180 / 270 counter-clockwise, the device turned
	                                                the other way to hold it upright: the stick's up is then the picture's
	                                                right (90), down (180), left (270)) */
		int gx = x, gy = y;
		if (opt_rot == 1) { gx = y; gy = -x; } else if (opt_rot == 2) { gx = -x; gy = -y; } else if (opt_rot == 3) { gx = -y; gy = x; }
		x = gx < -128 ? -128 : gx > 127 ? 127 : gx; y = gy < -128 ? -128 : gy > 127 ? 127 : gy; }
	*jx = (unsigned char) (x + 128); *jy = (unsigned char) (y + 128);
	/* the buttons 1 2 3 4: left, top, bottom, right of the face buttons as held (Y X B A; the device turned right for a
	 * picture turned 90: B Y A X - the bottom one is then the leftmost) */
	static const unsigned BT[4][4] = { { RETRO_DEVICE_ID_JOYPAD_Y, RETRO_DEVICE_ID_JOYPAD_X, RETRO_DEVICE_ID_JOYPAD_B, RETRO_DEVICE_ID_JOYPAD_A },
		{ RETRO_DEVICE_ID_JOYPAD_B, RETRO_DEVICE_ID_JOYPAD_Y, RETRO_DEVICE_ID_JOYPAD_A, RETRO_DEVICE_ID_JOYPAD_X },
		{ RETRO_DEVICE_ID_JOYPAD_A, RETRO_DEVICE_ID_JOYPAD_B, RETRO_DEVICE_ID_JOYPAD_X, RETRO_DEVICE_ID_JOYPAD_Y },
		{ RETRO_DEVICE_ID_JOYPAD_X, RETRO_DEVICE_ID_JOYPAD_A, RETRO_DEVICE_ID_JOYPAD_Y, RETRO_DEVICE_ID_JOYPAD_B } };
	const unsigned *bm = BT[opt_rot_stick && opt_rot > 0 && opt_rot < 4 ? opt_rot : 0];
	*bt = (input_cb (port, RETRO_DEVICE_JOYPAD, 0, bm[0]) ? 1 : 0) | (input_cb (port, RETRO_DEVICE_JOYPAD, 0, bm[1]) ? 2 : 0)
	    | (input_cb (port, RETRO_DEVICE_JOYPAD, 0, bm[2]) ? 4 : 0) | (input_cb (port, RETRO_DEVICE_JOYPAD, 0, bm[3]) ? 8 : 0) | ub;
}

/* ---- the beam (v0.9: Vectrex_EMU's line renderer, vecrender.py _build_np and CAPSULE_SHADER, on the CPU).
 * Each piece of the beam's path is a capsule: the pixels near it lit by their distance to it (a Gaussian spot), once -
 * not summed along the piece. Where one stroke goes on in the next piece the two are cut along the bisector of their
 * directions, so a joint is lit once (no bright knot at every joint, no dark wedge outside a bend). A piece where the
 * beam stood still (it moved less than REST_SPEED units a cycle: the corners, the dots, the stars) is a round spot,
 * brighter than a line by the time it rested (the corner dots), with a faint halo round a long rest. A spot slowing
 * down inside a stroke (a rest between two vectors of one shape) is found as vecrender.py dwell_rests does and lit the
 * same way. A line is fainter the faster the beam swept it; brightness and width follow the measured tube
 * (TUBE_LIGHT, the cutoff below intensity 63). The picture: the drawn light (the lit layer) plus the measured
 * two-part afterglow of what is no longer lit (PHOS_A) */
#define BEAM_W0 0.30                    /* width (FWHM, game px = 160 units) = W0 + WZ x intensity */
#define BEAM_WZ 0.18
#define BEAM_SPEED 0.35                 /* how much fainter the fastest sweeps get */
#define BEAM_LINE 1.15                  /* a line: BEAM_LINE x (1 - BEAM_SPEED e^(-e / 0.8)), e = cycles per game px */
#define BEAM_FADE 0.3                   /* the beam fading out after /BLANK (intensity bit 7) */
#define BEAM_HALO_W 1.6                 /* a resting spot's halo: FWHM (game px), brightness, cycles for full */
#define BEAM_HALO_K 0.014
#define BEAM_HALO_C 18.0
#define BEAM_DOTC 4.0                   /* a resting spot: BEAM_DOTMAX x min (1, cycles / BEAM_DOTC) */
#define BEAM_DOTMAX 1.6
#define BEAM_DOTW 1.0                   /* its width: the line's x DOTW + DOTWK per cycle (game px) */
#define BEAM_DOTWK 0.002
#define BEAM_BLOOM_E0 20.0              /* a long bright rest blooms: wider (x BLOOM_W) and its halo brighter (x BLOOM_H) */
#define BEAM_BLOOM_E1 38.0
#define BEAM_BLOOM_W 2.0
#define BEAM_BLOOM_H 2.5
#define REST_SPEED 0.5                  /* slower than this (units a cycle): the spot at rest */
#define REST_DWELL_SLOW 0.6             /* the dwell inside a stroke (vecrender.py dwell_rests) */
#define REST_DWELL_D0 3.0
#define REST_DWELL_MAX 30.0
#define REST_DWELL_TAU 12.0
#define REST_DWELL_C 28.0
#define REST_DWELL_SOFT 1.5
#define REST_DWELL_HALO 20.0
#define BEAM_LOWZ 0.5                   /* below this intensity the spot narrows (x LOWW at 0) */
#define BEAM_LOWW 0.45
#define BEAM_Z0DOT (23 / 127.0)         /* a long rest shows from a lower intensity than a line */
#define BEAM_DOTLOWC 40.0
#define BEAM_PEAK 0.62
#define PHOS_GAIN (1.0 / (1.0 - 0.40))
#define GAME_PX 160.0
static const double TUBE_LIGHT[] = {
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0.00125, 0.00249, 0.00374,
	0.00503, 0.00664, 0.00972, 0.01133, 0.01319, 0.01537, 0.01784, 0.02081, 0.02432, 0.02838, 0.03302, 0.03824,
	0.04402, 0.05034, 0.05705, 0.06431, 0.07209, 0.08041, 0.08926, 0.09862, 0.10846, 0.11877, 0.12951, 0.14066,
	0.15217, 0.164, 0.17611, 0.18845, 0.20098, 0.21364, 0.2264, 0.2392, 0.252, 0.26475, 0.27742, 0.28997, 0.30235,
	0.31455, 0.3256, 0.3368, 0.34826, 0.36011, 0.37248, 0.38549, 0.39929, 0.41402, 0.42782, 0.44165, 0.45549,
	0.46933, 0.48316, 0.49698, 0.51077, 0.52451, 0.53821, 0.55184, 0.56541, 0.57891, 0.59232, 0.60563, 0.61885,
	0.63197, 0.64497, 0.65785, 0.67062, 0.68325, 0.69575, 0.70812, 0.72035, 0.73243, 0.74437, 0.75616, 0.7678,
	0.77928, 0.79061, 0.80178, 0.8128, 0.82365, 0.83435, 0.84489, 0.85527, 0.86548, 0.87554, 0.88544, 0.89518,
	0.90476, 0.91418, 0.92345, 0.93256, 0.94151, 0.95032, 0.95897, 0.96747, 0.97582, 0.98403, 0.99208, 1 };
static double zk_tab[128];              /* the tube's light for each intensity, as a display value (^(1 / 2.2)) */

/* the lines being built: the frame's (or two frames' mixed: Smooth motion), with the resting spots and halos added */
typedef struct { double x0, y0, x1, y1, cyc, t; int b; unsigned char rgb; } wline;   /* (b: intensity | 128 fade
                                                                                         | 256 halo | 512 dwell) */
#define NW (MAXL * 3)
static wline W1[NW], W2[NW];
/* a piece in picture pixels: the segment (run on past an open end), the ends of its own box, the cuts, the spot */
typedef struct { float ax, ay, bx, by, x0, y0, x1, y1, n0x, n0y, c0, n1x, n1y, c1, k2, R, r, g, b; int to, cut; } pline;
static pline pl[NW];
#define GTAB 4096
#define GX 4.5                          /* (the spot out to 3 sigma: d^2 / 2 sigma^2 up to 4.5) */
static float gtab[GTAB + 1];

static void splat_band (const pline *q, int ylo, int yhi)
{
	const int ox = q->to * EW;
	const double R = q->R;
	int xa = (int) floor ((q->x0 < q->x1 ? q->x0 : q->x1) - R), xb = (int) ceil ((q->x0 > q->x1 ? q->x0 : q->x1) + R);
	int ya = (int) floor ((q->y0 < q->y1 ? q->y0 : q->y1) - R), yb = (int) ceil ((q->y0 > q->y1 ? q->y0 : q->y1) + R);
	if (ya < ylo) ya = ylo; if (yb > yhi) yb = yhi; if (ya > yb) return;
	if (xa < 0) xa = 0; if (xb > EW - 1) xb = EW - 1; if (xa > xb) return;
	for (int yy = ya; yy <= yb; yy++) { if (ox + xa < cur_l0[yy]) cur_l0[yy] = ox + xa; if (ox + xb > cur_l1[yy]) cur_l1[yy] = ox + xb; }
	const double ax = q->ax, ay = q->ay, dx = q->bx - ax, dy = q->by - ay, L2 = dx * dx + dy * dy, iL2 = L2 > 0 ? 1.0 / L2 : 0;
	const double R2 = R * R, tk = q->k2 * (GTAB / GX);
	const int c0 = q->cut & 1, c1 = q->cut & 2;
	for (int yy = ya; yy <= yb; yy++) {
		const double py = yy + 0.5;
		float *a0 = lacc[0] + (size_t) yy * FW + ox, *a1 = lacc[1] + (size_t) yy * FW + ox, *a2 = lacc[2] + (size_t) yy * FW + ox;
		for (int xx = xa; xx <= xb; xx++) {
			const double px = xx + 0.5;
			double t = ((px - ax) * dx + (py - ay) * dy) * iL2;
			if (t < 0) t = 0; else if (t > 1) t = 1;
			const double ex = px - (ax + dx * t), ey = py - (ay + dy * t), d2 = ex * ex + ey * ey;
			if (d2 >= R2) continue;
			if (c0 && px * q->n0x + py * q->n0y < q->c0) continue;    /* (the joints' cuts) */
			if (c1 && px * q->n1x + py * q->n1y > q->c1) continue;
			const float k = gtab[(int) (d2 * tk)];
			a0[xx] += q->r * k;
			if (NC == 3) { a1[xx] += q->g * k; a2[xx] += q->b * k; }
		}
	}
}

/* vecrender.py dwell_rests: per stroke (joined pieces, one brightness) its line speed; a zone of pieces slower than
 * REST_DWELL_SLOW x that, not at the stroke's ends, that held the spot REST_DWELL_D0 cycles or more longer than the line
 * speed would becomes a resting spot where the spot was slowest (its own resting pieces dropped) */
static int dwell_rests (const wline *L, int n, wline *out)
{
	static double ln[NW], sp[NW]; static unsigned char cont[NW], drop[NW], slow[NW];
	for (int i = 0; i < n; i++) {
		ln[i] = sqrt ((L[i].x1 - L[i].x0) * (L[i].x1 - L[i].x0) + (L[i].y1 - L[i].y0) * (L[i].y1 - L[i].y0));
		sp[i] = L[i].cyc > 0 ? ln[i] / L[i].cyc : 0; drop[i] = 0;
		cont[i] = i > 0 && fabs (L[i].x0 - L[i - 1].x1) <= 1.5 && fabs (L[i].y0 - L[i - 1].y1) <= 1.5
		       && fabs (L[i].t - (L[i - 1].t + L[i - 1].cyc)) <= 1.5 && L[i].b == L[i - 1].b && (L[i].b & 127);
	}
	int nr = 0; static wline R[NW / 4];
	for (int s = 0; s < n; ) {
		int e = s + 1; while (e < n && cont[e]) e++;
		double sl = 0, sc = 0; for (int i = s; i < e; i++) { sl += ln[i]; sc += L[i].cyc; }
		double v1 = sl / (sc > 1e-9 ? sc : 1e-9), fl = 0, fc = 0;
		for (int i = s; i < e; i++) if (sp[i] >= 0.8 * v1) { fl += ln[i]; fc += L[i].cyc; }
		double v = fl / (fc > 1e-9 ? fc : 1e-9);
		for (int i = s; i < e; i++) slow[i] = sp[i] < REST_DWELL_SLOW * v && v > 0;
		for (int i = s; i < e; ) {
			if (!slow[i]) { i++; continue; }
			int j = i; double ex = 0; int pk = i;
			while (j < e && slow[j]) { ex += L[j].cyc - ln[j] / (v > 1e-9 ? v : 1e-9); if (sp[j] < sp[pk]) pk = j; j++; }
			double D = ex - REST_DWELL_D0;
			if (i != s && j != e && D > 0 && nr < NW / 4) {
				for (int k = i; k < j; k++) if (ln[k] <= REST_SPEED * L[k].cyc) drop[k] = 1;
				wline *r = &R[nr++]; *r = L[pk];
				r->x0 = r->x1 = L[pk].x1; r->y0 = r->y1 = L[pk].y1; r->b = L[pk].b | 512;
				r->cyc = REST_DWELL_MAX * (1.0 - exp (-D / REST_DWELL_TAU)); r->t = L[pk].t + L[pk].cyc;
			}
			i = j;
		}
		s = e;
	}
	int m = 0;
	for (int i = 0; i < n; i++) if (!drop[i]) out[m++] = L[i];
	for (int i = 0; i < nr && m < NW; i++) out[m++] = R[i];
	return m;
}

/* ---- the phosphor glow (v0.12, option, off by default): Vectrex_EMU's bloom (vecrender.py GLBloomRenderer._finish).
 * There the picture the phosphor shows (the lit light plus the afterglow, in the White balance's tint) goes to a quarter
 * size image, each quarter pixel a soft 8 x 8 filter of the window's pixels (DOWN_SHADER: 1 3 3 1 per pair each way, so
 * a thin line counts the same wherever it lies), is blurred twice each way with a 9 tap Gaussian (BLUR_SHADER, the
 * default Ultra quality's 2 passes), and is added over the picture, scaled up bilinearly, at the Bloom strength 0.55
 * times the phosphor image's alpha 0.7, in the White balance's hue (the default 12000 K, its gains over their largest).
 * All of it is one soft Gaussian of 9.86 window px (sigma), which on the owner's screen (a picture 1970 px high for the
 * 41000 units of the canvas: his saved window 1609 x 2028) is 1.12 mm on the glass. Here: the same steps at the same
 * size in the core's pixels, a soft filter down to cells of 2 x 2 core pixels per picture scale, a Gaussian there (the
 * rest of the width), bilinear back up, the eyes kept apart; added to the light before the knee to 8 bits */
#define GLOW_K 0.385f                   /* the strength: Bloom 0.55 x the phosphor image's alpha 0.7 */
#define GLOW_WSIG 9.861                 /* the whole glow's sigma, window px (down 3.25 + blur 2 x 2 x 2.854 x 16 + up 2.67 px^2) */
#define GLOW_WPH 1970.0                 /* the canvas's height in the owner's window, px */
static const float GLOW_TINT[3] = { 0.6534f, 0.8297f, 1.2177f };   /* white_gain (12000 K) x it over its largest (WB_CORE:
                                                                      the source's cold () tint, then the bloom's hue) */
static float *glh, *glt, *glu, *glc[3]; static size_t gl_had;
static int GD, GEW, GCW, GCH, GTN; static float gdw[32], gtw[64];
static void glow_blur (float **src)
{
	GD = 2 * S; GEW = EW / GD; GCW = FW / GD; GCH = FH / GD;
	const int NF = 2 * GD, ne = FW / EW;   /* (the down filter: 4 groups of S pixels, 1 3 3 1) */
	double vd = 0;
	for (int i = 0; i < NF; i++) { static const float k4[4] = { 1, 3, 3, 1 }; gdw[i] = k4[i / S] / (8.0f * S);
		double p = i - 2 * S + 0.5; vd += gdw[i] * p * p; }
	{ const double sg = GLOW_WSIG * FH / (0.924 * GLOW_WPH), vu = GD * GD / 6.0, v = sg * sg - vd - vu;
	  const double sc = sqrt (v > 0.0625 * GD * GD ? v : 0.0625 * GD * GD) / GD;
	  GTN = (int) ceil (3.0 * sc); if (GTN > 31) GTN = 31; double t = 0;
	  for (int k = -GTN; k <= GTN; k++) { gtw[k + GTN] = (float) exp (-0.5 * k * k / (sc * sc)); t += gtw[k + GTN]; }
	  for (int k = 0; k <= 2 * GTN; k++) gtw[k] = (float) (gtw[k] / t); }
	const size_t nh = (size_t) FH * GCW, nc = (size_t) GCH * GCW;
	if (nh + 3 * nc != gl_had) {
		free (glh); free (glt); free (glu); for (int c = 0; c < 3; c++) free (glc[c]);
		glh = malloc (nh * 4); glt = malloc (nc * 4); glu = malloc (nc * 4); for (int c = 0; c < 3; c++) glc[c] = malloc (nc * 4);
		gl_had = nh + 3 * nc;
	}
	for (int c = 0; c < NC; c++) {
		const float *a = src[c]; int y;
		#pragma omp parallel for schedule(dynamic, 8)
		for (y = 0; y < FH; y++) for (int e = 0; e < ne; e++) {     /* down, across (each eye on its own) */
			const float *row = a + (size_t) y * FW + e * EW; float *o = glh + (size_t) y * GCW + e * GEW;
			float pad[4 * PW + 16];                                  /* (the row, its ends repeated: S each side) */
			for (int x = 0; x < S; x++) { pad[x] = row[0]; pad[EW + S + x] = row[EW - 1]; }
			memcpy (pad + S, row, (size_t) EW * 4);
			for (int j = 0; j < GEW; j++) { float s = 0; const float *p = pad + j * GD;
				for (int i = 0; i < NF; i++) s += gdw[i] * p[i];
				o[j] = s; } }
		#pragma omp parallel for schedule(dynamic, 4)
		for (y = 0; y < GCH; y++) {                                  /* down, up and down */
			float *o = glt + (size_t) y * GCW; const int y0 = y * GD - S;
			for (int x = 0; x < GCW; x++) o[x] = 0;
			for (int i = 0; i < NF; i++) { int yy = y0 + i; yy = yy < 0 ? 0 : yy > FH - 1 ? FH - 1 : yy;
				const float *r = glh + (size_t) yy * GCW, w = gdw[i]; for (int x = 0; x < GCW; x++) o[x] += w * r[x]; } }
		#pragma omp parallel for schedule(dynamic, 4)
		for (y = 0; y < GCH; y++) for (int e = 0; e < ne; e++) {    /* the Gaussian, across */
			const float *r = glt + (size_t) y * GCW + e * GEW; float *o = glu + (size_t) y * GCW + e * GEW;
			float pad[PW + 80];                                      /* (the row, its ends repeated: GTN each side) */
			for (int x = 0; x < GTN; x++) { pad[x] = r[0]; pad[GEW + GTN + x] = r[GEW - 1]; }
			memcpy (pad + GTN, r, (size_t) GEW * 4);
			for (int x = 0; x < GEW; x++) o[x] = 0;
			for (int k = 0; k <= 2 * GTN; k++) { const float w = gtw[k], *p = pad + k; for (int x = 0; x < GEW; x++) o[x] += w * p[x]; } }
		#pragma omp parallel for schedule(dynamic, 4)
		for (y = 0; y < GCH; y++) {                                  /* and up and down */
			float *o = glc[c] + (size_t) y * GCW;
			for (int x = 0; x < GCW; x++) o[x] = 0;
			for (int k = -GTN; k <= GTN; k++) { int yy = y + k; yy = yy < 0 ? 0 : yy > GCH - 1 ? GCH - 1 : yy;
				const float *r = glu + (size_t) yy * GCW, w = gtw[k + GTN]; for (int x = 0; x < GCW; x++) o[x] += w * r[x]; } }
	}
}

/* ---- the mains hum wobble (vecrender.py HUM_AXES / hum_coeffs, the owner's settings: 0.1074 px rms, wander 2.62):
 * the beam is pushed by the mains hum at the moment each line is drawn - a 50 fps game stands almost still */
static const double HUM[2][3][3] = { { { 1.0, 1, 0.3 }, { 0.55, 2, 1.7 }, { 0.25, 3, 4.1 } },
                                     { { 1.0, 2, 2.2 }, { 0.7, 1, 0.9 }, { 0.3, 3, 3.3 } } };
static double humP[2][4], humQ[2][4], hum_a0;
static void hum_coeffs (double t, int hz)
{
	const double PI2 = 6.283185307179586, strength = 0.1074, speed = 2.62;
	double mc = hz * t + speed * (0.002 * t + 0.15 * sin (PI2 * t / 61.0) + 0.08 * sin (PI2 * t / 23.0 + 1.0) + 0.04 * sin (PI2 * t / 9.7 + 2.0));
	hum_a0 = PI2 * mc;
	double breathe = 0.75 + 0.25 * sin (PI2 * 0.029 * t) * sin (PI2 * 0.047 * t + 1.0);
	for (int ax = 0; ax < 2; ax++) {
		double n2 = 0; for (int j = 0; j < 3; j++) n2 += HUM[ax][j][0] * HUM[ax][j][0];
		double k = 160.0 * strength * breathe / sqrt (n2 / 2.0);
		for (int h = 0; h < 4; h++) humP[ax][h] = humQ[ax][h] = 0;
		for (int j = 0; j < 3; j++) { int h = (int) HUM[ax][j][1]; humP[ax][h] += k * HUM[ax][j][0] * cos (HUM[ax][j][2]); humQ[ax][h] += k * HUM[ax][j][0] * sin (HUM[ax][j][2]); }
	}
}
static void hum_at (double cyc, int hz, double *dx, double *dy)
{
	double a = hum_a0 + cyc * 6.283185307179586 * hz / 1.5e6, s1 = sin (a), c1 = cos (a), s2 = 2 * s1 * c1, c2 = c1 * c1 - s1 * s1;
	double s3 = s2 * c1 + c2 * s1, c3 = c2 * c1 - s2 * s1;
	*dx = humP[0][1] * s1 + humQ[0][1] * c1 + humP[0][2] * s2 + humQ[0][2] * c2 + humP[0][3] * s3 + humQ[0][3] * c3;
	*dy = humP[1][1] * s1 + humQ[1][1] * c1 + humP[1][2] * s2 + humQ[1][2] * c2 + humP[1][3] * s3 + humQ[1][3] * c3;
}

/* ---- the sound through the Vectrex (vectrex_emu.py CrtSound): the buzz - the beam's and deflection's currents leak
 * into the audio amplifier, so it follows what is drawn - then the small built-in speaker, then what is beside it */
typedef struct { double b0, b1, b2, a1, a2, z1, z2; } biquad;
static biquad bq (char kind, double f0, double q, double gain_db)
{
	double w = 6.283185307179586 * f0 / 44100.0, cw = cos (w), sw = sin (w), al = sw / (2 * q), A = pow (10.0, gain_db / 40.0);
	double b[3], a[3];
	if (kind == 'l') { b[0] = (1 - cw) / 2; b[1] = 1 - cw; b[2] = (1 - cw) / 2; a[0] = 1 + al; a[1] = -2 * cw; a[2] = 1 - al; }
	else if (kind == 'h') { b[0] = (1 + cw) / 2; b[1] = -(1 + cw); b[2] = (1 + cw) / 2; a[0] = 1 + al; a[1] = -2 * cw; a[2] = 1 - al; }
	else if (kind == 's') { double sa = 2 * sqrt (A) * al;
		b[0] = A * ((A + 1) - (A - 1) * cw + sa); b[1] = 2 * A * ((A - 1) - (A + 1) * cw); b[2] = A * ((A + 1) - (A - 1) * cw - sa);
		a[0] = (A + 1) + (A - 1) * cw + sa; a[1] = -2 * ((A - 1) + (A + 1) * cw); a[2] = (A + 1) + (A - 1) * cw - sa; }
	else { b[0] = 1 + al * A; b[1] = -2 * cw; b[2] = 1 - al * A; a[0] = 1 + al / A; a[1] = -2 * cw; a[2] = 1 - al / A; }
	biquad f = { b[0] / a[0], b[1] / a[0], b[2] / a[0], a[1] / a[0], a[2] / a[0], 0, 0 }; return f;
}
static double bq_run (biquad *f, int n, double x)
{
	for (int j = 0; j < n; j++) { double y = f[j].b0 * x + f[j].z1; f[j].z1 = f[j].b1 * x - f[j].a1 * y + f[j].z2; f[j].z2 = f[j].b2 * x - f[j].a2 * y; x = y; }
	return x;
}
VXS_ON
static biquad f_buzz[8], f_spk[6], f_voice[2]; static int filters_made;
VXS_OFF
static void make_filters (void)
{
	biquad hp1 = { 0.957, -0.957, 0, -0.957, 0, 0, 0 };   /* (one pole ~300 Hz: a buzz, not a thump) */
	f_buzz[0] = hp1; f_buzz[1] = bq ('h', 1300, 0.7, 0); f_buzz[2] = bq ('h', 1300, 0.7, 0);
	f_buzz[3] = bq ('p', 1450, 4.0, 6.0); f_buzz[4] = bq ('p', 2750, 5.0, 5.0); f_buzz[5] = bq ('p', 3800, 3.0, 7.0);
	f_buzz[6] = bq ('l', 4700, 0.8, 0); f_buzz[7] = bq ('l', 4700, 0.8, 0);
	f_spk[0] = bq ('h', 220, 0.9, 0); f_spk[1] = bq ('h', 175, 0.7, 0); f_spk[2] = bq ('p', 270, 1.3, 2.0);
	f_spk[3] = bq ('p', 900, 1.0, 4.0); f_spk[4] = bq ('l', 6200, 0.7, 0); f_spk[5] = bq ('l', 8000, 0.7, 0);
	f_voice[0] = bq ('h', 40, 0.7, 0); f_voice[1] = bq ('s', 150, 0.7, 6.0);
	filters_made = 1;
}
static double soft (double v)
{
	if (v > 24000) return 24000 + 8767 * tanh ((v - 24000) / 8767);
	if (v < -24000) return -24000 - 8767 * tanh ((-v - 24000) / 8767);
	return v;
}
/* v0.9: the sound of each game frame goes into a queue; each retro_run sends one screen refresh's worth (44100 / the
 * screen rate), so the sound stays even whether a refresh ran no game frame or two */
#define AQ 65536
VXS_ON
static int16_t aq[AQ * 2]; static int aq_r, aq_w; static double aq_frac;
static long aq_frames;   /* (test: game frames run) */
VXS_OFF
static int aq_fill (void) { return (aq_w - aq_r + AQ) % AQ; }
static void audio_frame (void)
{
	extern int vt_speech_sep, vt_vecvoice, vt_uvm2, vt_uvm2_cart, vt_imager; int two = (vt_speech_sep && vt_vecvoice) || vt_uvm2_cart || vt_imager;   /* (v0.28, Vectrex_EMU v1.6.0: a UVMC2 cartridge - its BIOS intro too) */
	static double act[16384 + 1]; const int16_t *s = (const int16_t *) abuf; int n = two ? abuf_n / 4 : abuf_n / 2;
	if (n > 16384) n = 16384;
	if (!filters_made) make_filters ();
	aq_frames++;
	{ static FILE *df; static int dfo; const char *dn = getenv ("VXPOC_ADUMP");   /* (test: each game frame's raw sound and lines) */
	  if (dn && !dfo) { dfo = 1; df = fopen (dn, "wb"); }
	  if (df) { int32_t h[3] = { n, two, nlast }; fwrite (h, 4, 3, df); fwrite (s, two ? 4 : 2, n, df);
		for (int i = 0; i < nlast; i++) { uint32_t r[3] = { last[i].cyc, last[i].t0, last[i].c }; fwrite (r, 4, 3, df); }
		fflush (df); } }
	if (opt_buzz && n) {                    /* the buzz: each line's beam energy spread over the samples it was drawn in */
		const double k = 44100.0 / 1.5e6;
		memset (act, 0, sizeof (double) * (n + 1));
		/* (each line as vectrex_emu.py gets it from the core, live.c: at most 30000 lines, the beam's time 16 bits) */
		for (int i = 0; i < nlast && i < 30000; i++) {
			unsigned cyc = last[i].cyc > 65535 ? 65535 : last[i].cyc, t0 = last[i].t0 & 0xffff;
			double ev = cyc / 8.0, e = ev * ((last[i].c & 127) / 127.0);
			long i0 = (long) (t0 * k), i1 = (long) ((t0 + ev) * k);
			if (i0 >= n) continue; if (i1 > n - 1) i1 = n - 1; if (i1 < i0) i1 = i0;
			double w = e / (i1 - i0 + 1); act[i0] += w; act[i1 + 1] -= w; }
		double run = 0; for (int i = 0; i < n; i++) { run += act[i]; act[i] = run; }
		for (int i = 0; i < n && i < 8; i++) act[i] += 300.0 * (1.0 - i / 8.0);   /* (the frame's start: a sharp pulse) */
	}
	const double buzz_g = 0.0912 * 47.5;
	for (int i = 0; i < n; i++) {
		double a = two ? s[2 * i] : s[i], c = two ? s[2 * i + 1] : 0;
		if (opt_buzz) a += bq_run (f_buzz, 8, act[i] * buzz_g);
		if (opt_spk) { a = bq_run (f_spk, 6, a); a = 32767.0 * tanh (a / 26000.0); }   /* (the driver's limited excursion) */
		if (c != 0 || two) {                /* beside the Vectrex, not through its speaker */
			if (vt_imager) a = soft (a + c * 1.0);                                  /* (the goggles' motor, on the head) */
			else if (vt_uvm2_cart) a = soft (a + c * 2.0);                               /* (the UVMC2's headphone jack, clean) */
			else a = soft (a + bq_run (f_voice, 2, c * 3.0));                       /* (the speech module, full-range speakers) */
		}
		int v = (int) a; int16_t o = (int16_t) (v > 32767 ? 32767 : v < -32768 ? -32768 : v);
		if (aq_fill () < AQ - 1) { aq[2 * aq_w] = aq[2 * aq_w + 1] = o; aq_w = (aq_w + 1) % AQ; } }
}
/* v0.15 switching on and off (see the header): the state - saved with the game (run-ahead) */
VXS_ON
static int pw_on = 1, pw_mode, pw_events, pw_seq_seen = -1, pw_poll_n, pw_nstreak; static double pw_t = 99, pw_fade = 0.3, pw_bulge;
static wline pw_streak[81]; static unsigned pw_rng = 12345;
VXS_OFF
static double pw_gain = 1, pw_sx = 1, pw_width = 1;   /* (this picture's: the light, the size, the spot's width) */
static int pw_zap_done, vx_noaudio;   /* (not saved: the zap drawn for this switch-off, once on a shown refresh; a hidden refresh's sound) */
static const char *pw_file;
static int16_t *pw_snd_on, *pw_snd_off, *pw_play; static int pw_n_on, pw_n_off, pw_n_play, pw_pos;
#define POWER_SND_GAIN 1.6                  /* (vectrex_emu.py: the recordings 60 % louder, the owner 2026-10-08) */
#define CRT_COLLAPSE 0.05
static double pw_rand (void) { pw_rng = pw_rng * 1664525u + 1013904223u; return (pw_rng >> 8) / 16777216.0; }
#include "sound_tab.h"                      /* (1.1: the sounds built in, as the desktop's exe has them) */
static int16_t *pw_wav_mem (const unsigned char *b, long L, int *n);
static int16_t *pw_wav (const char *dir, const char *name, int *n)   /* (16-bit mono; the silence before it skipped) */
{
	char fn[1700]; snprintf (fn, sizeof fn, "%s" SEP "vectrex_emu" SEP "%s", dir, name); *n = 0;
	FILE *f = dir ? fopen (fn, "rb") : NULL;
	if (!f) return !strcmp (name, "on.wav") ? pw_wav_mem (SND_ON, (long) sizeof SND_ON, n) : pw_wav_mem (SND_OFF, (long) sizeof SND_OFF, n);   /* (no file: the built-in one) */
	fseek (f, 0, SEEK_END); long L = ftell (f); fseek (f, 0, SEEK_SET);
	unsigned char *b = malloc (L); if (!b || fread (b, 1, L, f) != (size_t) L) { fclose (f); free (b); return NULL; } fclose (f);
	int16_t *o = pw_wav_mem (b, L, n); free (b); return o;
}
static int16_t *pw_wav_mem (const unsigned char *b, long L, int *n)
{
	long i = 12; int16_t *o = NULL; *n = 0;
	while (i + 8 <= L) { uint32_t cl = b[i + 4] | b[i + 5] << 8 | b[i + 6] << 16 | (uint32_t) b[i + 7] << 24;
		if (!memcmp (b + i, "data", 4)) { long m = (long) (cl > (uint32_t) (L - i - 8) ? L - i - 8 : cl) / 2; const int16_t *d = (const int16_t *) (b + i + 8);
			int pk = 1; for (long k = 0; k < m; k++) { int a = d[k] < 0 ? -d[k] : d[k]; if (a > pk) pk = a; }
			long k0 = 0; while (k0 < m && (d[k0] < 0 ? -d[k0] : d[k0]) <= pk * 0.05) k0++; k0 = k0 > 88 ? k0 - 88 : 0;
			o = malloc ((m - k0) * 2); memcpy (o, d + k0, (m - k0) * 2); *n = (int) (m - k0); break; }
		i += 8 + cl + (cl & 1); }
	return o;
}
static void pw_sound (int on) { pw_play = on ? pw_snd_on : pw_snd_off; pw_n_play = on ? pw_n_on : pw_n_off; pw_pos = 0; }
/* the zap (vectrex_emu.py crt_off): one smooth freehand stroke, 6 in 10 5-8 cm long, 3 in 10 2-5 cm, 1 in 10 a squiggle
 * (a small loop, then a longer curved tail), its bend changing smoothly, kept inside the visible picture */
static void pw_make_streak (void)
{
	const double cm = ALG_MAX_Y / 24.0, PI2 = 2 * M_PI;
	double roll = pw_rand (), length = roll < 0.6 ? (5 + 3 * pw_rand ()) * cm : roll < 0.9 ? (2 + 3 * pw_rand ()) * cm : (4 + 3 * pw_rand ()) * cm;
	int squiggle = roll >= 0.9;
	#define BEND() ((pw_rand () < 0.5 ? -1 : 1) * (0.8 + 2.4 * pw_rand ()))
	double k0 = BEND (), k1 = BEND ();
	double kw = pw_rand () < 0.5 ? 0.0 : 1.5 + 2.5 * pw_rand (), fw = 0.6 + pw_rand (), pwp = 6.3 * pw_rand ();
	double loop = (pw_rand () < 0.5 ? -1 : 1) * (1.4 + 0.5 * pw_rand ()) * PI2;
	#undef BEND
	double x = (0.08 + 0.84 * pw_rand ()) * ALG_MAX_X, y = (0.08 + 0.84 * pw_rand ()) * ALG_MAX_Y, a = PI2 * pw_rand ();
	const int n = 80; double step = length / n, px[81], py[81]; px[0] = x; py[0] = y;
	for (int i = 0; i < n; i++) {
		double u = i / (double) n, da;
		if (squiggle && u < 0.35) da = loop * (u / 0.35) * 2.0 / (0.35 * n);
		else da = (k0 * (1 - u) * (1 - u) + k1 * u * u + kw * sin (PI2 * fw * u + pwp)) / n;
		a += da; x += step * cos (a); y += step * sin (a); px[i + 1] = x; py[i + 1] = y;
	}
	double bx0 = 0.06 * ALG_MAX_X, bx1 = 0.94 * ALG_MAX_X, by0 = 0.05 * ALG_MAX_Y, by1 = 0.95 * ALG_MAX_Y;
	double mnx = 1e18, mxx = -1e18, mny = 1e18, mxy = -1e18;
	for (int i = 0; i <= n; i++) { mnx = fmin (mnx, px[i]); mxx = fmax (mxx, px[i]); mny = fmin (mny, py[i]); mxy = fmax (mxy, py[i]); }
	double k = fmin (1.0, fmin ((bx1 - bx0) / fmax (1.0, mxx - mnx), (by1 - by0) / fmax (1.0, mxy - mny))), cx = (mnx + mxx) / 2, cy = (mny + mxy) / 2;
	for (int i = 0; i <= n; i++) { px[i] = cx + (px[i] - cx) * k; py[i] = cy + (py[i] - cy) * k; }
	mnx = cx + (mnx - cx) * k; mxx = cx + (mxx - cx) * k; mny = cy + (mny - cy) * k; mxy = cy + (mxy - cy) * k;
	double dx = fmax (0.0, bx0 - mnx) - fmax (0.0, mxx - bx1), dy = fmax (0.0, by0 - mny) - fmax (0.0, mxy - by1);
	for (int i = 0; i < n; i++) {           /* (intensity 90, 40 cycles x 8 energy each: vectrex_emu.py's streak pieces) */
		wline *w = &pw_streak[i]; w->x0 = px[i] + dx; w->y0 = py[i] + dy; w->x1 = px[i + 1] + dx; w->y1 = py[i + 1] + dy;
		w->cyc = 40 / 8.0; w->t = i * (40 / 8.0); w->b = 90; w->rgb = 7; }
	pw_nstreak = n;
}
static void audio_out (void)
{
	static int16_t st[2 * 8192];
	aq_frac += 44100.0 / opt_rate; int want = (int) aq_frac; aq_frac -= want;
	if (!pw_on || pw_play) {                  /* (v0.15: switched off - silence, the sound kept even; the power recordings) */
		int fill0 = aq_fill (), n = pw_on ? (fill0 < want ? fill0 : want) : want; if (n > 8192) n = 8192;
		if (!pw_on && want > n) n = want > 8192 ? 8192 : want;
		for (int i = 0; i < n; i++) {
			double v = 0; if (pw_on && aq_r != aq_w) { v = aq[2 * aq_r] * vx_vol; aq_r = (aq_r + 1) % AQ; }
			if (pw_play && pw_pos < pw_n_play && !vx_noaudio) v += pw_play[pw_pos++] * POWER_SND_GAIN;
			int iv = (int) v; st[2 * i] = st[2 * i + 1] = (int16_t) (iv > 32767 ? 32767 : iv < -32768 ? -32768 : iv); }
		if (pw_play && pw_pos >= pw_n_play) pw_play = NULL;
		if (n) audio_cb (st, n);
		return;
	}
	int fill = aq_fill ();
	if (fill > want + 2048) want += (fill - want - 2048) / 32 + 1;   /* (the queue kept short: the sound in step) */
	int n = fill < want ? fill : want; if (n > 8192) n = 8192;
	for (int i = 0; i < n; i++) { st[2 * i] = (int16_t) (aq[2 * aq_r] * vx_vol); st[2 * i + 1] = (int16_t) (aq[2 * aq_r + 1] * vx_vol); aq_r = (aq_r + 1) % AQ; }
	if (n) audio_cb (st, n);
	{ static FILE *lf; static int lo; const char *ln = getenv ("VXPOC_AQLOG");   /* (test: the queue per refresh) */
	  if (ln && !lo) { lo = 1; lf = fopen (ln, "w"); }
	  if (lf) fprintf (lf, "%d %d %d %ld\n", fill, want, n, aq_frames); }
}

/* one pixel of the finished picture: the overlay (painted in, or the gain for EmuVR's sheet), the soft knee, 8 bits */
/* v0.14 the White balance (vecrender.py white_gain, white_core_k, the shaders' cold ()): the beam's light tinted to the
 * colour temperature - the glow's gains, the bright core's (white_core_k) over smoothstep (0.35, 0.9) of the brightest
 * channel; the overlay keeps its colours. 12000 K: the real Vectrex's bluish white (the owner, 2026-10-27) */
static float wb_g[3] = { 1, 1, 1 }, wb_c[3] = { 1, 1, 1 }; static int wb_on;
static void white_gain (double T, float out[3])
{
	double c[3], c0[3];
	for (int pass = 0; pass < 2; pass++) {
		double t = pass ? 6504.0 : T, x, y; t = t < 4000 ? 4000 : t > 25000 ? 25000 : t;
		if (t <= 7000) x = -4.6070e9 / (t * t * t) + 2.9678e6 / (t * t) + 0.09911e3 / t + 0.244063;
		else x = -2.0064e9 / (t * t * t) + 1.9018e6 / (t * t) + 0.24748e3 / t + 0.237040;
		y = -3.0 * x * x + 2.870 * x - 0.275; double X = x / y, Z = (1 - x - y) / y;
		double *d = pass ? c0 : c;
		d[0] = 3.2406 * X - 1.5372 - 0.4986 * Z; d[1] = -0.9689 * X + 1.8758 + 0.0415 * Z; d[2] = 0.0557 * X - 0.2040 + 1.0570 * Z;
	}
	for (int k = 0; k < 3; k++) c[k] /= c0[k];
	double L = 0.2126 * c[0] + 0.7152 * c[1] + 0.0722 * c[2];
	for (int k = 0; k < 3; k++) out[k] = (float) pow (fmax (0.0, c[k] / L), 1 / 2.2);
}
static double white_core_k (double T)
{
	const double MID = 6504.0, CORE = 0.3, CORE_HI = 12000.0, LO = 5600.0, HI = 16000.0, m = 1e6 / T, m0 = 1e6 / MID;
	if (T >= CORE_HI) return T;
	if (m <= m0) { double f = fmin (1.0, (m0 - m) / (m0 - 1e6 / CORE_HI)); if (f <= 1.0 - CORE) return MID;
		double g = (f - (1.0 - CORE)) / CORE; return 1e6 / (m0 - g * (m0 - 1e6 / CORE_HI)); }
	double f = -fmin (1.0, (m - m0) / (1e6 / LO - m0)); if (fabs (f) <= 1.0 - CORE) return MID;
	double g = (fabs (f) - (1.0 - CORE)) / CORE, mm = m0 + g * (1e6 / LO - m0); (void) HI; return 1e6 / mm;
}
static void white_make (void)
{
	white_gain (opt_white, wb_g); white_gain (white_core_k (opt_white), wb_c);
	wb_on = fabs (opt_white - 6504.0) > 1.0;
}
/* v0.35 (owner: "a 3 mm black border round the screen, a little blurred - the edge of the glass's visible area, as on a
 * real tube"): 0 within 3 mm of the glass's outline (a superellipse, Vectrex_EMU's GLASS_N, over the glass window the
 * picture covers), 1 inside, a 1.5 mm soft step - made once per picture size */
#include "glass_cut_lut.h"
static float *emask; static int emask_w, emask_h;
static float edge_mask (int x, int y)
{
	int ex = x % EW;
	if (!emask || emask_w != EW || emask_h != FH) {
		free (emask); emask = malloc (sizeof (float) * EW * FH); emask_w = EW; emask_h = FH;
		const double gA = 145.7 / 2, gB = 188.5 / 2, gN = 7.17, gIN = 3.0, gSOFT = 0.75;
		for (int yy = 0; yy < FH; yy++) for (int xx = 0; xx < EW; xx++) {
			double px = ((xx + 0.5) / EW - 0.5) * 162.0, py = ((yy + 0.5) / FH - 0.5) * 207.0;   /* (the frame: the 162 x 207 mm sheet, the glass in its middle) */
			/* (v0.36: the glass's visible edge where the new bezel walls cut the tube, glass_cut_lut.h - y up there, down here) */
			double deg = atan2 (-py, px) * 57.29578; if (deg < 0) deg += 360; int di = (int) deg; double ft = deg - di;
			double R = glass_cut[di % 360] * (1 - ft) + glass_cut[(di + 1) % 360] * ft, d = R - sqrt (px * px + py * py);
			(void) gA; (void) gB; (void) gN;
			double t = (d - (gIN - gSOFT)) / (2 * gSOFT); t = t < 0 ? 0 : t > 1 ? 1 : t;
			emask[(size_t) yy * EW + xx] = (float) (t * t * (3 - 2 * t));
		}
	}
	return y >= 0 && y < FH ? emask[(size_t) y * EW + ex] : 0;
}
#define OV_LINE_GAIN 1.6f                   /* (the lines under an overlay, as before: x 1.6 - the sheet's gain) */
#define OV_ROOM (0.0648f * 0.0648f / 0.075f)   /* (vecrender.py room_light at the default: the room's light on the filters - an evening room) */
#define OV_PRINT (0.36f * 0.0648f / 0.075f) /* (the print's brightness at that room light: art_print x (room / ART_ROOM_MID)) */
static float *ov_lm; static size_t ov_lm_n; static int ov_gen;   /* (ov_gen: made again - the GPU takes it anew) */
static int *ov_tx, *ov_ty, ov_tk[5];       /* (each picture column's overlay column, each row's first overlay pixel) */
static void ov_tabs (void)
{
	if (ov_tk[0] == FW && ov_tk[1] == FH && ov_tk[2] == ovw && ov_tk[3] == ovh && ov_tk[4] == EW) return;
	free (ov_tx); free (ov_ty); ov_tx = malloc (FW * sizeof (int)); ov_ty = malloc (FH * sizeof (int));
	for (int x = 0; x < FW; x++) ov_tx[x] = (x % EW) * ovw / EW;
	for (int y = 0; y < FH; y++) ov_ty[y] = (y * ovh / FH) * ovw;
	ov_tk[0] = FW; ov_tk[1] = FH; ov_tk[2] = ovw; ov_tk[3] = ovh; ov_tk[4] = EW;
}
static void ov_make (void)
{
	size_t n = (size_t) ovw * ovh;
	if (n != ov_lm_n) { free (ov_lm); ov_lm = malloc (n * 6 * sizeof (float)); ov_lm_n = ov_lm ? n : 0; }
	if (!ov_lm) return;
	for (size_t i = 0; i < n; i++) {
		const unsigned char *o = ov + i * 4; float a = o[3] / 255.0f, c[3] = { o[0] / 255.0f, o[1] / 255.0f, o[2] / 255.0f }, *m = ov_lm + i * 6;
		float filt = a >= 0.004f && a < 0.996f ? 1.0f : 0.0f, opq = a >= 0.996f ? 1.0f : 0.0f;
		float mx = c[0] > c[1] ? (c[0] > c[2] ? c[0] : c[2]) : (c[1] > c[2] ? c[1] : c[2]); if (mx < 0.02f) mx = 0.02f;
		for (int k = 0; k < 3; k++) {
			float hue = c[k] / mx, t = opt_ov_tint * filt; if (t > 1) t = 1;   /* (mix: past 1x the hue itself, deeper) */
			float mm = (1.0f + (hue * 1.15f - 1.0f) * t) * (1.0f - opq);
			if (opt_ov_tint > 1.0f && filt > 0) mm *= powf (hue < 1e-3f ? 1e-3f : hue, opt_ov_tint - 1.0f);
			m[k] = mm * OV_LINE_GAIN; m[3 + k] = c[k] * (filt * OV_ROOM * opt_room * opt_room + opq * OV_PRINT * opt_ov_print * opt_room);   /* (the room light:
			                                         vecrender.py room_light, the filters x its square, the print x it) */
		}
	}
	ov_made = 1; ov_gen++;
}
#if defined(__GNUC__) || defined(__clang__)
#define VX_INLINE static inline __attribute__((always_inline))
#else
#define VX_INLINE static __forceinline
#endif
/* (1.1: always inlined into the drawing loops, no divisions - the Nova's profile, 2026-11-15: 62 % of a refresh was here) */
VX_INLINE uint32_t pixel_out (float r, float g, float b, int x, int y)
{
	if (r <= 0 && g <= 0 && b <= 0 && !paint_all) return 0;   /* (empty: black - unless an overlay is painted in, or the glass grey) */
	if (opt_bright != 1.0f) { r *= opt_bright; g *= opt_bright; b *= opt_bright; }   /* (v0.18: the brightness option, before the knee) */
	if (wb_on) { float m = r > g ? (r > b ? r : b) : (g > b ? g : b), sm = (m - 0.35f) * (1.0f / 0.55f); sm = sm < 0 ? 0 : sm > 1 ? 1 : sm; sm = sm * sm * (3 - 2 * sm);
		r *= wb_g[0] + (wb_c[0] - wb_g[0]) * sm; g *= wb_g[1] + (wb_c[1] - wb_g[1]) * sm; b *= wb_g[2] + (wb_c[2] - wb_g[2]) * sm; }
	if (sheet_file) { }   /* (v0.35: an overlay's x1.6 is the mod's now - on the screen's light, not clipped here) */   /* (a sheet tints them in EmuVR: the same gain; v0.29: in EmuVR
	                                     *  the core never paints the overlay in - owner: colour left on the lines with no overlay on) */
	else if (ov && ov_on) {               /* the overlay (vecrender.py ART_SHADER): the lines through its clear filters in the
	                                         filter's hue (x tint), none through its opaque print; the print and the filters
	                                         lit by the room (the print x its brightness) - per overlay pixel in ov_lm / ov_la */
		if (!ov_made) ov_make ();
		const float *m = ov_lm + ((size_t) ov_ty[y] + ov_tx[x]) * 6;   /* (the overlay's pixel: tables - no divisions per pixel) */
		float lum = (r + g + b) * (1.0f / 3.0f);
		r = lum * m[0]; g = lum * m[1]; b = lum * m[2];   /* (1.1: the lines' light and the lit overlay added as light, not as
		                                         screen values - the trail looks the same at any room light: gamma 2) */
		r = sqrtf (r * r + m[3] * m[3]); g = sqrtf (g * g + m[4] * m[4]); b = sqrtf (b * b + m[5] * m[5]);
	}
	else { r *= OV_LINE_GAIN; g *= OV_LINE_GAIN; b *= OV_LINE_GAIN;
		if (glass_grey > 0) { const float gr = glass_grey * 0.92f, gg = glass_grey * 0.95f, gb = glass_grey;   /* (added as light, as above) */
			r = sqrtf (r * r + gr * gr); g = sqrtf (g * g + gg * gg); b = sqrtf (b * b + gb * gb); } }   /* (the bare glass in the room's
	                                         light: vecrender.py GLASS_GREY, its cold grey) */   /* (no overlay: as bright as through an overlay's
	                                         clear part - the owner on the Nova, 2026-11-15: with the overlay the trail
	                                         was right, without it hardly visible) */
	{ static FILE *hf; static int hn, hon = -1; static unsigned hist[64];   /* (test: the brightness before the knee, VXPOC_RAWHIST) */
	  if (hon < 0) hon = getenv ("VXPOC_RAWHIST") != NULL;
	  if (hon) { float mm = r > g ? (r > b ? r : b) : (g > b ? g : b); if (mm > 0.02f) { int bi = (int) (mm * 16); hist[bi > 63 ? 63 : bi]++; }
		if (++hn == 30000000) { hf = fopen (getenv ("VXPOC_RAWHIST"), "w"); for (int i = 0; i < 64; i++) fprintf (hf, "%.4f %u\n", i / 16.0, hist[i]); fclose (hf); } } }
	/* v0.35 (owner 2026-11-07: "the max 255 only for corners and dots, then down by the beam's speed - details resolve"):
	 * in EmuVR the picture is straight - divided by the dots' level (light_p, 2: the lines are 1.0 - 1.4 before the knee,
	 * the beam at rest 1.6 - 4), only that clipped; the knee squeezed every line into 91 - 98 %. The screen's light
	 * (the mod, x light_g) lifts it all alike. And the glass's edge: 3 mm black round its outline, softened (edge_mask) */
	if (sheet_file) {
		float k = 1.0f / light_p, e = edge_mask (x, y); r *= k * e; g *= k * e; b *= k * e;
		r = r > 1 ? 1 : r; g = g > 1 ? 1 : g; b = b > 1 ? 1 : b;
		int R0 = (int) (r * 255 + 0.5f), G0 = (int) (g * 255 + 0.5f), B0 = (int) (b * 255 + 0.5f);
		return (uint32_t) (R0 << 16 | G0 << 8 | B0);
	}
	/* (a soft knee near white: vecrender clips at 1; the lines ~0.95, the corner dots and crossings white) */
	r = r < 0.75f ? r : 0.75f + 0.25f * (1 - expf (-(r - 0.75f) * 4.0f));
	g = g < 0.75f ? g : 0.75f + 0.25f * (1 - expf (-(g - 0.75f) * 4.0f));
	b = b < 0.75f ? b : 0.75f + 0.25f * (1 - expf (-(b - 0.75f) * 4.0f));
	int R = (int) (r * 255 + 0.5f), G = (int) (g * 255 + 0.5f), B = (int) (b * 255 + 0.5f);
	return (uint32_t) ((R > 255 ? 255 : R) << 16 | (G > 255 ? 255 : G) << 8 | (B > 255 ? 255 : B));
}

/* (1.1, the Nova's profile: the colour of a pixel no light reaches - the overlay lit by the room, or the glass grey - made
 * once for the whole picture whenever it can change, then copied: most of a Vectrex picture is dark) */
static uint32_t *bgimg; static size_t bg_n; static float bg_key[12]; static const void *bg_ov;
static void bg_make (void)
{
	const float k[12] = { (float) FW, (float) FH, (float) EW, (float) (ov && ov_on), opt_room, opt_ov_print, opt_ov_tint, glass_grey, opt_bright,
		(float) opt_white, (float) ov_made, (float) ovw };
	const size_t n = (size_t) FW * FH;
	if (n == bg_n && bg_ov == (const void *) ov && !memcmp (k, bg_key, sizeof k)) return;
	if (n != bg_n) { free (bgimg); bgimg = malloc (n * 4); bg_n = n; }
	memcpy (bg_key, k, sizeof k); bg_ov = ov; int y;
	#pragma omp parallel for schedule(static)
	for (y = 0; y < FH; y++) for (int x = 0; x < FW; x++) bgimg[(size_t) y * FW + x] = pixel_out (0, 0, 0, x, y);
}

/* ---- the game frames, kept for Smooth motion (vectrex_emu.py smooth_frame): each with the time it was complete
 * (emulated seconds), where its lines' times count from, and whether it is the other half of a picture drawn in two
 * halves on alternate frames (vecrender.py frame_alternates) */
#define NH 6
typedef struct { line_t *L; int n; double t; unsigned long long p0; long long *keys; int nk, alt; } gframe;
static gframe hist[NH]; static int nh; static gframe prevf; static int have_prev;
VXS_ON
static double load_f = -1;              /* (brightness bending: the beam's load, smoothed) */
VXS_OFF
static int cmp_ll (const void *a, const void *b) { long long x = *(const long long *) a, y = *(const long long *) b; return x < y ? -1 : x > y; }
static void frame_keys (gframe *f)
{
	f->nk = -1;
	if (f->n == 0 || f->n > 6000) return;
	int k = 0;
	for (int i = 0; i < f->n; i++) { const line_t *l = &f->L[i]; if (!(l->c & 127)) continue;
		long long q0 = (long long) floor (l->x0 / 64.0), q1 = (long long) floor (l->y0 / 64.0), q2 = (long long) floor (l->x1 / 64.0), q3 = (long long) floor (l->y1 / 64.0);
		f->keys[k++] = ((q0 * 1024 + q1) * 1024 + q2) * 1024 + q3; }
	qsort (f->keys, k, sizeof (long long), cmp_ll);
	int u = 0; for (int i = 0; i < k; i++) if (!u || f->keys[i] != f->keys[u - 1]) f->keys[u++] = f->keys[i];
	f->nk = u;
}
static double frame_overlap (const gframe *a, const gframe *b)
{
	if (a->nk <= 0 || b->nk < 0) return -1;
	int hit = 0;
	for (int i = 0; i < a->nk; i++) { int lo = 0, hi = b->nk - 1; long long v = a->keys[i];
		while (lo <= hi) { int m = (lo + hi) / 2; if (b->keys[m] == v) { hit++; break; } if (b->keys[m] < v) lo = m + 1; else hi = m - 1; } }
	return (double) hit / a->nk;
}
static gframe spare;                    /* (the buffers of the frame that drops out, for the next) */
static void frame_pop (void);
static void frame_push (unsigned long long p0)
{
	gframe f;
	if (nh == NH) frame_pop ();
	if (spare.L) { f = spare; spare.L = NULL; }
	else { f.L = malloc (sizeof (line_t) * MAXL); f.keys = malloc (sizeof (long long) * 6000); }
	memcpy (f.L, last, sizeof (line_t) * nlast); f.n = nlast; f.t = total_cycles / 1.5e6; f.p0 = p0;
	frame_keys (&f);
	{ const gframe *f1 = nh ? &hist[nh - 1] : NULL, *f2 = nh > 1 ? &hist[nh - 2] : (nh == 1 && have_prev ? &prevf : NULL);
	  double o1 = f1 ? frame_overlap (&f, f1) : -1, o2 = f2 ? frame_overlap (&f, f2) : -1;
	  f.alt = o1 >= 0 && o2 >= 0 && o1 < 0.5 && o2 > o1 + 0.3; }
	hist[nh++] = f;
	/* the beam's load (brightness bending): the frame's lit cycles, smoothed (vecrender.py load_f) */
	double ld = 0; for (int i = 0; i < nlast; i++) ld += last[i].cyc / 8.0 * ((last[i].c & 127) / 127.0);
	ld /= 30000.0; if (load_f < 0) load_f = ld; load_f += (ld - load_f) * 0.3;
}
static void frame_pop (void)                /* (the oldest frame becomes the one before) */
{
	if (have_prev) { if (spare.L) { free (prevf.L); free (prevf.keys); } else spare = prevf; }
	prevf = hist[0]; have_prev = 1; memmove (hist, hist + 1, sizeof (gframe) * (nh - 1)); nh--;
}
static void pw_switch (int on, double off_for)
{
	if (on == pw_on) return;
	if (on) {                               /* (on: a cold start, the tube warming up) */
		extern void vecx_reset (void);
		pw_on = 1; pw_t = 0; pw_fade = fmax (0.3, fmin (5.0, off_for / 30.0 * 5.0)); pw_bulge = 0.07 * fmax (0.0, 1.0 - off_for / 30.0);
		vecx_reset (); while (nh) frame_pop (); aq_r = aq_w = 0;
	} else {
		pw_on = 0; pw_t = 0; pw_events++; pw_mode = pw_rand () < 0.7 ? 0 : 1;
		{ const char *fm = getenv ("VECTREX_OFF_MODE"); if (fm) pw_mode = !strcmp (fm, "warp"); }   /* (test, as vectrex_emu.py)  */   /* (0 the zap, 1 the collapse) */
		if (pw_mode == 0) pw_make_streak ();
	}
	if (opt_power) pw_sound (on);
}
static void pw_poll (void)                  /* (the front end's power switch: VECTREX_POWER, read every 6 refreshes) */
{
	if (!pw_file || (pw_poll_n++ % 6)) return;
	FILE *f = fopen (pw_file, "r"); int seq, on; if (!f) return;
	if (fscanf (f, "%d %d", &seq, &on) == 2 && seq != pw_seq_seen) { int first = pw_seq_seen < 0; pw_seq_seen = seq; if (!first) pw_switch (on, pw_t); }
	fclose (f);
}

/* VR: the disc shows each eye its pictures in turn (a half turn ~ one frame): each eye keeps the lines of its own
 * last half turn and both are drawn every refresh */
VXS_ON
static line_t eyeb[2][MAXL / 4]; static int neye[2];
VXS_OFF
static void eyes_take (void)
{
	int cnt[2] = { 0, 0 };
	for (int i = 0; i < nlast; i++) cnt[(last[i].rgb & 8) ? 1 : 0]++;
	for (int e = 0; e < 2; e++) if (cnt[e]) {
		neye[e] = 0;
		for (int i = 0; i < nlast && neye[e] < MAXL / 4; i++) if (((last[i].rgb & 8) ? 1 : 0) == e) eyeb[e][neye[e]++] = last[i];
	}
}

static int wl_from (wline *o, int m, const line_t *L, int n, const line_t *B, double alpha)
{
	for (int i = 0; i < n && m < NW; i++) {
		const line_t *l = &L[i]; wline *w = &o[m++];
		w->x0 = l->x0; w->y0 = l->y0; w->x1 = l->x1; w->y1 = l->y1;
		if (B) { w->x0 += (B[i].x0 - l->x0) * alpha; w->y0 += (B[i].y0 - l->y0) * alpha; w->x1 += (B[i].x1 - l->x1) * alpha; w->y1 += (B[i].y1 - l->y1) * alpha; }
		w->cyc = l->cyc / 8.0; w->t = l->t0; w->b = l->c; w->rgb = l->rgb;
	}
	return m;
}

/* the lines of one picture (one eye's) into picture pixels: the resting spots, the halos, the joints, the tube's map,
 * the brightness bending, the hum, and each piece's brightness and width (vecrender.py _build_np) */
/* v0.14 Smooth bends (vecrender.py smooth_bends): where a stroke turns by more than BEND_STEP degrees at a piece's ends,
 * the piece is cut into up to BEND_MAX pieces along the centripetal Catmull-Rom curve through its neighbours' points
 * (no overshoot), the cycles shared evenly; only as finely as the picture can show (BEND_SAG of a picture pixel) */
#define BEND_STEP 3.0
#define BEND_MAX 8
#define BEND_SAG 0.2
static int bend_cont (const wline *a, const wline *b)
{
	return fabs (b->x0 - a->x1) <= 1.5 && fabs (b->y0 - a->y1) <= 1.5 && fabs (b->t - (a->t + a->cyc)) <= 1.5 && b->b == a->b
	    && hypot (a->x1 - a->x0, a->y1 - a->y0) > 0.5 && hypot (b->x1 - b->x0, b->y1 - b->y0) > 0.5;
}
static double bend_turn (const wline *a, const wline *b)   /* (degrees, between a's and b's directions) */
{
	double ax = a->x1 - a->x0, ay = a->y1 - a->y0, bx = b->x1 - b->x0, by = b->y1 - b->y0;
	double c = (ax * bx + ay * by) / (hypot (ax, ay) * hypot (bx, by)); c = c < -1 ? -1 : c > 1 ? 1 : c;
	return acos (c) * 180.0 / M_PI;
}
static void crom (const double P[4][2], double s, double *x, double *y)   /* (the centripetal Catmull-Rom curve at s, 0..1 of P1-P2) */
{
	double d01 = sqrt (fmax (hypot (P[1][0] - P[0][0], P[1][1] - P[0][1]), 1e-3)), d12 = sqrt (fmax (hypot (P[2][0] - P[1][0], P[2][1] - P[1][1]), 1e-3));
	double d23 = sqrt (fmax (hypot (P[3][0] - P[2][0], P[3][1] - P[2][1]), 1e-3));
	double t0 = 0, t1 = d01, t2 = t1 + d12, t3 = t2 + d23, u = t1 + s * d12, o[2];
	for (int c = 0; c < 2; c++) {
		double a0 = P[0][c], a1 = P[1][c], a2 = P[2][c], a3 = P[3][c];
		double A1 = ((t1 - u) * a0 + (u - t0) * a1) / (t1 - t0), A2 = ((t2 - u) * a1 + (u - t1) * a2) / (t2 - t1), A3 = ((t3 - u) * a2 + (u - t2) * a3) / (t3 - t2);
		double B1 = ((t2 - u) * A1 + (u - t0) * A2) / (t2 - t0), B2 = ((t3 - u) * A2 + (u - t1) * A3) / (t3 - t1);
		o[c] = ((t2 - u) * B1 + (u - t1) * B2) / (t2 - t1);
	}
	*x = o[0]; *y = o[1];
}
static int smooth_bends (const wline *L, int n, wline *o)
{
	const double upx = (double) FH / SHEET_H;           /* (picture pixels per unit) */
	int m = 0;
	for (int i = 0; i < n && m < NW; i++) {
		const wline *l = &L[i];
		int cp = i > 0 && bend_cont (&L[i - 1], l), cn = i + 1 < n && bend_cont (l, &L[i + 1]);
		double t0 = cp ? bend_turn (&L[i - 1], l) : 0, t1 = cn ? bend_turn (l, &L[i + 1]) : 0, big = t0 > t1 ? t0 : t1;
		int k = (big > BEND_STEP && big < 120.0) ? (int) fmin (BEND_MAX, ceil (big / BEND_STEP)) : 1;
		if (k > 1) { double ln = hypot (l->x1 - l->x0, l->y1 - l->y0), sag = ln * (fmin (big, 120.0) * M_PI / 180.0) / 8.0 * upx;
			int ks = (int) fmax (1, ceil (sqrt (sag / BEND_SAG))); if (ks < k) k = ks; }
		if (k <= 1 || m + k > NW) { o[m++] = *l; continue; }
		double P[4][2] = { { 0, 0 }, { l->x0, l->y0 }, { l->x1, l->y1 }, { 0, 0 } };
		if (cp && t0 < 120.0) { P[0][0] = L[i - 1].x0; P[0][1] = L[i - 1].y0; } else { P[0][0] = 2 * l->x0 - l->x1; P[0][1] = 2 * l->y0 - l->y1; }
		if (cn && t1 < 120.0) { P[3][0] = L[i + 1].x1; P[3][1] = L[i + 1].y1; } else { P[3][0] = 2 * l->x1 - l->x0; P[3][1] = 2 * l->y1 - l->y0; }
		double ax = l->x0, ay = l->y0;
		for (int j = 0; j < k; j++) {
			wline *q = &o[m++]; *q = *l;
			double bx = l->x1, by = l->y1; if (j < k - 1) crom (P, (j + 1) / (double) k, &bx, &by);
			q->x0 = ax; q->y0 = ay; q->x1 = bx; q->y1 = by; q->cyc = l->cyc / k; q->t = l->t + l->cyc * j / k;
			ax = bx; ay = by;
		}
	}
	return m;
}
/* v0.14: the Tube geometry bends long lines: those longer than TUBE_SEG in pieces, each its share of the cycles and its
 * moment in the frame (vecrender.py _tube_lines); the pieces' ends then go through the map (tube_pt) */
static int tube_split (const wline *L, int n, wline *o)
{
	int m = 0;
	for (int i = 0; i < n && m < NW; i++) {
		const wline *l = &L[i]; double ln = hypot (l->x1 - l->x0, l->y1 - l->y0);
		int k = ln <= TUBE_SEG ? 1 : (int) (ln / TUBE_SEG) + 1;
		if (k == 1 || m + k > NW) { o[m++] = *l; continue; }
		for (int j = 0; j < k; j++) {
			wline *q = &o[m++]; *q = *l; double s0 = j / (double) k, s1 = (j + 1) / (double) k;
			q->x0 = l->x0 + (l->x1 - l->x0) * s0; q->y0 = l->y0 + (l->y1 - l->y0) * s0;
			q->x1 = j == k - 1 ? l->x1 : l->x0 + (l->x1 - l->x0) * s1; q->y1 = j == k - 1 ? l->y1 : l->y0 + (l->y1 - l->y0) * s1;
			q->cyc = l->cyc / k; q->t = l->t + l->cyc * j / k;
		}
	}
	return m;
}
#define ART_PARALLAX 0.008
static double par_dy (void)                  /* (vecrender.py par_dy: the overlay's parallax, emulator units - up) */
{ return opt_par && ov && ov_on && !sheet_file ? -ART_PARALLAX * 0.924 * ALG_MAX_Y : 0.0; }
static int vx_nobuild;                     /* (v0.13: a hidden refresh - no picture, the lines not built) */
static int build_lines (wline *L, int n, int to, double esh, int npl)
{
	extern int vt_imager;
	if (vx_nobuild) return npl;
	if (up_xf) for (int i = 0; i < n; i++) { picture_apply (&L[i].x0, &L[i].y0); picture_apply (&L[i].x1, &L[i].y1); }   /* (unit.c: the
	                                                    picture's pots and the yoke, after the integrators) */
	if (opt_dwell && n > 1) n = dwell_rests (L, n, W2), memcpy (L, W2, sizeof (wline) * n);
	if (opt_sbend && n > 1) n = smooth_bends (L, n, W2), memcpy (L, W2, sizeof (wline) * n);
	if (opt_geom && n) n = tube_split (L, n, W2), memcpy (L, W2, sizeof (wline) * n);
	/* the halos (corner glow) round the long rests: drawn as pieces of their own */
	static unsigned char zl[NW], jo[NW], halo[NW];
	int nhalo = 0;
	if (opt_halo) {
		for (int i = 0; i < n; i++) {
			const wline *l = &L[i]; double ln = sqrt ((l->x1 - l->x0) * (l->x1 - l->x0) + (l->y1 - l->y0) * (l->y1 - l->y0));
			if (ln <= REST_SPEED * l->cyc && l->cyc >= 8.0 && (l->b & 127) && !((l->b & 512) && l->cyc < REST_DWELL_HALO) && n + nhalo < NW) {
				W2[nhalo] = *l; W2[nhalo].b = (l->b & 127) | 256; nhalo++; }
		}
		if (nhalo) { memmove (L + nhalo, L, sizeof (wline) * (n < NW - nhalo ? n : NW - nhalo)); memcpy (L, W2, sizeof (wline) * nhalo); n += nhalo; if (n > NW) n = NW; }
	}
	for (int i = 0; i < n; i++) {
		const wline *l = &L[i];
		zl[i] = sqrt ((l->x1 - l->x0) * (l->x1 - l->x0) + (l->y1 - l->y0) * (l->y1 - l->y0)) <= REST_SPEED * l->cyc;
		halo[i] = (l->b & 256) != 0; jo[i] = 0;
	}
	for (int i = 1; i < n; i++)                   /* one stroke in several pieces: the joined ends open */
		if (fabs (L[i].x0 - L[i - 1].x1) <= 1.5 && fabs (L[i].y0 - L[i - 1].y1) <= 1.5 && fabs (L[i].t - (L[i - 1].t + L[i - 1].cyc)) <= 1.5
		    && !halo[i] && !halo[i - 1]) { if (!zl[i]) jo[i] |= 1; if (!zl[i - 1]) jo[i - 1] |= 2; }
	const double sx = EW / SHEET_W, sy = FH / SHEET_H, gp = GAME_PX * sy, bk = opt_sigma / 0.689;   /* (px per game px;
	                                                    the beam width option: "thin" = 0.55 px at 1x for intensity 127) */
	const double cxm = ALG_MAX_X / 2.0, cym = ALG_MAX_Y / 2.0;
	const double par = par_dy ();
	const double gs = (opt_bend && load_f > 0) ? 1.0 + 0.6 * load_f * 10.0 / 128.0 : 1.0;   /* (bending: grow) */
	const int n0 = npl;
	for (int i = 0; i < n && npl < NW; i++) {
		const wline *l = &L[i];
		int z = l->b & 127; const int web = (l->b & 255) == 128;   /* (the blanked beam: drawn while the knob is up) */
		if (!z && !(web && up_kb > 0)) continue;         /* (intensity 0: dark) */
		double x0, y0, x1, y1; tube_pt (l->x0, l->y0, &x0, &y0); tube_pt (l->x1, l->y1, &x1, &y1);
		if (zl[i]) { x0 = x1; y0 = y1; }                /* (a resting spot: where the spot came to rest) */
		x0 = cxm + (x0 - cxm) * gs; x1 = cxm + (x1 - cxm) * gs; y0 = cym + (y0 - cym) * gs; y1 = cym + (y1 - cym) * gs;
		if (pw_sx != 1.0) { x0 = cxm + (x0 - cxm) * pw_sx; x1 = cxm + (x1 - cxm) * pw_sx; y0 = cym + (y0 - cym) * pw_sx; y1 = cym + (y1 - cym) * pw_sx; }
		if (opt_wob) { double ax, ay, bx, by; hum_at (l->t, opt_wob, &ax, &ay); hum_at (l->t + l->cyc, opt_wob, &bx, &by);
			if (zl[i]) { ax = bx; ay = by; } x0 += ax; y0 += ay; x1 += bx; y1 += by; }
		x0 = (x0 - SHEET_X0) * sx + esh; x1 = (x1 - SHEET_X0) * sx + esh; y0 = (y0 - SHEET_Y0 + par) * sy; y1 = (y1 - SHEET_Y0 + par) * sy;
		/* the brightness and width (game px, FWHM) */
		const double zi = z / 127.0 + up_kb, cyc = l->cyc;   /* (the drive: the intensity + the brightness knob's bias) */
		if (zi <= 0) continue;
		double zk = zk_tab[z];
		if (up_kb != 0) { double d = zi * 127.0; d = d > 127 ? 127 : d; const int ii = d >= 126 ? 126 : (int) d;   /* (vecrender.py tube_light) */
			zk = pow (TUBE_LIGHT[ii] + (TUBE_LIGHT[ii + 1] - TUBE_LIGHT[ii]) * (d - ii), 1 / 2.2); }
		const double zw = zi >= BEAM_LOWZ ? 1.0 : BEAM_LOWW + (1.0 - BEAM_LOWW) * zi / BEAM_LOWZ;
		double fw = (BEAM_W0 + BEAM_WZ * zi) * zw * pw_width * up_width, kk; int dot = 0;   /* (up_width: R525's stiffer beam) */
		const double lp = sqrt ((x1 - x0) * (x1 - x0) + (y1 - y0) * (y1 - y0));
		if (halo[i]) {
			double bl = (cyc * zk - BEAM_BLOOM_E0) / (BEAM_BLOOM_E1 - BEAM_BLOOM_E0); bl = bl < 0 ? 0 : bl > 1 ? 1 : bl;
			fw = BEAM_HALO_W * zw * (1.0 + (BEAM_BLOOM_W - 1.0) * bl) / bk * pw_width;   /* (the glass's scatter: not the beam option's) */
			kk = BEAM_HALO_K * (cyc / BEAM_HALO_C < 1 ? cyc / BEAM_HALO_C : 1) * (1.0 + (BEAM_BLOOM_H - 1.0) * bl); dot = 1;
		} else if (!zl[i]) {
			double ee = lp > 1e-9 ? cyc / (lp / gp) : 1e9;
			kk = opt_speedb ? BEAM_LINE * (1.0 - BEAM_SPEED * exp (-ee / 0.8)) : BEAM_LINE * (1.0 - 0.5 * BEAM_SPEED);
		} else if (!opt_dots) {                   /* (corner dots off: the resting spot as faint as a line's) */
			kk = BEAM_LINE * (1.0 - BEAM_SPEED);
		} else {
			kk = BEAM_DOTMAX * (cyc / BEAM_DOTC < 1 ? cyc / BEAM_DOTC : 1); dot = 1;
			if (zi < BEAM_LOWZ) { double rd = (zi - BEAM_Z0DOT) / (BEAM_LOWZ - BEAM_Z0DOT); rd = rd < 0 ? 0 : rd;
				double zd = (0.55 + 0.45 * BEAM_LOWZ) * rd * (cyc / BEAM_DOTLOWC < 1 ? cyc / BEAM_DOTLOWC : 1); if (zd > zk) zk = zd; }
			double fwr = fw * BEAM_DOTW + BEAM_DOTWK * (cyc < 80 ? cyc : 80);
			double bl = (cyc * zk - BEAM_BLOOM_E0) / (BEAM_BLOOM_E1 - BEAM_BLOOM_E0); bl = bl < 0 ? 0 : bl > 1 ? 1 : bl;
			fw = fwr * (1.0 + (BEAM_BLOOM_W - 1.0) * bl);
			if (l->b & 512) { double wd = cyc / REST_DWELL_C < 1 ? cyc / REST_DWELL_C : 1;   /* (a dwell: soft while short) */
				kk *= pow (wd, 2.5); fw *= 1.0 + REST_DWELL_SOFT * (1.0 - wd); }
		}
		kk *= zk * BEAM_PEAK * PHOS_GAIN * pw_gain * up_light;   /* (up_light: the high voltage's light, unit.c) */
		if ((l->b & 128) && !web) kk *= BEAM_FADE;
		double sg = fw * gp / 2.355 * bk, sw = sg < 0.5 ? 0.5 : sg;   /* (at least half a pixel: a thinner spot as wide, its light kept) */
		kk *= dot ? (sg / sw) * (sg / sw) : sg / sw;
		if (kk < 1e-4) continue;
		float r = (float) kk, g = r, b = r;
		if (vt_imager && opt_imager) {          /* (the filter in front of the eye: its colour) */
			r = (l->rgb & 1) ? r : 0; g = (l->rgb & 2) ? g : 0; b = (l->rgb & 4) ? b : 0; if (!(l->rgb & 7)) r = g = b = 0; }
		pline *q = &pl[npl++];
		q->to = to; q->x0 = (float) x0; q->y0 = (float) y0; q->x1 = (float) x1; q->y1 = (float) y1;
		q->ax = (float) x0; q->ay = (float) y0; q->bx = (float) x1; q->by = (float) y1;
		q->k2 = (float) (1.0 / (2.0 * sw * sw)); q->R = (float) (sw * 3.0); q->r = r; q->g = g; q->b = b;
		q->cut = (!dot && lp > 1e-3) ? jo[i] : 0;          /* (open ends: cut below, when the neighbour is open too) */
		q->n0x = q->n0y = q->n1x = q->n1y = 0; q->c0 = q->c1 = 0;
	}
	/* the joints' cuts: the bisector of the two pieces' directions (a turn sharper than ~120 degrees: round ends) */
	for (int k = n0 + 1; k < npl; k++) {
		pline *p = &pl[k - 1], *q = &pl[k];
		if (!(p->cut & 2) || !(q->cut & 1)) { continue; }
		double pdx = p->bx - p->ax, pdy = p->by - p->ay, pl_ = sqrt (pdx * pdx + pdy * pdy);
		double qdx = q->bx - q->ax, qdy = q->by - q->ay, ql_ = sqrt (qdx * qdx + qdy * qdy);
		double ux = pdx / pl_ + qdx / ql_, uy = pdy / pl_ + qdy / ql_, ul = sqrt (ux * ux + uy * uy);
		if (ul <= 1.0) { p->cut &= ~2; q->cut &= ~1; continue; }
		ux /= ul; uy /= ul;
		p->n1x = (float) ux; p->n1y = (float) uy; p->c1 = (float) (ux * p->bx + uy * p->by);
		q->n0x = (float) ux; q->n0y = (float) uy; q->c0 = (float) (ux * q->ax + uy * q->ay);
		p->cut |= 4; q->cut |= 8;                       /* (marks: this end really cut) */
	}
	for (int k = n0; k < npl; k++) {                    /* (an open end without a cut: round; a cut end: the segment
	                                                        runs on past it, the cut ends it) */
		pline *q = &pl[k]; int c = 0;
		double dx = q->bx - q->ax, dy = q->by - q->ay, L = sqrt (dx * dx + dy * dy);
		if ((q->cut & 1) && (q->cut & 8)) { c |= 1; if (L > 0) { q->ax -= (float) (dx / L * 4 * q->R); q->ay -= (float) (dy / L * 4 * q->R); } }
		if ((q->cut & 2) && (q->cut & 4)) { c |= 2; if (L > 0) { q->bx += (float) (dx / L * 4 * q->R); q->by += (float) (dy / L * 4 * q->R); } }
		q->cut = c;
	}
	return npl;
}

/* ---- the screen rate and Smooth motion. Each retro_run is one refresh of the screen (the rate option): it runs the
 * emulator on until the emulated time has caught up with it (no game frame, one, or two), then draws a picture of its
 * own. Smooth motion (vectrex_emu.py smooth_frame): the picture of the moment one game frame and one refresh back,
 * the lines between the two game frames around it, where the two match line for line (as many, the same intensities,
 * none moved more than SMOOTH_MATCH units); else the earlier frame as it is. The phosphor (vecrender.py PHOS_A, the
 * afterglow measured on the owner's Vectrex, at the owner's time scale 0.6): what was lit and is not lit again starts
 * to glow away, in two parts (14 % fading with 27 ms, 5.8 % with 210 ms) */
#define SMOOTH_MATCH 2500
VXS_ON
static unsigned long long run_t; static double run_frac;   /* (v0.13: saved) */
VXS_OFF
static long sm_runs, sm_mixed;   /* (test: refreshes, those between two frames) */
           /* (the emulated time this refresh has to reach, cycles) */
static float *pb[2][3], *g1b[3], *g2b[3]; static int pbi;

/* the picture's buffers at its size (a game's start; a new size, area or rotation): the lit light now and a refresh ago,
 * the afterglow's two parts, one colour channel or three - all dark again */
static void pic_setup (void)
{
	const int glass = area_glass (); geom_calc (glass, &EW, &FH, &CW, &CH);
	S = (int) (FH / (double) PH + 0.5); if (S < 1) S = 1;   /* (the glow's cells: 2 x 2 per this many) */
	FW = stereo ? EW * 2 : EW; CX0 = (EW - CW) / 2; CY0 = (FH - CH) / 2; OW = stereo ? CW * 2 : CW; OH = CH;
	geo_glass = glass; geo_rot = opt_rot; geo_size = opt_size; geo_deck = opt_deck; geo_shape = opt_shape;
	/* (the control deck: the picture and the deck one block, black bars to the screen's shape - deck.c) */
	dk_on = (opt_deck == 1 || opt_deck == 2) && !stereo; dk_pop = opt_deck == 3 && !stereo; TW = OW; TH = OH; BX = BY = 0; DKH = 0;
	if (dk_on || dk_pop) DKH = (int) ((opt_deck == 2 ? 0.046 : 0.08134) * FH / 0.924 + 0.5);   /* (slim: one low row) */
	dk_y = dk_pop ? CH - DKH : CH; dk_dirty_set = 1;
	if (dk_on) {
		const double A = opt_shape ? 16.0 / 9.0 : 4.0 / 3.0, ab = (opt_rot & 1) ? 1.0 / A : A; const int bw = CW, bh = CH + DKH;
		TW = (int) (bh * ab + 0.5); if (TW < bw) TW = bw; TH = (int) (bw / ab + 0.5); if (TH < bh) TH = bh;
		BX = (TW - bw) / 2; BY = (TH - bh) / 2; OW = TW; OH = TH; dk_dirty_set = 1; }
	if ((size_t) OW * OH > rb_n) { free (rb); rb_n = (size_t) OW * OH; rb = malloc (rb_n * 4); }   /* (the turned picture) */
	{ static size_t cbn; extern uint32_t *dk_cb; if ((size_t) OW * OH > cbn) { free (dk_cb); cbn = (size_t) OW * OH; dk_cb = malloc (cbn * 4); } }   /* (the deck's block) */
	/* (the buffers at this picture's size: the lit light now and a refresh ago, the afterglow's two parts, one colour
	 *  channel or three) */
	{ extern int vt_imager; NC = vt_imager ? 3 : 1; }
	{ const size_t NP = (size_t) FW * FH; static size_t had; static int hadc;
	  if (NP != had || NC != hadc) {
		free (ob); free (fb);
		for (int c = 0; c < 3; c++) { if (c < hadc) { free (pb[0][c]); free (pb[1][c]); free (g1b[c]); free (g2b[c]); free (shown[c]); } }
		ob = malloc (NP * 4); fb = malloc (NP * 4);
		for (int c = 0; c < 3; c++) {
			if (c < NC) { pb[0][c] = calloc (NP, 4); pb[1][c] = calloc (NP, 4); g1b[c] = calloc (NP, 4); g2b[c] = calloc (NP, 4); shown[c] = calloc (NP, 4); }
			else { pb[0][c] = pb[0][0]; pb[1][c] = pb[1][0]; g1b[c] = g1b[0]; g2b[c] = g2b[0]; shown[c] = shown[0]; } }
		had = NP; hadc = NC;
		for (int k = 0; k < 2; k++) { free (sp_l0[k]); free (sp_l1[k]); sp_l0[k] = malloc (FH * sizeof (int)); sp_l1[k] = malloc (FH * sizeof (int)); }
		free (sp_g0); free (sp_g1); free (sp_f0); free (sp_f1);
		sp_g0 = malloc (FH * sizeof (int)); sp_g1 = malloc (FH * sizeof (int)); sp_f0 = malloc (FH * sizeof (int)); sp_f1 = malloc (FH * sizeof (int));
		spans_reset (FH, 1);
	  } else { for (int c = 0; c < NC; c++) { memset (pb[0][c], 0, NP * 4); memset (pb[1][c], 0, NP * 4); memset (g1b[c], 0, NP * 4); memset (g2b[c], 0, NP * 4); }
	           spans_reset (FH, 1); } }
}
#ifndef VX_EMUVR
#include "gpu_gl.c"
#include "gpu_phos.c"
/* the profile's choices (Ultra, High, Medium, Low, Basic) for the options left on auto */
static const prow FX_PROFILES[5] = {   /* (vecrender.py PROFILE_OFF: Ultra, High, Medium, Low, Basic) */
	{ 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 1.0f }, { 1, 1, 1, 0, 0, 0, 1, 1, 1, 1, 1, 1, 0, 0, 1.0f },
	{ 1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 1, 0, 0, 0, 0.75f }, { 1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 1, 0, 1, 0, 0.5f },
	{ 1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 1, 0, 1, 1, 0.5f } };
static prow opt_row; static int opt_row_ok; static const prow *fx_force;   /* (Optimize's result: gpu_opt.c; a measurement's row) */
static void fx_apply (void)
{
	/* the GPU: vecrender.py PROFILE_OFF, switch for switch - Ultra all; High without the room's reflections (on the glass,
	 * the overlay, the glass's grey) and with one glow pass; Medium also without the tube's curve, the glass edge, the
	 * overlay's scratches and plastic glow, the case's reflections and the dither, the phosphor at 0.75 x; Low the
	 * phosphor at half size and 30 pictures a second; Basic the lines at the phosphor's size too */
	/* the CPU alone (no GPU): its own costs - the glow, the bends' smoothing, smooth motion (Medium by default) */
	static const unsigned char T[5][3] = { { 1, 1, 1 }, { 1, 1, 1 }, { 1, 1, 1 }, { 0, 1, 1 }, { 0, 0, 0 } };
	const int g = gpu_ok && !stereo, gl = g && ph_ok;
	const int pr = opt_profile ? opt_profile - 1 : gpu_ok ? 0 : 2;
	const unsigned char *t = T[pr];
	const prow *const R = fx_force ? fx_force : !opt_profile && gl && opt_row_ok ? &opt_row : &FX_PROFILES[pr];
	#define PICK(raw, v) ((raw) >= 0 ? (raw) : (v))
	opt_glow = PICK (raw_glow, gl ? 1 : t[0]); opt_sbend = PICK (raw_sbend, gl ? R->sbend : t[1]); opt_smooth = PICK (raw_smooth, gl ? 1 : t[2]);
	opt_geom = PICK (raw_geom, R->curve); opt_par = PICK (raw_par, R->par);
	fx_passes = PICK (raw_passes, R->passes >= 2) ? 2 : 1; fx_dith = gl && PICK (raw_dith, R->dith);
	fx_ps = gl ? R->ps : 1.0f; fx_half = gl && R->half; fx_lh = gl && R->lh;
	gfx.glass = g && PICK (raw_glass, R->glass); gfx.ceiling = g && (raw_ceiling >= 0 ? raw_ceiling : R->light);
	gfx.cse = g && PICK (raw_case, 1);
	const int groom = PICK (raw_groom, R->groom);
	float ll = opt_room / 2.0f; ll = ll > 1 ? 1 : ll; const float ll3 = ll * ll * ll;   /* (vecrender.py light_level) */
	gfx.lk = gfx.ceiling ? ll3 * (k_ceiling > 0 ? k_ceiling : 1.0f) : 0.0f;
	gfx.grey = g && groom ? glass_grey_v : 0.0f; gfx.croom = groom ? ll3 : 0.0f;
	gfx.crefl = PICK (raw_crefl, R->crefl) ? (k_crefl > 0 ? k_crefl : 1.0f) : 0.0f;
	gfx.cwear = PICK (raw_cwear, 1) ? (k_cwear > 0 ? k_cwear : 1.0f) : 0.0f;
	glass_grey = !g && groom ? glass_grey_v : 0.0f;              /* (the GPU draws the glass's grey itself) */
	gfx.fx = g && !(ov && ov_on && !sheet_file) && (gfx.glass || gfx.cse || gfx.lk > 0 || gfx.grey > 0);
	ovfx.ovl = gl && (k_ovl >= 0 ? k_ovl > 0 : R->lov) ? ll3 * (k_ovl > 0 ? k_ovl : 1.0f) : 0.0f;
	ovfx.glow = gl && (k_ovglow >= 0 ? k_ovglow > 0 : R->aglow) ? (k_ovglow > 0 ? k_ovglow : 1.0f) : 0.0f;
	ovfx.scr = gl && PICK (raw_scr, R->scr);
	#undef PICK
}
#include "gpu_opt.c"
int gpu_frame_shown (void) { return gpu_ok && !stereo && !(ov && ov_on && !sheet_file) && (gfx.cse || gfx.glass); }
#else
int gpu_frame_shown (void) { return 0; }
#endif
/* (1.1) the emulation on a thread of its own (UVMC2 / PiTrex games - the option Threaded emulation): retro_run waits
 * for the refresh it shows, builds its lines, sends its sound, starts the next refresh's emulation and draws while that
 * runs. The picture is one refresh behind the input (17 ms); what reads or changes the emulator waits first (emu_wait) */
static volatile int emu_busy;
static void emu_loop (void);
/* (one thread kept for the whole game: a new one each refresh started on a little core every time) */
#ifdef _WIN32
static HANDLE emu_th, emu_go, emu_done;
static DWORD WINAPI emu_main (LPVOID p)
{
	(void) p;
	for (;;) { WaitForSingleObject (emu_go, INFINITE); emu_loop (); SetEvent (emu_done); }
	return 0;
}
static int emu_make (void)
{
	if (emu_th) return 1;
	emu_go = CreateEvent (NULL, FALSE, FALSE, NULL); emu_done = CreateEvent (NULL, FALSE, FALSE, NULL);
	emu_th = emu_go && emu_done ? CreateThread (NULL, 0, emu_main, NULL, 0, NULL) : NULL;
	return emu_th != NULL;
}
static void emu_kick (void) { SetEvent (emu_go); }
static void emu_join (void) { WaitForSingleObject (emu_done, INFINITE); }
#else
#include <pthread.h>
static pthread_t emu_th; static int emu_made, emu_req, emu_fin;
static pthread_mutex_t emu_mx = PTHREAD_MUTEX_INITIALIZER; static pthread_cond_t emu_cv = PTHREAD_COND_INITIALIZER;
static void *emu_main (void *p)
{
	(void) p;
	for (;;) {
		pthread_mutex_lock (&emu_mx); while (!emu_req) pthread_cond_wait (&emu_cv, &emu_mx); emu_req = 0; pthread_mutex_unlock (&emu_mx);
		emu_loop ();
		pthread_mutex_lock (&emu_mx); emu_fin = 1; pthread_cond_broadcast (&emu_cv); pthread_mutex_unlock (&emu_mx);
	}
	return NULL;
}
static int emu_make (void)
{
	if (emu_made) return 1;
	if (pthread_create (&emu_th, NULL, emu_main, NULL)) return 0;
	pthread_detach (emu_th); emu_made = 1; return 1;
}
static void emu_kick (void) { pthread_mutex_lock (&emu_mx); emu_fin = 0; emu_req = 1; pthread_cond_broadcast (&emu_cv); pthread_mutex_unlock (&emu_mx); }
static void emu_join (void) { pthread_mutex_lock (&emu_mx); while (!emu_fin) pthread_cond_wait (&emu_cv, &emu_mx); pthread_mutex_unlock (&emu_mx); }
#endif
static void run_t_next (void)               /* (the emulated time this refresh has to reach: one refresh on) */
{
	if (!run_t) run_t = total_cycles;
	run_t += (unsigned long long) (1.5e6 / opt_rate + 0.5);
	run_frac += 1.5e6 / opt_rate - (double) (unsigned long long) (1.5e6 / opt_rate + 0.5);
	if (run_frac >= 1) { run_t++; run_frac -= 1; } else if (run_frac <= -1) { run_t--; run_frac += 1; }
	if (!pw_on) run_t = total_cycles;
}
static void emu_loop (void)                 /* (game frames until the emulated time has reached run_t) */
{
	extern int vt_imager;
	for (int k = 0; total_cycles < run_t && k < 8; k++) {
		extern unsigned long long vt_piece_t0;
		unsigned long long p0 = vt_piece_t0;   /* (the frame being drawn began here: its lines' times count from it) */
		abuf_n = 0;
		run_frame ();
		frame_push (p0);
		if (vt_imager) eyes_take ();
		audio_frame ();
	}
	if (total_cycles + 3000000 < run_t) run_t = total_cycles;   /* (far behind - a stall: on from here) */
}
static void emu_wait (void) { if (!emu_busy) return; emu_join (); emu_busy = 0; }
static void emu_start (void)
{
	run_t_next ();
	if (emu_make ()) { emu_busy = 1; emu_kick (); }
	else emu_loop ();                       /* (no thread: here, as before) */
}
static int emu_threaded (void) { return opt_async && !vx_ok (); }
static void emu_wait_ext (void) { emu_wait (); }   /* (UVMC2 / PiTrex: no save states, no run-ahead) */
#include "usbkm.c"
#ifndef VX_EMUVR
#include "browser.c"
#include "deck.c"
#endif
/* the picture out: the GPU's, or the CPU's frame (with the control deck: its block) - turned (also a paused refresh's) */
static void frame_out (int shot_now, const uint32_t **pic_o, int *pw_o, int *ph_o)
{
	const uint32_t *pic = ob; int pw = OW, ph = OH; const int Wd = FW;   /* (what is sent, and shot) */
#ifndef VX_EMUVR
	if (gpu_asked && !shot_now) {               /* (1.1: the GPU: up, turned, the glass and the case on top - or, the context not
	                                               there yet, the last picture again) */
		if (gpu_ok && Wd / EW == 1) gpu_present (ph_ok && ph_made && !stereo ? NULL : fb, FW, FH, Wd, (float) CX0 / EW, 1.0f - (float) (CY0 + CH) / FH, (float) (CX0 + CW) / EW, 1.0f - (float) CY0 / FH, CW, CH, opt_rot);
		else video_cb (NULL, (unsigned) ((opt_rot & 1) ? OH : OW), (unsigned) ((opt_rot & 1) ? OW : OH), 0);
	} else
#endif
#ifndef VX_EMUVR
	if (dk_on || dk_pop) {                      /* (the control deck: the block - bars, the picture, the deck - then turned) */
		dk_compose (dk_cb, fb + (size_t) CY0 * Wd + CX0, (size_t) Wd);
		const uint32_t *src = dk_cb; const size_t sp = (size_t) TW;
		if (opt_rot) { const int W = TW, Hh = TH, odd = opt_rot & 1, dw = odd ? Hh : W, dh = odd ? W : Hh; int ty;
			#pragma omp parallel for schedule(dynamic, 1)
			for (ty = 0; ty < dh; ty += 32) for (int tx = 0; tx < dw; tx += 32) {
				const int y1 = ty + 32 < dh ? ty + 32 : dh, x1 = tx + 32 < dw ? tx + 32 : dw;
				for (int y = ty; y < y1; y++) { uint32_t *d = rb + (size_t) y * dw;
					if (opt_rot == 1) for (int x = tx; x < x1; x++) d[x] = src[(size_t) x * sp + (W - 1 - y)];
					else if (opt_rot == 2) for (int x = tx; x < x1; x++) d[x] = src[(size_t) (Hh - 1 - y) * sp + (W - 1 - x)];
					else for (int x = tx; x < x1; x++) d[x] = src[(size_t) (Hh - 1 - x) * sp + y]; } }
			pic = rb; pw = dw; ph = dh; }
		else { pic = src; pw = TW; ph = TH; }
		video_cb (pic, (unsigned) pw, (unsigned) ph, (size_t) pw * 4);
	} else
#endif
	if (Wd / EW == 1 && !shot_now && !opt_rot)  /* v0.18: one picture - its window sent straight from the frame (pitch), no copy */
		video_cb (fb + (size_t) CY0 * Wd + CX0, CW, CH, (size_t) Wd * 4);
	else {
		const uint32_t *src; size_t sp;
		if (Wd / EW == 1) { src = fb + (size_t) CY0 * Wd + CX0; sp = (size_t) Wd; }
		else { for (int e = 0; e < Wd / EW; e++)   /* (the area sent: each eye's window) */
				for (int y = 0; y < CH; y++) memcpy (ob + (size_t) y * OW + e * CW, fb + (size_t) (CY0 + y) * Wd + e * EW + CX0, (size_t) CW * 4);
			src = ob; sp = (size_t) OW; }
		if (opt_rot) {                         /* (1.0: turned here, 90 / 180 / 270 counter-clockwise - in tiles, cache-kind) */
			const int W = OW, Hh = OH, odd = opt_rot & 1, dw = odd ? Hh : W, dh = odd ? W : Hh; int ty;
			#pragma omp parallel for schedule(dynamic, 1)
			for (ty = 0; ty < dh; ty += 32) for (int tx = 0; tx < dw; tx += 32) {
				const int y1 = ty + 32 < dh ? ty + 32 : dh, x1 = tx + 32 < dw ? tx + 32 : dw;
				for (int y = ty; y < y1; y++) { uint32_t *d = rb + (size_t) y * dw;
					if (opt_rot == 1) for (int x = tx; x < x1; x++) d[x] = src[(size_t) x * sp + (W - 1 - y)];
					else if (opt_rot == 2) for (int x = tx; x < x1; x++) d[x] = src[(size_t) (Hh - 1 - y) * sp + (W - 1 - x)];
					else for (int x = tx; x < x1; x++) d[x] = src[(size_t) (Hh - 1 - x) * sp + y]; } }
			pic = rb; pw = dw; ph = dh;
		} else if (src != ob) { for (int y = 0; y < CH; y++) memcpy (ob + (size_t) y * OW, src + (size_t) y * sp, (size_t) CW * 4); }
		video_cb (pic, (unsigned) pw, (unsigned) ph, (size_t) pw * 4);
	}
	if (pic_o) { *pic_o = pic; *pw_o = pw; *ph_o = ph; }
}
/* (test: VXPOC_SHOT=file.png at refresh VXPOC_FRAME (300) - counted over the shown refreshes) */
static int shot_env = -1, shot_due; static const char *shot_path; static long shot_at, shot_fno;
static int shot_pending (void)
{
	if (shot_env < 0) { shot_path = getenv ("VXPOC_SHOT"); shot_at = atol (getenv ("VXPOC_FRAME") ? getenv ("VXPOC_FRAME") : "300"); shot_env = shot_path != NULL; }
	shot_fno++; shot_due = shot_env && shot_fno == shot_at; return shot_due;
}
static void shot_write (void)                /* (the GPU's picture, read back into fb: its window, not turned) */
{
	if (!shot_due) return;
	unsigned char *rgb = malloc ((size_t) CW * CH * 3);
	for (int y = 0; y < CH; y++) for (int x = 0; x < CW; x++) { const uint32_t v = fb[(size_t) (CY0 + y) * FW + CX0 + x]; unsigned char *o = rgb + ((size_t) y * CW + x) * 3;
		o[0] = (unsigned char) (v >> 16); o[1] = (unsigned char) (v >> 8); o[2] = (unsigned char) v; }
	write_png (shot_path, rgb, CW, CH); free (rgb); fprintf (stderr, "[vectrex_emu] shot %s %dx%d (GPU)\n", shot_path, CW, CH);
}
/* (test: VXPOC_PROF=1 - where a refresh's time goes, printed at the game's end) */
static double pf_now (void) { return vt_wall (); }   /* (vtest.c: the wall clock, every system) */
void retro_run (void)
{
	if (pf_on < 0) pf_on = getenv ("VXPOC_PROF") != NULL;
	double pf0 = pf_on ? pf_now () : 0, pf1 = 0, pf2 = 0, pf3 = 0;
	emu_wait ();                                /* (1.1: the refresh to show, emulated meanwhile - UVMC2 / PiTrex) */
	net_runs++;
	km_send ();                                 /* (RetroArch's keyboard and mouse to a UVMC2 / PiTrex: usbkm.c) */
	if (sw_next[0]) { char p[1100]; snprintf (p, sizeof p, "%s", sw_next); sw_next[0] = 0;   /* (the ROM browser's / eject's game) */
		if (game_switch_now (p)) { if (sw_eject == 1) ej_out = 1; else { ej_out = 0; snprintf (cur_game, sizeof cur_game, "%s", p); }
#ifndef VX_EMUVR
			if (sw_eject != 1) snprintf (br_game, sizeof br_game, "%s", p);
#endif
		} sw_eject = 0; }
	const int asy = emu_threaded (), primed = asy && emu_primed;
	bool upd = false;
	if (env_cb (RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE, &upd) && upd) { read_options (); input_names (); }
	poll_cb ();
	unsigned char m[8] = { 128, 128, 0, 0x40, 128, 128, 0, 0 };
	pad (0, &m[0], &m[1], &m[2]); pad (1, &m[4], &m[5], &m[6]);
#ifndef VX_EMUVR
	if (br_input (m)) { m[0] = m[1] = 128; m[2] = 0; }   /* (the browser reads player 1 as the game does: turned in tate) */   /* (the ROM browser open, or just closed by a button: the game gets none) */
	if (br_dirty || (br_open && !br_shown)) br_draw ();
	{ static int pz; const int pi_ = btn_id ("vectrex_emu_btn_pause"), p_ = pi_ >= 0 && input_cb (0, RETRO_DEVICE_JOYPAD, 0, (unsigned) pi_) != 0;
	  if (p_ && !pz && !br_open && !net_on ()) { vx_paused = !vx_paused; dk_dirty_set = 1; } pz = p_;
	  if (net_on () && vx_paused) { vx_paused = 0; dk_dirty_set = 1; } }   /* (netplay: RetroArch's own pause) */
	dk_pointer (); dk_tick (0);
	{ static int l2, r2; const int ri = btn_id ("vectrex_emu_btn_reset"), ei = btn_id ("vectrex_emu_btn_eject");
	  const int a = ri >= 0 && input_cb (0, RETRO_DEVICE_JOYPAD, 0, (unsigned) ri) != 0, b = ei >= 0 && ei != ri && input_cb (0, RETRO_DEVICE_JOYPAD, 0, (unsigned) ei) != 0;
	  if (a && !l2 && !br_open && !net_on ()) { extern void vecx_reset_button (void); emu_wait_ext (); vecx_reset_button (); }   /* (the RESET button;
	                                              netplay: RetroArch's own Reset, sent to both) */
	  if (b && !r2 && !br_open && !net_on ()) eject_toggle ();
	  l2 = a; r2 = b; }
#endif
	{ int sel = opt_ov_btn ? input_cb (0, RETRO_DEVICE_JOYPAD, 0, opt_ov_btn == 2 ? RETRO_DEVICE_ID_JOYPAD_SELECT : RETRO_DEVICE_ID_JOYPAD_START) : 0;   /* START (or SELECT): the overlay off / on */
	  if (sel && !sel_held && ovl_manual != 1 && opt_overlay) ov_on = !ov_on;   /* (v0.25: the automatic overlay on / off; a hand-placed one stays) */   /* (v0.20: a hand-placed one comes off first) */
	  sel_held = sel; }
#ifndef VX_EMUVR
	fx_apply ();
	if (scr_new) { scr_new = 0; if (size_auto && scr_short && scr_short != opt_size) { opt_size = scr_short; fprintf (stderr, "[vectrex_emu] picture size: the screen's %d px\n", scr_short); } }
	if (area_glass () != geo_glass || opt_rot != geo_rot || opt_size != geo_size || opt_deck != geo_deck || opt_shape != geo_shape) { pic_setup (); geo_tell (); }
	if (ov && ov_on && !sheet_file) { if (!ov_made) ov_make (); ov_tabs (); }
	opt_check ();                               /* (the Quality profile on auto: measured once per case - gpu_opt.c) */
#endif
	paint_all = !sheet_file && ((ov && ov_on) || glass_grey > 0);
	if (ov && ov_on && !sheet_file) { if (!ov_made) ov_make (); ov_tabs (); }   /* (before the drawing threads) */
	if (paint_all) bg_make ();
	/* v0.27: RetroArch's run-ahead runs the core a few frames ahead and then puts its state back - a cartridge swap or a
	 * reset taken in one of those frames was put back with it. They are taken only in the frame that stays: the first after
	 * a state load (or any, without run-ahead: no load for a while) */
	{ int real = !unser_seen || runs_since_unser == 0 || runs_since_unser > 4; runs_since_unser++;
	  ovl_poll (); auto_keep (); if (real) cart_poll (); }
	if (lightpen) {                       /* the light pen: RetroArch's light gun (port 1 or 2), over the picture */
		extern void vt_set_analog (int id, double v);
		unsigned p = input_cb (1, RETRO_DEVICE_LIGHTGUN, 0, RETRO_DEVICE_ID_LIGHTGUN_TRIGGER) ? 1 : 0;
		int trig = input_cb (p, RETRO_DEVICE_LIGHTGUN, 0, RETRO_DEVICE_ID_LIGHTGUN_TRIGGER);
		int sx = input_cb (p, RETRO_DEVICE_LIGHTGUN, 0, RETRO_DEVICE_ID_LIGHTGUN_SCREEN_X), sy = input_cb (p, RETRO_DEVICE_LIGHTGUN, 0, RETRO_DEVICE_ID_LIGHTGUN_SCREEN_Y);
		if (input_cb (0, RETRO_DEVICE_POINTER, 0, RETRO_DEVICE_ID_POINTER_PRESSED)) {   /* (1.1: a finger on the touch screen: the pen there) */
			sx = input_cb (0, RETRO_DEVICE_POINTER, 0, RETRO_DEVICE_ID_POINTER_X); sy = input_cb (0, RETRO_DEVICE_POINTER, 0, RETRO_DEVICE_ID_POINTER_Y); trig = 1; }
		double fx = (sx + 32768) / 65536.0, fy = (sy + 32768) / 65536.0;
		{ const double ax = fx, ay = fy;      /* (the picture turned: back to its own way up) */
		  if (opt_rot == 1) { fx = 1 - ay; fy = ax; } else if (opt_rot == 2) { fx = 1 - ax; fy = 1 - ay; } else if (opt_rot == 3) { fx = ay; fy = 1 - ax; } }
		fx = (fx * TW - BX) / CW; fy = (fy * TH - BY) / CH;   /* (the deck's block: the picture within it) */
		{ double lx, ly; tube_inv (SHEET_X0 + (CX0 + fx * CW) / EW * SHEET_W, SHEET_Y0 + (CY0 + fy * CH) / (double) FH * SHEET_H - par_dy (), &lx, &ly); if (up_xf) picture_undo (&lx, &ly);
		  vt_set_analog (24, lx); vt_set_analog (25, ly); } vt_set_analog (26, trig ? 1.0 : 0.0);
	}
#ifndef VX_EMUVR
	if (vx_paused || br_open) {                 /* (paused - or the ROM browser open: no emulation, silence, the picture as it was) */
		static int16_t z[2 * 2048]; int want = (int) (44100.0 / opt_rate) + 1; if (want > 2048) want = 2048; audio_cb (z, (size_t) want);
		frame_out (0, NULL, NULL, NULL); return; }
#endif
	apply_input (m);
	{ int av = 3; vx_noaudio = env_cb (RETRO_ENVIRONMENT_GET_AUDIO_VIDEO_ENABLE, &av) && !(av & 2); }
	pw_poll (); pw_t += 1.0 / opt_rate;
	/* the emulator: game frames until the emulated time has reached this refresh's (none while switched off) */
	extern int vt_imager;
	const double dt = 1.0 / opt_rate;
	if (pf_on) pf1 = pf_now ();
	if (!primed) { run_t_next (); emu_loop (); if (asy) emu_primed = 1; }
	if (pf_on) pf2 = pf_now ();
	{ int av = 3; vx_nobuild = env_cb (RETRO_ENVIRONMENT_GET_AUDIO_VIDEO_ENABLE, &av) && !(av & 1); }
	/* the picture: which lines, from which game frames */
	const double now = run_t / 1.5e6;
	int npl = 0; unsigned long long ft0 = nh ? hist[nh - 1].p0 : 0;
	pw_gain = 1; pw_sx = 1; pw_width = 1;
	if (opt_power && pw_on && pw_t < pw_fade) {   /* (switched on: the light comes up, slowly at first; a warm tube bulges) */
		double f = pw_t / pw_fade; pw_gain = pow (f * f * (3 - 2 * f), 2.6);
		if (pw_t < 0.5 && pw_bulge > 0) pw_sx = 1.0 + pw_bulge * sin (M_PI * pw_t / 0.5) * (1.0 - pw_t / 0.5) * 2.0; }
	if (!pw_on) {                              /* (switched off: the zap once, or the picture collapsing; then only the afterglow) */
		if (opt_power && pw_mode == 0 && pw_zap_done != pw_events && !vx_nobuild) {
			pw_zap_done = pw_events; pw_gain = 1.2; memcpy (W1, pw_streak, sizeof (wline) * pw_nstreak);
			npl = build_lines (W1, pw_nstreak, 0, 0, npl);
		} else if (opt_power && pw_mode == 1 && pw_t < CRT_COLLAPSE + 1.0 / opt_rate && nh) {
			double pp = fmin (1.0, pw_t / CRT_COLLAPSE); pw_sx = 1.0 - 0.7 * pp; pw_width = 1.0 / pow (pw_sx, 0.15); pw_gain = 0.15 + 0.85 * pp * pp;
			const gframe *a = &hist[nh - 1]; int n = wl_from (W1, 0, a->L, a->n, NULL, 0);
			if (opt_wob) hum_coeffs (a->p0 / 1.5e6, opt_wob);
			npl = build_lines (W1, n, 0, 0, npl);
		}
	} else if (vt_imager) {                          /* (the 3D Imager: the two half turns' pictures - side by side, or both in
	                                              colour on one; not mixed between frames, the disc's halves alternate) */
		for (int e = 0; e < 2; e++) {
			/* (the second half turn's lines, rgb bit 3, are the LEFT eye's: so the depth comes out right - in 3D Crazy
			 *  Coaster the near track and the rider's hands lie ~100 px apart, the horizon ~200 px (~52 mm on the real
			 *  screen, nearly the eyes' distance: far away) - checked 2026-11-04; "swap eyes" turns it round) */
			int to = stereo ? e ^ 1 ^ opt_swap : 0;
			/* the eye shift: the right eye's picture (as shown) moved right (+) or left (-) against the left eye's, half each */
			double esh = stereo ? (to ? 0.5 : -0.5) * opt_shift * S : 0;
			if (opt_wob) hum_coeffs (ft0 / 1.5e6, opt_wob);
			int n = wl_from (W1, 0, eyeb[e], neye[e], NULL, 0);
			npl = build_lines (W1, n, to, esh, npl);
		}
	} else if (nh) {
		int n = 0;
		if (opt_smooth && opt_rate != 50) {
			double per = nh > 1 ? (hist[nh - 1].t - hist[0].t) / (nh - 1) : 0.02;
			per = per < 0.01 ? 0.01 : per > 0.1 ? 0.1 : per;
			double D = now - per - dt;
			while (nh > 2 && hist[1].t <= D) frame_pop ();
			if (D < hist[0].t - 0.25) while (nh > 1) frame_pop ();
			const gframe *a = &hist[0], *b = nh > 1 && hist[1].t > a->t ? &hist[1] : NULL;
			double alpha = b ? (D - a->t) / (b->t - a->t) : 0; alpha = alpha < 0 ? 0 : alpha > 1 ? 1 : alpha;
			const line_t *B = NULL;
			if (b && alpha > 0 && b->n == a->n) {
				int ok = 1;
				for (int i = 0; i < a->n && ok; i++) { const line_t *p = &a->L[i], *q = &b->L[i];
					if (p->c != q->c || labs (q->x0 - p->x0) >= SMOOTH_MATCH || labs (q->y0 - p->y0) >= SMOOTH_MATCH
					    || labs (q->x1 - p->x1) >= SMOOTH_MATCH || labs (q->y1 - p->y1) >= SMOOTH_MATCH) ok = 0; }
				if (ok) B = b->L;
			}
			sm_runs++; if (B) sm_mixed++;
			if (a->alt && have_prev) n = wl_from (W1, n, prevf.L, prevf.n, NULL, 0);   /* (a picture in halves: both) */
			n = wl_from (W1, n, a->L, a->n, B, alpha);
			/* (the hum at the nearest real frame's moment: the beam draws only when the game draws) */
			ft0 = (b && alpha >= 0.5) ? b->p0 : a->p0;
		} else {
			const gframe *a = &hist[nh - 1];
			if (a->alt) { const gframe *p = nh > 1 ? &hist[nh - 2] : have_prev ? &prevf : NULL; if (p) n = wl_from (W1, n, p->L, p->n, NULL, 0); }
			n = wl_from (W1, n, a->L, a->n, NULL, 0);
			ft0 = a->p0;
			while (nh > 3) frame_pop ();
		}
		if (opt_wob) hum_coeffs (ft0 / 1.5e6, opt_wob);
		npl = build_lines (W1, n, 0, 0, npl);
	}
	/* v0.13: a refresh RetroArch does not show (run-ahead's hidden runs: the video off) draws no picture - after the
	 * choice of frames above, which keeps the kept frames' list the same whether a refresh is shown or not */
	{ int av = 3; if (env_cb (RETRO_ENVIRONMENT_GET_AUDIO_VIDEO_ENABLE, &av) && !(av & 1)) { vx_hidden++; audio_out (); if (asy) { flash_save (0); emu_start (); } return; } }
	vx_shown++;
	if (asy) { audio_out (); flash_save (0); emu_start (); }   /* (threaded: this refresh's sound out, the next one's emulation
	                                              started - the drawing below reads only the pieces built above) */
	if (pf_on) pf3 = pf_now ();
	/* the bands: the lines, the phosphor, then (no glow) the knee and the 8-bit picture - each band on its own core */
	const int Wd = FW, H = FH; const size_t NP = (size_t) Wd * H;
#ifndef VX_EMUVR
	/* (1.1: the light on the GPU - gpu_phos.c; the CPU's bands below otherwise) */
	const int gph = gpu_asked && gpu_ok && ph_ok && !stereo && !sheet_file;
	static int half_skip; static double half_dt;
	if (gph && fx_half && (half_skip ^= 1) && ph_made && !shot_due) {   /* (Low: 30 pictures a second - this one the last again) */
		half_dt += dt;
		gpu_present (NULL, FW, FH, FW, (float) CX0 / EW, 1.0f - (float) (CY0 + CH) / FH, (float) (CX0 + CW) / EW, 1.0f - (float) CY0 / FH, CW, CH, opt_rot);
		sheet_write (); if (!asy) flash_save (0); if (!asy) audio_out ();
		return;
	}
	if (gph) {
		const double pdt = dt + half_dt; half_dt = 0;
		if (ph_frame (npl, pdt, opt_glow, opt_after, shot_pending ())) {
			if (pf_on) { double t4 = pf_now (); pf_g[0] += t4 - pf3; }
			gpu_present (NULL, FW, FH, FW, (float) CX0 / EW, 1.0f - (float) (CY0 + CH) / FH, (float) (CX0 + CW) / EW, 1.0f - (float) CY0 / FH, CW, CH, opt_rot);
			shot_write ();
			sheet_write ();
			if (!asy) flash_save (0);
			if (pf_on) { double pf5 = pf_now (); pf_t[0] += pf1 - pf0; pf_t[1] += pf2 - pf1; pf_t[2] += pf3 - pf2; pf_t[3] += pf5 - pf3; pf_n++; }
			if (!asy) audio_out ();
			return;
		}
	}
#endif
	const int cb = pbi; float **cur = pb[pbi], **prv = pb[pbi ^ 1]; pbi ^= 1;
	for (int c = 0; c < 3; c++) lacc[c] = cur[c];
	cur_l0 = sp_l0[cb]; cur_l1 = sp_l1[cb];
	const int *pl0 = sp_l0[cb ^ 1], *pl1 = sp_l1[cb ^ 1];
	const int NB = 64, BH = (H + NB - 1) / NB;
	const int glow = opt_glow, after = opt_after;
	const int painted = paint_all;             /* (the overlay painted in, or the glass grey: every pixel has colour) */
	const int ry0 = glow ? 0 : CY0, ry1 = glow ? H - 1 : CY0 + CH - 1;   /* (no glow: only the window's rows are sent) */
	const int cx0 = glow ? 0 : CX0, cx1 = glow ? Wd - 1 : (FW - EW) + CX0 + CW - 1;
	const float tk = ov && ov_on && !sheet_file ? opt_trail_k : opt_trail_k0;   /* (1.0: its own without an overlay) */
	const float a1 = 0.1404f * tk, a2 = 0.0582f * tk,   /* (v0.16: x the trail brightness option) */
	             ta1 = 0.0454f * opt_trail, ta2 = 0.350f * opt_trail;   /* (the trail's time scale: Vectrex_EMU's 0.6, a real Vectrex 1.0) */
	const float kd1 = expf (-(float) dt / ta1), kd2 = expf (-(float) dt / ta2), e1 = a1 * kd1, e2 = a2 * kd2;
	int bi;
	#pragma omp parallel for schedule(dynamic, 1)
	for (bi = 0; bi < NB; bi++) {
		int y0b = bi * BH, y1b = y0b + BH - 1; if (y1b > H - 1) y1b = H - 1;
		if (y0b < ry0) y0b = ry0; if (y1b > ry1) y1b = ry1;
		if (y0b > y1b) continue;
		for (int y = y0b; y <= y1b; y++) {               /* (this buffer's light of two refreshes ago: cleared where it was) */
			if (cur_l0[y] <= cur_l1[y]) for (int c = 0; c < NC; c++) memset (cur[c] + (size_t) y * Wd + cur_l0[y], 0, (size_t) (cur_l1[y] - cur_l0[y] + 1) * 4);
			cur_l0[y] = SP_NONE; cur_l1[y] = -1; }
		for (int k = 0; k < npl; k++) splat_band (&pl[k], y0b, y1b);
		for (int y = y0b; y <= y1b; y++) {
			int xs, xe;
			if (glow || painted) { xs = cx0; xe = cx1; }
			else { xs = cur_l0[y]; xe = cur_l1[y];
				if (after) { if (pl0[y] < xs) xs = pl0[y]; if (pl1[y] > xe) xe = pl1[y]; if (sp_g0[y] < xs) xs = sp_g0[y]; if (sp_g1[y] > xe) xe = sp_g1[y]; }
				if (xs < cx0) xs = cx0; if (xe > cx1) xe = cx1; }
			uint32_t *frow = fb + (size_t) y * Wd;
			{ int f0 = sp_f0[y], f1 = sp_f1[y] > Wd - 1 ? Wd - 1 : sp_f1[y];   /* (the picture's old light outside this span: black) */
			  if (f0 <= f1) { if (xs > xe) memset (frow + f0, 0, (size_t) (f1 - f0 + 1) * 4);
				else { if (f0 < xs) memset (frow + f0, 0, (size_t) ((f1 < xs - 1 ? f1 : xs - 1) - f0 + 1) * 4);
				       if (f1 > xe) { int z = f0 > xe + 1 ? f0 : xe + 1; memset (frow + z, 0, (size_t) (f1 - z + 1) * 4); } } } }
			int ng0 = SP_NONE, ng1 = -1, nf0 = SP_NONE, nf1 = -1;
			for (int x = xs; x <= xe; x++) {
				const size_t i = (size_t) y * Wd + x;
				float f[3], o[3];
				int any = 0;
				for (int c = 0; c < NC; c++) { f[c] = cur[c][i]; o[c] = f[c]; any |= f[c] > 0; }
				if (after) {
					float p[3], lp_ = 0, cap = 1.0f; int pv = 0, gl = 0;
					for (int c = 0; c < NC; c++) { p[c] = prv[c][i]; pv |= p[c] > 0; lp_ += p[c]; }
					if (pv) { lp_ /= NC; if (lp_ > 1.0f) cap = 1.0f / lp_; }
					for (int c = 0; c < NC; c++) {
						float g1 = g1b[c][i], g2 = g2b[c][i], l = pv ? p[c] * cap - f[c] : 0;
						if (g1 > 0 || g2 > 0 || l > 0) {
							g1 *= kd1; g2 *= kd2;           /* (one refresh on) */
							if (l > 0) { g1 += l * e1; g2 += l * e2; }   /* (no longer lit: it starts to glow away) */
							if (f[c] > 0) { g1 -= f[c] * a1; g2 -= f[c] * a2; if (g1 < 0) g1 = 0; if (g2 < 0) g2 = 0; }   /* (lit again) */
							if (g1 < 1e-4f) g1 = 0; if (g2 < 1e-4f) g2 = 0;
							g1b[c][i] = g1; g2b[c][i] = g2; o[c] += g1 + g2; any |= g1 + g2 > 0; gl |= g1 + g2 > 0; }
					}
					if (gl) { if (x < ng0) ng0 = x; ng1 = x; }
				}
				if (glow) { for (int c = 0; c < NC; c++) shown[c][i] = o[c]; continue; }
				if (!any) { frow[x] = painted ? bgimg[i] : 0; continue; }   /* (no light: black, or the background made once) */
				frow[x] = NC == 3 ? pixel_out (o[0], o[1], o[2], x, y) : pixel_out (o[0], o[0], o[0], x, y);
				if (x < nf0) nf0 = x; nf1 = x;
			}
			if (after) { sp_g0[y] = ng0; sp_g1[y] = ng1; }   /* (the afterglow left in this row: none outside xs..xe) */
			if (glow) { sp_f0[y] = 0; sp_f1[y] = 1 << 29; } else { sp_f0[y] = nf0; sp_f1[y] = nf1; }
		}
	}
	double pgA = pf_on ? pf_now () : 0, pgB = pgA;   /* (test: the bands, the glow's blur, its adding) */
	if (glow) {                                /* the phosphor glow, added before the knee (glow_blur) */
		glow_blur (shown); if (pf_on) pgB = pf_now ();
		static int *bx0; static float *bxw; static int bxn;      /* (the bilinear step up: each column's cells, per eye) */
		if (bxn != Wd) { free (bx0); free (bxw); bx0 = malloc (Wd * sizeof (int)); bxw = malloc (Wd * sizeof (float)); bxn = Wd; }
		for (int x = 0; x < Wd; x++) { const int e = x / EW; float f = ((x - e * EW) + 0.5f) / GD - 0.5f; if (f < 0) f = 0;
			int j = (int) f; if (j > GEW - 2) j = GEW - 2; float w = f - j; if (w > 1) w = 1; bx0[x] = e * GEW + j; bxw[x] = w; }
		const float gk = GLOW_K * (glow == 2 ? 2.0f : 1.0f);
		const float t0 = gk * GLOW_TINT[0], t1 = gk * GLOW_TINT[1], t2 = gk * GLOW_TINT[2];
		/* (no overlay painted in: a pixel whose light rounds to 0 is black at once - the same as pixel_out gives) */
		const int painted = paint_all; float thr = 0.5f / (255.0f * 1.6f);
		if (painted) thr /= 1.2f * (opt_bright > 1 ? opt_bright : 1);   /* (painted: below this the light changes no level of the
		                                                   background - the overlay's tint up to x 1.15, the brightness) */
		const int nc = NC; float *const s0 = shown[0], *const s1 = shown[1], *const s2 = shown[2]; uint32_t *const fo = fb;
		int yy;
		#pragma omp parallel for schedule(dynamic, 8)
		for (yy = 0; yy < H; yy++) {
			float fy = (yy + 0.5f) / GD - 0.5f; if (fy < 0) fy = 0; int j = (int) fy; if (j > GCH - 2) j = GCH - 2;
			float wy = fy - j; if (wy > 1) wy = 1;
			float cr[3][2 * PW + 4];                             /* (this row's cells, between the two cell rows) */
			for (int c = 0; c < nc; c++) { const float *q0 = glc[c] + (size_t) j * GCW, *q1 = q0 + GCW;
				for (int x = 0; x < GCW; x++) cr[c][x] = q0[x] + (q1[x] - q0[x]) * wy; }
			const size_t i0 = (size_t) yy * Wd;
			for (int x = 0; x < Wd; x++) { const size_t i = i0 + x; const int k = bx0[x]; const float wx = bxw[x];
				const float G0 = cr[0][k] + (cr[0][k + 1] - cr[0][k]) * wx;
				float r, g, b;
				if (nc == 3) { const float G1 = cr[1][k] + (cr[1][k + 1] - cr[1][k]) * wx, G2 = cr[2][k] + (cr[2][k + 1] - cr[2][k]) * wx;
					r = s0[i] + t0 * G0; g = s1[i] + t1 * G1; b = s2[i] + t2 * G2; }
				else { const float o = s0[i]; r = o + t0 * G0; g = o + t1 * G0; b = o + t2 * G0; }
				if (r < thr && g < thr && b < thr) { fo[i] = painted ? bgimg[i] : 0; continue; }
				fo[i] = pixel_out (r, g, b, x, yy); }
		}
	}
	(void) NP;
	double pf4 = pf_on ? pf_now () : 0;
	if (pf_on) { pf_g[0] += pgA - pf3; pf_g[1] += pgB - pgA; pf_g[2] += pf4 - pgB; }
#ifndef VX_EMUVR
	if (br_shown && !(gpu_asked && gpu_ok)) { br_blend_cpu (fb, FW);   /* (the ROM browser over the CPU's picture - the GPU draws it in front of the
	                                              case itself; those pixels painted anew next time) */
		for (int y = br_by0 < 0 ? 0 : br_by0; y <= br_by1 && y < FH; y++) { if (br_bx0 < sp_f0[y]) sp_f0[y] = br_bx0 < 0 ? 0 : br_bx0; if (br_bx1 > sp_f1[y]) sp_f1[y] = br_bx1; } }
#endif
	const int shot_now = shot_pending ();
	const uint32_t *pic; int pw, ph; frame_out (shot_now, &pic, &pw, &ph);
	sheet_write ();
	if (!asy) flash_save (0);                   /* (1.1: the flash cartridge's saves - live.c: a second after its last write) */
	if (pf_on) { double pf5 = pf_now (); pf_t[0] += pf1 - pf0; pf_t[1] += pf2 - pf1; pf_t[2] += pf3 - pf2; pf_t[3] += pf4 - pf3; pf_t[4] += pf5 - pf4; pf_n++; }
	{ const char *sh = shot_path;   /* (test: the picture at refresh N as a PNG) */
	  if (shot_now) {
		unsigned char *rgb = malloc ((size_t) pw * ph * 3);
		for (size_t i = 0; i < (size_t) pw * ph; i++) { rgb[3 * i] = (unsigned char) (pic[i] >> 16); rgb[3 * i + 1] = (unsigned char) (pic[i] >> 8); rgb[3 * i + 2] = (unsigned char) pic[i]; }
		write_png (sh, rgb, pw, ph); free (rgb); fprintf (stderr, "[vectrex_emu] shot %s %dx%d, %d lines, %d pieces drawn; smooth motion: %ld of %ld refreshes between two frames\n", sh, pw, ph, nlast, npl, sm_mixed, sm_runs); } }
	if (!asy) audio_out ();
}

/* ---- the speech module (v0.11, vectrex_emu.py port2_needed): the games that talk get a VecVoice or VecVox in port 2,
 * by a title line of the cartridge header (SPEECH_TITLES) or the file's name (SPEECH_FILES) - only when the SP0256-AL2
 * ROM is there (they run without it, silent) */
static const char *speech_dev (const unsigned char *h, size_t hn, const char *path)
{
	static const char *const TT[][2] = { { "BERZERK ULTIMATE", "vecvox" }, { "SPACE FRENZY", "vecvox" }, { "VECFAHREN", "vecvox" },
		{ "VECTORBLADE", "vecvox" }, { "VECVOX XMAS", "vecvox" }, { "VECTALK", "vecvox" }, { "VERZERK", "vecvoice" },
		{ "PYTHAGORIAN THEOREM", "vecvoice" }, { "PYTHAGOREAN THEOREM", "vecvoice" }, { "PROTECTOR/YASI", "vecvoice" } };
	static const char *const FF[][2] = { { "brecher", "vecvox" }, { "color clash", "vecvox" }, { "debris", "vecvox" }, { "rushhour", "vecvox" },
		{ "rush hour", "vecvox" }, { "shifted", "vecvox" }, { "spike's circus", "vecvox" }, { "spikes circus", "vecvox" },
		{ "vectoblox", "vecvox" }, { "v-hockey", "vecvox" }, { "vhockey", "vecvox" }, { "vecvox demo", "vecvox" },
		{ "vecvox driver demo", "vecvox" }, { "vectalk", "vecvox" }, { "b.e.t.h", "vecvoice" }, { "deathchase", "vecvoice" },
		{ "vecvoice demo", "vecvoice" }, { "yasi", "vecvoice" } };
	char name[1024]; const char *b = path, *s;
	if ((s = strrchr (b, '\\'))) b = s + 1; if ((s = strrchr (b, '/'))) b = s + 1;
	snprintf (name, sizeof name, "%s", b);
	for (char *p = name; *p; p++) *p = (char) tolower ((unsigned char) *p);
	const char *ext = strrchr (name, '.');
	if (!ext || (strcmp (ext, ".bin") && strcmp (ext, ".vec") && strcmp (ext, ".gam"))) return NULL;
	for (size_t i = 0; i + 7 <= hn; i++) if (!memcmp (h + i, "SPINNER", 7)) return NULL;   /* (a hack for a spinner in port 2) */
	if (hn >= 5 && !memcmp (h, "g GCE", 5)) {   /* the title lines (gce_titles): after 0x80 and the music's address, each line's
	                                               height, width, y, x and its text up to 0x80, until a zero byte */
		const unsigned char *e = memchr (h, 0x80, hn); size_t i = e ? (size_t) (e - h) + 3 : hn; int nt = 0;
		while (i + 4 < hn && h[i] != 0 && nt < 4) {
			const unsigned char *j = memchr (h + i + 4, 0x80, hn - i - 4); size_t a = i + 4, z = j ? (size_t) (j - h) : hn;
			for (size_t k = 0; k < sizeof TT / sizeof TT[0]; k++) { size_t L = strlen (TT[k][0]);
				for (size_t q = a; q + L <= z; q++) if (!memcmp (h + q, TT[k][0], L)) return TT[k][1]; }
			nt++; if (!j) break; i = z + 1;
		}
	}
	for (size_t k = 0; k < sizeof FF / sizeof FF[0]; k++) if (strstr (name, FF[k][0])) return FF[k][1];
	for (const char *p = name; (p = strstr (p, "nox")); p++)   /* (\bnox\b) */
		if ((p == name || !isalnum ((unsigned char) p[-1]) && p[-1] != '_') && !isalnum ((unsigned char) p[3]) && p[3] != '_') return "vecvoice";
	return NULL;
}

/* ---- v0.13: save states (see the header). The layout: a header, the state's variables by name (vars_*.h), a flash
 * cartridge's image, the lines in progress and the last frame's, the 3D Imager's eye buffers, the kept game frames -
 * the same bytes on every system (vx_raw: the variables' bytes) */
static size_t vx_raw, vx_size; static int vx_inited;
#define VX_MAGIC 0x35315856u                /* "VX15" (1.1: the lines compact; 1.2: the variables by name) */
/* (the lines in a state: 26 bytes each, the same on every system - x0 y0 x1 y1 as 32 bits, the intensity, the colour, the
 *  cycles, the start - at most ST_L a frame (ST_E an eye): RetroArch's netplay keeps a ring of states and sends one to who
 *  joins; at 40000 lines of 48 bytes reserved for each list a state was 19 MB and the join failed, 2026-11-15. A 6809 game
 *  draws a few thousand at most; more would trim only the picture after a load, never the game) */
#define ST_L 6000
#define ST_E 3000
#define LREC 26
static unsigned char *put_lines (unsigned char *o, const line_t *L, int n, int cap)
{
	int32_t m = n < cap ? n : cap; memcpy (o, &m, 4); o += 4;
	for (int i = 0; i < m; i++) { const line_t *l = &L[i]; int32_t v[4] = { (int32_t) l->x0, (int32_t) l->y0, (int32_t) l->x1, (int32_t) l->y1 };
		uint32_t c2[2] = { l->cyc, l->t0 }; memcpy (o, v, 16); o[16] = l->c; o[17] = l->rgb; memcpy (o + 18, c2, 8); o += LREC; }
	return o;
}
static const unsigned char *get_lines (const unsigned char *i, line_t *L, int *n)
{
	int32_t m; memcpy (&m, i, 4); i += 4; if (m < 0) m = 0;
	for (int k = 0; k < m; k++) { line_t *l = &L[k]; int32_t v[4]; uint32_t c2[2]; memcpy (v, i, 16); memcpy (c2, i + 18, 8);
		l->x0 = v[0]; l->y0 = v[1]; l->x1 = v[2]; l->y1 = v[3]; l->c = i[16]; l->rgb = i[17]; l->cyc = c2[0]; l->t0 = c2[1]; i += LREC; }
	*n = m; return i;
}
#define VX_FRAME_HDR (sizeof (int) * 3 + sizeof (double) + sizeof (unsigned long long))
static int extern_cart_len (void) { extern size_t vt_cart_len; return (int) (vt_cart_len > sizeof cart ? sizeof cart : vt_cart_len); }
static int vx_ok (void) { extern int vt_uvm2, vt_uvm2_cart, vt_pitrex; return !vt_uvm2 && !vt_uvm2_cart && !vt_pitrex; }
/* (2026-11-15: the variables by name, in one order, 'long' as 8 bytes - the same state on every system, so netplay joins
 *  between Windows, Android and Linux. Before: the save-state sections whole, which each compiler lays out its own way.
 *  The lists: vars_*.h, made by tools/gen_state_vars.py from both builds' symbols) */
#include "vars_vx.h"
void vxs_vars_vecx (void (*add) (void *, size_t, int)), vxs_vars_e6809 (void (*add) (void *, size_t, int)), vxs_vars_e8910 (void (*add) (void *, size_t, int));
typedef struct { unsigned char *p; size_t n; int l; } vxv_t;
static vxv_t *vxv; static int nvxv, cvxv;
static void vxv_add (void *p, size_t n, int l)
{
	if (nvxv == cvxv) { cvxv = cvxv ? cvxv * 2 : 1024; vxv = realloc (vxv, sizeof (vxv_t) * cvxv); }
	vxv[nvxv].p = p; vxv[nvxv].n = n; vxv[nvxv].l = l; nvxv++;
	vx_raw += l ? n / sizeof (long) * 8 : n;
}
static unsigned char *vxv_save (unsigned char *o)
{
	for (int k = 0; k < nvxv; k++) { const vxv_t *v = &vxv[k];
		if (!v->l) { memcpy (o, v->p, v->n); o += v->n; continue; }
		for (size_t j = 0; j < v->n / sizeof (long); j++) { int64_t x = ((const long *) v->p)[j]; memcpy (o, &x, 8); o += 8; } }
	return o;
}
static const unsigned char *vxv_load (const unsigned char *i)
{
	for (int k = 0; k < nvxv; k++) { const vxv_t *v = &vxv[k];
		if (!v->l) { memcpy (v->p, i, v->n); i += v->n; continue; }
		for (size_t j = 0; j < v->n / sizeof (long); j++) { int64_t x; memcpy (&x, i, 8); ((long *) v->p)[j] = (long) x; i += 8; } }
	return i;
}
static void vx_state_init (void)
{
	if (!vx_inited) {                       /* (the lines in progress, the last frame's and the eye buffers: saved apart, with their counts) */
		vx_inited = 1; vx_raw = 0;
		vxs_vars_vecx (vxv_add); vxs_vars_e6809 (vxv_add); vxs_vars_e8910 (vxv_add); vxs_vars_vx (vxv_add);
		if (getenv ("VXPOC_STATESPEC")) for (int k = 0; k < nvxv; k++) fprintf (stderr, "[statespec] %d %zu %d%c", k, vxv[k].n, vxv[k].l, 10);   /* (test: each one's size here) */
	}
	vx_size = 16 + vx_raw + (size_t) extern_cart_len () + 2 * (4 + ST_L * LREC) + 2 * (4 + ST_E * LREC)
	        + 2 * sizeof (int) + (NH + 1) * (VX_FRAME_HDR + 4 + ST_L * LREC + 6000 * sizeof (long long));
	if (getenv ("VXPOC_STATELOG")) fprintf (stderr, "[vectrex_emu] save state: %zu bytes of sections, at most %zu bytes%c", vx_raw, vx_size, 10);
}
size_t retro_serialize_size (void) { if (!vx_ok ()) return 0; if (!vx_size) vx_state_init (); return vx_size; }
#define PUT(src, n) do { memcpy (o, (src), (n)); o += (n); } while (0)
#define GET(dst, n) do { memcpy ((dst), i, (n)); i += (n); } while (0)
static unsigned char *put_frame (unsigned char *o, const gframe *f)
{
	int nk = f->nk > 0 ? f->nk : 0;
	if (nk > 6000) nk = 6000;
	PUT (&f->n, sizeof (int)); PUT (&nk, sizeof (int)); PUT (&f->alt, sizeof (int)); PUT (&f->t, sizeof (double)); PUT (&f->p0, sizeof (unsigned long long));
	o = put_lines (o, f->L, f->n, ST_L); PUT (f->keys, sizeof (long long) * nk);
	return o;
}
static int net_dbg = -1;
#define NDBG(...) do { if (net_dbg < 0) net_dbg = getenv ("VXPOC_NETDBG") != NULL; if (net_dbg) fprintf (stderr, __VA_ARGS__); } while (0)
bool retro_serialize (void *d, size_t sz)
{
	emu_wait_ext (); net_mark ();
	{ static long n; if (++n % 120 == 1) NDBG ("[netdbg] serialize #%ld sz %zu vx_size %zu run %ld\n", n, sz, vx_size, net_runs); }
	if (!vx_ok () || !vx_size) vx_state_init ();
	if (!vx_ok () || sz < vx_size) return false;
	unsigned char *o = d; uint32_t h[4] = { VX_MAGIC, (uint32_t) vx_raw, (uint32_t) extern_cart_len (), 0 };
	PUT (h, 16);
	o = vxv_save (o);
	PUT (cart, extern_cart_len ());
	o = put_lines (o, cur, ncur, ST_L); o = put_lines (o, last, nlast, ST_L);
	o = put_lines (o, eyeb[0], neye[0], ST_E); o = put_lines (o, eyeb[1], neye[1], ST_E);
	PUT (&nh, sizeof (int)); PUT (&have_prev, sizeof (int));
	if (have_prev) o = put_frame (o, &prevf);
	for (int k = 0; k < nh; k++) o = put_frame (o, &hist[k]);
	memset (o, 0, (unsigned char *) d + sz - o);   /* (the rest: zeros, so two saves of one moment compare equal) */
	return true;
}
bool retro_unserialize (const void *d, size_t sz)
{
	emu_wait_ext (); net_mark ();
	unser_seen = 1; runs_since_unser = 0;
	if (!vx_ok () || !vx_size) vx_state_init ();
	const unsigned char *i = d; uint32_t h[4];
	if (!vx_ok () || sz < 16) { NDBG ("[netdbg] unserialize: no (ok %d sz %zu)\n", vx_ok (), sz); return false; }
	GET (h, 16);
	if (h[0] != VX_MAGIC || h[1] != vx_raw || h[2] != (uint32_t) extern_cart_len ()) {
		NDBG ("[netdbg] unserialize: header %08x %u %u, mine %08x %zu %ld - sz %zu\n", h[0], h[1], h[2], VX_MAGIC, vx_raw, (long) extern_cart_len (), sz); return false; }
	{ static long n; if (++n % 60 == 1) NDBG ("[netdbg] unserialize #%ld ok sz %zu run %ld\n", n, sz, net_runs); }
	i = vxv_load (i);
	GET (cart, extern_cart_len ());
	{ int m; i = get_lines (i, cur, &m); ncur = m; i = get_lines (i, last, &m); nlast = m;   /* (the counts as saved: a list over the cap trimmed) */
	  i = get_lines (i, eyeb[0], &m); neye[0] = m; i = get_lines (i, eyeb[1], &m); neye[1] = m; }
	/* the kept game frames: their buffers reused (the ones held now and the spare), more made if needed */
	gframe pool[NH + 3]; int np = 0;
	for (int k = 0; k < nh; k++) pool[np++] = hist[k];
	if (have_prev) pool[np++] = prevf;
	if (spare.L) { pool[np++] = spare; spare.L = NULL; }
	int n_h, n_p; GET (&n_h, sizeof (int)); GET (&n_p, sizeof (int));
	gframe *dst[NH + 1]; int nd = 0;
	if (n_p) dst[nd++] = &prevf;
	for (int k = 0; k < n_h; k++) dst[nd++] = &hist[k];
	for (int k = 0; k < nd; k++) {
		gframe f;
		if (np) f = pool[--np]; else { f.L = malloc (sizeof (line_t) * MAXL); f.keys = malloc (sizeof (long long) * 6000); }
		int fn; GET (&fn, sizeof (int)); GET (&f.nk, sizeof (int)); GET (&f.alt, sizeof (int)); GET (&f.t, sizeof (double)); GET (&f.p0, sizeof (unsigned long long));
		i = get_lines (i, f.L, &f.n); (void) fn; GET (f.keys, sizeof (long long) * (f.nk > 0 ? f.nk : 0));
		*dst[k] = f;
	}
	nh = n_h; have_prev = n_p;
	if (np) { spare = pool[--np]; }
	while (np) { np--; free (pool[np].L); free (pool[np].keys); }
	return true;
}
#undef PUT
#undef GET
/* ---- a game starts */
static int try_overlay_dir (const char *dir, const char *rom);
static void frame_pop (void);
static void cart_swap (const char *path)
{
	emu_wait_ext (); emu_primed = 0;
	extern int vt_vb, vt_hb512, vt_multicart; extern int vtest_main (int, char **);
	const char *ovname = path; path = v4e_open (path); if (!path) return;
	vt_vb = 0; vt_hb512 = 0; vt_multicart = 0;           /* (the last cartridge's boards out: vtest main only sets them) */
	game_env (path, g_sys[0] ? g_sys : NULL, g_dir);
	char scr[1600]; snprintf (scr, sizeof scr, "%s" SEP "vectrex_emu_empty.txt", tmpdir);
	FILE *f = fopen (scr, "w"); if (f) fclose (f);
	char *argv[] = { "vx", g_bios, (char *) path, scr, tmpdir, NULL };
	{ extern const char *vt_flash_file; vt_flash_file = NULL; }
	int rc = vtest_main (5, argv); remove (scr);
	if (rc != 2) flash_to_saves (path);
	fprintf (stderr, "[vectrex_emu] cartridge swapped: %s (%s)\n", path, rc == 2 ? "FAILED" : "running");
	while (nh) frame_pop (); aq_r = aq_w = 0;
	if (ov) stbi_image_free (ov); ov = NULL; ov_path[0] = 0;
	{ char od[1600]; if (g_sys[0]) { snprintf (od, sizeof od, "%s" SEP "vectrex_emu" SEP "overlays", g_sys); try_overlay_dir (od, ovname); }
	  if (!ov) { snprintf (od, sizeof od, "%s" SEP "Overlays", g_dir); try_overlay_dir (od, ovname); } }
	boot_id = VX_TICKS () | 1; sheet_written = -1; sheet_write ();
}
/* a game started: the common part of RetroArch's load and the ROM browser's (first: RetroArch's own - the GPU asked for) */
static bool game_open (const char *gpath, const char *sys, int first)
{
	char dir[1024]; snprintf (dir, sizeof dir, "%s", gpath);
	{ char *s = strrchr (dir, '\\'), *t = strrchr (dir, '/'); if (t > s) s = t; if (s) *s = 0; else strcpy (dir, "."); }
	/* the BIOS (8 KB): in the system folder, or next to the game (EmuVR keeps it there) */
	char bios[1600] = "";
	const char *names[] = { "vectrex_emu" SEP "bios.bin", "bios.bin", "[BIOS] GCE Vectrex (World).bin", "vectrex.bin", NULL };
	const char *dirs[] = { sys, dir, NULL };
	for (int d = 0; dirs[d] && !bios[0]; d++) for (int k = 0; names[k]; k++) {
		char p[1600]; snprintf (p, sizeof p, "%s" SEP "%s", dirs[d], names[k]);
		FILE *f = fopen (p, "rb"); if (f) { fseek (f, 0, SEEK_END); long L = ftell (f); fclose (f); if (L == 8192) { strcpy (bios, p); break; } } }
	if (!bios[0]) { fprintf (stderr, "[vectrex_emu] no BIOS (bios.bin in the system folder)\n"); return false; }
	snprintf (g_bios, sizeof g_bios, "%s", bios); snprintf (g_sys, sizeof g_sys, "%s", sys ? sys : ""); snprintf (g_dir, sizeof g_dir, "%s", dir);
	{ const char *ce = getenv ("VECTREX_CART"); cart_file = ce && *ce ? ce : NULL; cart_seq = -1; }
	if (first) rate_set = 0;                    /* (each game: the screen rate read anew - not a game the browser loads) */
	read_options ();
	const char *gp = v4e_open (gpath);           /* (a .v4e: its unpacked image) */
	if (!gp) return false;
	game_env (gp, sys, dir);
	/* (an empty script: vtest's main loads, resets and returns - the emulator stays set up) */
	/* (2026-11-15: the first of the system folder, RetroArch's save folder and the game's folder that can be written -
	 *  Android's storage rules may keep one of them read-only) */
	char scr[1600]; FILE *f = NULL;
	{ const char *sv = NULL; env_cb (RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY, &sv);
	  const char *cand[] = { sys, sv, dir, NULL };
	  for (int k = 0; k < 3 && !f; k++) { if (!cand[k] || !*cand[k]) continue;
		snprintf (tmpdir, sizeof tmpdir, "%s", cand[k]); snprintf (scr, sizeof scr, "%s" SEP "vectrex_emu_empty.txt", tmpdir);
		f = fopen (scr, "w"); } }
	if (!f) { fprintf (stderr, "[vectrex_emu] no folder to write in (system, saves, the game's)\n"); return false; } fclose (f);
	char *argv[] = { "vx", bios, (char *) gp, scr, tmpdir, NULL };
	{ extern const char *vt_flash_file; vt_flash_file = NULL; }
	int rc = vtest_main (5, argv);
	remove (scr);
	if (rc != 2) flash_to_saves (gp);
	if (rc == 2) { fprintf (stderr, "[vectrex_emu] the game could not be loaded: %s\n", gpath); return false; }
	if (!abuf) { abuf_cap = 65536; abuf = malloc (abuf_cap); }
	{ extern int vt_uvm2, vt_uvm2_cart, vt_pitrex; frame_budget = (vt_uvm2 || vt_uvm2_cart || vt_pitrex) ? 30000 : 33000; }
	{ extern int vt_imager; stereo = vt_imager && opt_imager == 2; }
	rate_set = 1;
	pic_setup ();
	for (int k = 0; k < GTAB + 1; k++) gtab[k] = (float) exp (-(double) k * GX / GTAB);
	for (int z = 0; z < 128; z++) zk_tab[z] = pow (TUBE_LIGHT[z], 1 / 2.2);
	run_t = 0; run_frac = 0; load_f = -1; while (nh) frame_pop (); aq_r = aq_w = 0; aq_frac = 0;
	vx_size = 0; if (vx_ok ()) vx_state_init ();   /* (v0.13: the save state's layout, for this game) */
	/* v0.15: switched on - the tube warming up for the time it was off (a first start: cold), the power sound */
	{ char sp[1100]; double off_for = 1e9; pw_stamp_path (sp, sizeof sp); FILE *f = fopen (sp, "r"); long long ts;
	  if (f) { if (fscanf (f, "%lld", &ts) == 1) off_for = (double) (time (NULL) - ts); fclose (f); }
	  if (off_for < 0) off_for = 0;
	  if (!pw_snd_on) { pw_snd_on = pw_wav (sys, "on.wav", &pw_n_on); pw_snd_off = pw_wav (sys, "off.wav", &pw_n_off); }
	  pw_rng = (unsigned) time (NULL) * 2654435761u; pw_file = getenv ("VECTREX_POWER"); pw_seq_seen = -1; pw_poll_n = 0;
	  pw_on = 0; pw_switch (1, off_for); pw_poll (); pw_poll_n = 0; }
	if (opt_is ("vectrex_emu_skip_intro", "on")) skip_boot ();   /* (1.1: as the desktop's Skip the intro - the BIOS run unseen) */
	auto_load ();
	char od[1600];
	if (sys) { snprintf (od, sizeof od, "%s" SEP "vectrex_emu" SEP "overlays", sys); try_overlay_dir (od, gpath); }
	if (!ov) { snprintf (od, sizeof od, "%s" SEP "Overlays", dir); try_overlay_dir (od, gpath); }
#ifndef VX_EMUVR
	if (first) { const char *rv = optv ("vectrex_emu_renderer"); opt_renderer = !strcmp (rv, "CPU only") ? 2 : !strcmp (rv, "GPU") ? 1 : 0;
	  gpu_asked = opt_renderer != 2 ? gpu_request () : 0; if (!gpu_asked) gpu_ok = 0; }   /* (each game: RetroArch sets the context up after this) */
#endif
	if (area_glass () != geo_glass) pic_setup ();   /* (the overlay found: the area it asks for, before RetroArch asks the size) */
	return true;
}
/* (the ROM browser, eject: another game in the slot while running - the card and the flash written back first, as
 *  at the end of a game; RetroArch's own content stays what it loaded) */
static char ej_last[1100]; static int ej_out;
static char sw_next[1100]; static int sw_eject;          /* (a switch asked for: done at the next refresh's start - see retro_run) */
static int game_switch_now (const char *path);
static int game_switch (const char *path) { snprintf (sw_next, sizeof sw_next, "%s", path); return 1; }
static int game_switch_now (const char *path)
{
	emu_wait_ext (); emu_primed = 0;
	if (sd_img[0] && sd_folder[0]) sdc_sync_back (sd_img, sd_folder);
	flash_save (1);
	if (ov) stbi_image_free (ov); ov = NULL; ov_path[0] = 0; ov_made = 0; ov_on = opt_overlay;   /* (the last game's overlay off) */
	{ extern int vt_vb, vt_hb512, vt_multicart, vt_uvm2, vt_uvm2_cart, vt_pitrex; extern const char *vt_flash_file;   /* (the last cartridge's
	                                              boards and modes out: vtest's main only sets them - Vectorblade then Scramble ran Scramble on
	                                              Vectorblade's board: Mine Storm, the owner 2026-11-15) */
	  extern int vt_uvm2_intro, vt_uvm2_go; vt_vb = vt_hb512 = vt_multicart = 0; vt_uvm2 = vt_uvm2_cart = vt_uvm2_intro = vt_uvm2_go = vt_pitrex = 0; vt_flash_file = NULL; }
	const char *sys = NULL; env_cb (RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY, &sys);
	if (!game_open (path, sys, 0)) { fprintf (stderr, "[vectrex_emu] could not load %s\n", path);
		struct retro_message msg = { "Vectrex_EMU: this game could not be loaded", 180 }; env_cb (RETRO_ENVIRONMENT_SET_MESSAGE, &msg); return 0; }
#ifndef VX_EMUVR
	ph_made = 0;                                /* (the phosphor dark: a new game) */
#endif
	struct retro_system_av_info av; retro_get_system_av_info (&av); env_cb (RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO, &av);
	fprintf (stderr, "[vectrex_emu] now in the slot: %s\n", path);
	return 1;
}
static char cur_game[1100];
/* (the desktop's eject: the cartridge out - the Vectrex plays Mine Storm; again: the same one back in) */
static void eject_toggle (void)
{
	if (!ej_out) {
		snprintf (ej_last, sizeof ej_last, "%s", cur_game);
		char p[1200]; snprintf (p, sizeof p, "%s" SEP "Mine Storm.bin", tmpdir); FILE *f = fopen (p, "wb"); if (f) fclose (f);   /* (no cartridge: empty) */
		game_switch (p); sw_eject = 1;
	} else if (ej_last[0]) { game_switch (ej_last); sw_eject = 2; }
}
static void input_names (void)
{
	  static struct retro_input_descriptor d[2 * 13 + 8]; int n = 0;
	for (unsigned p = 0; p < 2; p++) {
		d[n++] = (struct retro_input_descriptor) { p, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y, "Button 1" };
		d[n++] = (struct retro_input_descriptor) { p, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X, "Button 2" };
		d[n++] = (struct retro_input_descriptor) { p, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B, "Button 3" };
		d[n++] = (struct retro_input_descriptor) { p, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A, "Button 4" };
		d[n++] = (struct retro_input_descriptor) { p, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP, "Joystick up (full)" };
		d[n++] = (struct retro_input_descriptor) { p, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN, "Joystick down (full)" };
		d[n++] = (struct retro_input_descriptor) { p, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT, "Joystick left (full)" };
		d[n++] = (struct retro_input_descriptor) { p, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT, "Joystick right (full)" };
		d[n++] = (struct retro_input_descriptor) { p, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_X, "Joystick X" };
		d[n++] = (struct retro_input_descriptor) { p, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_Y, "Joystick Y" };
		d[n++] = (struct retro_input_descriptor) { p, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_X, "Joystick X (right stick)" };
		d[n++] = (struct retro_input_descriptor) { p, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_Y, "Joystick Y (right stick)" };
		if (p == 0) d[n++] = (struct retro_input_descriptor) { p, RETRO_DEVICE_JOYPAD, 0, opt_ov_btn == 2 ? RETRO_DEVICE_ID_JOYPAD_SELECT : RETRO_DEVICE_ID_JOYPAD_START, "Overlay off / on" };
	}
#ifndef VX_EMUVR
	{ const int b = btn_id ("vectrex_emu_btn_browser"); if (b >= 0) d[n++] = (struct retro_input_descriptor) { 0, RETRO_DEVICE_JOYPAD, 0, (unsigned) b, "ROM browser" };
	  const int pz = btn_id ("vectrex_emu_btn_pause"); if (pz >= 0 && pz != b) d[n++] = (struct retro_input_descriptor) { 0, RETRO_DEVICE_JOYPAD, 0, (unsigned) pz, "Pause" }; }
	{ const int r = btn_id ("vectrex_emu_btn_reset"), e = btn_id ("vectrex_emu_btn_eject");   /* (none by default: Ports) */
	  if (r >= 0) d[n++] = (struct retro_input_descriptor) { 0, RETRO_DEVICE_JOYPAD, 0, (unsigned) r, "RESET button" };
	  if (e >= 0 && e != r) d[n++] = (struct retro_input_descriptor) { 0, RETRO_DEVICE_JOYPAD, 0, (unsigned) e, "Eject / reinsert the cartridge" }; }
#endif
	d[n] = (struct retro_input_descriptor) { 0 }; env_cb (RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS, d);
}

bool retro_load_game (const struct retro_game_info *g)
{
	emu_wait_ext (); emu_primed = 0;
	boot_id = VX_TICKS () | 1;   /* (v0.25: a new game booted) */
	if (!g || !g->path) return false;
	const char *sys = NULL; env_cb (RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY, &sys);
#ifdef __ANDROID__
	/* (Android: stderr goes nowhere - the core's messages to <system>/vectrex_emu/vectrex_emu.log, for a game that will
	 *  not start: the BIOS, the overlay found, the speech module) */
	if (sys) { char lp[1100]; snprintf (lp, sizeof lp, "%s/vectrex_emu/vectrex_emu.log", sys); FILE *lf = fopen (lp, "w");
		if (lf) { fflush (stderr); dup2 (fileno (lf), 2); fclose (lf); setvbuf (stderr, NULL, _IONBF, 0);
			fprintf (stderr, "[vectrex_emu] core 1.1, game %s\n", g && g->path ? g->path : "(none)"); } }
#endif
	enum retro_pixel_format pf = RETRO_PIXEL_FORMAT_XRGB8888;
	if (!env_cb (RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &pf)) return false;
	input_names ();
	snprintf (cur_game, sizeof cur_game, "%s", g->path); ej_out = 0;
#ifndef VX_EMUVR
	snprintf (br_game, sizeof br_game, "%s", g->path); br_dir[0] = 0;
#endif
	return game_open (g->path, sys, 1);
}
