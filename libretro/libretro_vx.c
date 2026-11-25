#ifndef _WIN32
#define _GNU_SOURCE                             /* (dladdr: the core pins itself, see retro_init) */
#endif
#if defined(__APPLE__)
#include <TargetConditionals.h>
#define VX_IOS TARGET_OS_IPHONE                 /* (iOS / iPadOS: the RetroArch app for iPhone and iPad) */
#else
#define VX_IOS 0
#endif
#if !defined(VX_APP_ANDROID)
#define VX_DECK_AUTO 1                          /* (the Control deck's auto, every RetroArch: the slim pop-up on a real 4:3 screen, else off - the apps choose their own) */
#else
#define VX_DECK_AUTO 0
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
 * slower than REST_SPEED: corners, dots, stars) round spots, brighter the longer they rested (dot_light: the Dot Test
 * photos), with a faint halo (Corner glow); the dwells inside strokes found as
 * dwell_rests does; brightness bending (the picture grows a little with the beam's load); the hum wobble applied
 * after the tube's map, as vecrender does. The afterglow is the measured one (PHOS_A, at the owner's time scale 0.6):
 * what is no longer lit glows away in two parts. The screen rate option (90 Hz by default, for the Valve Index): each
 * retro_run is one refresh, the emulator runs whole game frames until the emulated time has caught up, and the sound
 * goes out evenly through a queue. Smooth motion (vectrex_emu.py smooth_frame): each refresh shows the moment one game
 * frame and one refresh back, its lines between the two game frames around it, stroke by stroke (smooth.c). A 3D
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
#elif defined(__clang__) && defined(__APPLE__)        /* (Mach-O: a segment and a section) */
#define VXS_ON _Pragma ("clang section data=\"__DATA,vxd\" bss=\"__DATA,vxb\"")
#define VXS_OFF _Pragma ("clang section data=\"\" bss=\"\"")
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
#include <float.h>
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
#define STBI_ONLY_JPEG                              /* (the games list's pictures: PNG or JPEG - the shipped ones JPEG) */
#include "stb_image.h"
#include "genart.c"                                 /* (the library's pictures for a game with none: a cartridge, a box - library.c, the apps) */

#define PW 624                          /* one eye's picture: the overlay sheet, 162 x 207 mm */
#define PH 800
#define SHEET_H (0.924 * ALG_MAX_Y)     /* the sheet in emulator units (vecrender.py ART_TUBE_K) */
#define SHEET_W (SHEET_H * 162.0 / 207.0)
#include "glass_cut_cad_lut.h"               /* (route A: GLASS_CAD_W / _H and glass_cut_cad[360]) */
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
 * imperfections (tubemap_tab.h, from tubemap.py), bilinear between the table's points - x the Tube geometry strength.
 * Then the glass's curve undone (vecrender.py glass_flat, Glass curvature: 1 as seen on a Vectrex, 0 even squares): the
 * tube's face curves away towards its edges, a point a distance s along it shows at R sin (s / R) - R 97 mm across,
 * 205 mm up and down; undone with strength g: R / g. Only the lines (and the light pen): the glass, overlay and case stay */
#include "tubemap_tab.h"
#ifdef _OPENMP
#include <omp.h>
#endif
#define TUBE_SEG 1200.0                     /* (lines longer than this, emulator units, are bent in pieces) */
static int opt_geom = 1;
static double opt_geom_k = 1.0, opt_glass_g;   /* (the strength; the glass's curve undone: 1 - Glass curvature) */
#define GLASS_RX_U (97.0 / (103.5 / 0.924))   /* (the face's radii in half picture heights: mm / MM_UNIT) */
#define GLASS_RY_U (205.0 / (103.5 / 0.924))
#define GLASS_T_MAX 0.95                    /* (past 95 % of the radius: straight on with the slope there) */
static double glass_flat (double p, double r0)
{
	const double R = r0 / opt_glass_g, t = p / R, tc = t < -GLASS_T_MAX ? -GLASS_T_MAX : t > GLASS_T_MAX ? GLASS_T_MAX : t;
	return R * (asin (tc) + (t - tc) / sqrt (1 - GLASS_T_MAX * GLASS_T_MAX));
}
#include "unit.c"
static void tube_pt (double x, double y, double *X, double *Y)
{
	const double XM = ALG_MAX_X / 2.0, YM = ALG_MAX_Y / 2.0;
	double u = (x - XM) / TU, v = (y - YM) / TU;
	if (uw_on) unit_wp (&u, &v);                 /* (this Vectrex's own picture shape: unit.c) */
	double Xu = LIN_A * u + LIN_B, Yv = LIN_C * v + LIN_D;
	if (opt_geom && opt_geom_k > 0) {
		const double SU = (TM_NU - 1) / (TM_U1 - TM_U0), SV = (TM_NV - 1) / (TM_V1 - TM_V0);
		double fu = (u - TM_U0) * SU, fv = (v - TM_V0) * SV;
		if (fu < 0) fu = 0; if (fu > TM_NU - 1.000001) fu = TM_NU - 1.000001;
		if (fv < 0) fv = 0; if (fv > TM_NV - 1.000001) fv = TM_NV - 1.000001;
		const int i = (int) fu, j = (int) fv, k = j * TM_NU + i; const double a = fu - i, b = fv - j;
		const short *DX = TM_D, *DY = TM_D + TM_NU * TM_NV;
		Xu += ((DX[k] * (1 - a) + DX[k + 1] * a) * (1 - b) + (DX[k + TM_NU] * (1 - a) + DX[k + TM_NU + 1] * a) * b) * TM_Q * opt_geom_k;
		Yv += ((DY[k] * (1 - a) + DY[k + 1] * a) * (1 - b) + (DY[k + TM_NU] * (1 - a) + DY[k + TM_NU + 1] * a) * b) * TM_Q * opt_geom_k;
	}
	if (opt_glass_g > 0) { Xu = glass_flat (Xu, GLASS_RX_U); Yv = glass_flat (Yv, GLASS_RY_U); }
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
static int opt_cad;                         /* (route A: 1 = the owner's CAD console - its window and glass edge, glass_cut_cad_lut.h) */
static retro_environment_t env_cb;
static retro_video_refresh_t video_cb;
static retro_audio_sample_batch_t audio_cb;
static retro_input_poll_t poll_cb;
static retro_input_state_t input_cb;
static int opt_overlay = 0, opt_imager = 2, opt_swap = 0, opt_glow = 0, opt_after = 1;
static int opt_res = 1, opt_wob = 50, opt_buzz = 1, opt_spk = 1, opt_shift = 0; static double opt_sigma = 0.55;
static int opt_fill;   /* (Fill skipped lines: off - as a real Vectrex) */
static double opt_wob_k = 1, opt_bend_k = 1, opt_buzz_k = 1, opt_voice_k = 1, opt_motor_k = 1, opt_jack_k = 1; static int opt_jack = 1;   /* (2026-11-23: the desktop's sliders - Tube, Sound, Ports) */
static int opt_rate = 60, opt_smooth = 1, opt_halo = 1, opt_bend = 1, opt_dwell = 1;   /* (v0.9) */
static int vx_beam;                          /* (the beam on the phosphor: the picture is the light of the spot's own path - bt2_prepare; not EmuVR's flavour) */
static int bt_gpu;                           /* (.. as light on the GPU - gpu_phos.c: pieces add, the glass's glow; else the CPU's display values) */
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
static int size_auto = 1, scr_short, scr_new, scr_w, scr_h, geo_sw = -1, geo_sh = -1;   /* (scr_w / scr_h: the screen as it stands, 0 unknown) */   /* (Picture size auto: the screen's short side - measured at the GPU's start) */
static const char *optv (const char *key);
static void scr_file (char *o, size_t n)
{ const char *sv = NULL; env_cb (RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY, &sv); if (sv && *sv) snprintf (o, n, "%s" SEP "vectrex_emu_screen.txt", sv); else o[0] = 0; }
static void scr_load (void)
{ char f[1200]; scr_file (f, sizeof f); FILE *t = f[0] ? fopen (f, "r") : NULL; int v = 0; if (t) { if (fscanf (t, "%d", &v) != 1) v = 0; fclose (t); }
  if (v >= 240 && v <= 4320) scr_short = v; }
uint32_t *dk_cb;
static int dk_pop, dk_y, dk_shown;   /* (the pop-up deck: over the picture's bottom, dk_y its top in the block; shown now) */
static int deck_auto;                            /* (Control deck auto: the pop-up on a 4:3 screen, else off - once the screen is known) */
static int deck_auto_mode (void);
static int opt_deck_look = 1;                   /* (the control deck: 1 the modern look - the app's panels, 0 the classic strip) */
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
static void auto_path (char *o, size_t n) { snprintf (o, n, "%s" SEP "vectrex_emu_auto_overlay.txt", tmp_dir ()); }   /* (EmuVR's sheet
                                                file: the overlay on / off kept beside it; RetroArch keeps it as the option) */
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
	static int stereo_written = -1;   /* (the 3D Imager's eye pair said in so many words - the mod need not guess it from the frame's shape) */
	if (st == sheet_written && sel_count == sel_written && boot_id == boot_written && !strcmp (look, look_written) && stereo == stereo_written) return;
	sheet_written = st; sel_written = sel_count; boot_written = boot_id; snprintf (look_written, sizeof look_written, "%s", look); stereo_written = stereo;
	FILE *f = fopen (sheet_file, "w"); if (!f) return;
	if (st != -2) fprintf (f, "%s|%d\n", ov_path, st); else fprintf (f, "\n");
	fprintf (f, "sel %d\nboot %u\nlook %s\nstereo %d\n", sel_count, boot_id, look, stereo ? 1 : 0);   /* (stereo 1: the frame is the eye pair, left eye on the left) */
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
static volatile unsigned wd_runs;   /* (Android's watchdog: the refreshes run - see wd_start) */
static void wd_start (void), wd_stop (void), wd_thr_set (void);
static int sp_ok, dk_p2;   /* (deck.c) */
/* (RetroArch's netplay: one Vectrex on both machines, kept the same by rewinding with save states. Seen by the reason
 *  RetroArch gives a save - GET_SAVESTATE_CONTEXT 72, ROLLBACK_NETPLAY 3; while it runs, nothing one side does alone may
 *  change the emulation: the ROM browser, eject, RESET, pause, the deck's power and port 2, the USB pad read directly) */
static long net_seen = -1000000, net_runs;
static int ls_state, ls_reset_req;          /* (netls.c: a UVMC2 / PiTrex game online in lockstep - the same holds then) */
static int net_on (void) { return net_runs - net_seen < 180 || ls_state; }
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
/* (2026-11-18: the keyboard as the desktop's - Controls > Keyboard as on the PC: held keys and pressed shortcuts, from
 *  RetroArch's keyboard callback, used at the next refresh; Ctrl+K: the keys to a UVMC2 / PiTrex instead, as the desktop) */
enum { KB_UP = 1, KB_DOWN = 2, KB_LEFT = 4, KB_RIGHT = 8, KB_B1 = 16, KB_B2 = 32, KB_B3 = 64, KB_B4 = 128, KB_SHIFT = 256,
       KB_ENTER = 512, KB_BACK = 1024, KB_CTRL = 2048 };
enum { KE_PAUSE = 1, KE_BROWSER = 2, KE_OVERLAY = 4, KE_RESET = 8, KE_EJECT = 16, KE_VOLUP = 32, KE_VOLDN = 64, KE_ESC = 128 };
static int kb_on, km_typing; static volatile unsigned kb_held, kb_ev;
static int br_open;                                 /* (browser.c) */ static int lib_on; static void br_show (int on);   /* (library.c) */
#ifdef VX_EMUVR
static void br_show (int on) { (void) on; }        /* (EmuVR: no ROM browser on the picture - EmuVR's own shelf) */
#endif
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
static struct retro_variable vars[] = {   /* (2026-11-25, D1: the overlay's and the bezel's look - scratches, plastic glow, case mirror / wear, the tube face's grey - read by
	                                         the mod from its own UserData file; the sheet's "look" line keeps their defaults for older mods) */
	{ "vectrex_emu_overlay", "Automatic overlay (the game's own put on at each game - SELECT takes it off / puts it on); off|on" },
	{ "vectrex_emu_screen_light", "Screen light in EmuVR (all of the picture alike - the trail ends up at this / the dots level); 2.4x|1x|1.5x|2x|2.8x|3x|4x" },
	{ "vectrex_emu_dot_level", "Screen light in EmuVR: what is the brightest (the beam's dots and corners at 2 - lines below them by their speed); 2|1.5|2.5|3|4" },
	{ "vectrex_emu_imager", "3D Imager; vr side by side|colour|left eye|right eye|off" },
	{ "vectrex_emu_swap_eyes", "3D Imager: swap eyes; off|on" },
	{ "vectrex_emu_eye_shift", "3D Imager: right eye's picture left (-) or right (+), pixels; 0|-10|-20|-30|-40|-50|-60|-70|-80|-90|-100|-110|-120|-130|-140|-150|-160|10|20|30|40|50|60|70|80|90|100|110|120|130|140|150|160" },
	{ "vectrex_emu_beam", "Beam width; thin|medium|wide" },
	{ "vectrex_emu_area", "Picture area (from the next game start); glass window 145.7x188.5 mm|overlay sheet 162x207 mm" },
	{ "vectrex_emu_console", "Console model: its glass window (from the next game start); auto|classic|owner's CAD model" },   /* (route A, 2026-11-25: auto = as the mod says) */
	{ "vectrex_emu_renderer", "Picture (from the next game start): GPU - as the PC's - or the CPU's (the picture before); GPU|CPU only" },
	{ "vectrex_emu_res", "Picture size (from the next game start); 1x 624x800|2x 1248x1600|3x 1872x2400|4x 2496x3200" },
	{ "vectrex_emu_rate", "Screen rate (from the next game start); 90 Hz|72 Hz|80 Hz|120 Hz|144 Hz|60 Hz|50 Hz (game)" },
	{ "vectrex_emu_smooth", "Smooth motion (not at 50 Hz); on|off" },
	{ "vectrex_emu_fill", "Fill skipped lines (not as a real Vectrex); off|on" },
	{ "vectrex_emu_corner_glow", "Corner glow; on|off" },
	{ "vectrex_emu_bending", "Brightness bending; on|off" },
	{ "vectrex_emu_geometry", "Tube geometry (the owner's measured tube); on|off" },
	{ "vectrex_emu_unit", "Vectrex unit (the owner's measured one: as his console draws); owner's Vectrex (measured)|ideal" },
	{ "vectrex_emu_geometry_k", "Tube geometry strength (x1: the owner's Vectrex, x0: ideal); x1|x0|x0.25|x0.5|x0.75|x1.25|x1.5|x1.75|x2" },
	{ "vectrex_emu_glass_curve", "Glass curvature (1: as on a Vectrex, 0: even squares); 1.00|0.90|0.80|0.70|0.60|0.50|0.40|0.30|0.20|0.10|0.00" },
	{ "vectrex_emu_smooth_bends", "Smooth curves at bends (fine detail, off: VR hardly shows it); off|on" },
	{ "vectrex_emu_white", "White balance; 12000 K (real Vectrex)|9300 K|16000 K|6504 K (neutral)" },
	{ "vectrex_emu_power_fx", "Switching on / off (tube warm-up, switch-off zap); on|off" },
	{ "vectrex_emu_glow", "Phosphor glow (bloom); off|on|strong" },
	{ "vectrex_emu_afterglow", "Phosphor trail (afterglow); on|long (real Vectrex)|short|off" },
	{ "vectrex_emu_brightness", "Brightness (VR: the screen behind its glass looks dimmer than a monitor); 1x (measured)|1.25x|1.5x|1.75x|2x" },
	{ "vectrex_emu_trail_bright", "Phosphor trail brightness (VR headsets lose faint light); 1x (real)|0.5x|0.75x|1.5x|2x|3x|4x" },
	{ "vectrex_emu_look", "Look (Bright: all lines pushed up to white - bright vectors on a dim screen; Vectrex: as a real Vectrex - dots and slow lines stand out, wants a bright screen); bright|vectrex" },
	{ "vectrex_emu_corner_dots", "Corner dots (the beam resting); on|off" },
	{ "vectrex_emu_dwell", "Dwell spots inside strokes; on|off" },
	{ "vectrex_emu_speed_bright", "Fast sweeps fainter; on|off" },
	{ "vectrex_emu_wobble", "Mains hum wobble; 50 Hz|60 Hz|off" },
	{ "vectrex_emu_buzz", "Vectrex buzz; on|off" },
	{ "vectrex_emu_speaker", "Vectrex speaker sound; on|off" },
	{ "vectrex_emu_buzz_vol", "Buzz volume; 1x|0x|0.25x|0.5x|0.75x|1.25x|1.5x|1.75x|2x" },   /* (EmuVR: all the sound - the owner, 2026-11-25; values as the desktop table's) */
	{ "vectrex_emu_voice_vol", "Speech module volume (VecVoice, VecVox); 100%|0%|25%|50%|75%|125%|150%|175%|200%" },
	{ "vectrex_emu_motor_vol", "3D Imager disc motor sound; 50%|0%|10%|20%|30%|40%|60%|70%|80%|90%|100%" },
	{ "vectrex_emu_jack", "UVMC2 headphone jack (clean: line out); clean|through the speaker|off" },
	{ "vectrex_emu_jack_vol", "UVMC2 headphone jack volume; 100%|0%|25%|50%|75%|125%|150%|175%|200%" },
	{ NULL, NULL } };
#else
#define VX_RATE_DEF 60
#define VX_TRAIL_DEF 1.5                    /* (the trail: the owner's 1.5x on black - now the same everywhere) */
/* (1.0: the options in submenus - RetroArch's core options v2, its categories; older frontends get the same list flat,
 *  each name led by its submenu's) */
struct vx_opt { const char *key, *cat, *desc, *info, *vals; };
static const struct vx_opt OPTS[] = {
	/* (2026-11-23: each submenu's rows in the order of the desktop's Config tab - Display, Overlay, Case and Room light, Tube,
	 *  Ports, Sound, Unit, Circuit - with its labels, ranges and defaults; rows only RetroArch or the apps have after them) */
	{ "vectrex_emu_profile", "video", "Quality profile",
	  "Sets the options left on auto. Auto: the best this device runs at full speed, measured once (GPU; CPU only: Medium).",
	  "auto|basic|low|medium|high|ultra" },
	{ "vectrex_emu_palette", "video", "Colours", "The deck's, the menus' and the buttons' colour: a deeper blue, or green. An app's own menus: from its next start.", "blue|green" },
	{ "vectrex_emu_gamelist", "video", "Game list", "Library: the games as tiles - cartridges, boxes or overlays, a search, categories. Simple: the plain list of the roms folder.", "library|simple" },
	{ "vectrex_emu_rate", "video", "Screen rate", "50 Hz needs a screen that can run at 50 Hz.",
	  "60 Hz|50 Hz (game)|72 Hz|90 Hz|120 Hz|144 Hz" },
	{ "vectrex_emu_smooth", "video", "Smooth motion", "Every refresh a picture of its own: the lines where they are between two game frames (not at 50 Hz).", "auto|on|off" },
	{ "vectrex_emu_fill", "video", "Fill skipped lines", "A line a game leaves out of a frame stays lit until the game draws it again. Steadier than a real Vectrex.", "off|on" },
	{ "vectrex_emu_rotate", "video", "Rotation (tate)", "Turns the picture for playing with the device held upright.",
#if VX_IOS                                          /* (an iPhone / iPad: its screen upright already - the picture as it is) */
	  "off|90 (turn the device right)|270 (turn the device left)|180" },
#else
	  "90 (turn the device right)|off|270 (turn the device left)|180" },
#endif
	{ "vectrex_emu_size", "video", "Picture size",
	  "Auto: the screen's height, as RetroArch shows it (the sharpest lines, with Integer Scale on). Or a height of its own.",
	  "auto|800 px|480 px|540 px|600 px|720 px|768 px|900 px|960 px|1080 px|1200 px|1440 px|1600 px|2160 px" },
	{ "vectrex_emu_area", "video", "Picture area",
	  "Auto: the whole sheet while an overlay or the case is shown, else the glass window (a larger picture).",
	  "auto|glass window|overlay sheet" },
#if !VX_IOS                                         /* (an iPhone / iPad: the block takes the screen's own shape - no choice) */
	{ "vectrex_emu_shape", "video", "Screen shape", "With the control deck: the picture and the deck fitted to a 4:3 or a 16:9 screen.", "4:3|16:9" },
#endif
#if VX_DECK_AUTO                                 /* (every RetroArch: auto - the slim pop-up on a real 4:3 screen, as the apps) */
	{ "vectrex_emu_deck", "video", "Control deck", "The desktop's deck below the picture (mouse or touch). Slim: one low row. Pop-up: over the picture, tap its bottom. Auto: pop-up where the deck does not fit below the picture (4:3, square), else off.", "auto|off|full|slim|pop-up" },
#else
	{ "vectrex_emu_deck", "video", "Control deck", "The desktop's deck below the picture (mouse or touch). Slim: one low row. Pop-up: over the picture, tap its bottom.", "off|full|slim|pop-up" },
#endif
#if defined(__ANDROID__) || VX_IOS                  /* (a phone, a handheld: the app's panels by default - easier to read and hit on a small screen) */
	{ "vectrex_emu_deck_look", "video", "Control deck look", "Modern: the app's panels, a word under each. Classic: the desktop's strip.", "modern|classic" },
#else
	{ "vectrex_emu_deck_look", "video", "Control deck look", "Modern: the app's panels, a word under each. Classic: the desktop's strip.", "classic|modern" },
#endif
	{ "vectrex_emu_renderer", "video", "Renderer (next game start)", "GPU adds the glass and case effects (video driver gl / glcore).",
	  "auto|GPU|CPU only" },
	{ "vectrex_emu_power_fx", "video", "Switching on / off", "Tube warm-up and switch-off effects.", "on|off" },
	{ "vectrex_emu_btn_reset", "controls", "RESET button", "A RetroPad button for the Vectrex's RESET button.", "L|off|R|L2|R2|L3|R3" },
	{ "vectrex_emu_btn_browser", "controls", "ROM browser button", "A RetroPad button that opens and closes the ROM browser.", "L3|SELECT|off|L|R|L2|R2|R3" },
	{ "vectrex_emu_btn_pause", "controls", "Pause button", "A RetroPad button that pauses and resumes the game.", "L2|off|L|R|R2|L3|R3" },
#if VX_IOS                                          /* (an iPhone / iPad: R - the keyboard's E, the desktop's Ctrl+E) */
	{ "vectrex_emu_btn_eject", "controls", "Eject button", "A RetroPad button: the cartridge out (Mine Storm), again: back in.", "R|off|L|L2|R2|L3|R3" },
#else
	{ "vectrex_emu_btn_eject", "controls", "Eject button", "A RetroPad button: the cartridge out (Mine Storm), again: back in.", "off|L|R|L2|R2|L3|R3" },
#endif
#if VX_IOS                                          /* (an iPhone / iPad: a controller does not turn with the screen) */
	{ "vectrex_emu_rotate_stick", "controls", "Controls turned with the picture", "In tate the stick and buttons keep their places.", "off|on" },
	{ "vectrex_emu_keys", "controls", "Keyboard as on the PC", "Arrows the stick (Shift: half), A W S D buttons 1-4, P pause, L ROM browser, O overlay, Ctrl+R RESET, Ctrl+E eject, Page Up / Down volume, Ctrl+K typing to a UVMC2 / PiTrex.", "on|off" },
#else
	{ "vectrex_emu_rotate_stick", "controls", "Controls turned with the picture", "In tate the stick and buttons keep their places.", "on|off" },
	{ "vectrex_emu_keys", "controls", "Keyboard as on the PC", "Arrows the stick (Shift: half), A W S D buttons 1-4, P pause, L ROM browser, O overlay, Ctrl+R RESET, Ctrl+E eject, Page Up / Down volume, Ctrl+K typing to a UVMC2 / PiTrex.", "off|on" },
#endif
	{ "vectrex_emu_overlay", "overlay", "Overlay", "The game's printed plastic sheet in front of the screen. START switches it (any button: Quick Menu > Controls).", "on|off" },
	{ "vectrex_emu_ov_tint", "overlay", "Tint strength", "How strongly the coloured plastic colours the lines.", "1x|0x|0.25x|0.5x|0.75x|1.25x|1.5x|1.75x|2x" },
	{ "vectrex_emu_ov_print", "overlay", "Printed art brightness", "How bright the sheet's printed artwork shows in the room's light.", "1x|0x|0.25x|0.5x|0.75x|1.25x|1.5x|1.75x|2x" },
	{ "vectrex_emu_parallax", "overlay", "Parallax", "The sheet sits a little in front of the tube: seen from the usual height the picture sits a little higher on it.", "auto|on|off" },
	{ "vectrex_emu_ov_glow", "overlay", "Plastic glow", "The plastic spreads a little of the light: a soft glow in the sheet's colour around bright lines (GPU). 0x: off.", "auto|0x|0.25x|0.5x|0.75x|1x|1.25x|1.5x|1.75x|2x" },
	{ "vectrex_emu_scratches", "overlay", "Scratches", "Decades of handling: the vectors light up the sheet's scratches (GPU).", "auto|on|off" },
	{ "vectrex_emu_scr_wear", "overlay", "Scratches: wear", "All the scratches together: from almost new to twice as worn.", "1x|0x|0.25x|0.5x|0.75x|1.25x|1.5x|1.75x|2x" },
	{ "vectrex_emu_scr_fine", "overlay", "Fine scratches", "The matte buff.", "1x|0x|0.25x|0.5x|0.75x|1.25x|1.5x|1.75x|2x" },
	{ "vectrex_emu_scr_medium", "overlay", "Medium scratches", NULL, "1x|0x|0.25x|0.5x|0.75x|1.25x|1.5x|1.75x|2x" },
	{ "vectrex_emu_scr_damage", "overlay", "Damage", "The few deep ones, through the colour.", "0.1x|0x|0.25x|0.5x|0.75x|1x|1.25x|1.5x|1.75x|2x" },
	{ "vectrex_emu_scr_scrib", "overlay", "Scratches: scribbles", "The share of curly ones.", "1x|0x|0.25x|0.5x|0.75x|1.25x|1.5x|1.75x|2x" },
	{ "vectrex_emu_scr_glint", "overlay", "Glint strength", "How strongly the scratches catch the light.", "1x|0x|0.25x|0.5x|0.75x|1.25x|1.5x|1.75x|2x" },
	{ "vectrex_emu_scr_reach", "overlay", "Scratches: glint reach", "Low: only close to the lines. High: farther out.", "1x|0x|0.25x|0.5x|0.75x|1.25x|1.5x|1.75x|2x" },
	{ "vectrex_emu_scr_seed", "overlay", "Scratch pattern", "Another random set of scratches.", "1|2|3|4|5|6|7|8|9|10" },
	{ "vectrex_emu_case", "case", "Case", "The Vectrex's black plastic case round the tube, without an overlay.", "auto|on|off" },
	{ "vectrex_emu_case_refl", "case", "Case reflections", "How strongly the picture is mirrored in the case's walls. 0x: none.", "auto|0x|0.25x|0.5x|0.75x|1x|1.25x|1.5x|1.75x|2x" },
	{ "vectrex_emu_case_wear", "case", "Case wear", "Scratches, the band the overlays rubbed. 0x: none.", "auto|0x|0.25x|0.5x|0.75x|1x|1.25x|1.5x|1.75x|2x" },
	{ "vectrex_emu_room", "case", "Room light", "How much the room lights up the overlay, the glass and the case.", "1x (evening)|off (dark room)|0.5x|0.75x|1.25x|1.5x|1.75x|2x" },
	{ "vectrex_emu_room_glass", "case", "Glass catches room light", "Without an overlay the glass turns grey in the room's light. Off: always black.", "auto|on|off" },
	{ "vectrex_emu_ceiling", "case", "Ceiling light on the glass", "A ceiling light mirrored in the glass: its strength. 0x: off.", "auto|0x|0.25x|0.5x|0.75x|1x|1.25x|1.5x|1.75x|2x" },
	{ "vectrex_emu_ceiling_ov", "case", "Ceiling light on the overlay", "Its reflection on the sheet: strength (GPU). 0x: off.", "auto|0x|0.25x|0.5x|0.75x|1x|1.25x|1.5x|1.75x|2x" },
	{ "vectrex_emu_lamp_x", "case", "Light position: left / right", NULL, "0%|10%|20%|30%|40%|50%|60%|70%|80%|90%|100%" },
	{ "vectrex_emu_lamp_y", "case", "Light position: high / low", NULL, "27%|0%|10%|20%|30%|40%|50%|60%|70%|80%|90%|100%" },
#if defined(__ANDROID__)                            /* (a handheld: the lines lifted and wider by default, as the apps on a phone) */
	{ "vectrex_emu_tube_look", "tube", "Look", "Vectrex: as a real Vectrex. Bright: brighter, wider lines - for a small screen.", "Bright|Vectrex" },
#else
	{ "vectrex_emu_tube_look", "tube", "Look", "Vectrex: as a real Vectrex. Bright: brighter, wider lines - for a small screen.", "Vectrex|Bright" },
#endif
	{ "vectrex_emu_afterglow", "tube", "Afterglow", "The phosphor keeps glowing after the beam has passed: its length. 1x is the measured length; 0.6x was matched to a real Vectrex side by side.",
	  "0.6x|off|0.05x|0.1x|0.2x|0.3x|0.4x|0.5x|0.7x|0.8x|0.9x|1x|1.1x|1.2x" },
	{ "vectrex_emu_trail_bright", "tube", "Afterglow brightness", NULL, "1x|0.5x|0.75x|1.25x|1.5x|2x|3x" },
	{ "vectrex_emu_white", "tube", "White balance", "The lines' colour temperature: 12000 K the bluish white of a real Vectrex, 6504 K neutral. The overlay and the case keep theirs.",
	  "12000 K|5600 K|6000 K|6504 K|7000 K|7500 K|8000 K|9300 K|10000 K|11000 K|13000 K|14000 K|16000 K" },
	{ "vectrex_emu_geometry", "tube", "Tube geometry", "Where the beam really lands, measured on a real Vectrex: the picture a little narrower towards the sides, lines bowed slightly.", "auto|on|off" },
	{ "vectrex_emu_geometry_k", "tube", "Tube geometry strength", "0x: an ideal, straight picture. 1x: the measured Vectrex.", "1x|0x|0.25x|0.5x|0.75x|1.25x|1.5x|1.75x|2x" },
	{ "vectrex_emu_glass_curve", "tube", "Glass curvature", "The tube's face curves away towards its edges. 1: as seen on a Vectrex (the overlays fit it). 0: even squares.", "1.00|0.90|0.80|0.70|0.60|0.50|0.40|0.30|0.20|0.10|0.00" },
	{ "vectrex_emu_glass_edge", "tube", "Glass edge", "The tube's glass ends in rounded corners with a soft edge.", "auto|on|off" },
	{ "vectrex_emu_dither", "tube", "Dither", "The soft glows fade out smoothly instead of in steps (GPU).", "auto|on|off" },
	{ "vectrex_emu_wobble", "tube", "Mains hum wobble", "The picture sways a tiny bit with the mains: 50 or 60 Hz.", "50 Hz|60 Hz|off" },
	{ "vectrex_emu_wobble_k", "tube", "Wobble strength", NULL, "1x|0x|0.25x|0.5x|0.75x|1.25x|1.5x|1.75x|2x" },
	{ "vectrex_emu_bending", "tube", "Brightness bending", "A busy, bright picture grows a little.", "on|off" },
	{ "vectrex_emu_bending_k", "tube", "Bending strength", NULL, "1x|0x|0.25x|0.5x|0.75x|1.25x|1.5x|1.75x|2x" },
	{ "vectrex_emu_cart_hw", "system", "Cartridge hardware", "Auto: by the ROM's size (next game start).",
	  "auto|off (plain 32 KB)|pb6 (64 KB)|vb (Vectorblade 4 x 48 KB)|hb512 (512 KB flash)|multi (Multi-Cart)|ram (8 KB RAM)" },
	{ "vectrex_emu_port2", "system", "Controller port 2", "Auto: a game that needs the 3D Imager, the light pen or a speech module gets it (next game start).",
	  "auto|none|3D Imager|light pen|VecVoice|VecVox" },
	{ "vectrex_emu_imager", "system", "3D Imager", "Colour: both eyes' pictures at once, as seen without the goggles. Left eye / right eye: only that eye's pictures, no double image.", "colour|left eye|right eye|off" },
	{ "vectrex_emu_usb_hid", "system", "USB keyboard and mouse (UVMC2, PiTrex)", "RetroArch's keyboard and mouse as the cartridge's USB devices. Keys: Game Focus (Scroll Lock).", "on|off" },
	{ "vectrex_emu_jack", "system", "UVMC2 headphone jack", "Clean: line out. Through the speaker: the Vectrex's own sound.", "clean|through the speaker|off" },
	{ "vectrex_emu_jack_vol", "system", "UVMC2 headphone jack volume", NULL, "100%|0%|25%|50%|75%|125%|150%|175%|200%" },
	{ "vectrex_emu_uvm2_mode", "system", "UVMC2 emulation", "Exact: as the cartridge, cycle for cycle. Fast: the same picture, much less work. Dev: Fast with an ideal beam.", "fast|exact|dev" },
	{ "vectrex_emu_threaded", "system", "Threaded emulation (UVMC2, PiTrex)", "Emulates while drawing: full speed on handhelds, one frame more input delay.", "on|off" },
	{ "vectrex_emu_buzz", "audio", "Vectrex buzz", "The famous Vectrex buzz from the speaker.", "on|off" },
	{ "vectrex_emu_buzz_vol", "audio", "Buzz volume", NULL, "1x|0x|0.25x|0.5x|0.75x|1.25x|1.5x|1.75x|2x" },
	{ "vectrex_emu_speaker", "audio", "Vectrex speaker", "All sound through the small built-in speaker: no deep bass, a little harsh.", "on|off" },
	{ "vectrex_emu_voice_vol", "audio", "Speech module volume", "VecVoice and VecVox: 100 % is the MiSTer core's Normal level.", "100%|0%|25%|50%|75%|125%|150%|175%|200%" },
	{ "vectrex_emu_motor_vol", "audio", "Disc motor sound", "The 3D Imager's disc motor while the goggles spin. 0 %: off.", "50%|0%|10%|20%|30%|40%|60%|70%|80%|90%|100%" },
	{ "vectrex_emu_unit", "unit", "Unit", "Which Vectrex: the owner's (measured), an ideal one, a random or a cranky one (each number its own).", "owner's Vectrex (measured)|ideal|random 1|random 2|random 3|random 4|random 5|random 6|random 7|random 8|random 9|random 10|cranky 1|cranky 2|cranky 3|cranky 4|cranky 5|cranky 6|cranky 7|cranky 8|cranky 9|cranky 10" },
	{ "vectrex_emu_skip_intro", "unit", "Skip the Vectrex intro", "The BIOS's start screen runs unseen and the cartridge starts straight away (next game start).", "off|on" },
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
	{ "vectrex_emu_unit_xrate", "circuit", NULL, NULL, "@" },
	{ "vectrex_emu_unit_yrate", "circuit", NULL, NULL, "@" },
	{ "vectrex_emu_unit_gap", "circuit", NULL, NULL, "@" },
	{ NULL, NULL, NULL, NULL, NULL } };
static const char *const OPT_CATS[][3] = {   /* (RetroArch's own kind of menus, the most used first - the owner, 2026-11-15) */
	{ "video", "Video", "Quality, colours, game list, screen rate, smooth motion, rotation, picture size, control deck" },
	{ "controls", "Controls", "Buttons for the ROM browser, pause, RESET and eject; tate; the keyboard" },
	{ "overlay", "Overlay", "The sheet: tint, print, plastic and scratches, parallax" },
	{ "audio", "Audio", "The Vectrex's buzz and speaker, the speech module, the 3D Imager's motor" },
	{ "tube", "Tube", "The look, the afterglow, white balance, the tube's shape and glass, mains hum and bending" },
	{ "case", "Case & Room light", "The plastic round the glass, the room's light, the ceiling light" },
	{ "system", "System", "The cartridge hardware, port 2, USB keyboard and mouse, UVMC2 and PiTrex" },
	{ "unit", "Vectrex Unit", "Which Vectrex, the intro; the knob, the pots and the yoke; the picture's shape" },
	{ "circuit", "Vectrex Circuit", "The service trim pots; its parts: zero reference, beam, zeroing, lag, DAC, rates" } };
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

#ifndef VX_EMUVR
/* (2026-11-22, the beam on the phosphor: the picture's and the unit's settings - Tube, Overlay, Case & Room light, Unit, Circuit -
 *  back to their defaults once, as a clean install (the owner). A mark in the system folder says it is done; the frontend takes
 *  each value through RETRO_ENVIRONMENT_SET_VARIABLE - RetroArch 1.17 on, the own apps) */
#define VX_OPT_EPOCH 3
static void opts_reset_once (void)
{
	const char *sys = NULL; char f[1024]; int have = 0;
	if (!env_cb (RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY, &sys) || !sys || !sys[0]) return;
	snprintf (f, sizeof f, "%s/vectrex_emu_settings_epoch.txt", sys);
	{ FILE *fp = fopen (f, "r"); if (fp) { if (fscanf (fp, "%d", &have) != 1) have = 0; fclose (fp); } }
	if (have >= VX_OPT_EPOCH) return;
	int n = 0;
	if (have < 2) for (size_t i = 0; OPTS[i].key; i++) {
		const char *c = OPTS[i].cat; if (strcmp (c, "tube") && strcmp (c, "overlay") && strcmp (c, "case") && strcmp (c, "unit") && strcmp (c, "circuit")) continue;
		if (!strcmp (OPTS[i].key, "vectrex_emu_overlay") || !strcmp (OPTS[i].key, "vectrex_emu_unit") || !strcmp (OPTS[i].key, "vectrex_emu_skip_intro")) continue;   /* (the overlay on / off, which Vectrex, the intro: a choice, not a tuning) */
		static char d[128]; const char *v = OPTS[i].vals[0] == '@' ? "auto" : OPTS[i].vals; size_t k = 0; while (v[k] && v[k] != '|' && k < sizeof d - 1) { d[k] = v[k]; k++; } d[k] = 0;
		struct retro_variable cur = { OPTS[i].key, NULL };
		if (env_cb (RETRO_ENVIRONMENT_GET_VARIABLE, &cur) && cur.value && !strcmp (cur.value, d)) continue;
		struct retro_variable sv = { OPTS[i].key, d }; if (env_cb (70, &sv)) n++; }
	/* (epoch 3, 2026-11-23: the Beam switch-off delay's fraction acts in the beam engine again (vecx.c) - a value set by hand
	 *  keeps the picture it gave: its whole cycles, as the desktop's unit_fix 2) */
	{ struct retro_variable cur = { "vectrex_emu_unit_beamoff", NULL };
	  if (env_cb (RETRO_ENVIRONMENT_GET_VARIABLE, &cur) && cur.value && cur.value[0] >= '0' && cur.value[0] <= '9') {
		static char d[32]; snprintf (d, sizeof d, "%.1f cycles", floor (atof (cur.value)));
		if (strcmp (d, cur.value)) { struct retro_variable sv = { "vectrex_emu_unit_beamoff", d }; if (env_cb (70, &sv)) n++; } } }
	{ FILE *fp = fopen (f, "w"); if (fp) { fprintf (fp, "%d\n", VX_OPT_EPOCH); fclose (fp); } }
	fprintf (stderr, "[vectrex_emu] settings: %s, once (%d changed)\n", have < 2 ? "the picture's and the unit's back to their defaults" : "the beam switch-off delay in whole cycles", n);
}
#endif
#ifndef VX_EMUVR
/* (the line renderer's options, gone 2026-11-22: not asked for - RetroArch logs an error for each unknown name) */
static int opt_gone (const char *key)
{
	static const char *const G[] = { "glow", "look", "brightness", "smooth_bends", "passes", "corner_glow", "corner_dots", "beam", "dwell", "speed_bright", "beam_trace", NULL };
	if (strncmp (key, "vectrex_emu_", 12)) return 0;
	for (int i = 0; G[i]; i++) if (!strcmp (key + 12, G[i])) return 1;
	return 0;
}
#else
#define opt_gone(k) 0
#endif
static double bt_halo_f, bt_halo_s;            /* (defined with the beam's constants below) */
static int opt_is (const char *key, const char *val)
{
	if (opt_gone (key)) return 0;
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
	opt_imager = opt_is ("vectrex_emu_imager", "off") ? 0 : opt_is ("vectrex_emu_imager", "colour") ? 1 : opt_is ("vectrex_emu_imager", "left eye") ? 3
	           : opt_is ("vectrex_emu_imager", "right eye") ? 4 : 2;   /* (3 / 4: one eye's pictures only - the other half turn left out) */
#ifdef VX_EMUVR
	opt_swap = opt_is ("vectrex_emu_swap_eyes", "on");
	{ extern float vx_bright; vx_bright = opt_is ("vectrex_emu_look", "vectrex") ? 0.0f : 1.0f; }   /* (the EmuVR table's Look, bright the default - the owner, 2026-11-25: also the GPU picture's Bright) */
	opt_glass = !opt_is ("vectrex_emu_area", "overlay sheet 162x207 mm");
	{ const char *ce = getenv ("VECTREX_CONSOLE");   /* (route A: the mod names the console that starts this RetroArch; the option overrides) */
	  opt_cad = opt_is ("vectrex_emu_console", "owner's CAD model") ? 1 : opt_is ("vectrex_emu_console", "classic") ? 0 : (ce && !strcmp (ce, "cad")); }
#endif
	opt_glow = opt_is ("vectrex_emu_glow", "on") ? 1 : opt_is ("vectrex_emu_glow", "strong") ? 2 : 0;
	raw_glow = opt_is ("vectrex_emu_glow", "auto") ? -1 : opt_glow;
	opt_sigma = opt_is ("vectrex_emu_beam", "wide") ? 1.1 : opt_is ("vectrex_emu_beam", "medium") ? 0.8 : 0.55;
#ifdef VX_EMUVR
	opt_res = opt_is ("vectrex_emu_res", "4x 2496x3200") ? 4 : opt_is ("vectrex_emu_res", "3x 1872x2400") ? 3 : opt_is ("vectrex_emu_res", "2x 1248x1600") ? 2 : 1;
#endif
	{ extern void vt_set_analog (int id, double v); const int bt = !(getenv ("VX_BEAM") && !atoi (getenv ("VX_BEAM")));   /* (2026-11-22: as the PC - always; VX_BEAM=0: a test, the rules before.
	                                              EmuVR too since 2026-11-25: without the beam's path its moving shapes came apart at the
	                                              corners - Smooth motion and the slow game's hold pair the path's pieces, not the old lines) */
	  if (bt != vx_beam) { vx_beam = bt; vt_set_analog (11, bt ? 1.0 : 0.0); } }
#ifndef VX_EMUVR
	{ const char *z = getenv ("VX_HALO"); if (z) sscanf (z, "%lf,%lf", &bt_halo_f, &bt_halo_s); }   /* (a test: the glass's glow - its share of the light, its sigma in units) */
	{ extern int vx_app_noshape; extern float vx_bright;   /* (Tube > Look, as the desktop's: RetroArch's option - the own apps set it themselves, App > Look) */
	  if (!vx_app_noshape) vx_bright = opt_is ("vectrex_emu_tube_look", "Bright") ? 1.0f : 0.0f; }
	{ const char *z = getenv ("VX_BRIGHT"); if (z) { extern float vx_bright; vx_bright = (float) atof (z); } }   /* (a test: the Bright look) */
	{ const char *z = getenv ("VX_HDRPEAK"); if (z) { extern float vx_hdr_peak; vx_hdr_peak = (float) atof (z); } }   /* (a test: an HDR screen's top, x white) */
	{ const char *z = getenv ("VX_GPROF"); if (z) { extern int vx_gprof; vx_gprof = atoi (z); } }   /* (a test: the GPU diagnostic - each stage's time in the log) */
#endif
	opt_wob = opt_is ("vectrex_emu_wobble", "off") ? 0 : opt_is ("vectrex_emu_wobble", "60 Hz") ? 60 : 50;
	opt_buzz = !opt_is ("vectrex_emu_buzz", "off"); opt_spk = !opt_is ("vectrex_emu_speaker", "off");
	/* (2026-11-23, the desktop's sliders: the wobble's and the bending's strength, the sound's volumes - 1x / 100 % / 50 % the default) */
	{ double k = optx ("vectrex_emu_wobble_k", 1.0); opt_wob_k = k >= 0 && k <= 2 ? k : 1.0; k = optx ("vectrex_emu_bending_k", 1.0); opt_bend_k = k >= 0 && k <= 2 ? k : 1.0;
	  k = optx ("vectrex_emu_buzz_vol", 1.0); opt_buzz_k = k >= 0 && k <= 2 ? k : 1.0;
	  k = optn ("vectrex_emu_voice_vol", 100) / 100.0; opt_voice_k = k >= 0 && k <= 2 ? k : 1.0;
	  k = optn ("vectrex_emu_motor_vol", 50) / 50.0; opt_motor_k = k >= 0 && k <= 2 ? k : 1.0;
	  k = optn ("vectrex_emu_jack_vol", 100) / 100.0; opt_jack_k = k >= 0 && k <= 2 ? k : 1.0;
	  opt_jack = opt_is ("vectrex_emu_jack", "off") ? 0 : opt_is ("vectrex_emu_jack", "through the speaker") ? 2 : 1; }
#ifdef VX_EMUVR
	{ struct retro_variable v = { "vectrex_emu_eye_shift", NULL }; opt_shift = env_cb (RETRO_ENVIRONMENT_GET_VARIABLE, &v) && v.value ? atoi (v.value) : 0; }
#endif
	opt_after = !opt_is ("vectrex_emu_afterglow", "off");
	/* (Afterglow: its length, as the desktop's slider - 0.05x .. 1.2x, 0.6x the default; the values before 2026-11-23: on 0.6x, long 1x, short 0.3x) */
	{ const char *ag = optv ("vectrex_emu_afterglow"); double t = !strcmp (ag, "long (real Vectrex)") ? 1.0 : !strcmp (ag, "short") ? 0.3 : ag[0] >= '0' && ag[0] <= '9' ? atof (ag) : 0.6;
	  opt_trail = (float) (t < 0.05 ? 0.05 : t > 10.0 ? 10.0 : t); }
	{ struct retro_variable v = { "vectrex_emu_trail_bright", NULL }; double k = env_cb (RETRO_ENVIRONMENT_GET_VARIABLE, &v) && v.value ? atof (v.value) * VX_TRAIL_DEF : VX_TRAIL_DEF;   /* (1x: the default) */
	  opt_trail_k = k >= 0.25 && k <= 6.0 ? (float) k : (float) VX_TRAIL_DEF; }   /* (v0.22: also dimmer, 0.5x / 0.75x - the owner) */
#ifdef VX_EMUVR
	if (!opt_is ("vectrex_emu_renderer", "CPU only")) opt_trail_k *= 1.5f;   /* (the GPU picture: the PC's trail, 1.5x on black - its tuning
	                                              kept exactly, the owner 2026-11-25; the CPU's picture keeps its own 1x) */
#endif
	{ extern int vx_app_noshape, vx_app_dots; extern float vx_hdr_lift, vx_trail_mul;   /* (the look - as the own apps' setting; they set it themselves) */
	  if (!vx_app_noshape) { const int v = opt_is ("vectrex_emu_look", "vectrex"); vx_hdr_lift = v ? 2.0f : 0.0f; vx_trail_mul = v ? 2.1f : 1.0f; vx_app_dots = 1; } }
	{ struct retro_variable v = { "vectrex_emu_brightness", NULL }; double k = !opt_gone (v.key) && env_cb (RETRO_ENVIRONMENT_GET_VARIABLE, &v) && v.value ? atof (v.value) : 1.0;
	  opt_bright = k >= 1.0 && k <= 2.0 ? (float) k : 1.0f; }
	opt_dots = !opt_is ("vectrex_emu_corner_dots", "off"); opt_dwell = !opt_is ("vectrex_emu_dwell", "off"); opt_speedb = !opt_is ("vectrex_emu_speed_bright", "off");
	opt_fill = opt_is ("vectrex_emu_fill", "on");
	opt_smooth = !opt_is ("vectrex_emu_smooth", "off"); raw_smooth = opt_is ("vectrex_emu_smooth", "auto") ? -1 : opt_smooth; opt_halo = !opt_is ("vectrex_emu_corner_glow", "off"); opt_bend = !opt_is ("vectrex_emu_bending", "off");
	raw_geom = TRI ("vectrex_emu_geometry"); opt_geom = raw_geom != 0;
	{ double k = optx ("vectrex_emu_geometry_k", 1.0), c = optn ("vectrex_emu_glass_curve", 1.0);
	  opt_geom_k = k >= 0 && k <= 2 ? k : 1.0; opt_glass_g = c >= 0 && c <= 1 ? 1.0 - c : 0.0; }
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
#ifdef VX_EMUVR
	opt_async = opt_is ("vectrex_emu_threaded", "on");   /* (EmuVR: off - no key in its list; the builder, 2026-11-25) */
#else
	opt_async = !opt_is ("vectrex_emu_threaded", "off");
#endif
	{ const int w = atoi (optv ("vectrex_emu_white")); opt_white = w >= 5600 && w <= 16000 ? w : 12000; }   /* (the desktop's 5600 - 16000 K; "6504 K (neutral)" before 2026-11-23 reads as 6504) */
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
	  t = t < 0 ? 0 : t > 2 ? 2 : t; pr = pr < 0 ? 0 : pr > 2 ? 2 : pr;   /* (the desktop's ranges: 0 .. twice the default - an older 3x .. 6x print as 2x) */
	  const char *rv = optv ("vectrex_emu_room"); float rm = !strncmp (rv, "off", 3) ? 0.0f : rv[0] ? (float) atof (rv) : 1.0f;
	  if (!(rm >= 0 && rm <= 4)) rm = 1.0f;
	  if (t != opt_ov_tint || pr != opt_ov_print || rm != opt_room) { opt_ov_tint = t; opt_ov_print = pr; opt_room = rm; ov_made = 0; }
	  /* (vecrender.py: the bare glass grey x the light level - (room / twice the default) cubed, 1 at most) */
	  { float ll = rm / 2.0f; ll = ll > 1 ? 1 : ll; glass_grey_v = 0.20f * ll * ll * ll; glass_grey = opt_room_glass ? glass_grey_v : 0.0f; }   /* (fx_apply: whether it shows) */ }
	/* (the core turns the picture itself: RetroArch's own rotation kept the upright picture's shape on the screen) */
	opt_rot = opt_is ("vectrex_emu_rotate", "90 (turn the device right)") ? 1 : opt_is ("vectrex_emu_rotate", "180") ? 2
	        : opt_is ("vectrex_emu_rotate", "270 (turn the device left)") ? 3 : 0;
	opt_rot_stick = !opt_is ("vectrex_emu_rotate_stick", "off"); kb_on = opt_is ("vectrex_emu_keys", "on");
	{ const char *zv = optv ("vectrex_emu_size"); int z = atoi (zv); size_auto = !zv[0] || !strcmp (zv, "auto");
	  if (size_auto) { if (!scr_short) scr_load (); z = scr_short; }   /* (the screen's short side: gpu_gl.c scr_detect, kept) */
	  opt_size = z >= 240 && z <= 2400 ? z : 800; }
	opt_area = opt_is ("vectrex_emu_area", "glass window") ? 1 : opt_is ("vectrex_emu_area", "overlay sheet") ? 2 : 0;
	{ const int lo = !opt_is ("vectrex_emu_gamelist", "simple"); if (lo != lib_on) { lib_on = lo; if (br_open) br_show (1); } }
	{ extern int vx_palette; const int pl = opt_is ("vectrex_emu_palette", "green"); if (pl != vx_palette) { vx_palette = pl; dk_dirty_set = 1; } }
	{ const int lk = !opt_is ("vectrex_emu_deck_look", "classic"); if (lk != opt_deck_look) { opt_deck_look = lk; dk_dirty_set = 1; } }
	opt_deck = opt_is ("vectrex_emu_deck", "full") || opt_is ("vectrex_emu_deck", "on") ? 1 : opt_is ("vectrex_emu_deck", "slim") ? 2 : opt_is ("vectrex_emu_deck", "pop-up") ? 3 : 0;
	deck_auto = VX_DECK_AUTO && opt_is ("vectrex_emu_deck", "auto"); if (deck_auto) opt_deck = deck_auto_mode (); opt_shape = !VX_IOS && opt_is ("vectrex_emu_shape", "16:9");
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
	/* (2026-11-16: the controller types - RetroArch's netplay sends only what the port's type has: the plain RetroPad no
	 *  sticks (the owner's Nova: d-pad only online). The Vectrex controller is the RetroPad with its sticks) */
	{ static const struct retro_controller_description t[] = { { "Vectrex controller", RETRO_DEVICE_ANALOG }, { "RetroPad (d-pad only)", RETRO_DEVICE_JOYPAD } };
	  static const struct retro_controller_info ci[] = { { t, 2 }, { t, 2 }, { NULL, 0 } };
	  cb (RETRO_ENVIRONMENT_SET_CONTROLLER_INFO, (void *) ci); }
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
/* (2026-11-22: the timing as the owner's Vectrex measured - vectrex_emu.py Core: the shift register's model, the spot's lag in three
 *  stages, RAMP's and the beam's delays, the S/H's. The core reads them as its test variables: set here unless a test set them) */
static void timing_defaults (void)
{
	static const char *const kv[5][2] = { { "VT_SRMODEL", "1" }, { "VT_DEFLAGN", "3" }, { "VT_RAMPDLY", "0,2,3" }, { "VT_ZDELAY", "1" }, { "VT_XYZDLY", "3,3,8" } };
	for (int i = 0; i < 5; i++) if (!getenv (kv[i][0])) {
#if defined(_WIN32)
		_putenv_s (kv[i][0], kv[i][1]);
#else
		setenv (kv[i][0], kv[i][1], 0);
#endif
	}
}
void retro_init (void)
{
	timing_defaults ();   /* (EmuVR too since 2026-11-25: without it its strokes started and stopped off the
	                         measured moments - a shape's halves apart, the owner's Mine Storm in VR) */
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
	/* (2026-11-16: OpenMP not to bind its threads to cores: Android moves an app between core sets itself (foreground,
	 *  background), and where the cores are hidden - an emulator's ARM translation - OpenMP's start failed an assertion
	 *  and ended RetroArch) */
	if (!getenv ("KMP_AFFINITY")) setenv ("KMP_AFFINITY", "disabled", 1);
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
#ifdef _OPENMP                                   /* (the release core is built without: build_libretro_win.bat) */
		omp_set_num_threads (n);
#else
		(void) n;
#endif
	} }
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
	*cw = glass ? (int) (*ew * (opt_cad ? GLASS_CAD_W : 145.7) / 162.0 + 0.5) : *ew; *ch = glass ? (int) (*fh * (opt_cad ? GLASS_CAD_H : 188.5) / 207.0 + 0.5) : *fh;
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
/* (each port's type, as RetroArch sets it: in its netplay a plain RetroPad port carries no sticks - and asked for them,
 *  RetroArch answers with player 1's: player 2's ship then followed player 1's stick. Such a port's sticks are not read
 *  in netplay; the Vectrex controller (the RetroPad with its sticks) is the type for it, 2026-11-16) */
static unsigned port_dev[2] = { RETRO_DEVICE_ANALOG, RETRO_DEVICE_ANALOG };
void retro_set_controller_port_device (unsigned p, unsigned d) { if (p < 2) port_dev[p] = d; }
#ifdef VX_EMUVR
static void ov_flip (const char *by) { (void) by; ov_on = !ov_on; }   /* (EmuVR: the switch only - the mod shows or hides the real sheet; RetroArch 1.7.5
                                                takes no option back - as the installed EmuVR core, 2026-11-15) */
#endif
#ifndef VX_EMUVR
/* (the first start, in tate - the default: why the picture lies on its side, said once; a mark in RetroArch's saves
 *  folder keeps it from coming again. The owner, 2026-11-16) */
struct vx_message_ext { const char *msg; unsigned duration, priority; int level, target, type; int8_t progress; };   /* (libretro.h's
                                                    retro_message_ext, SET_MESSAGE_EXT 60 - newer than this copy of the header) */
static void say_long (const char *m, unsigned ms)
{
	struct vx_message_ext x = { m, ms, 3, 1, 0, 0, -1 };   /* (info, on screen and in the log, a notification) */
	if (!env_cb (60, &x)) { struct retro_message o = { m, ms * 60 / 1000 }; env_cb (RETRO_ENVIRONMENT_SET_MESSAGE, &o); }
}
static void ov_flip (const char *by)            /* (the overlay on / off by hand: the option Overlay itself, one switch - RetroArch
                                                keeps it, its menu shows it; a separate on / off kept apart from it showed nothing
                                                with the option on, iPhone 2026-11-18) */
{
	ov_on = !ov_on;
	{ struct retro_variable v = { "vectrex_emu_overlay", ov_on ? "on" : "off" };   /* (RETRO_ENVIRONMENT_SET_VARIABLE: RetroArch 1.17 on) */
	  if (env_cb (70, &v)) { opt_overlay = opt_overlay_seen = ov_on; } }
	fprintf (stderr, "[vectrex_emu] overlay %s (%s)\n", ov_on ? "on" : "off", by);   /* (the log only: no message on screen) */
}
static void tate_note (void)
{
	static int done; if (done || net_runs < 30) return;   /* (half a second in: the picture is there) */
	done = 1; if (!opt_rot) return;
	const char *sv = NULL; env_cb (RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY, &sv);
	char p[1200]; snprintf (p, sizeof p, "%s" SEP "vectrex_emu_tate_told.txt", sv && *sv ? sv : tmpdir);
	FILE *f = fopen (p, "r"); if (f) { fclose (f); return; }
	fprintf (stderr, "[vectrex_emu] tate mode %d: the controls turn with the picture\n", opt_rot);   /* (the log only: the player chose it) */
	f = fopen (p, "w"); if (f) { fputs ("the tate note was shown\n", f); fclose (f); }
}
#endif
static int netvx_playing (void);
void retro_reset (void) { if (ls_state == 2 || netvx_playing ()) { ls_reset_req = 1; return; }   /* (lockstep: both reset in the same game frame - netls.c) */
	extern void vecx_reset_button (void); emu_wait_ext (); vecx_reset_button (); }

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
static int ov_uvmc2_tag (const char *s)       /* ("(UVMC2)" in a name, any case) */
{
	static const char t[] = "(uvmc2)";
	for (; *s; s++) { int k = 0; while (k < 7 && s[k] && (s[k] | 0x20) == (t[k] | 0x20)) k++; if (k == 7) return 1; }
	return 0;
}
static int try_overlay_dir (const char *dir, const char *rom)
{
	char want[512], wantl[512]; base_of (rom, want, sizeof want); letters (want, wantl, sizeof wantl);
	/* (a UVMC2 program: a Vectrex game's overlay only for its UVMC2 edition - "Last Human (UVMC2)" - else only one named for
	 *  the UVMC2, "Armor Attack (UVMC2).png": the card's arcade games renamed to the Vectrex game's name got the cartridge's
	 *  overlay, made for another screen - Tony on Facebook, 2026-11-25. As the PC's find_overlay) */
	const char *rx = strrchr (rom, '.'); const char *rs = strrchr (rom, '/'), *rb = strrchr (rom, '\\'); if (rb > rs) rs = rb; rs = rs ? rs + 1 : rom;
	const int card_only = rx && !vx_stricmp (rx, ".um2") && !ov_uvmc2_tag (rs);
	char best[1600] = ""; int bestk = 0; size_t bestlen = 99999;
#ifdef _WIN32
	char pat[1200]; snprintf (pat, sizeof pat, "%s\\*.*", dir);
	WIN32_FIND_DATAA fd; HANDLE h = FindFirstFileA (pat, &fd);
	if (h == INVALID_HANDLE_VALUE) return 0;
	do { const char *fn = fd.cFileName, *x = strrchr (fn, '.');
		if (!OV_EXT (x) || (card_only && !ov_uvmc2_tag (fn))) continue;
#else
	DIR *dh = opendir (dir); struct dirent *de;
	if (!dh) return 0;
	while ((de = readdir (dh))) { const char *fn = de->d_name, *x = strrchr (fn, '.');
		if (!OV_EXT (x) || (card_only && !ov_uvmc2_tag (fn))) continue;
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
/* (2026-11-23, as the desktop's uvm2_cfg_force: v1.8.1 and earlier shipped a wrong UVMC2 calibration) the card folder
 * <base>/vectrex_emu/uvmc2 gets the calibration that comes with the emulator (uvm2_cfg_tab.h, from
 * package/uvmc2/.conf/uvm2.cfg by make_uvm2_cfg.py) once per UVM2_CFG_VERSION, in .conf/ and config/, whatever is there -
 * a player's own file too, or none; a different file kept beside it as uvm2.cfg.before_<version> (the first one only).
 * The marker .conf/vectrex_emu_cal (the version) written last: the player's later calibrations stay. No card folder:
 * one made (in an existing <base>/vectrex_emu) - with an old card image there (uvmc2_sd.img) its files copied into it
 * first; an image next to the game is the player's own card: left. Before the card is built (sdc_prepare) */
#include "uvm2_cfg_tab.h"
typedef char uvm2_cfg_is_512[sizeof uvm2_cfg_new == 512 ? 1 : -1];
static int uvm2_isdir (const char *p) { sdc_st st; return !sdc_stat (p, &st) && (st.st_mode & S_IFMT) == S_IFDIR; }
static int uvm2_isfile (const char *p) { sdc_st st; return !sdc_stat (p, &st) && (st.st_mode & S_IFMT) == S_IFREG; }
static int uvm2_cal_force (const char *base, const char *game_dir)   /* (returns 1 when it wrote) */
{
	char vd[1700], fo[1760], mk[1900], d[1800], p[1900], bk[1950];
	char ver[16]; int vn = 0; for (const char *q = UVM2_CFG_VERSION; *q && vn < 15; q++) if (*q != '-') ver[vn++] = *q; ver[vn] = 0;
	snprintf (vd, sizeof vd, "%s" SEP "vectrex_emu", base); snprintf (fo, sizeof fo, "%s" SEP "uvmc2", vd);
	snprintf (mk, sizeof mk, "%s" SEP ".conf" SEP "vectrex_emu_cal", fo);
	{ FILE *m = fopen (mk, "rb"); if (m) { char b[64] = { 0 }; size_t n = fread (b, 1, 63, m); fclose (m);
		while (n && (b[n - 1] == '\n' || b[n - 1] == '\r' || b[n - 1] == ' ')) b[--n] = 0;
		if (!strcmp (b, UVM2_CFG_VERSION)) return 0; } }
	if (!uvm2_isdir (fo)) {
		if (!uvm2_isdir (vd)) return 0;                      /* (no vectrex_emu folder: not set up here - nothing made) */
		snprintf (p, sizeof p, "%s" SEP "uvmc2_sd.img", game_dir);
		if (uvm2_isfile (p)) { fprintf (stderr, "[vectrex_emu] UVMC2 calibration: the card is %s - left as it is\n", p); return 0; }
		vx_mkdir (fo); if (!uvm2_isdir (fo)) return 0;
		snprintf (p, sizeof p, "%s" SEP "uvmc2_sd.img", vd);
		if (uvm2_isfile (p)) { sdc_older_than = 0; int n = sdc_sync_back (p, fo);
			fprintf (stderr, "[vectrex_emu] UVMC2 card: %s into the folder %s (%d files)\n", p, fo, n);
			if (n <= 0) {                                    /* (an image it cannot read: the image stays the card) */
#ifdef _WIN32
				extern int _rmdir (const char *); _rmdir (fo);
#else
				rmdir (fo);
#endif
				return 0; } } }
	int ok = 1;
	for (int k = 0; k < 2; k++) {
		snprintf (d, sizeof d, "%s" SEP "%s", fo, k ? "config" : ".conf"); vx_mkdir (d);
		snprintf (p, sizeof p, "%s" SEP "uvm2.cfg", d);
		unsigned char b[sizeof uvm2_cfg_new + 1]; size_t n = 0; FILE *f = fopen (p, "rb"); int had = f != NULL;
		if (f) { n = fread (b, 1, sizeof b, f); fclose (f); }
		if (had && n == sizeof uvm2_cfg_new && !memcmp (b, uvm2_cfg_new, n)) continue;
		snprintf (bk, sizeof bk, "%s" SEP "uvm2.cfg.before_%s", d, ver);
		if (had && !uvm2_isfile (bk)) { FILE *i = fopen (p, "rb"), *o = i ? fopen (bk, "wb") : NULL;
			if (o) { char c[4096]; size_t r; while ((r = fread (c, 1, sizeof c, i)) > 0) if (fwrite (c, 1, r, o) != r) { ok = 0; break; }
				if (fclose (o)) ok = 0; }
			else ok = 0;
			if (i) fclose (i);
			if (!ok) break; }
		FILE *o = fopen (p, "wb"); if (!o) { ok = 0; break; }
		if (fwrite (uvm2_cfg_new, 1, sizeof uvm2_cfg_new, o) != sizeof uvm2_cfg_new) ok = 0;
		if (fclose (o)) ok = 0;
		if (!ok) break; }
	if (ok) { FILE *m = fopen (mk, "wb"); ok = m && fprintf (m, "%s\n", UVM2_CFG_VERSION) > 0; if (m && fclose (m)) ok = 0; }
	fprintf (stderr, "[vectrex_emu] UVMC2 calibration %s on the card %s%s\n", UVM2_CFG_VERSION, fo, ok ? "" : " - FAILED");
	return ok;
}
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
#include "netls.c"                          /* (2026-11-16: UVMC2 / PiTrex games online - lockstep, the desktop's way) */
#include "netvx.c"                          /* (online play the desktop's way, for the own iOS app: vx_net_*) */
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
	  const char *p2 = ls_cfg_on && ls_port2[0] ? ls_port2 : optv ("vectrex_emu_port2"); int p2_none = !strcmp (p2, "none"), p2_im = !strcmp (p2, "3D Imager"), p2_lp = !strcmp (p2, "light pen");
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
	  if (ls_cfg_on) nv_sv_apply (path); else { vx_putenv ("VT_FLASH_CLEAN="); vx_putenv ("VT_FLASH_IMG="); }   /* (online: the host's saves -
	                                              netvx.c, temporary files only; else the player's own) */
	  unit_make (unit_preset (), optv); unit_env (unit_addon); unit_force (unit_addon);
	  km_on = unit_addon && !opt_is ("vectrex_emu_usb_hid", "off"); km_n = 0; km_mods = 0; km_dirty = km_on; km_btn = -1;
	  vx_putenv (km_on ? "VT_USBHID=1" : "VT_USBHID=");   /* (the desktop's usb_hid: a UVMC2 / PiTrex gets the keyboard and mouse) */
	  km_typing = 0; kb_held = 0; kb_ev = 0;
	  if (km_on || kb_on) { static struct retro_keyboard_callback kc = { km_key }; env_cb (RETRO_ENVIRONMENT_SET_KEYBOARD_CALLBACK, &kc); } }   /* (unit.c: the desktop's Config > Unit / Circuit) */
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
	if (!ls_cfg_on) { static char ds[1900]; ds[0] = 0; unsigned char h[96] = { 0 };   /* (online: the host's chip or none - nv_sv_apply) */
	  { FILE *c = fopen (path, "rb"); if (c) { if (fread (h, 1, 96, c)) {} fclose (c); } }
	  if (!ls_cfg_on && (v4e_ds || (!memcmp (h, "g GCE", 5) && memmem_title (h, 96, "LAST") && memmem_title (h, 96, "HUMAN")))) save_path (path, ".ds2431", ds, sizeof ds);   /* (online: no chip -
	                                              both start alike, as the desktop: each player's own calibration parted the games) */
	  { static char e[1950]; snprintf (e, sizeof e, "VT_DS2431=%s", ds); vx_putenv (e); }
	  extern void vt_ds2431_reload (void); vt_ds2431_reload ();
	  if (ds[0]) fprintf (stderr, "[vectrex_emu] save chip %s\n", ds); }
	else { extern void vt_ds2431_reload (void); vt_ds2431_reload (); }
	/* (1.1: the SD card of a PiTrex program (.img / .pit) or a UVMC2 game (.um2) - a FAT image, next to the game or in
	 *  <system>/vectrex_emu: pitrex_sd.img / uvmc2_sd.img, as the desktop Vectrex_EMU builds them from its card folders) */
	{ const char *e = strrchr (path, '.'); int px = e && (!vx_stricmp (e, ".img") || !vx_stricmp (e, ".pit")), um = e && !vx_stricmp (e, ".um2");
	  sd_img[0] = sd_folder[0] = 0;
	  if (um) uvm2_cal_force (sys ? sys : dir, dir);   /* (this version's calibration on the card, once) */
	  if (px || um) { /* (1.1, as the desktop: the card is a folder - <system>/vectrex_emu/uvmc2 or pitrex - built into an image in
	                     RetroArch's saves folder before the game and what the game wrote copied back after it) */
		char fo[1800]; snprintf (fo, sizeof fo, "%s" SEP "vectrex_emu" SEP "%s", sys ? sys : dir, px ? "pitrex" : "uvmc2"); sdc_st st;
		if (ls_cfg_on) vx_mkdir (fo);          /* (online: a card on both sides, empty without the folder - one side with it, one without, ran apart) */
		if (!sdc_stat (fo, &st) && (st.st_mode & S_IFMT) == S_IFDIR) {
			const char *sv = NULL; env_cb (RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY, &sv);
			char cd[1700]; snprintf (cd, sizeof cd, "%s" SEP "vectrex_emu_cards", sv && *sv ? sv : dir); vx_mkdir (cd);
			snprintf (sd_img, sizeof sd_img, "%s" SEP "%s%s", cd, ls_cfg_on ? (ls_me ? "online_p2_" : "online_p1_") : "", px ? "pitrex_sd.img" : "uvmc2_sd.img");
			if (ls_cfg_on) {                      /* (online, netls.c: the files both have alike, one fixed time - the same card on both
			                                         sides; nothing written back) */
				snprintf (sdc_root, sizeof sdc_root, "%s", fo); int ok = sdc_build (fo, sd_img); sdc_root[0] = 0;
				if (ok) { static char e[2000]; snprintf (e, sizeof e, "%s=%s", px ? "VT_PITREX_SD" : "VT_UVM2_SD", sd_img); vx_putenv (e); }
				else sd_img[0] = 0;
				fprintf (stderr, "[vectrex_emu] online SD card %s: %d files%s\n", sd_img, sdc_nonly, ok ? "" : " - FAILED"); }
			else if (sdc_prepare (fo, sd_img)) { static char e[2000]; snprintf (sd_folder, sizeof sd_folder, "%s", fo);
				snprintf (e, sizeof e, "%s=%s", px ? "VT_PITREX_SD" : "VT_UVM2_SD", sd_img); vx_putenv (e); }
			else sd_img[0] = 0; } }
	  if ((px || um) && !sd_folder[0] && !(ls_cfg_on && sd_img[0])) { static char sd[1800];   /* (online: its own card, built above) */ const char *nm = px ? "pitrex_sd.img" : "uvmc2_sd.img"; const char *dd[] = { dir, sys, NULL }; sd[0] = 0;
		for (int d = 0; dd[d] && !sd[0]; d++) { char q[1700]; snprintf (q, sizeof q, d ? "%s" SEP "vectrex_emu" SEP "%s" : "%s" SEP "%s", dd[d], nm);
			FILE *t = fopen (q, "rb"); if (t) { fclose (t); snprintf (sd, sizeof sd, "%s=%s", px ? "VT_PITREX_SD" : "VT_UVM2_SD", q); } }
		if (sd[0]) { vx_putenv (sd); fprintf (stderr, "[vectrex_emu] SD card %s\n", strchr (sd, '=') + 1); }
		else fprintf (stderr, "[vectrex_emu] no SD card (%s in <system>/vectrex_emu)\n", nm); } }
	netls_env ();                               /* (online in lockstep: the host's settings - netls.c) */
	return im_out;
}

bool retro_load_game_special (unsigned t, const struct retro_game_info *i, size_t n) { (void) t; (void) i; (void) n; return false; }
static long vx_hidden, vx_shown;          /* (test: refreshes run hidden / shown - run-ahead's hidden runs) */
static void pw_stamp_path (char *o, size_t n) { snprintf (o, n, "%s" SEP "vectrex_emu_power_off.txt", tmp_dir ()); }
void retro_unload_game (void)
{
	wd_stop ();
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
	const int sticks = !(net_runs - net_seen < 180 && (port_dev[port & 1] & RETRO_DEVICE_MASK) == RETRO_DEVICE_JOYPAD);   /* (RetroArch's netplay:
	                                                    a RetroPad port has none - see port_dev) */
	int x = sticks ? input_cb (port, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_X) / 256 : 0;
	int y = sticks ? -input_cb (port, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_Y) / 256 : 0;
	/* (the right stick: the same - held in tate it is the one under the thumb; the stronger of the two) */
	if (sticks) { const int rx = input_cb (port, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_X) / 256,
	            ry = -input_cb (port, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_Y) / 256;
	  if (rx * rx + ry * ry > x * x + y * y) { x = rx; y = ry; } }
	if (x > 127) x = 127; if (y > 127) y = 127; if (x < -128) x = -128; if (y < -128) y = -128;
	if (input_cb (port, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT)) x = -127;
	if (input_cb (port, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT)) x = 127;
	if (input_cb (port, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP)) y = 127;
	if (input_cb (port, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN)) y = -127;
	unsigned ub = 0; int ux = 0, uy = 0;
	if (port == 0 && !net_on () && usb_pad (&ux, &uy, &ub)) { if (ux) x = ux; if (uy) y = uy; }   /* (not in netplay: RetroArch's input only) */   /* (the Vectrex USB Controller, player 1) */
	if (port == 0 && kb_on && !net_on ()) {         /* (the desktop's keys: the arrows the stick - Shift half -, A W S D buttons 1-4;
	                                                    in the ROM browser Enter loads and Backspace goes back, its buttons 4 and 1) */
		{ static const unsigned bits[] = { KB_UP, KB_DOWN, KB_LEFT, KB_RIGHT, KB_B1, KB_B2, KB_B3, KB_B4, KB_SHIFT };   /* (a key held that
		                                              *  RetroArch says is up - its release lost (iPad, up and down at once: the stick
		                                              *  stayed down, 2026-11-18): let go after 3 refreshes; trusted once RetroArch has
		                                              *  shown a held key down) */
		  static const unsigned keys[] = { RETROK_UP, RETROK_DOWN, RETROK_LEFT, RETROK_RIGHT, RETROK_a, RETROK_w, RETROK_s, RETROK_d, RETROK_LSHIFT };
		  static int trust; static unsigned char miss[9];
		  for (int i = 0; i < 9; i++) { if (!(kb_held & bits[i])) { miss[i] = 0; continue; }
			int dn = input_cb (0, RETRO_DEVICE_KEYBOARD, 0, keys[i]) != 0; if (i == 8) dn = dn || input_cb (0, RETRO_DEVICE_KEYBOARD, 0, RETROK_RSHIFT);
			if (dn) { trust = 1; miss[i] = 0; } else if (trust && ++miss[i] >= 3) { kb_held &= ~bits[i]; miss[i] = 0;
				fprintf (stderr, "[vectrex_emu] key %u held but up: let go\n", keys[i]); } } }
		const unsigned k = kb_held; const int s = k & KB_SHIFT ? 64 : 127;
		if (k & KB_LEFT) x = -s; if (k & KB_RIGHT) x = s; if (k & KB_UP) y = s; if (k & KB_DOWN) y = -s;
		ub |= (k & KB_B1 ? 1u : 0) | (k & KB_B2 ? 2u : 0) | (k & KB_B3 ? 4u : 0) | (k & KB_B4 ? 8u : 0);
		if (br_open) ub |= (k & KB_ENTER ? 8u : 0) | (k & KB_BACK ? 1u : 0); }
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
/* the afterglow's faint end lifted: g x (1 + A / (1 + g / G0)) - up to A x G0 more, reached already where it is faint. On
 * black the trail so looks as it did through an overlay, whose plastic glow (added as a screen value) lifted it there;
 * measured on Armor Attack's jeep, overlay on / off, the owner's wish 2026-11-19. The same numbers in gpu_phos.c (lift) */
#define TRAIL_LIFT_A 3.0f
#define TRAIL_LIFT_G0 0.00625f
extern float vx_hdr_peak;               /* (gpu_phos.c; 1.0: no HDR) */
/* HDR (the own iOS app): how much brighter than a line the beam at rest lights the tube - the owner's Corner Test photo
 * (2026-10-26, ProRAW: linear): 2.3x at 16 cycles, 3.2x at 24, 4.9x at 32, 7-9x from 48 on. Without HDR such a spot is
 * the screen's white, as every line's core; with it the rest's light above white, as an sRGB value less white.
 * Mine Storm's shots rest 36 cycles (intensity 95), Last Human's 53 + 66 in one place (127): the brightest */
static double hdr_rest (double cyc)
{
	static const double C[] = { 8, 16, 24, 32, 48, 96 }, R[] = { 1.0, 2.3, 3.2, 4.9, 8.0, 8.5 };
	double r = R[5];
	if (cyc <= C[0]) return 0;
	for (int i = 1; i < 6; i++) if (cyc <= C[i]) { r = R[i - 1] + (R[i] - R[i - 1]) * (cyc - C[i - 1]) / (C[i] - C[i - 1]); break; }
	return 1.055 * pow (r, 1 / 2.4) - 0.055 - 1.0;   /* (as an sRGB value, as the app makes it light: VXApp.m) */
}
/* a resting spot, the owner's Dot Test photos (2026-11-21, ProRAW, isolated dots 4-128 cycles at intensity 7F, 5F, 40, 30):
 * a spot as wide as the line at that intensity whose light grows with the rest - A = DOT_A x T x ln (1 + cycles / T) times
 * the line's peak (the tube's light of the intensity), 0.8x at 4 cycles, 2.9x at 16, 7x at 48, 13.5x at 128 - never wider;
 * round it a faint glow (about 0.4 % of that light). Matched in the picture (SDR, both looks): every dot's radius at 50, 25
 * and 12 % of white within about 0.1 unit of the photo's on average, each look. Shots alike (36-119 cycles,
 * a double shot as one of the summed time); a rest at a joint or corner as long as an isolated dot of the same time */
/* per look (0 Bright, 1 Vectrex - the lines' light differs): the spot's peak (display value) for A = 1, its width (the line's
 * x DOT_W: the photos' camera widens the line more than ours), the glow's share and FWHM - each matched to the photos' dot
 * sizes (Bright: within 0.12 unit on average, Vectrex 0.11) */
static const double DOT_K[2] = { 1.5, 3.0 }, DOT_W[2] = { 1.4, 1.6 }, DOT_HALO[2] = { 0.003, 0.005 }, DOT_HALO_W[2] = { 1.3, 1.3 };
#define DOT_A 0.209
#define DOT_T 52.0
static double dot_light (double cyc) { return cyc > 0 ? DOT_A * DOT_T * log (1.0 + cyc / DOT_T) : 0; }
#define DOT_C0 9.0                      /* a rest joined to a lit line (a joint, a corner): the beam slows into it - this many cycles
                                           less (the photos: a 16-cycle corner 1.3x the line, an isolated 16-cycle dot 2.9x) */
#define DOT_WL 1.5                      /* the spot's width grows from the line's to DOT_W x up to this light (x the 7F line's):
                                           a faint or short rest a slight thickening, a bright long one a dot */
#define REST_SPEED 0.5                  /* slower than this (units a cycle): the spot at rest */
#define REST_DWELL_SLOW 0.6             /* the dwell inside a stroke (vecrender.py dwell_rests) */
#define REST_DWELL_D0 3.0
#define REST_DWELL_MAX 30.0
#define REST_DWELL_TAU 12.0
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
typedef struct { double x0, y0, x1, y1, cyc, t; int b; unsigned char rgb, wq; float lt, ag; } wline;   /* (lt: beam trace - the piece's light, bt2_prepare; < 0: by the rules. ag: the age of its frame's start at
                                                  this picture's moment, s - a line's own age ag - t / 1.5e6 (wl_from); 0 unknown. In the struct's padding: its size, and the
                                                  save state's pw_streak, as before) */   /* (b: intensity | 128 fade
                                                                                         | 256 halo | 512 dwell | 1024 a rest
                                                                                         joined to a lit line: a joint, a corner) */
#define NW (MAXL * 3)
static wline W1[NW], W2[NW];
/* a piece in picture pixels: the segment (run on past an open end), the ends of its own box, the cuts, the spot */
typedef struct { float ax, ay, bx, by, x0, y0, x1, y1, n0x, n0y, c0, n1x, n1y, c1, k2, R, r, g, b, hx, ag; int to, cut; } pline;   /* (ag: the light's age at this picture, s - gpu_phos.c, where the afterglow starts) */
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
	const double PI2 = 6.283185307179586, strength = 0.1074 * opt_wob_k, speed = 2.62;   /* (x Wobble strength) */
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
	const double buzz_g = 0.0912 * 47.5 * opt_buzz_k;   /* (x Buzz volume) */
	for (int i = 0; i < n; i++) {
		double a = two ? s[2 * i] : s[i], c = two ? s[2 * i + 1] : 0;
		if (vt_uvm2_cart && !vt_imager && opt_jack == 2) { a += c * 2.0 * opt_jack_k; c = 0; }   /* (the UVMC2's headphone jack through the Vectrex's speaker) */
		if (opt_buzz) a += bq_run (f_buzz, 8, act[i] * buzz_g);
		if (opt_spk) { a = bq_run (f_spk, 6, a); a = 32767.0 * tanh (a / 26000.0); }   /* (the driver's limited excursion) */
		if (c != 0 || two) {                /* beside the Vectrex, not through its speaker */
			if (vt_imager) a = soft (a + c * 1.0 * opt_motor_k);                    /* (the goggles' motor, on the head: x Disc motor sound / 50 %) */
			else if (vt_uvm2_cart) a = soft (a + (opt_jack == 1 ? c * 2.0 * opt_jack_k : 0.0));   /* (the UVMC2's headphone jack, clean - x its volume; off) */
			else a = soft (a + bq_run (f_voice, 2, c * 3.0 * opt_voice_k));         /* (the speech module, full-range speakers - x its volume) */
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
		w->cyc = 40 / 8.0; w->t = i * (40 / 8.0); w->b = 90; w->rgb = 7; w->wq = 255; }
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
static float *emask; static int emask_w, emask_h, emask_cad = -1;
static float edge_mask (int x, int y)
{
	int ex = x % EW;
	if (!emask || emask_w != EW || emask_h != FH || emask_cad != opt_cad) {
		free (emask); emask = malloc (sizeof (float) * EW * FH); emask_w = EW; emask_h = FH; emask_cad = opt_cad;
		const float *gcut = opt_cad ? glass_cut_cad : glass_cut;   /* (route A) */
		const double gA = 145.7 / 2, gB = 188.5 / 2, gN = 7.17, gIN = 3.0, gSOFT = 0.75;
		for (int yy = 0; yy < FH; yy++) for (int xx = 0; xx < EW; xx++) {
			double px = ((xx + 0.5) / EW - 0.5) * 162.0, py = ((yy + 0.5) / FH - 0.5) * 207.0;   /* (the frame: the 162 x 207 mm sheet, the glass in its middle) */
			/* (v0.36: the glass's visible edge where the new bezel walls cut the tube, glass_cut_lut.h - y up there, down here) */
			double deg = atan2 (-py, px) * 57.29578; if (deg < 0) deg += 360; int di = (int) deg; double ft = deg - di;
			double R = gcut[di % 360] * (1 - ft) + gcut[(di + 1) % 360] * ft, d = R - sqrt (px * px + py * py);
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
typedef struct { line_t *L; int n; double t; unsigned long long p0; long long *keys; int nk, alt; unsigned long long gen; int open; } gframe;   /* (gen: this content's
                                                own number - a new one each push, join or state load; Smooth motion's kept work, smooth.c;
                                                open: its last piece was cut short by the budget, the game's frame goes on - frame_push) */
static unsigned long long frame_gen;       /* (not in the save state: the frames get new numbers when one is loaded) */
#define SHORT_PIECE 0.015
#define HOLD_SLOW 1.1                     /* (the hold below for a game this much slower than the screen: Vectrexians 45 fps at 60 Hz) */
#define HOLD_GRACE 2.0                    /* (a line not found again: lit this many of its frames - one may leave it out) */
#define HOLD_LIFE 0.15                    /* (vecrender.py HOLD_MAX: a line no frame draws again goes out after this) */
static gframe hist[NH]; static int nh; static gframe prevf; static int have_prev;
VXS_ON
static double load_f = -1;              /* (brightness bending: the beam's load, smoothed) */
#define BEND_LOAD_CYC (30000.0 / 1.0556)  /* (its unit - vecrender.py BEND_LOAD_CYC: the Test Cartridge's screen 1 0.366, as the tube map was measured) */
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
static void frame_push (unsigned long long p0, int cut)
{
	gframe f;
	/* (a piece shorter than SHORT_PIECE joins the next one, as the desktop's frame queue does (vectrex_emu.py, "pieces
	 *  shorter than 15 ms"): a game that calls Wait_Recal twice in one frame - Last Human in arena 2 with many shots -
	 *  made a short frame with nothing drawn, and the refreshes that showed it were black, ~0.1 s, the Nova 2026-11-17.
	 *  And a piece the budget cut (cut 2, vtest.c vt_piece_cut) joins the rest of the game's frame - the next piece, if that
	 *  one ends where the game ends its frame (cut 0); else it stands alone. The Test Cartridge Rev. 4's screen 1 (its frames
	 *  timed by timer 2, 42000 cycles) was cut at 33000 and the 6 ms rest joined the next frame's start: the cut moved through
	 *  the picture, one frame had the line at x 11211 twice and the next none - with Smooth motion a line gone for 2-3
	 *  refreshes every 0.3 s; on the real Vectrex it is steady (the owner, 2026-11-23)) */
	{ extern int vt_imager, vt_uvm2, vt_pitrex;   /* (a UVMC2 / PiTrex ends its frames itself, on its bus: not joined -
	                                              VecDoom's 66 a second were all joined, 2 ms a refresh on the Nova) */
	  if (nh && !vt_imager && !vt_uvm2 && !vt_pitrex) {
		gframe *h = &hist[nh - 1]; const double pt = nh > 1 ? hist[nh - 2].t : have_prev ? prevf.t : -1;
		const int join = h->open ? cut == 0 : pt >= 0 && h->t >= pt && h->t - pt < SHORT_PIECE;
		if (join && h->n + nlast <= MAXL && p0 >= h->p0) {
			const unsigned sh = (unsigned) (p0 - h->p0);
			for (int i = 0; i < nlast; i++) { h->L[h->n + i] = last[i]; h->L[h->n + i].t0 += sh; }
			h->n += nlast; h->t = total_cycles / 1.5e6; h->gen = ++frame_gen; h->open = cut == 2; frame_keys (h);
			{ const gframe *f1 = nh > 1 ? &hist[nh - 2] : have_prev ? &prevf : NULL, *f2 = nh > 2 ? &hist[nh - 3] : (nh == 2 && have_prev ? &prevf : NULL);
			  double o1 = f1 ? frame_overlap (h, f1) : -1, o2 = f2 ? frame_overlap (h, f2) : -1;
			  h->alt = o1 >= 0 && o2 >= 0 && o1 < 0.5 && o2 > o1 + 0.3; }
			return; } } }
	if (nh == NH) frame_pop ();
	if (spare.L) { f = spare; spare.L = NULL; }
	else { f.L = malloc (sizeof (line_t) * MAXL); f.keys = malloc (sizeof (long long) * 6000); }
	memcpy (f.L, last, sizeof (line_t) * nlast); f.n = nlast; f.t = total_cycles / 1.5e6; f.p0 = p0; f.gen = ++frame_gen; f.open = cut == 2;
	frame_keys (&f);
	{ const gframe *f1 = nh ? &hist[nh - 1] : NULL, *f2 = nh > 1 ? &hist[nh - 2] : (nh == 1 && have_prev ? &prevf : NULL);
	  double o1 = f1 ? frame_overlap (&f, f1) : -1, o2 = f2 ? frame_overlap (&f, f2) : -1;
	  f.alt = o1 >= 0 && o2 >= 0 && o1 < 0.5 && o2 > o1 + 0.3; }
	hist[nh++] = f;
	/* the beam's load (brightness bending): the frame before this one - whole now, a short piece joined to it (above) - its lit
	 * cycles, smoothed (vecrender.py beam_load, load_f: the PC's joined picture). Counted as each frame came, a short piece's
	 * joined lines were left out: the Test Cartridge's screen 1 (a 5 ms piece and a 23 ms one) 0.105 instead of 0.366 */
	{ const gframe *g = nh > 1 ? &hist[nh - 2] : have_prev ? &prevf : NULL;
	  if (g) { double ld = 0; for (int i = 0; i < g->n; i++) ld += g->L[i].cyc / 8.0 * ((g->L[i].c & 127) / 127.0);
	    ld /= BEND_LOAD_CYC; if (load_f < 0) load_f = ld; load_f += (ld - load_f) * 0.3; } }
}
static void frame_pop (void)                /* (the oldest frame becomes the one before) */
{
	if (have_prev) { if (spare.L) { free (prevf.L); free (prevf.keys); } else spare = prevf; }
	prevf = hist[0]; have_prev = 1; memmove (hist, hist + 1, sizeof (gframe) * (nh - 1)); nh--;
}
/* (as the desktop's renderer (vecrender.py hold): each line lit from the moment the beam drew it until the game draws
 *  its frame again - two frames on for a picture in halves, the newest frame's lines until the next one (at most
 *  HOLD_LIFE: a stalled game fades) - and a refresh's light each line's share of the refresh, up to T, it was lit. A game
 *  that leaves things out of some frames (Vectrexians: aliens, rows, the ship, its 45 frames a second at 60 Hz) showed
 *  one frame per refresh: what a frame left out was dark for one or two whole refreshes, a hard flicker the desktop does
 *  not have - the Nova, 2026-11-17) */
static int wl_from (wline *o, int m, const line_t *L, int n, const line_t *B, double alpha);
/* (the hold's grid: for each cell of HOLD_CELL emulator units, when a later frame next draws there - the beam's path is
 *  cut into pieces differently in each frame (8 to 32 cycles), so a line is found again by its place, not its piece) */
#define HOLD_CELL 128
#define HGW 448
/* (and by its own drawing: of the drawings along the line (its direction, or the two beside it: four 45 degrees apart; a spot
 *  - a resting beam - by spots) on or next to its middle, the one nearest the line's own moment in that frame - a still line's
 *  copy is drawn where it was in the frame, a moved one's too. The earliest drawing there, of anything, ended a line's hold
 *  where something else was drawn first: the Test Cartridge Rev. 4's grid, Smooth motion off - the centre lines dark from
 *  where a line across them or a stub beside them was drawn until their own copy, a third of the refreshes (2026-11-23)) */
#define HOLD_SPOT 32.0                    /* (a piece shorter than this (units): a spot, no direction) */
typedef struct { float t; int next; unsigned char d; } hold_ent;   /* (a drawing in a cell: its moment (s after hgb), the cell's next, its direction) */
static int hgh[HGW * HGW]; static unsigned hgg[HGW * HGW], hgen; static double hgb; static hold_ent *hge; static int hge_n, hge_cap;
static int hold_cell (long x, long y) { const long cx = (x + 4096) / HOLD_CELL, cy = (y + 4096) / HOLD_CELL;
	return cx < 0 || cy < 0 || cx >= HGW || cy >= HGW ? -1 : (int) (cy * HGW + cx); }
static void hold_new (double base) { if (++hgen == 0) { memset (hgg, 0, sizeof hgg); hgen = 1; } hgb = base; hge_n = 0; }   /* (an empty grid) */
static int hold_dir (const line_t *l)   /* (0 along x, 1 and 3 the diagonals, 2 along y; 4 a spot) */
{
	const double dx = (double) l->x1 - l->x0, dy = (double) l->y1 - l->y0, ax = fabs (dx), ay = fabs (dy);
	if (dx * dx + dy * dy < HOLD_SPOT * HOLD_SPOT) return 4;
	if (ay <= 0.41421356 * ax) return 0;
	if (ax <= 0.41421356 * ay) return 2;
	return (dx > 0) == (dy > 0) ? 1 : 3;
}
/* (a later drawing on or next to the line's middle, after `after`: want < 0 the earliest of any; else, along the line, the one
 *  nearest want (the line's own moment in that frame) - -1: none) */
static double hold_find (const line_t *l, double after, double want)
{
	const long mx = (l->x0 + l->x1) / 2, my = (l->y0 + l->y1) / 2; const int d = want < 0 ? -1 : hold_dir (l); double best = -1, bd = 0;
	for (int oy = -1; oy <= 1; oy++) for (int ox = -1; ox <= 1; ox++) { const int c = hold_cell (mx + ox * HOLD_CELL, my + oy * HOLD_CELL);
		if (c < 0 || hgg[c] != hgen) continue;
		for (int q = hgh[c]; q >= 0; q = hge[q].next) { const hold_ent *h = &hge[q];
			if (d >= 0 && (d == 4 || h->d == 4 ? h->d != d : ((h->d - d) & 3) == 2)) continue;   /* (not along the line) */
			const double e = hgb + h->t; if (e <= after) continue;
			const double k = want < 0 ? e : fabs (e - want);
			if (best < 0 || k < bd) { best = e; bd = k; } } }
	return best;
}
static void hold_put (const line_t *l, double tau)          /* (the cells along the line: drawn at tau) */
{
	const double dx = l->x1 - l->x0, dy = l->y1 - l->y0; const int k = 1 + (int) (sqrt (dx * dx + dy * dy) / (HOLD_CELL / 2));
	const unsigned char d = (unsigned char) hold_dir (l); const float v = (float) (tau - hgb); int was = -1;
	for (int i = 0; i <= k; i++) { const int c = hold_cell (l->x0 + (long) (dx * i / k), l->y0 + (long) (dy * i / k));
		if (c < 0 || c == was) continue;
		was = c;
		if (hge_n == hge_cap) { const int nc = hge_cap ? hge_cap * 2 : 16384; hold_ent *ne = realloc (hge, sizeof (hold_ent) * (size_t) nc); if (!ne) return; hge = ne; hge_cap = nc; }
		if (hgg[c] != hgen) { hgg[c] = hgen; hgh[c] = -1; }
		hge[hge_n].t = v; hge[hge_n].d = d; hge[hge_n].next = hgh[c]; hgh[c] = hge_n++; }
}
/* (as the desktop's renderer (vecrender.py hold): each line lit from the moment the beam drew it until the game draws it
 *  again - found in a later frame (hold_find): a still line stays lit through a frame that left it out, a moved one
 *  hands over to its new copy as that is drawn, and the light never doubles where two copies overlap; a line not drawn
 *  again goes out after its own frame's length; the newest frame's lines until the next frame, at most HOLD_LIFE - and
 *  a refresh's light each line's share of the refresh, up to T, it was lit. A game that leaves things out of some
 *  frames (Vectrexians: aliens, rows, the ship; its 45 frames a second at 60 Hz) showed one frame per refresh: what a
 *  frame left out was dark for one or two whole refreshes, a hard flicker the desktop does not have - the Nova,
 *  2026-11-17) */
/* (each frame's lines' ends - when each is drawn again - worked out once, as each later frame arrives, not again every
 *  refresh; kept by the frame's contents (hold_id), so run-ahead's rewind finds them again - and with the later frames
 *  they were worked out against: run-ahead runs a frame ahead with this moment's input and then again with the next
 *  one's, and a frame that came out different starts its matching again (VecDoom 4 ms a refresh less on the Nova; ends
 *  matched against a frame thrown away let Vectrexians' aliens flicker again) */
#define HOLD_EC 16
#define HOLD_AP 8
static struct { unsigned long long id; int n, cap, nap; unsigned long long ap[HOLD_AP]; float *end; unsigned use; } hec[HOLD_EC]; static unsigned hec_use;
static unsigned long long hold_id (const gframe *f)
{
	unsigned long long h = 1469598103934665603ull ^ f->p0 ^ ((unsigned long long) f->n << 40);
	for (int j = 0; j < f->n; j += 3) { const line_t *l = &f->L[j];
		h = (h ^ (unsigned long long) (l->x0 * 31 + l->y0 * 977 + l->x1 * 7 + l->y1 + l->t0 * 13 + l->c)) * 1099511628211ull; }
	return h;
}
static int hold_entry (const gframe *f, unsigned long long id)
{
	int lru = 0;
	for (int i = 0; i < HOLD_EC; i++) { if (hec[i].end && hec[i].id == id && hec[i].n == f->n) { hec[i].use = ++hec_use; return i; }
		if (hec[i].use < hec[lru].use) lru = i; }
	if (hec[lru].cap < f->n) { free (hec[lru].end); hec[lru].cap = f->n > 1024 ? f->n : 1024; hec[lru].end = malloc (sizeof (float) * hec[lru].cap); }
	if (!hec[lru].end) { hec[lru].cap = 0; return -1; }
	hec[lru].id = id; hec[lru].n = f->n; hec[lru].nap = 0; hec[lru].use = ++hec_use;
	for (int j = 0; j < f->n; j++) hec[lru].end[j] = -1;
	return lru;
}
static int hold_lines (double T, double dt)
{
	(void) dt;
	const gframe *F[NH + 1]; double S[NH + 1]; int E[NH + 1], nf = 0; unsigned long long ID[NH + 1];
	if (have_prev) { F[nf] = &prevf; S[nf] = nh ? 2 * prevf.t - hist[0].t : prevf.t - 0.02; nf++; }
	for (int i = 0; i < nh; i++) { F[nf] = &hist[i]; S[nf] = i ? hist[i - 1].t : have_prev ? prevf.t : hist[i].t - 0.02; nf++; }
	{ static int fill_was = -1; if (opt_fill != fill_was) { fill_was = opt_fill; for (int i = 0; i < HOLD_EC; i++) hec[i].id = 0; } }   /* (the option changed: matched again) */
	for (int k = 0; k < nf; k++) { ID[k] = hold_id (F[k]); E[k] = hold_entry (F[k], ID[k]); }
	/* (each frame's lines matched against the later frames in order - those already done skipped, a different one
	 *  than before: matched again from the start) */
	int grid_of = -1;
	for (int k = 0; k < nf - 1; k++) { if (E[k] < 0) continue;
		int m = 0, ok = 1;
		for (int j = k + 1; j < nf && ok; j++) { if (m < hec[E[k]].nap && hec[E[k]].ap[m] == ID[j]) m++; else if (m < hec[E[k]].nap) ok = 0; }
		if (!ok) { hec[E[k]].nap = 0; for (int q = 0; q < F[k]->n; q++) hec[E[k]].end[q] = -1; }
	}
	for (int j = 1; j < nf; j++) {
		for (int k = 0; k < j; k++) { if (E[k] < 0 || (!opt_fill && j != k + 1)) continue;   /* (off: the next frame only) */
			int done = 0; for (int a = 0; a < hec[E[k]].nap; a++) if (hec[E[k]].ap[a] == ID[j]) done = 1;
			if (done) continue;
			if (grid_of != j) { hold_new (S[j]);
				for (int q = 0; q < F[j]->n; q++) if (F[j]->L[q].c & 127) hold_put (&F[j]->L[q], S[j] + F[j]->L[q].t0 / 1.5e6);
				grid_of = j; }
			float *en = hec[E[k]].end; const gframe *f = F[k];
			/* (the newest frame not whole yet - a piece the next one joins, frame_push: a line whose moment lies past its end is
			 *  not drawn again in it yet; matched to what is there, the Test Cartridge Rev. 4's right edge went dark until the
			 *  rest came) */
			extern int vt_uvm2, vt_pitrex;
			const int grows = j == nf - 1 && !vt_uvm2 && !vt_pitrex && (F[j]->open || F[j]->t - S[j] < SHORT_PIECE);
			for (int q = 0; q < f->n; q++) { if (en[q] >= 0) continue; const double tau = S[k] + f->L[q].t0 / 1.5e6, want = S[j] + f->L[q].t0 / 1.5e6;
				if (grows && want > F[j]->t) continue;
				const double e = hold_find (&f->L[q], tau, want); if (e > tau) en[q] = (float) (e - S[k]); }   /* (kept from the frame's start) */
			if (hec[E[k]].nap < HOLD_AP) hec[E[k]].ap[hec[E[k]].nap++] = ID[j]; }
	}
	int n = 0;
	for (int k = 0; k < nf; k++) {
		const gframe *f = F[k]; const double s = S[k], len = f->t - s; const float *en = E[k] >= 0 ? hec[E[k]].end : NULL;
		if (s > T || (k < nf - 1 && s + HOLD_LIFE + len < T)) continue;   /* (none of its lines lit now) */
		for (int j = 0; j < f->n; j++) { const line_t *l = &f->L[j]; const double tau = s + l->t0 / 1.5e6;
			if (tau > T || tau + HOLD_LIFE <= T) continue;   /* (not lit at this refresh's moment) */
			double e = en && en[j] >= 0 ? s + en[j] : -1;
			if (e < tau) e = k == nf - 1 ? tau + HOLD_LIFE : tau + (opt_fill ? HOLD_GRACE : 1.0) * len;   /* (not drawn again: on, through one
			                                              frame left out; off, out after its frame - as a real tube) */
			if (e > tau + HOLD_LIFE) e = tau + HOLD_LIFE;
			if (e <= T) continue;                  /* (drawn again by now, or gone out) */
			n = wl_from (W1, n, l, 1, NULL, 0); }
	}
	return n;
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

/* The afterglow starts at the light's real age (vecrender.py GLOW_COMMON: a flash leaving the lit layer excites the two parts at
 * their share at its age - from when the beam drew it to the picture it is left in). The picture's moment wl_tshow (s): set each
 * refresh (retro_run); a line's own age: ag - its time in the frame (build_lines). Before 2026-11-23 the afterglow started at one
 * refresh's age: at 60 Hz its first ghosts 1.3 - 2.3 x the PC's in light (Test Cart page 1). wl_base: the frame a line array belongs
 * to - its start, cycles (-1: none - the age unknown, 0) */
static double wl_tshow, wl_off;   /* (wl_off: total_cycles - the vector clock alg_cycles, which the frames' p0 count in) */
static double wl_base (const line_t *L)
{
	for (int i = 0; i < nh; i++) if (hist[i].L && L >= hist[i].L && L < hist[i].L + hist[i].n) return (double) hist[i].p0;
	if (have_prev && prevf.L && L >= prevf.L && L < prevf.L + prevf.n) return (double) prevf.p0;
	return -1;
}
static int wl_from (wline *o, int m, const line_t *L, int n, const line_t *B, double alpha)
{
	const double base = n > 0 && !B ? wl_base (L) : -1;
	const float ag = base >= 0 ? (float) (wl_tshow - (base + wl_off) / 1.5e6) : 0.0f;
	for (int i = 0; i < n && m < NW; i++) {
		const line_t *l = &L[i]; wline *w = &o[m++];
		w->x0 = l->x0; w->y0 = l->y0; w->x1 = l->x1; w->y1 = l->y1;
		if (B) { w->x0 += (B[i].x0 - l->x0) * alpha; w->y0 += (B[i].y0 - l->y0) * alpha; w->x1 += (B[i].x1 - l->x1) * alpha; w->y1 += (B[i].y1 - l->y1) * alpha; }
		w->cyc = l->cyc / 8.0; w->t = l->t0; w->b = l->c; w->rgb = l->rgb; w->wq = 255; w->lt = -1; w->ag = ag;
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
			q->cyc = l->cyc / k; q->t = l->t + l->cyc * j / k; if (l->lt > 0) q->lt = l->lt / k;
		}
	}
	return m;
}
#define ART_PARALLAX 0.008
static double par_dy (void)                  /* (vecrender.py par_dy: the overlay's parallax, emulator units - up) */
{ return opt_par && ov && ov_on && !sheet_file ? -ART_PARALLAX * 0.924 * ALG_MAX_Y : 0.0; }
/* HDR: a rest's light with the beam's slow parts round it - the end of the stroke before, the start of the one after (the
 * integrators slowing down and speeding up: Test Cartridge Rev. 4's joints rest 11 cycles, with ~20 more cycles within one
 * spot; the owner's photos, 2026-11-20, ProRAW: the joint 3.5x the line's peak at every intensity). Weighted by distance (the
 * spot: sigma half a game px) and by intensity; other rests not (their own spots) */
static double rest_cyc (const wline *L, int n, int i, const unsigned char *zl, const unsigned char *halo)
{
	const double px = L[i].x1, py = L[i].y1, sg = 0.5 * GAME_PX, z = L[i].b & 127; double c = L[i].cyc;
	for (int d = -1; d <= 1; d += 2)
		for (int j = i + d, k = 0; j >= 0 && j < n && k < 12; j += d, k++) {
			const wline *l = &L[j]; if (halo[j] || zl[j]) { if (zl[j]) break; continue; }
			const double mx = (l->x0 + l->x1) / 2 - px, my = (l->y0 + l->y1) / 2 - py, dd = mx * mx + my * my;
			if (dd > 9 * sg * sg) break;
			if (!(l->b & 127) || (l->b & 128)) continue;
			c += l->cyc * exp (-dd / (2 * sg * sg)) * ((l->b & 127) < z ? (l->b & 127) / z : 1.0); }
	return c;
}
/* ---- Beam trace (a test: VX_BEAM=1 / the option vectrex_emu_beam_trace on; vecrender.py bt2_light / bt2_prepare). The core
 * sends the spot's own path (vt_set_analog 11); the light follows the Photo Test's law: the light a unit of line against
 * the beam's dwell there (cycles a unit) at intensities $30 / $40 / $5F / $7F, by intensity at 8.3 cycles a unit 0.022 /
 * 0.12 / 0.51 / 0.76 / 1. Along every run of lit pieces that join in place and time the dwell at a place = the beam's cycles
 * within BT2_W units of it along the path: a rest, a slow start, a line's end follow from the one law; runs add */
#define BT2_W 1.0
#define BT2_ND 12
static const double BT2_DW[BT2_ND] = { 1.048, 1.386, 2.080, 2.773, 4.159, 5.546, 8.319, 16.64, 33.28, 48.0, 128.0, 1024.0 };
static const double BT2_Z[4] = { 48, 64, 95, 127 };
static const double BT2_L9[4][9] = { { 0.0333 * 0.194, 0.0428 * 0.194, 0.0619 * 0.194, 0.08 * 0.194, 0.11 * 0.194, 0.14 * 0.194, 0.21 * 0.194, 0.40 * 0.194, 0.73 * 0.194 },
	{ 0.21 * 0.194, 0.27 * 0.194, 0.39 * 0.194, 0.51 * 0.194, 0.72 * 0.194, 0.90 * 0.194, 1.25 * 0.194, 2.21 * 0.194, 3.70 * 0.194 },
	{ 0.24, 0.30, 0.41, 0.50, 0.66, 0.79, 1.05, 1.79, 3.20 }, { 0.49, 0.59, 0.78, 0.94, 1.21, 1.43, 1.87, 3.24, 5.75 } };
static double bt2_ld[BT2_ND], bt2_ll[4][BT2_ND]; static int bt2_ready;
#define BT2_K (1.054 / 0.647)           /* (the reference line - 1.59 cycles a unit, $7F: light 1.054 a unit) */
#define BT_EXPO 0.20                    /* (vecrender.py: the exposure, the spot's width x, the spot's share of the display width) */
#define BT_SPOT 1.25
#define BT_SG 0.674
static double bt_halo_f = 0.008, bt_halo_s = 2.5;   /* (the two below: a test may set them - VX_HALO=share,sigma) */
#define BT_HALO_F_ 0.008                 /* (the glass: this share of every piece's light spread wide - sigma BT_HALO_S units) */
#define BT_HALO_F bt_halo_f
#define BT_HALO_S bt_halo_s
static double bt2_tl (double z)            /* (the tube's light by the drive: TUBE_LIGHT x the Photo Test's page 4 against it) */
{
	static const double ZK[][2] = { { 0, 0.25 }, { 48, 0.25 }, { 63, 0.48 }, { 95, 0.76 }, { 111, 0.89 }, { 127, 1.0 } };
	if (z < 0) z = 0; if (z > 127) z = 127;
	const int i = z >= 126 ? 126 : (int) z; double k = 1.0;
	for (int j = 1; j < 6; j++) if (z <= ZK[j][0]) { k = ZK[j - 1][1] + (ZK[j][1] - ZK[j - 1][1]) * (z - ZK[j - 1][0]) / (ZK[j][0] - ZK[j - 1][0]); break; }
	return (TUBE_LIGHT[i] + (TUBE_LIGHT[i + 1] - TUBE_LIGHT[i]) * (z - i)) * k;
}
static double bt2_light (double z, double d)   /* (the light a unit of path: the drive in intensity steps, the dwell in cycles a unit) */
{
	if (!bt2_ready) { bt2_ready = 1;
		for (int j = 0; j < BT2_ND; j++) bt2_ld[j] = log (BT2_DW[j]);
		for (int r = 0; r < 4; r++) { double v = 0;
			for (int j = 0; j < BT2_ND; j++) { v = j < 9 ? BT2_L9[r][j] : j == 9 ? v * pow (48.0 / 33.28, 0.8) : j == 10 ? v * pow (128.0 / 48.0, 0.64) : v * pow (8.0, 0.3); bt2_ll[r][j] = log (v); } } }
	const double ld = log (d > 1e-3 ? d : 1e-3);
	int j = 0; while (j < BT2_ND - 2 && ld >= bt2_ld[j + 1]) j++;
	const double f = (ld - bt2_ld[j]) / (bt2_ld[j + 1] - bt2_ld[j]);
	const double zc = z < BT2_Z[0] ? BT2_Z[0] : z > BT2_Z[3] ? BT2_Z[3] : z;
	int i = 0; while (i < 2 && zc >= BT2_Z[i + 1]) i++;
	const double g = (zc - BT2_Z[i]) / (BT2_Z[i + 1] - BT2_Z[i]);
	const double a = bt2_ll[i][j] + (bt2_ll[i][j + 1] - bt2_ll[i][j]) * f, b = bt2_ll[i + 1][j] + (bt2_ll[i + 1][j + 1] - bt2_ll[i + 1][j]) * f;
	double o = exp (a + (b - a) * g);
	if (z < BT2_Z[0]) o *= bt2_tl (z) / bt2_tl (BT2_Z[0]);
	return BT2_K * o;
}
static double bt2_xs[2 * NW], bt2_ys[2 * NW], bt2_s0[NW]; static int bt2_nk; static unsigned char bt2_fl[NW];   /* (bt2_fl: 1 lit, 2 joins the lit piece before) */
/* (the knots looked up from where the last lookup ended: a picture's lookups only go forward along the path - the same knot as a
 *  search from the start finds, in a step or two instead of ~15; one going back searches from the start. Vectrexians at 120 Hz:
 *  bt2_prepare 1.24 -> 0.45 ms a refresh on the PC, the pieces bit for bit the same) */
static int bt2_ch, bt2_cl;
static double bt2_thi (double s)           /* (the beam's cycles up to s along the path, a rest at s counted) */
{
	const int n = bt2_nk; if (s >= bt2_xs[n - 1]) return bt2_ys[n - 1]; if (s < bt2_xs[0]) return bt2_ys[0];
	int lo = bt2_ch;                       /* (the last knot <= s) */
	if (lo < 0 || lo > n - 2 || bt2_xs[lo] > s) { int hi = n - 1; lo = 0; while (hi - lo > 1) { const int m = (lo + hi) / 2; if (bt2_xs[m] <= s) lo = m; else hi = m; } }
	else while (bt2_xs[lo + 1] <= s) lo++;
	bt2_ch = lo;
	const double dx = bt2_xs[lo + 1] - bt2_xs[lo]; return bt2_ys[lo] + (dx > 1e-9 ? (s - bt2_xs[lo]) / dx : 0.0) * (bt2_ys[lo + 1] - bt2_ys[lo]);
}
static double bt2_tlo (double s)           /* (.. a rest at s not counted) */
{
	const int n = bt2_nk; if (s <= bt2_xs[0]) return bt2_ys[0]; if (s > bt2_xs[n - 1]) return bt2_ys[n - 1];
	int lo, hi = bt2_cl;                   /* (hi: the first knot >= s) */
	if (hi < 1 || hi > n - 1 || bt2_xs[hi - 1] >= s) { lo = 0; hi = n - 1; while (hi - lo > 1) { const int m = (lo + hi) / 2; if (bt2_xs[m] < s) lo = m; else hi = m; } }
	else while (bt2_xs[hi] < s) hi++;
	lo = hi - 1; bt2_cl = hi;
	const double dx = bt2_xs[hi] - bt2_xs[lo]; return bt2_ys[lo] + (dx > 1e-9 ? (s - bt2_xs[lo]) / dx : 1.0) * (bt2_ys[hi] - bt2_ys[lo]);
}
static double bt2_dwell (double s) { return (bt2_thi (s + BT2_W / 2) - bt2_tlo (s - BT2_W / 2)) / BT2_W; }
static int bt2_prepare_cpu (const wline *L, int n, wline *o, double kb)
{
	int m = 0, prev = -1; double S = 0, T = 0; bt2_nk = 0; bt2_ch = bt2_cl = 0;
	for (int i = 0; i < n; i++) {
		const wline *l = &L[i]; const int z = l->b & 127; bt2_fl[i] = 0;
		if (!z || (l->b & 128)) continue;
		const double len = hypot (l->x1 - l->x0, l->y1 - l->y0) / 134.0;
		const int cont = prev == i - 1 && prev >= 0 && fabs (l->x0 - L[prev].x1) <= 3.0 && fabs (l->y0 - L[prev].y1) <= 3.0
		                 && fabs (l->t - (L[prev].t + L[prev].cyc)) <= 1.5 && l->b == L[prev].b;
		if (!cont) S += 4.0 * BT2_W;
		bt2_fl[i] = cont ? 3 : 1; bt2_s0[i] = S;
		bt2_xs[bt2_nk] = S; bt2_ys[bt2_nk++] = T; S += len; T += l->cyc; bt2_xs[bt2_nk] = S; bt2_ys[bt2_nk++] = T; prev = i;
	}
	/* (the picture adds display values, not light: pieces must not lie on each other - a run's caps go into its first and
	    last moving piece (their round ends carry them); a run that never moves is one spot) */
	double cap = 0; int mv = -1;
	for (int i = 0; i < n && m < NW - 5; i++) {
		const wline *l = &L[i];
		if (!bt2_fl[i]) { o[m] = *l; o[m++].lt = -1; continue; }
		const double z = (l->b & 127) + kb * 127.0, len = hypot (l->x1 - l->x0, l->y1 - l->y0) / 134.0, s0 = bt2_s0[i];
		const int last = i + 1 >= n || !(bt2_fl[i + 1] & 2);
		if (!(bt2_fl[i] & 2)) { cap = BT2_W / 2 * bt2_light (z, bt2_dwell (s0 - BT2_W / 4)); mv = -1; }
		if (len > 1e-6) {
			const int np = len > BT2_W ? 3 : len > BT2_W / 2 ? 2 : 1; const double h = np == 3 ? (BT2_W / 2) / len : 0.5, F[4] = { 0, np == 1 ? 1 : h, np == 3 ? 1 - h : 1, 1 };
			for (int k = 0; k < np; k++) {
				const double f0 = F[k], f1 = F[k + 1]; wline *q = &o[m]; *q = *l;
				q->x0 = l->x0 + (l->x1 - l->x0) * f0; q->y0 = l->y0 + (l->y1 - l->y0) * f0; q->x1 = l->x0 + (l->x1 - l->x0) * f1; q->y1 = l->y0 + (l->y1 - l->y0) * f1;
				q->cyc = l->cyc * (f1 - f0); q->t = l->t + l->cyc * f0;
				q->lt = (float) (len * (f1 - f0) * bt2_light (z, bt2_dwell (s0 + len * (f0 + f1) / 2)) + cap); cap = 0; mv = m++;
			}
		}
		if (last) { const double c1 = BT2_W / 2 * bt2_light (z, bt2_dwell (s0 + len + BT2_W / 4));
			if (mv >= 0) o[mv].lt += (float) c1;
			else { wline *q = &o[m++]; *q = *l; q->x0 = l->x1; q->y0 = l->y1; q->cyc = 1; q->lt = (float) (cap + c1); }
			cap = 0; }
	}
	return m;
}
/* (vecrender.py bt2_prepare as it is - for the GPU's light image, where pieces add as light: every lit piece of a run (a long
 *  one as its two ends, BT2_W / 2 each, and its middle), a cap at each end of a run (no length, half the footprint beyond the
 *  end); the others - the beam fading, the blanked beam - piece by piece) */
static int bt2_prepare (const wline *L, int n, wline *o)
{
	int m = 0, prev = -1; double S = 0, T = 0; bt2_nk = 0; bt2_ch = bt2_cl = 0;
	for (int i = 0; i < n; i++) {
		const wline *l = &L[i]; const int z = l->b & 127; bt2_fl[i] = 0;
		if (!z || (l->b & 128)) continue;
		const double len = hypot (l->x1 - l->x0, l->y1 - l->y0) / 134.0;
		const int cont = prev == i - 1 && prev >= 0 && fabs (l->x0 - L[prev].x1) <= 3.0 && fabs (l->y0 - L[prev].y1) <= 3.0
		                 && fabs (l->t - (L[prev].t + L[prev].cyc)) <= 1.5 && l->b == L[prev].b;
		if (!cont) S += 4.0 * BT2_W;
		bt2_fl[i] = cont ? 3 : 1; bt2_s0[i] = S;
		bt2_xs[bt2_nk] = S; bt2_ys[bt2_nk++] = T; S += len; T += l->cyc; bt2_xs[bt2_nk] = S; bt2_ys[bt2_nk++] = T; prev = i;
	}
	for (int i = 0; i < n && m < NW - 6; i++) {
		const wline *l = &L[i]; const double z = l->b & 127, len = hypot (l->x1 - l->x0, l->y1 - l->y0) / 134.0;
		if (!bt2_fl[i]) { o[m] = *l; o[m++].lt = (float) ((len + BT2_W) * bt2_light (z, l->cyc / (len + BT2_W))); continue; }
		const double s0 = bt2_s0[i]; const int last = i + 1 >= n || !(bt2_fl[i + 1] & 2);
		if (!(bt2_fl[i] & 2)) { wline *q = &o[m++]; *q = *l; q->x1 = l->x0; q->y1 = l->y0; q->cyc = 1;   /* (the run's first cap) */
			q->lt = (float) (BT2_W / 2 * bt2_light (z, bt2_dwell (s0 - BT2_W / 4))); }
		if (len > 1e-6) {
			const int np = len > 1.5 * BT2_W ? 3 : 1; const double h = (BT2_W / 2) / len, F[4] = { 0, np == 1 ? 1 : h, np == 3 ? 1 - h : 1, 1 };
			for (int k = 0; k < np; k++) {
				const double f0 = F[k], f1 = F[k + 1]; wline *q = &o[m++]; *q = *l;
				q->x0 = l->x0 + (l->x1 - l->x0) * f0; q->y0 = l->y0 + (l->y1 - l->y0) * f0; q->x1 = l->x0 + (l->x1 - l->x0) * f1; q->y1 = l->y0 + (l->y1 - l->y0) * f1;
				q->cyc = l->cyc * (f1 - f0); q->t = l->t + l->cyc * f0;
				q->lt = (float) (len * (f1 - f0) * bt2_light (z, bt2_dwell (s0 + len * (f0 + f1) / 2)));
			}
		}
		if (last) { wline *q = &o[m++]; *q = *l; q->x0 = l->x1; q->y0 = l->y1; q->t = l->t + l->cyc; q->cyc = 1;   /* (.. and its last) */
			q->lt = (float) (BT2_W / 2 * bt2_light (z, bt2_dwell (s0 + len + BT2_W / 4))); }
	}
	return m;
}
/* (and when: a line that moves on by SM_EXW or more a frame starts its afterglow as soon as the gliding line is that far past
 *  its place - the trail's newest line then lies within a step of it, as with one picture a game frame; started a whole frame
 *  later it lay one to two steps behind, an uneven gap (the owner's PC, 2026-11-22). The others - slow, still, gone - a frame
 *  later, when the next frame has taken over. sm_exf: a's lines started already; sm_exfp: the frame's before) */
#define SM_EXW 96
static unsigned char *sm_exf, *sm_exfp; static int sm_exf_n, sm_exfp_n, sm_exf_cap, sm_exfp_cap, *sm_exi, sm_exi_cap, sm_exc;
static unsigned long long sm_exf_id, sm_exfp_id;
static unsigned long long sm_ex_shown, sm_ex_next;   /* (Smooth motion's afterglow: the game frame the last drawn picture started it for, this refresh's) */
int vx_owe_hidden;                         /* (set by the apps: a hidden catch-up run's frame is drawn with the next picture) */
static int vx_nobuild;                     /* (v0.13: a hidden refresh - no picture, the lines not built) */
static int bt_gpu_ok (void);
#define BL_WIDTH 1.3                        /* (the Bright look: the beam this much wider - vecrender.py BL_WIDTH; its glass's glow not: gpu_phos.c) */
static int build_lines (wline *L, int n, int to, double esh, int npl)
{
	extern int vt_imager;
	if (vx_nobuild) return npl;
	if (up_xf) for (int i = 0; i < n; i++) { picture_apply (&L[i].x0, &L[i].y0); picture_apply (&L[i].x1, &L[i].y1); }   /* (unit.c: the
	                                                    picture's pots and the yoke, after the integrators) */
	if (vx_beam) { if (n) n = bt_gpu ? bt2_prepare (L, n, W2) : bt2_prepare_cpu (L, n, W2, up_kb), memcpy (L, W2, sizeof (wline) * n); }   /* (the light from the path - no rest, dwell or bend rules) */
	else {
	if (opt_dwell && n > 1) n = dwell_rests (L, n, W2), memcpy (L, W2, sizeof (wline) * n);
	if (opt_sbend && n > 1) n = smooth_bends (L, n, W2), memcpy (L, W2, sizeof (wline) * n);
	}
	if (((opt_geom && opt_geom_k > 0) || opt_glass_g > 0) && n) n = tube_split (L, n, W2), memcpy (L, W2, sizeof (wline) * n);
	/* the halos (corner glow) round the long rests: drawn as pieces of their own */
	static unsigned char zl[NW], jo[NW], halo[NW];
	int nhalo = 0;
	const int bgpu = vx_beam && bt_gpu;            /* (the beam on the GPU: its pieces use no joint marks, open ends or cuts - not made) */
	if (!bgpu) for (int i = 0; i < n; i++) {                 /* a rest joined to a lit line (a joint, a corner): 1024 */
		wline *l = &L[i]; l->b &= ~1024;
		if (sqrt ((l->x1 - l->x0) * (l->x1 - l->x0) + (l->y1 - l->y0) * (l->y1 - l->y0)) > REST_SPEED * l->cyc) continue;
		for (int d = -1; d <= 1; d += 2) {
			const int j = i + d; if (j < 0 || j >= n) continue;
			const wline *m = &L[j]; if (!(m->b & 127) || (m->b & 128)) continue;
			if (sqrt ((m->x1 - m->x0) * (m->x1 - m->x0) + (m->y1 - m->y0) * (m->y1 - m->y0)) <= REST_SPEED * m->cyc) continue;
			const double ex = d < 0 ? m->x1 : m->x0, ey = d < 0 ? m->y1 : m->y0, et = d < 0 ? m->t + m->cyc : m->t, lt = d < 0 ? l->t : l->t + l->cyc;
			if (fabs (ex - (d < 0 ? l->x0 : l->x1)) <= 0.5 * GAME_PX && fabs (ey - (d < 0 ? l->y0 : l->y1)) <= 0.5 * GAME_PX && fabs (et - lt) <= 2.0) { l->b |= 1024; break; }
		}
		if (l->b & 512) l->b |= 1024;
	}
	if (opt_halo && !vx_beam) {
		for (int i = 0; i < n; i++) {
			const wline *l = &L[i]; double ln = sqrt ((l->x1 - l->x0) * (l->x1 - l->x0) + (l->y1 - l->y0) * (l->y1 - l->y0));
			if (ln <= REST_SPEED * l->cyc && l->cyc >= 8.0 && (l->b & 127) && !((l->b & 512) && l->cyc < REST_DWELL_HALO) && n + nhalo < NW) {
				W2[nhalo] = *l; W2[nhalo].b = (l->b & (127 | 1024)) | 256; nhalo++; }
		}
		if (nhalo) { memmove (L + nhalo, L, sizeof (wline) * (n < NW - nhalo ? n : NW - nhalo)); memcpy (L, W2, sizeof (wline) * nhalo); n += nhalo; if (n > NW) n = NW; }
	}
	for (int i = 0; i < n; i++) {
		const wline *l = &L[i];
		zl[i] = sqrt ((l->x1 - l->x0) * (l->x1 - l->x0) + (l->y1 - l->y0) * (l->y1 - l->y0)) <= REST_SPEED * l->cyc;
		if (vx_beam && l->lt >= 0) zl[i] = l->x0 == l->x1 && l->y0 == l->y1;   /* (beam trace: only a piece without length is a spot) */
		halo[i] = (l->b & 256) != 0; jo[i] = 0;
	}
	if (!bgpu) for (int i = 1; i < n; i++)                   /* one stroke in several pieces: the joined ends open */
		if (fabs (L[i].x0 - L[i - 1].x1) <= 1.5 && fabs (L[i].y0 - L[i - 1].y1) <= 1.5 && fabs (L[i].t - (L[i - 1].t + L[i - 1].cyc)) <= 1.5
		    && !halo[i] && !halo[i - 1]) { if (!zl[i]) jo[i] |= 1; if (!zl[i - 1]) jo[i - 1] |= 2; }
	const double sx = EW / SHEET_W, sy = FH / SHEET_H, gp = GAME_PX * sy, bk = opt_sigma / 0.689;   /* (px per game px;
	                                                    the beam width option: "thin" = 0.55 px at 1x for intensity 127) */
	const double cxm = ALG_MAX_X / 2.0, cym = ALG_MAX_Y / 2.0;
	const double par = par_dy ();
	const double gs = (opt_bend && load_f > 0) ? 1.0 + 0.6 * opt_bend_k * load_f * 10.0 / 128.0 : 1.0;   /* (bending: grow - x Bending strength) */
	const int n0 = npl;
	double tk_x = 0, tk_y = 0, tv_x = 0, tv_y = 0, hk_t = 0, hv_x = 0, hv_y = 0; int tk_ok = 0, hk_ok = 0;   /* (the last end: where it was, its map, its hum -
	                                                    the next piece starts at the same place and moment: the same values, done once) */
	for (int i = 0; i < n && npl < NW; i++) {
		const wline *l = &L[i];
		int z = l->b & 127; const int web = (l->b & 255) == 128;   /* (the blanked beam: drawn while the knob is up) */
		if (!z && !(web && up_kb > 0)) continue;         /* (intensity 0: dark) */
		double x0, y0, x1, y1;
		if (tk_ok && l->x0 == tk_x && l->y0 == tk_y) { x0 = tv_x; y0 = tv_y; } else tube_pt (l->x0, l->y0, &x0, &y0);
		tube_pt (l->x1, l->y1, &x1, &y1); tk_x = l->x1; tk_y = l->y1; tv_x = x1; tv_y = y1; tk_ok = 1;
		if (zl[i]) { x0 = x1; y0 = y1; }                /* (a resting spot: where the spot came to rest) */
		x0 = cxm + (x0 - cxm) * gs; x1 = cxm + (x1 - cxm) * gs; y0 = cym + (y0 - cym) * gs; y1 = cym + (y1 - cym) * gs;
		if (pw_sx != 1.0) { x0 = cxm + (x0 - cxm) * pw_sx; x1 = cxm + (x1 - cxm) * pw_sx; y0 = cym + (y0 - cym) * pw_sx; y1 = cym + (y1 - cym) * pw_sx; }
		if (opt_wob) { double ax, ay, bx, by; const double te = l->t + l->cyc;
			if (hk_ok && l->t == hk_t) { ax = hv_x; ay = hv_y; } else hum_at (l->t, opt_wob, &ax, &ay);
			hum_at (te, opt_wob, &bx, &by); hk_t = te; hv_x = bx; hv_y = by; hk_ok = 1;
			if (zl[i]) { ax = bx; ay = by; } x0 += ax; y0 += ay; x1 += bx; y1 += by; }
		x0 = (x0 - SHEET_X0) * sx + esh; x1 = (x1 - SHEET_X0) * sx + esh; y0 = (y0 - SHEET_Y0 + par) * sy; y1 = (y1 - SHEET_Y0 + par) * sy;
		/* the brightness and width (game px, FWHM) */
		const double zi = z / 127.0 + up_kb, cyc = l->cyc;   /* (the drive: the intensity + the brightness knob's bias) */
		if (zi <= 0) continue;
		double zk = zk_tab[z];
		if (up_kb != 0) { double d = zi * 127.0; d = d > 127 ? 127 : d; const int ii = d >= 126 ? 126 : (int) d;   /* (vecrender.py tube_light) */
			zk = pow (TUBE_LIGHT[ii] + (TUBE_LIGHT[ii + 1] - TUBE_LIGHT[ii]) * (d - ii), 1 / 2.2); }
		const double zw = zi >= BEAM_LOWZ ? 1.0 : BEAM_LOWW + (1.0 - BEAM_LOWW) * zi / BEAM_LOWZ;
		double fw = (BEAM_W0 + BEAM_WZ * zi) * zw * pw_width * up_width, kk, hx = 0; int dot = 0;   /* (up_width: R525's stiffer beam) */
		extern float vx_hdr_lift; const int dlk = vx_hdr_lift > 1.5f;   /* (the look: 1 Vectrex, 0 Bright) */
		const double lp = sqrt ((x1 - x0) * (x1 - x0) + (y1 - y0) * (y1 - y0));
		const int bt = vx_beam && l->lt >= 0;
		if (bt && bt_gpu) {                       /* (vecrender.py _build_np, beam: the light image takes the piece's light spread by its spot -
		                                             PH_FS_BEAM; the glass's glow from the same pieces, ph_frame) */
			const double upx = 134.0 * sy; double sb = BT_SG * fw * gp / 2.355 * BT_SPOT; if (sb < 0.5) sb = 0.5;
			double P = l->lt / (6.283185307 * (sb / upx) * (sb / upx)) * (1.0 - BT_HALO_F) * pw_gain * up_light * (L[i].wq / 255.0);
			if ((l->b & 128) && !web) P *= BEAM_FADE;
			if (!(P > 1e-7)) continue;
			float r = (float) P, g = r, b = r;
			if (vt_imager && opt_imager) { r = (l->rgb & 1) ? r : 0; g = (l->rgb & 2) ? g : 0; b = (l->rgb & 4) ? b : 0; if (!(l->rgb & 7)) r = g = b = 0; }
			pline *q = &pl[npl++];
			q->to = to; q->x0 = (float) x0; q->y0 = (float) y0; q->x1 = (float) x1; q->y1 = (float) y1;
			q->ax = (float) x0; q->ay = (float) y0; q->bx = (float) x1; q->by = (float) y1;
			{ extern float vx_bright; if (vx_bright > 0.5f) sb *= BL_WIDTH; }   /* (Bright: the beam wider, its peak kept - more light from a thin line) */
			q->k2 = (float) sb; q->R = (float) (3.5 * sb + 1.0); q->r = r; q->g = g; q->b = b; q->hx = (float) (BT_HALO_S * upx);   /* (k2: the spot's sigma, hx: the glow's - px) */
			{ const double a = l->ag > 0 ? l->ag - l->t / 1.5e6 : 0; q->ag = (float) (a < 0 ? 0 : a > 1 ? 1 : a); }   /* (the light's age: wl_from) */
			q->cut = 0; q->n0x = q->n0y = q->n1x = q->n1y = 0; q->c0 = q->c1 = 0;
			continue;
		}
		if (bt) {                                 /* (beam trace: the piece's light as its peak - a line's a unit / (sigma sqrt 2 pi), a spot's / (2 pi sigma^2),
		                                             sigma the light's (the display's / sqrt 2.2, here BT_SG x), as a display value) */
			fw *= BT_SPOT; const double su = BT_SG * fw / 2.355 * (GAME_PX / 134.0), lu = hypot (l->x1 - l->x0, l->y1 - l->y0) / 134.0;
			const double P = zl[i] || lu < 1e-6 ? l->lt / (6.283185307 * su * su) : l->lt / lu / (su * 2.506628275);
			kk = pow (BT_EXPO * BT_SPOT * P, 1 / 2.2) / OV_LINE_GAIN; dot = zl[i];   /* (the PC's screen value / the sheet's gain: the scale the picture's last step works in) */
		} else if (halo[i]) {
			fw = DOT_HALO_W[dlk] * zw / bk * pw_width;    /* (the glass's scatter: not the beam option's) */
			kk = pow (DOT_HALO[dlk] * dot_light ((l->b & 1024) ? cyc - DOT_C0 : cyc), 1 / 2.2); dot = 1;   /* (as a display value, as the lines' light) */
		} else if (!zl[i]) {
			double ee = lp > 1e-9 ? cyc / (lp / gp) : 1e9;
			kk = opt_speedb ? BEAM_LINE * (1.0 - BEAM_SPEED * exp (-ee / 0.8)) : BEAM_LINE * (1.0 - 0.5 * BEAM_SPEED);
			{ extern float vx_hdr_lift, vx_hdr_ee, vx_hdr_eq, vx_hdr_ec; if (vx_hdr_lift > 1.5f && ee > vx_hdr_ee) kk *= pow (fmin (ee / vx_hdr_ee, vx_hdr_ec), vx_hdr_eq); }   /* (HDR, the Vectrex look:
			                                                  a slow line brighter by the beam's time on it - a real tube, the test cartridge's bars) */
		} else if (!opt_dots) {                   /* (corner dots off: the resting spot as faint as a line's) */
			kk = BEAM_LINE * (1.0 - BEAM_SPEED);
		} else {
			const double ce = (l->b & 1024) ? cyc - DOT_C0 : cyc, al = dot_light (ce);   /* (joined: the beam slows into it - DOT_C0 less) */
			kk = DOT_K[dlk] * pow (al, 1 / 2.2); dot = 1;
			{ extern float vx_hdr_lift, vx_hdr_dk, vx_hdr_dx; if (vx_hdr_lift > 1.5f && vx_hdr_peak > 1.0f) { kk *= vx_hdr_dk; if (vx_hdr_peak > 1.0f) hx = hdr_rest (rest_cyc (L, n, i, zl, halo)) * zk * zk * vx_hdr_dx; }
			  else if (vx_hdr_peak > 1.0f) hx = hdr_rest (cyc) * zk; }   /* (the Vectrex look, HDR: the dots above the lines in their own channel - the lines keep the lower part) */   /* (HDR: above white, the rest's own light - with the slow beam round it) */
			if (zi < BEAM_LOWZ) { double rd = (zi - BEAM_Z0DOT) / (BEAM_LOWZ - BEAM_Z0DOT); rd = rd < 0 ? 0 : rd;
				double zd = (0.55 + 0.45 * BEAM_LOWZ) * rd * (cyc / BEAM_DOTLOWC < 1 ? cyc / BEAM_DOTLOWC : 1); if (zd > zk) zk = zd; }
			{ const double lr = al * TUBE_LIGHT[z] / DOT_WL;   /* (its width: the line's, up to DOT_W x with its light - a faint or
			                                                  short rest a slight thickening, a bright long one a dot) */
			  fw *= 1.0 + (DOT_W[dlk] - 1.0) * (lr < 1 ? lr : 1); }
		}
		kk *= (bt ? 1.0 : zk * BEAM_PEAK * PHOS_GAIN) * pw_gain * up_light * (L[i].wq / 255.0);   /* (up_light: the high voltage's light, unit.c) */
		if ((l->b & 128) && !web) kk *= BEAM_FADE;
		double sg = fw * gp / 2.355 * bk, sw = sg < 0.5 ? 0.5 : sg;   /* (at least half a pixel: a thinner spot as wide, its light kept) */
		kk *= dot ? (sg / sw) * (sg / sw) : sg / sw;
		hx *= (sg / sw) * (sg / sw) * pw_gain * up_light * (L[i].wq / 255.0); if ((l->b & 128) && !web) hx *= BEAM_FADE;
		if (kk < 1e-4) continue;
		float r = (float) kk, g = r, b = r;
		if (vt_imager && opt_imager) { hx = 0;  /* (the filter in front of the eye: its colour; no HDR light past it) */
			r = (l->rgb & 1) ? r : 0; g = (l->rgb & 2) ? g : 0; b = (l->rgb & 4) ? b : 0; if (!(l->rgb & 7)) r = g = b = 0; }
		pline *q = &pl[npl++]; q->ag = 0;
		q->to = to; q->x0 = (float) x0; q->y0 = (float) y0; q->x1 = (float) x1; q->y1 = (float) y1;
		q->ax = (float) x0; q->ay = (float) y0; q->bx = (float) x1; q->by = (float) y1;
		q->k2 = (float) (1.0 / (2.0 * sw * sw)); q->R = (float) (sw * 3.0); q->r = r; q->g = g; q->b = b; q->hx = (float) hx;
		q->cut = (!dot && lp > 1e-3) ? jo[i] : 0;          /* (open ends: cut below, when the neighbour is open too) */
		q->n0x = q->n0y = q->n1x = q->n1y = 0; q->c0 = q->c1 = 0;
	}
	/* the joints' cuts: the bisector of the two pieces' directions (a turn sharper than ~120 degrees: round ends) */
	if (!bgpu) for (int k = n0 + 1; k < npl; k++) {
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
	if (!bgpu) for (int k = n0; k < npl; k++) {                    /* (an open end without a cut: round; a cut end: the segment
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
 * the lines between the two game frames around it, stroke by stroke where a stroke's partner in the other frame is
 * certain (smooth.c: paired by place; none moved more than SMOOTH_MATCH units); the others as drawn. The phosphor (vecrender.py PHOS_A, the
 * afterglow measured on the owner's Vectrex, at the owner's time scale 0.6): what was lit and is not lit again starts
 * to glow away, in two parts (14 % fading with 27 ms, 5.8 % with 210 ms) */
#define SMOOTH_MATCH 2500
#include "smooth.c"                       /* (sm_draw, sm_merge: included below the things they use) */
VXS_ON
static unsigned long long run_t; static double run_frac;   /* (v0.13: saved) */
VXS_OFF
static long sm_runs, sm_mixed;   /* (test: refreshes, those between two frames) */
           /* (the emulated time this refresh has to reach, cycles) */
static float *pb[2][3], *g1b[3], *g2b[3]; static int pbi;

/* the picture's buffers at its size (a game's start; a new size, area or rotation): the lit light now and a refresh ago,
 * the afterglow's two parts, one colour channel or three - all dark again */
static int deck_auto_mode (void)
{
	const int a = scr_w > scr_h ? scr_w : scr_h, b = scr_w > scr_h ? scr_h : scr_w;
	return b > 0 && a < 1.4 * b ? 3 : 0;   /* (the deck does not fit below the picture: a 4:3 or a square screen (RGB30) - the pop-up) */
}
int vx_app_pich;                                /* (the own iOS app: the picture height it has room for, px; 0: the screen's) */
int vx_app_noshape;                            /* (set by a host that lays out the picture itself: the deck block exactly the
                                                picture and the deck, no bars to the screen's shape - the own iOS app) */
static void pic_setup (void)
{
	const int glass = area_glass (); geom_calc (glass, &EW, &FH, &CW, &CH);
	S = (int) (FH / (double) PH + 0.5); if (S < 1) S = 1;   /* (the glow's cells: 2 x 2 per this many) */
	FW = stereo ? EW * 2 : EW; CX0 = (EW - CW) / 2; CY0 = (FH - CH) / 2; OW = stereo ? CW * 2 : CW; OH = CH;
	geo_glass = glass; geo_rot = opt_rot; geo_size = opt_size; geo_deck = opt_deck; geo_shape = opt_shape; geo_sw = scr_w; geo_sh = scr_h;
	/* (the control deck: the picture and the deck one block, black bars to the screen's shape - deck.c) */
	dk_on = (opt_deck == 1 || opt_deck == 2) && !stereo; dk_pop = opt_deck == 3 && !stereo; TW = OW; TH = OH; BX = BY = 0; DKH = 0;
	if (dk_on || dk_pop) DKH = (int) ((opt_deck == 2 ? 0.046 : 0.08134) * FH / 0.924 + 0.5);   /* (slim: one low row) */
	dk_y = dk_pop ? CH - DKH : CH; dk_dirty_set = 1;
	if (dk_on) {
		const double A = opt_shape ? 16.0 / 9.0 : 4.0 / 3.0; const int bw = CW, bh = CH + DKH;
		/* (an upright screen - a phone, a tablet held upright - the picture not turned: the block in the screen's own
		 *  shape, the deck below the picture; the 4:3 / 16:9 shapes are wide ones - an iPad showed the block small) */
		const int own = scr_w > 0 && !(opt_rot & 1) && (scr_h > scr_w || VX_IOS);   /* (iPhone / iPad: either way round) */
		const double ab = own ? (double) scr_w / scr_h : (opt_rot & 1) ? 1.0 / A : A;
		TW = (int) (bh * ab + 0.5); if (TW < bw) TW = bw; TH = (int) (bw / ab + 0.5); if (TH < bh) TH = bh;
		if (vx_app_noshape) { TW = bw; TH = bh; }           /* (the own iOS app: it places the picture and the deck itself) */
		BX = (TW - bw) / 2; BY = (TH - bh) / 2; OW = TW; OH = TH; dk_dirty_set = 1; }
#ifndef VX_EMUVR
	{ static int lw, lh; if (OW != lw || OH != lh) { lw = OW; lh = OH;   /* (the log only) */
		fprintf (stderr, "[vectrex_emu] Picture %d x %d - screen %d x %d\n", OW, OH, scr_w, scr_h); } }
#endif
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
/* (2026-11-25, route (a): EmuVR too - the PC's beam on the phosphor on the GPU; there the glass, the case, the room's light and
 *  the overlay stay off (the mod and EmuVR draw them), the sheet's picture and the 3D Imager's eye pair on the GPU as well) */
#ifdef VX_EMUVR
#define VX_GPU_ALL 1                        /* (the GPU light also for the sheet's picture and the eye pair) */
#else
#define VX_GPU_ALL 0
#endif
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
#ifdef VX_EMUVR
	/* (EmuVR: the light alone - its own glass, case, room light and the mod's overlay are in the VR room: none drawn here) */
	gfx.glass = gfx.ceiling = gfx.cse = gfx.fx = 0; gfx.lk = gfx.grey = gfx.croom = gfx.crefl = gfx.cwear = 0.0f; glass_grey = 0.0f;
	ovfx.ovl = ovfx.glow = 0.0f; ovfx.scr = 0;
#endif
}
#include "gpu_opt.c"
/* the own apps' first start (and again after an update that measures anew, or on a bigger screen): each screen rate
 * measured once, at the largest picture this screen can show - upright or across, the deck off - with the overlay on,
 * so no game is held up later (2026-11-24). lng / shrt: the screen's sides in pixels; rates: those it can run. dry: only
 * how many would be measured. Returns that count. Called on the game thread, the context current, after a refresh */
static char tune_report[1200];
const char *vx_app_tune_report (void) { return tune_report; }   /* (the last tuning's result, a line a rate) */
void vx_app_tune_status (char *o, int n) { snprintf (o, (size_t) n, "%s", opt_status); }   /* (the step running now) */
int vx_app_tune (int lng, int shrt, const int *rates, int n, int dry)
{
	if (!gpu_ok || !ph_ok || stereo || vx_nobuild || FW <= 0 || FH <= 0 || n <= 0 || lng <= 0 || shrt <= 0) return 0;
	const double su = fmin ((double) shrt / FW, (double) lng / FH), sa = fmin ((double) lng / FW, (double) shrt / FH);
	double s = su > sa ? su : sa; if (s < 1.0) s = 1.0;   /* (upright or across: the larger picture; never smaller than now) */
	const long px = (long) (FW * s) * (long) (FH * s);
	int need = 0, todo[16]; for (int i = 0; i < n && i < 16; i++) { prow v; int rg = 0; long pg = 0; todo[i] = !(opt_lookup (rates[i], px, &v, &rg, &pg) && rg == rates[i]); need += todo[i]; }
	if (dry || !need) return need;
	const int keep_size = opt_size, keep_rate = opt_rate;
	opt_size = (int) (opt_size * s + 0.5); pic_setup ();
	fprintf (stderr, "[vectrex_emu] Optimize: tuning this device - %d rate%s at %d x %d (screen %d x %d), the overlay on\n", need, need > 1 ? "s" : "", FW, FH, lng, shrt);
	tune_report[0] = 0;
	for (int i = 0; i < n && i < 16; i++) if (todo[i]) { opt_rate = rates[i]; prow v = opt_measure (); opt_store (rates[i], (long) FW * FH, &v);
		const size_t k = strlen (tune_report);   /* (the player's report: each rate, all on or what went off) */
		if (!opt_last_off[0]) snprintf (tune_report + k, sizeof tune_report - k, "%s%d Hz: Every effect on, full quality - %.0f %% of each picture's time used", k ? "\n" : "", rates[i], 100 * opt_last_share);
		else snprintf (tune_report + k, sizeof tune_report - k, "%s%d Hz: Full speed with these effects off: %s%s", k ? "\n" : "", rates[i], opt_last_off, opt_last_fit ? "" : " - still short of time, a busy game may slow down"); }
	opt_rate = keep_rate; opt_size = keep_size; pic_setup (); ph_made = 0; opt_key_seen[0] = 0; opt_pending = 0; fx_apply ();
	return need;
}
static int bt_gpu_ok (void) { return gpu_asked && gpu_ok && ph_ok && ph_beam && ph_sat && (VX_GPU_ALL || (!stereo && !sheet_file)); }   /* (as retro_run's gph) */
int gpu_frame_shown (void) { return gpu_ok && !stereo && !(ov && ov_on && !sheet_file) && (gfx.cse || gfx.glass); }
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
void (*vx_emu_thread_hook) (void);              /* (the own apps: called on the emulation thread as it starts - the Android app puts
                                                   it on the fastest core from there: moved from outside, Android refused it) */
static void *emu_main (void *p)
{
	(void) p;
#ifdef __ANDROID__
	pthread_setname_np (pthread_self (), "vx-emu");      /* (named: the Android app's thread figures) */
#endif
	if (vx_emu_thread_hook) vx_emu_thread_hook ();
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
	double st = 1.5e6 / opt_rate;
	if (netvx_playing ()) { extern double netvx_pace (void); st *= 1.0 - netvx_pace (); }   /* (online: the side ahead a little slower - netvx.c) */
	run_t += (unsigned long long) (st + 0.5);
	run_frac += st - (double) (unsigned long long) (st + 0.5);
	if (run_frac >= 1) { run_t++; run_frac -= 1; } else if (run_frac <= -1) { run_t--; run_frac += 1; }
	if (!pw_on) run_t = total_cycles;
}
static int emu_stood;                      /* (online: this run waited for the other side's input and made no game frame) */
static void emu_loop (void)                 /* (game frames until the emulated time has reached run_t) */
{
	extern int vt_imager;
	emu_stood = 0;
	uint32_t replay_to = 0; int most = 8;
	if (netvx_playing () && nv_stream && nv_role == 2) { netvx_stream_guest (); return; }   /* (streamed: player 2 runs nothing - the host's frames) */
	if (netvx_playing ()) { replay_to = netvx_rollback (); if (replay_to) most = 8 + NV_RB_W; }   /* (online: a guess was wrong - back
	                                              to that frame's state, the frames since run again with the right input, unheard) */
	for (int k = 0; total_cycles < run_t && k < most; k++) {
		extern unsigned long long vt_piece_t0;
		unsigned long long p0 = vt_piece_t0;   /* (the frame being drawn began here: its lines' times count from it) */
		if (ls_state == 2 || netvx_playing ()) { unsigned char lm[8];   /* (online in lockstep: a game frame only with both players' input for it) */
			if (!(ls_state == 2 ? netls_frame (lm) : nv_stream ? netvx_stream_input (lm) : netvx_frame (lm))) { run_t = total_cycles; emu_stood = !k; break; }
			apply_input (lm); }
		abuf_n = 0;
		{ static int dp = -1; if (dp < 0) dp = getenv ("VX_DIRTY") != NULL;   /* (test: a card game's frame - its time, the memory pages it changed) */
		  if (dp) { extern int vt_uvm2, vt_pitrex, uvm2_dirty_probe (int *), pitrex_dirty_probe (int *); static double tsum, tmax; static long fn, psum, pmax; static int tot;
			const double t0 = vt_wall (); run_frame (); const double t1 = vt_wall () - t0;
			const int pg = vt_uvm2 ? uvm2_dirty_probe (&tot) : vt_pitrex ? pitrex_dirty_probe (&tot) : 0;
			if (fn > 0) { tsum += t1; if (t1 > tmax) tmax = t1; psum += pg; if (pg > pmax) pmax = pg; }
			if (++fn % 200 == 0) { fprintf (stderr, "DIRTY frames %ld: emulation %.2f ms a frame (most %.2f), pages changed %.1f a frame (most %ld) of %d - %.0f KB a frame%c",
				fn, tsum / 199 * 1e3, tmax * 1e3, psum / 199.0, pmax, tot, psum / 199.0 * 4, 10); tsum = tmax = 0; psum = pmax = 0; } }
		  else run_frame (); }
		const int replay = replay_to && nv_f < replay_to;   /* (a frame run again: its sound was heard the first time) */
		if (ls_state == 2) netls_after (); else if (netvx_playing () && !nv_stream) netvx_after ();
		{ extern int vt_piece_cut; frame_push (p0, vt_piece_cut); }
		if (vt_imager) eyes_take ();
		{ const int a0 = aq_w; if (!replay) audio_frame ();
		  if (nv_stream && netvx_playing ()) netvx_stream_send (p0, a0); }   /* (streamed: the host's frame to player 2) */
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
static int emu_threaded (void) { return opt_async && !vx_ok () && ls_state != 2; }   /* (lockstep: here - RetroArch's network is called from this thread) */
static void emu_wait_ext (void) { emu_wait (); }   /* (UVMC2 / PiTrex: no save states, no run-ahead) */
#include "usbkm.c"
#ifndef VX_EMUVR
#include "pxfetch.c"                        /* (2026-11-18: the PiTrex's files fetched by the core - the browser's "PiTrex: get it") */
#include "browser.c"
#include "deck.c"
#include "dbgsend.c"                       /* (2026-11-18: the private iOS test build's logs, status, screenshots, crash reports to the server) */
#endif
/* the picture out: the GPU's, or the CPU's frame (with the control deck: its block) - turned (also a paused refresh's) */
static void frame_out (int shot_now, const uint32_t **pic_o, int *pw_o, int *ph_o)
{
	const uint32_t *pic = ob; int pw = OW, ph = OH; const int Wd = FW;   /* (what is sent, and shot) */
	if (gpu_asked && !shot_now) {               /* (1.1: the GPU: up, turned, the glass and the case on top - or, the context not
	                                               there yet, the last picture again) */
		if (gpu_ok && (Wd / EW == 1 || VX_GPU_ALL)) gpu_present (ph_ok && ph_made && (VX_GPU_ALL || !stereo) ? NULL : fb, FW, FH, Wd, (float) CX0 / EW, 1.0f - (float) (CY0 + CH) / FH, (float) (CX0 + CW) / EW, 1.0f - (float) CY0 / FH, CW, CH, opt_rot);
		else video_cb (NULL, (unsigned) ((opt_rot & 1) ? OH : OW), (unsigned) ((opt_rot & 1) ? OW : OH), 0);
	} else
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
/* (the own apps' log: where a refresh's processor time goes - emulation, Smooth motion, the picture's lines, the afterglow's lines,
 *  the GPU's commands - each refresh's added up, their largest kept; the app reads and restarts them every 5 s: vx_app_cpu_split) */
static double cs_now[5], cs_sum[5], cs_max[5]; static long cs_n;
void vx_app_cpu_split (double mean[5], double most[5])   /* (ms a refresh, since the last call) */
{
	for (int k = 0; k < 5; k++) { mean[k] = cs_n ? cs_sum[k] / cs_n * 1e3 : 0; most[k] = cs_max[k] * 1e3; cs_sum[k] = cs_max[k] = 0; }
	cs_n = 0;
}
static void retro_run_one (void);
void retro_run (void)
{
	memset (cs_now, 0, sizeof cs_now); retro_run_one ();
	for (int k = 0; k < 5; k++) { cs_sum[k] += cs_now[k]; if (cs_now[k] > cs_max[k]) cs_max[k] = cs_now[k]; } cs_n++;
}
static void retro_run_one (void)
{
	if (pf_on < 0) pf_on = getenv ("VXPOC_PROF") != NULL;
	double pf0 = pf_on ? pf_now () : 0, pf1 = 0, pf2 = 0, pf3 = 0;
	wd_runs++; wd_thr_set ();
	emu_wait ();                                /* (1.1: the refresh to show, emulated meanwhile - UVMC2 / PiTrex) */
	net_runs++;
	{ static long ts_at = -2; static char ts_to[1100];   /* (test: VX_TEST_SWITCH="refresh:game" - the browser's switch at that refresh) */
	  if (ts_at == -2) { const char *e = getenv ("VX_TEST_SWITCH"); ts_at = -1; if (e && sscanf (e, "%ld:%1099[^\n]", &ts_at, ts_to) != 2) ts_at = -1; }
	  if (ts_at > 0 && (long) net_runs == ts_at) game_switch (ts_to); }
	km_send ();                                 /* (RetroArch's keyboard and mouse to a UVMC2 / PiTrex: usbkm.c) */
	if (sw_next[0]) { char p[1100]; snprintf (p, sizeof p, "%s", sw_next); sw_next[0] = 0;   /* (the ROM browser's / eject's game) */
		if (game_switch_now (p)) { if (sw_eject == 1) ej_out = 1; else { ej_out = 0; snprintf (cur_game, sizeof cur_game, "%s", p); }
#ifndef VX_EMUVR
			if (sw_eject != 1) { snprintf (br_game, sizeof br_game, "%s", p); br_rec_note (p); }
#endif
		} sw_eject = 0; }
	netvx_hot_apply ();                         /* (online: the host's running game, once its cartridge is in) */
	const int asy = emu_threaded (), primed = asy && emu_primed;
	bool upd = false;
	if (env_cb (RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE, &upd) && upd) { read_options (); input_names (); }
	poll_cb ();
	unsigned char m[8] = { 128, 128, 0, 0x40, 128, 128, 0, 0 };
	pad (0, &m[0], &m[1], &m[2]); pad (1, &m[4], &m[5], &m[6]);
#ifndef VX_EMUVR
	tate_note ();
#endif
	{ static int told; const int rb = net_runs - net_seen < 180;   /* (RetroArch's netplay with a RetroPad port: said once) */
	  if (rb && !told && ((port_dev[0] & RETRO_DEVICE_MASK) == RETRO_DEVICE_JOYPAD || (port_dev[1] & RETRO_DEVICE_MASK) == RETRO_DEVICE_JOYPAD)) {
		struct retro_message msg = { "Netplay: for the sticks, Port 1 and Port 2 as Vectrex controller (Controls)", 480 }; env_cb (RETRO_ENVIRONMENT_SET_MESSAGE, &msg); told = 1; }
	  if (!rb) told = 0; }
	ls_pad[0] = m[0]; ls_pad[1] = m[1]; ls_pad[2] = m[2];   /* (online in lockstep: this side's controller - netls.c) */
	netls_solo_tick ();
#ifndef VX_EMUVR
	{ const int bi = br_input (m);              /* (the browser reads player 1 as the game does: turned in tate) */
	  if (bi) { m[0] = m[1] = 128; m[2] = bi == 2 ? m[2] & 6 : 0; }   /* (the ROM browser open: the game gets none; just closed by a button:
	                                              buttons 2 and 3 only, until 1, 4 and the stick are let go) */
	  if (lib_cal_until > 0) { if (vt_wall () < lib_cal_until) m[2] |= 6; else lib_cal_until = 0; } }   /* (the library's Calibration
	                                              (2 + 3 held): held 30 s - library.c) */
	if (px_redo) { const int r = px_redo; px_redo = 0;   /* (the PiTrex's download: its progress; done - the list again, it chosen) */
		if (r & 1 && br_open) { char px[1200]; br_px_boot (px, sizeof px); if (lib_on) lib_scan (px); else br_scan (br_dir, px); }
		br_dirty = 1; }
	if (br_dirty || (br_open && !br_shown)) br_draw ();
	dbg_tick ();
	{ static int pz; const int pi_ = btn_id ("vectrex_emu_btn_pause"), p_ = pi_ >= 0 && input_cb (0, RETRO_DEVICE_JOYPAD, 0, (unsigned) pi_) != 0;
	  if (p_ && !pz && !br_open && !net_on ()) { vx_paused = !vx_paused; dk_dirty_set = 1; } pz = p_;
	  if (net_on () && vx_paused) { vx_paused = 0; dk_dirty_set = 1; } }   /* (netplay: RetroArch's own pause) */
	dk_pointer (); dk_tick (0);
	{ static int l2, r2; const int ri = btn_id ("vectrex_emu_btn_reset"), ei = btn_id ("vectrex_emu_btn_eject");
	  const int a = ri >= 0 && input_cb (0, RETRO_DEVICE_JOYPAD, 0, (unsigned) ri) != 0, b = ei >= 0 && ei != ri && input_cb (0, RETRO_DEVICE_JOYPAD, 0, (unsigned) ei) != 0;
	  if (a && !l2 && !br_open && netvx_playing ()) ls_reset_req = 1;   /* (online, the app's: both players, the same game frame) */
	  else if (a && !l2 && !br_open && !net_on ()) { extern void vecx_reset_button (void); emu_wait_ext (); vecx_reset_button (); }   /* (the RESET button;
	                                              netplay: RetroArch's own Reset, sent to both) */
	  if (b && !r2 && !br_open && !net_on () && !netvx_playing ()) eject_toggle ();
	  l2 = a; r2 = b; }
	if (kb_ev) { const unsigned e = kb_ev; kb_ev = 0;   /* (the desktop's shortcut keys - usbkm.c kb_desk; netplay: none, as the buttons) */
		if (!net_on ()) {
			if (e & KE_BROWSER) br_show (!br_open);
			if (e & KE_ESC && br_open) br_show (0);
			if (e & KE_PAUSE && !br_open) { vx_paused = !vx_paused; dk_dirty_set = 1; }
			if (e & KE_RESET && !br_open) { if (netvx_playing ()) ls_reset_req = 1; else { extern void vecx_reset_button (void); emu_wait_ext (); vecx_reset_button (); } }
			if (e & KE_EJECT && !br_open && !netvx_playing ()) eject_toggle ();
			if (e & KE_OVERLAY && ovl_manual != 1 && (opt_overlay || !sheet_file)) ov_flip ("O"); }
		if (e & KE_VOLUP) turn_volume (0.05);
		if (e & KE_VOLDN) turn_volume (-0.05); }
#endif
	{ int sel = opt_ov_btn ? input_cb (0, RETRO_DEVICE_JOYPAD, 0, opt_ov_btn == 2 ? RETRO_DEVICE_ID_JOYPAD_SELECT : RETRO_DEVICE_ID_JOYPAD_START) : 0;   /* START (or SELECT): the overlay off / on */
	  if (sel && !sel_held && ovl_manual != 1 && (opt_overlay || !sheet_file)) ov_flip (opt_ov_btn == 2 ? "SELECT" : "START");   /* (v0.25: the automatic overlay on / off; a hand-placed one stays) */   /* (v0.20: a hand-placed one comes off first) */
	  sel_held = sel; }
#ifndef VX_EMUVR
	fx_apply ();
	{ static int lr = -1, ld = -1; static double ts; const double now = vt_wall ();   /* (Picture size auto again: the rotation or
	                                              the deck changed; on an iPhone / iPad also the device turned - looked at once a second) */
	  if (opt_rot != lr || opt_deck != ld || (VX_IOS && now - ts > 1.0)) { lr = opt_rot; ld = opt_deck; ts = now; if (gpu_ok) scr_detect (); } }
	{ extern int vx_app_pich;                   /* (the own iOS app: the picture's height it has room for, its own pixels -
	                                              Picture size auto follows it, 1:1, as the layout changes) */
	  if (size_auto && vx_app_pich >= 200 && vx_app_pich != opt_size) { opt_size = vx_app_pich; fprintf (stderr, "[vectrex_emu] picture size: the app's %d px\n", vx_app_pich); scr_new = 0; } }
	if (scr_new && !(size_auto && vx_app_pich >= 200)) { scr_new = 0; if (size_auto && scr_short && scr_short != opt_size) { opt_size = scr_short; fprintf (stderr, "[vectrex_emu] picture size: the screen's %d px\n", scr_short); } }
	if (deck_auto) opt_deck = deck_auto_mode ();   /* (the screen known or turned: auto chooses again) */
	if (area_glass () != geo_glass || opt_rot != geo_rot || opt_size != geo_size || opt_deck != geo_deck || opt_shape != geo_shape
	    || (dk_on && (scr_w != geo_sw || scr_h != geo_sh))) { pic_setup (); geo_tell (); }   /* (with the deck: the screen turned - its shape) */
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
	{ const double c0 = vt_wall (); if (!primed) { run_t_next (); emu_loop (); if (asy) emu_primed = 1; } cs_now[0] += vt_wall () - c0; }
	const int stood = emu_stood && (ls_state == 2 || netvx_playing ());   /* (read before the next run starts) */
	if (pf_on) pf2 = pf_now ();
	{ int av = 3; vx_nobuild = env_cb (RETRO_ENVIRONMENT_GET_AUDIO_VIDEO_ENABLE, &av) && !(av & 1); }
	/* the picture: which lines, from which game frames */
	const double now = run_t / 1.5e6;
	int npl = 0; unsigned long long ft0 = nh ? hist[nh - 1].p0 : 0;
	/* (the picture's moment for the light's age, wl_from: now - a game slower than the screen (hold_lines: each line lit from its own
	 *  moment until drawn again, as the PC's fixed refresh); a frame a refresh (the game's 50 Hz), as the PC's G-Sync: the frame shown
	 *  as it ends - the moment its end, a refresh on each time it is shown again (ag_c, the plain branch below) */
	static unsigned long long ag_p0 = ~0ull; static double ag_c;
	{ extern unsigned long long vt_piece_t0;
	  wl_tshow = now; wl_off = (double) total_cycles - (double) vt_piece_t0;   /* (the newest frame ended as the next piece began) */
	  if (nh) { const gframe *g = &hist[nh - 1]; if (g->p0 != ag_p0) { ag_p0 = g->p0; ag_c = g->t - now; } } }
	bt_gpu = vx_beam && bt_gpu_ok ();
	pw_gain = 1; pw_sx = 1; pw_width = 1;
	if (opt_power && pw_on && pw_t < pw_fade) {   /* (switched on: the light comes up, slowly at first; a warm tube bulges) */
		double f = pw_t / pw_fade; pw_gain = pow (f * f * (3 - 2 * f), 2.6);
		if (pw_t < 0.5 && pw_bulge > 0) pw_sx = 1.0 + pw_bulge * sin (M_PI * pw_t / 0.5) * (1.0 - pw_t / 0.5) * 2.0; }
	if (!pw_on) {                              /* (switched off: the zap once, or the picture collapsing; then only the afterglow) */
		if (opt_power && pw_mode == 0 && pw_zap_done != pw_events && !vx_nobuild) {
			pw_zap_done = pw_events; pw_gain = 1.2; memcpy (W1, pw_streak, sizeof (wline) * pw_nstreak); for (int i = 0; i < pw_nstreak; i++) W1[i].wq = 255;
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
			if (opt_imager >= 3 && e != ((opt_imager == 3) ^ opt_swap)) continue;   /* (one eye: e 1 the left, e 0 the right - "swap eyes" turns it round) */
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
		int n = 0; int sm_ex = 0; const gframe *sm_ex_a = NULL, *sm_ex_cur = NULL;
		const double hper = nh > 1 ? (hist[nh - 1].t - hist[0].t) / (nh - 1) : 0;
		const int slow = nh > 1 && hper > HOLD_SLOW * dt;   /* (a game slower than the screen: hold_lines) */
		if (opt_smooth && opt_rate != 50) {
			/* (Smooth motion, as the desktop's: one game frame and one refresh back, the frames a and b round that moment -
			 *  their lines in between, stroke by stroke where a stroke's partner is certain (smooth.c); without a b, a slow
			 *  game's lines as held (hold_lines) at the same moment) */
			double per = nh > 1 ? (hist[nh - 1].t - hist[0].t) / (nh - 1) : 0.02;
			per = per < 0.01 ? 0.01 : per > 0.1 ? 0.1 : per;
			const double D = now - per - dt;
			if (D < hist[0].t - 0.25) while (nh > 1) frame_pop ();
			if (!slow) while (nh > 2 && hist[1].t <= D) frame_pop ();   /* (the frames before D: only a slow game's hold keeps them) */
			int ia = 0; while (ia + 1 < nh && hist[ia + 1].t <= D) ia++;
			const gframe *a = &hist[ia], *b = ia + 1 < nh && hist[ia + 1].t > a->t ? &hist[ia + 1] : NULL;
			const gframe *pa = ia ? &hist[ia - 1] : have_prev ? &prevf : NULL;
			double alpha = b ? (D - a->t) / (b->t - a->t) : 0; alpha = alpha < 0 ? 0 : alpha > 1 ? 1 : alpha;
			const line_t *B = NULL;
			/* (line for line: a line that jumped - a thing gone round the screen's edge, a new shape in its place - is not
			 *  moved, it stands in a's place until halfway and in b's after; the others still move. One such line took the
			 *  in-between away from the whole picture before: that refresh every moving thing stood at a game frame's place
			 *  instead of between two - gaps and pairs in every trail, a stutter (the owner's iPhone, 2026-11-22) */
			 /* (and the two frames need not have as many lines: the beam's path is cut into pieces a little differently each
			 *  frame - the Test Cart's 146, 145, 144 - and a piece more or less took the in-between away as well; strokes are
			 *  paired by place, smooth.c - one without a certain partner is such a standing line) */
			if (b && alpha > 0) B = b->L;
			sm_runs++; if (B) sm_mixed++;
			sm_exc = 0;
			if (B && !fx_half && !vx_nobuild) {             /* (Low draws every other picture: all a frame later there) */
				if (a->p0 != sm_exf_id) { unsigned char *t = sm_exfp; const int tc = sm_exfp_cap; sm_exfp = sm_exf; sm_exfp_cap = sm_exf_cap; sm_exfp_n = sm_exf_n; sm_exfp_id = sm_exf_id;
					sm_exf = t; sm_exf_cap = tc; if (sm_exf_cap < a->n) { free (sm_exf); sm_exf_cap = a->n + 256; sm_exf = malloc ((size_t) sm_exf_cap); }
					if (sm_exf) memset (sm_exf, 0, (size_t) a->n); sm_exf_n = sm_exf ? a->n : 0; sm_exf_id = a->p0; }
				if (sm_exi_cap < a->n) { free (sm_exi); sm_exi_cap = a->n + 256; sm_exi = malloc (sizeof (int) * (size_t) sm_exi_cap); }
				sm_ex_cur = sm_exf && sm_exi && sm_exf_id == a->p0 ? a : NULL; }
			if (B) { sm_ex = 1; sm_ex_next = a->p0; if (a->p0 != sm_ex_shown) sm_ex_a = pa; }   /* (the afterglow: from the game's own frames - the one before a, as its place is left a whole step behind: gpu_phos.c ph_ex_mode) */
			if (slow && !B) n = vx_nobuild ? 0 : hold_lines (D, dt);   /* (run-ahead's hidden runs: no picture, no hold) */
			else {
				if (a->alt && pa) n = wl_from (W1, n, pa->L, pa->n, NULL, 0);   /* (a picture in halves: both) */
				if (!B) n = wl_from (W1, n, a->L, a->n, NULL, 0);
				else {
					const double c0 = vt_wall (); n = sm_draw (a, b, alpha, n, sm_ex_cur != NULL); cs_now[1] += vt_wall () - c0;   /* (stroke by stroke: smooth.c) */
				} }
			/* (the hum at the nearest real frame's moment: the beam draws only when the game draws) */
			ft0 = (b && alpha >= 0.5) ? b->p0 : a->p0;
		} else if (slow) {
			n = vx_nobuild ? 0 : hold_lines (now, dt); ft0 = hist[nh - 1].p0;
		} else {
			const gframe *a = &hist[nh - 1];
			wl_tshow = now + ag_c;                     /* (G-Sync's moment: the frame's end, then on by the refresh) */
			/* (the apps: a game frame that a catch-up run hid - two runs in one refresh, the first without a picture: the screen
			 *  slower than the game, an iPhone's 48 Hz for 50 - is owed its light: its lines that the newest frame does not draw
			 *  again (they moved) go into this picture too. Without it that frame's copy was missing from every trail, twice a
			 *  second - the owner's iPhone, 2026-11-22. A still line is in the newest frame: not drawn twice) */
			{ static unsigned long long shown_p0;
			  if (vx_owe_hidden && !vx_nobuild && !a->alt && nh > 1 && shown_p0 && hist[nh - 2].p0 > shown_p0) { const gframe *h = &hist[nh - 2];
				hold_new (0); for (int q = 0; q < a->n; q++) if (a->L[q].c & 127) hold_put (&a->L[q], 1.0);
				for (int q = 0; q < h->n; q++) if ((h->L[q].c & 127) && hold_find (&h->L[q], 0.0, -1) < 0) n = wl_from (W1, n, &h->L[q], 1, NULL, 0); }
			  if (!vx_nobuild) shown_p0 = a->p0; }
			if (a->alt) { const gframe *p = nh > 1 ? &hist[nh - 2] : have_prev ? &prevf : NULL; if (p) n = wl_from (W1, n, p->L, p->n, NULL, 0); }
			n = wl_from (W1, n, a->L, a->n, NULL, 0);
			ft0 = hist[nh - 1].p0;
			while (nh > 4) frame_pop ();
		}
		if (opt_wob) hum_coeffs (ft0 / 1.5e6, opt_wob);
		{ const double c0 = vt_wall (); npl = build_lines (W1, n, 0, 0, npl); cs_now[2] += vt_wall () - c0; }
		/* (Smooth motion: the game frame's own lines, once a frame, after the picture's - where the afterglow starts: gpu_phos.c ph_ex_mode) */
		const double c0 = vt_wall ();
		ph_ex_mode = sm_ex && !vx_nobuild; ph_ex_n = 0;
		if (ph_ex_mode) { int n2 = 0, ne = 0;
			/* (the pieces of one straight line as one line, as the PC's: smooth.c sm_merge) */
			static line_t *exl; static int exl_cap; const int want = (sm_ex_cur ? sm_exc : 0) + (sm_ex_a ? sm_ex_a->n : 0);
			if (exl_cap < want) { free (exl); exl_cap = want + 256; exl = malloc (sizeof (line_t) * (size_t) exl_cap); if (!exl) exl_cap = 0; }
			if (exl) {
				if (sm_ex_cur) for (int k = 0; k < sm_exc; k++) exl[ne++] = sm_ex_cur->L[sm_exi[k]];   /* (a's lines the gliding ones have just cleared) */
				if (sm_ex_a) { const int fl = sm_exfp && sm_exfp_id == sm_ex_a->p0;   /* (the frame before: what has not started yet) */
					for (int i = 0; i < sm_ex_a->n; i++) if (!(fl && i < sm_exfp_n && sm_exfp[i])) exl[ne++] = sm_ex_a->L[i]; }
				n2 = sm_merge (0, exl, ne); }
			if (n2) ph_ex_n = build_lines (W1, n2, 0, 0, npl) - npl; }
		cs_now[3] += vt_wall () - c0;
	}
	/* v0.13: a refresh RetroArch does not show (run-ahead's hidden runs: the video off) draws no picture - after the
	 * choice of frames above, which keeps the kept frames' list the same whether a refresh is shown or not */
	{ int av = 3; const int avk = env_cb (RETRO_ENVIRONMENT_GET_AUDIO_VIDEO_ENABLE, &av);
	  { static int told; if (!told) { told = 1; fprintf (stderr, "[vectrex_emu] hidden refreshes (run-ahead): the frontend %s\n", avk ? "says which" : "does not say - each refresh drawn"); } }
	  if (avk && !(av & 1)) { vx_hidden++; audio_out (); if (asy) { flash_save (0); emu_start (); } return; } }
	vx_shown++;
	if (asy) { audio_out (); flash_save (0); emu_start (); }   /* (threaded: this refresh's sound out, the next one's emulation
	                                              started - the drawing below reads only the pieces built above) */
	if (pf_on) pf3 = pf_now ();
	/* the bands: the lines, the phosphor, then (no glow) the knee and the 8-bit picture - each band on its own core */
	const int Wd = FW, H = FH; const size_t NP = (size_t) Wd * H;
	/* (1.1: the light on the GPU - gpu_phos.c; the CPU's bands below otherwise) */
	const int gph = gpu_asked && gpu_ok && ph_ok && (VX_GPU_ALL || (!stereo && !sheet_file));
	static int half_skip; static double half_dt;
	if (gph) gq_begin ();                      /* (the GPU meter: this picture's GPU time, the light and the glass) */
	if (stood && !shot_due && (!gph || ph_made)) {   /* (online, waiting for the other side: the picture as it was, as paused - the
		                                       *  held frame's light added each refresh made it glow brighter and brighter) */
		if (gph) gpu_present (NULL, FW, FH, FW, (float) CX0 / EW, 1.0f - (float) (CY0 + CH) / FH, (float) (CX0 + CW) / EW, 1.0f - (float) CY0 / FH, CW, CH, opt_rot);
		else frame_out (0, NULL, NULL, NULL);
		if (nv_stream && nv_role == 2 && !nv_pause) half_dt += dt;   /* (streamed, player 2: the host's time goes on between its frames
		                                       *  (50 a second, 120 refreshes here) - the afterglow fades over it too; it lasted 2.5x long) */
		sheet_write (); if (!asy) flash_save (0); if (!asy) audio_out ();
		return;
	}
	if (gph && fx_half && (half_skip ^= 1) && ph_made && !shot_due) {   /* (Low: 30 pictures a second - this one the last again) */
		half_dt += dt;
		gpu_present (NULL, FW, FH, FW, (float) CX0 / EW, 1.0f - (float) (CY0 + CH) / FH, (float) (CX0 + CW) / EW, 1.0f - (float) CY0 / FH, CW, CH, opt_rot);
		sheet_write (); if (!asy) flash_save (0); if (!asy) audio_out ();
		return;
	}
	if (gph) {
		const double pdt = dt + half_dt; half_dt = 0;
		/* (the apps' GPU diagnostic, vx_gprof: one picture in 30 timed with the GPU waited for after each stage - the light, the glass and
		 *  the case; iOS's OpenGL ES has no timer queries) */
		extern int vx_gprof; static long gpc; static double gpt[3], gpw; static long gpn; const int gpm = vx_gprof && (++gpc % 30) == 0; double gp0 = 0, gp1 = 0;
		{ extern int vx_gprof_now; vx_gprof_now = gpm; }
		if (gpm) { glFinish_ (); gp0 = vt_wall (); }
		if (ph_ex_mode) sm_ex_shown = sm_ex_next;       /* (this picture is drawn - Low leaves every other one out: its game frame's afterglow with the next) */
		const int gb_exm = ph_ex_mode, gb_exn = ph_ex_n; (void) gb_exm; (void) gb_exn;   /* (VX_GPUBENCH below) */
		const double c0 = vt_wall (); const int phd = ph_frame (npl, pdt, opt_glow, opt_after, shot_pending ()); cs_now[4] += vt_wall () - c0;
		if (phd) {
			if (pf_on) { double t4 = pf_now (); pf_g[0] += t4 - pf3; }
			if (gpm) { glFinish_ (); gp1 = vt_wall (); }
			/* (the diagnostic, no overlay: the glass and the case drawn once more with a part off - not shown, the real one after it -
			 *  what each part costs: 0 no case, 1 no case and no room light on the glass, 2 the case without its reflections) */
			static double gvt[3]; static long gvn[3];
			if (gpm && gfx.cse) { const int vi = (int) ((gpc / 30) % 3), kc = gfx.cse, kn = gpu_novideo; const float kl = gfx.lk, kg = gfx.grey, kr = gfx.crefl;
				if (vi <= 1) gfx.cse = 0; if (vi == 1) { gfx.lk = 0; gfx.grey = 0; } if (vi == 2) gfx.crefl = 0;
				gpu_novideo = 1; gpu_present (NULL, FW, FH, FW, (float) CX0 / EW, 1.0f - (float) (CY0 + CH) / FH, (float) (CX0 + CW) / EW, 1.0f - (float) CY0 / FH, CW, CH, opt_rot);
				glFinish_ (); { const double tv = vt_wall (); gvt[vi] += tv - gp1; gvn[vi]++; gp1 = tv; }
				gfx.cse = kc; gfx.lk = kl; gfx.grey = kg; gfx.crefl = kr; gpu_novideo = kn; }
			{ const double c1 = vt_wall (); gpu_present (NULL, FW, FH, FW, (float) CX0 / EW, 1.0f - (float) (CY0 + CH) / FH, (float) (CX0 + CW) / EW, 1.0f - (float) CY0 / FH, CW, CH, opt_rot); cs_now[4] += vt_wall () - c1; }
			{ /* (a test, VX_GPUBENCH=K: one refresh in 30 this picture's GPU work - the light, the sheet, the glass and the case - done K times
			   *  more back to back, timed by a GL_TIME_ELAPSED query: the GPU's own time a picture, no gaps for the CPU. It moves the phosphor
			   *  on K refreshes: for timing runs only. The log: GPUBENCH) */
			  static int gbk = -1; if (gbk < 0) { const char *z = getenv ("VX_GPUBENCH"); gbk = z ? atoi (z) : 0; }
			  static long gbc, gbn; static double gbs, gbw;
			  if (gbk > 0 && gq_ok > 0 && gq_cur < 0 && (++gbc % 30) == 0) {
				  const int kn = gpu_novideo; GLuint q = gq_id[(gq_next + GQ_N - 1) % GQ_N]; unsigned long long ns = 0;
				  for (int k = 0; k < GQ_N; k++) if (!gq_busy[k]) { q = gq_id[k]; break; }
				  glFinish_ (); gq_beg (0x88BF, q); gpu_novideo = 1; const double gw0 = vt_wall ();
				  for (int k = 0; k < gbk; k++) { ph_ex_mode = gb_exm; ph_ex_n = gb_exn; ph_frame (npl, pdt, opt_glow, opt_after, 0);
					  gpu_present (NULL, FW, FH, FW, (float) CX0 / EW, 1.0f - (float) (CY0 + CH) / FH, (float) (CX0 + CW) / EW, 1.0f - (float) CY0 / FH, CW, CH, opt_rot); }
				  gq_endf (0x88BF); gbw += (vt_wall () - gw0) * 1e3 / gbk; gpu_novideo = kn; gq_u64 (q, 0x8866, &ns);
				  gbs += ns / 1e6 / gbk; if (++gbn % 20 == 0) { fprintf (stderr, "GPUBENCH %d x %d px: %.3f ms a picture on the GPU, %.3f ms the CPU's commands (%ld timed)\n", FW, FH, gbs / 20, gbw / 20, gbn); gbs = gbw = 0; } } }
			if (gpm) { glFinish_ (); const double gp2 = vt_wall (); gpt[0] += gp1 - gp0; gpt[1] += gp2 - gp1; gpn++;
				extern double vx_gpst[4], vx_oxst[8]; extern int vx_gprof_now; vx_gprof_now = 0;
				if (gpw == 0) gpw = gp2; else if (gp2 - gpw >= 5.0) { fprintf (stderr, "[vectrex_emu] GPU per picture (%ld timed): the light %.2f ms (lines %.2f, phosphor %.2f, glow %.2f, picture %.2f), the glass and the case %.2f ms - %d x %d px, profile %s; the sheet's passes %.2f %.2f %.2f %.2f %.2f %.2f %.2f %.2f; the glass and case with a part off: no case %.2f, no case nor room light %.2f, no reflections %.2f ms%c",
					gpn, gpt[0] / gpn * 1e3, vx_gpst[0] / gpn * 1e3, vx_gpst[1] / gpn * 1e3, vx_gpst[2] / gpn * 1e3, vx_gpst[3] / gpn * 1e3, gpt[1] / gpn * 1e3, FW, FH, opt_profile == 0 ? "auto" : opt_profile == 1 ? "ultra" : opt_profile == 2 ? "high" : opt_profile == 3 ? "medium" : "low", vx_oxst[0] / gpn * 1e3, vx_oxst[1] / gpn * 1e3, vx_oxst[2] / gpn * 1e3, vx_oxst[3] / gpn * 1e3, vx_oxst[4] / gpn * 1e3, vx_oxst[5] / gpn * 1e3, vx_oxst[6] / gpn * 1e3, vx_oxst[7] / gpn * 1e3, gvn[0] ? gvt[0] / gvn[0] * 1e3 : 0.0, gvn[1] ? gvt[1] / gvn[1] * 1e3 : 0.0, gvn[2] ? gvt[2] / gvn[2] * 1e3 : 0.0, 10); memset (vx_oxst, 0, sizeof vx_oxst); memset (gvt, 0, sizeof gvt); memset (gvn, 0, sizeof gvn); gpt[0] = gpt[1] = 0; vx_gpst[0] = vx_gpst[1] = vx_gpst[2] = vx_gpst[3] = 0; gpn = 0; gpw = gp2; } }
			shot_write ();
			sheet_write ();
			if (!asy) flash_save (0);
			if (pf_on) { double pf5 = pf_now (); pf_t[0] += pf1 - pf0; pf_t[1] += pf2 - pf1; pf_t[2] += pf3 - pf2; pf_t[3] += pf5 - pf3; pf_n++; }
			if (!asy) audio_out ();
			return;
		}
	}
	const int cb = pbi; float **cur = pb[pbi], **prv = pb[pbi ^ 1]; pbi ^= 1;
	for (int c = 0; c < 3; c++) lacc[c] = cur[c];
	cur_l0 = sp_l0[cb]; cur_l1 = sp_l1[cb];
	const int *pl0 = sp_l0[cb ^ 1], *pl1 = sp_l1[cb ^ 1];
	const int NB = 64, BH = (H + NB - 1) / NB;
	const int glow = opt_glow, after = opt_after;
	const int painted = paint_all;             /* (the overlay painted in, or the glass grey: every pixel has colour) */
	const int ry0 = glow ? 0 : CY0, ry1 = glow ? H - 1 : CY0 + CH - 1;   /* (no glow: only the window's rows are sent) */
	const int cx0 = glow ? 0 : CX0, cx1 = glow ? Wd - 1 : (FW - EW) + CX0 + CW - 1;
	extern float vx_trail_mul; const float tk = (ov && ov_on && !sheet_file ? opt_trail_k : opt_trail_k0) * vx_trail_mul;   /* (1.0: its own without an overlay; the apps' look) */
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
							g1b[c][i] = g1; g2b[c][i] = g2; { const float g = g1 + g2; o[c] += g * (1.0f + TRAIL_LIFT_A / (1.0f + g / TRAIL_LIFT_G0)); }   /* (lifted: gpu_phos.c lift) */
							any |= g1 + g2 > 0; gl |= g1 + g2 > 0; }
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
int vx_app_card (void) { return !vx_ok (); }   /* (the own apps: a UVMC2 or PiTrex program running - its screen rate) */
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
	const int ao = (f->alt ? 1 : 0) | (f->open ? 2 : 0);   /* (bit 1: open, frame_push - in alt's place: the state's size and older states as they were) */
	PUT (&f->n, sizeof (int)); PUT (&nk, sizeof (int)); PUT (&ao, sizeof (int)); PUT (&f->t, sizeof (double)); PUT (&f->p0, sizeof (unsigned long long));
	o = put_lines (o, f->L, f->n, ST_L); PUT (f->keys, sizeof (long long) * nk);
	return o;
}
static int net_dbg = -1;
#define NDBG(...) do { if (net_dbg < 0) net_dbg = getenv ("VXPOC_NETDBG") != NULL; if (net_dbg) fprintf (stderr, __VA_ARGS__); } while (0)
/* (the cartridge in the slot, in each save state: its first 64K as loaded - the image, $FF after its end) */
static uint32_t cart_id;
static void cart_id_set (void) { uint32_t h = 2166136261u; for (int k = 0; k < 65536; k++) h = (h ^ cart[k]) * 16777619u; cart_id = h | 1; }
static size_t vx_save_to (unsigned char *d)   /* (the state into d - at most vx_size bytes; its length: online's rollback saves
                                                these from inside the emulation, netvx.c) */
{
	if (!vx_size) vx_state_init ();
	unsigned char *o = d; uint32_t h[4] = { VX_MAGIC, (uint32_t) vx_raw, (uint32_t) extern_cart_len (), cart_id };
	PUT (h, 16);
	o = vxv_save (o);
	PUT (cart, extern_cart_len ());
	o = put_lines (o, cur, ncur, ST_L); o = put_lines (o, last, nlast, ST_L);
	o = put_lines (o, eyeb[0], neye[0], ST_E); o = put_lines (o, eyeb[1], neye[1], ST_E);
	PUT (&nh, sizeof (int)); PUT (&have_prev, sizeof (int));
	if (have_prev) o = put_frame (o, &prevf);
	for (int k = 0; k < nh; k++) o = put_frame (o, &hist[k]);
	return (size_t) (o - d);
}
bool retro_serialize (void *d, size_t sz)
{
	emu_wait_ext (); net_mark ();
	{ static long n; if (++n % 120 == 1) NDBG ("[netdbg] serialize #%ld sz %zu vx_size %zu run %ld\n", n, sz, vx_size, net_runs); }
	if (!vx_ok () || !vx_size) vx_state_init ();
	if (!vx_ok () || sz < vx_size) return false;
	const size_t n = vx_save_to (d);
	memset ((unsigned char *) d + n, 0, sz - n);   /* (the rest: zeros, so two saves of one moment compare equal) */
	return true;
}
static bool vx_load_from (const void *d, size_t sz);
bool retro_unserialize (const void *d, size_t sz)
{
	if (netvx_playing ()) { fprintf (stderr, "[vectrex_emu] a saved state: not during an online game\n"); return false; }   /* (it would part the two games - the apps' buttons, any way in) */
	emu_wait_ext (); net_mark ();
	unser_seen = 1; runs_since_unser = 0;
	return vx_load_from (d, sz);
}
/* (online's rollback: what the state holds for RetroArch's run-ahead but that belongs to now - the time to reach, the sound
 *  queue and its filters (the frames run again are not heard), the tube's warm-up, the overlay switch - kept through the load:
 *  the time went back with the state and every rollback cost its frames' time, the game ran a third slow) */
static void vx_rb_keep (int back)
{
	static int16_t k_aq[AQ * 2]; static int k_r, k_w, k_fm, k_ov, k_sel; static double k_fr, k_rf, k_pw; static long k_fs; static unsigned long long k_rt;
	static biquad k_b[8], k_s[6], k_v[2];
	if (!back) { memcpy (k_aq, aq, sizeof aq); k_r = aq_r; k_w = aq_w; k_fr = aq_frac; k_fs = aq_frames; memcpy (k_b, f_buzz, sizeof f_buzz); memcpy (k_s, f_spk, sizeof f_spk);
		memcpy (k_v, f_voice, sizeof f_voice); k_fm = filters_made; k_rt = run_t; k_rf = run_frac; k_pw = pw_t; k_ov = ov_on; k_sel = sel_held; }
	else { memcpy (aq, k_aq, sizeof aq); aq_r = k_r; aq_w = k_w; aq_frac = k_fr; aq_frames = k_fs; memcpy (f_buzz, k_b, sizeof f_buzz); memcpy (f_spk, k_s, sizeof f_spk);
		memcpy (f_voice, k_v, sizeof f_voice); filters_made = k_fm; run_t = k_rt; run_frac = k_rf; pw_t = k_pw; ov_on = k_ov; sel_held = k_sel; }
}
static bool vx_load_from (const void *d, size_t sz)   /* (retro_unserialize's work - also online's rollback, from inside the emulation) */
{
	if (!vx_ok () || !vx_size) vx_state_init ();
	const unsigned char *i = d; uint32_t h[4];
	if (!vx_ok () || sz < 16) { NDBG ("[netdbg] unserialize: no (ok %d sz %zu)\n", vx_ok (), sz); return false; }
	GET (h, 16);
	if (h[0] != VX_MAGIC || h[1] != vx_raw || h[2] != (uint32_t) extern_cart_len ()) {
		NDBG ("[netdbg] unserialize: header %08x %u %u, mine %08x %zu %ld - sz %zu\n", h[0], h[1], h[2], VX_MAGIC, vx_raw, (long) extern_cart_len (), sz); return false; }
	if (h[3] && h[3] != cart_id) {   /* (another cartridge's state - from before a switch: run-ahead loads the state of the frame
	                                  *  before, and the browser's switch came in that frame; loaded, the new game ran on with the
	                                  *  old one's CPU and RAM - the Nova's Scramble after Pole Position, hung. Not loaded: this one
	                                  *  runs on; RetroArch's run-ahead on as before. 2026-11-16) */
		static int said; if (!said++) fprintf (stderr, "[vectrex_emu] a save state of another cartridge: not loaded\n");
		return true; }
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
		f.open = (f.alt & 2) != 0; f.alt &= 1;   /* (open in alt's bit 1: put_frame) */
		f.gen = ++frame_gen;                       /* (loaded: new content - Smooth motion's kept work does not apply) */
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
	int rc = vtest_main (5, argv); remove (scr); cart_id_set ();
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
	{
	opt_quiet = 90;                             /* (Optimize: a case not measured yet may be measured in the first refreshes of a game - never later in it) */
	}
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
#ifndef VX_EMUVR
	opts_reset_once ();
#endif
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
	if (ls_cfg_on) { extern unsigned long vt_power_n; total_cycles = frame_start_cycles = last_busframe = last_recal = idle_cycles = idle_frame = 0; vt_power_n = 0; }   /* (online: the game from cycle 0 on both
	                                              sides, the frame counters with it - a switch kept the last game's, which the checksum and the frames' ends have:
	                                              out of step at frame 0; after a join into a running game, at the next game) */
	int rc = vtest_main (5, argv);
	remove (scr); cart_id_set ();
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
	if (have_prev) { if (spare.L) { free (prevf.L); free (prevf.keys); } else spare = prevf; have_prev = 0; }   /* (the last game's last frame
	                                              too: online the time starts at 0 again - its later time joined every new frame
	                                              into one, ever more lines drawn at once: a growing glow, the iPhone 2026-11-20) */
	vx_size = 0; if (vx_ok ()) vx_state_init ();   /* (v0.13: the save state's layout, for this game) */
	/* v0.15: switched on - the tube warming up for the time it was off (a first start: cold), the power sound */
	{ char sp[1100]; double off_for = 1e9; pw_stamp_path (sp, sizeof sp); FILE *f = fopen (sp, "r"); long long ts;
	  if (f) { if (fscanf (f, "%lld", &ts) == 1) off_for = (double) (time (NULL) - ts); fclose (f); }
	  if (off_for < 0) off_for = 0;
	  if (!pw_snd_on) { pw_snd_on = pw_wav (sys, "on.wav", &pw_n_on); pw_snd_off = pw_wav (sys, "off.wav", &pw_n_off); }
	  pw_rng = (unsigned) time (NULL) * 2654435761u; pw_file = getenv ("VECTREX_POWER"); pw_seq_seen = -1; pw_poll_n = 0;
	  pw_on = 0; pw_switch (1, off_for); pw_poll (); pw_poll_n = 0; }
	if (ls_cfg_on ? ls_intro_skip : opt_is ("vectrex_emu_skip_intro", "on")) skip_boot ();   /* (1.1: as the desktop's Skip the intro - the BIOS run unseen) */
	auto_load ();
	char od[1600];
	if (sys) { snprintf (od, sizeof od, "%s" SEP "vectrex_emu" SEP "overlays", sys); try_overlay_dir (od, gpath); }
	if (!ov) { snprintf (od, sizeof od, "%s" SEP "Overlays", dir); try_overlay_dir (od, gpath); }
	{
	if (first) { const char *rv = optv ("vectrex_emu_renderer"); opt_renderer = !strcmp (rv, "CPU only") ? 2 : !strcmp (rv, "GPU") ? 1 : 0;
	  gpu_asked = opt_renderer != 2 ? gpu_request () : 0; if (!gpu_asked) gpu_ok = 0; }   /* (each game: RetroArch sets the context up after this) */
	}
	if (area_glass () != geo_glass) pic_setup ();   /* (the overlay found: the area it asks for, before RetroArch asks the size) */
	return true;
}
/* (the ROM browser, eject: another game in the slot while running - the card and the flash written back first, as
 *  at the end of a game; RetroArch's own content stays what it loaded) */
static char ej_last[1100]; static int ej_out;
static char sw_next[1100]; static int sw_eject;          /* (a switch asked for: done at the next refresh's start - see retro_run) */
static int game_switch_now (const char *path);
static int game_switch (const char *path) { snprintf (sw_next, sizeof sw_next, "%s", path); return 1; }
/* (the own iOS app: its own game library in place of the ROM browser - asked for (the deck's cartridge, a button, L)
 *  the browser is closed again at once and the app shows its library; a game it picks loads as the browser's do) */
int vx_app_browser_wanted (void) { if (br_open) { br_show (0); return 1; } return 0; }
void vx_app_load (const char *path) { if (netvx_playing ()) { fprintf (stderr, "[vectrex_emu] another game: not during an online game\n"); return; } game_switch (path); }
const char *vx_app_game (void) { return cur_game; }
static void eject_toggle (void);
void vx_app_reset (void)
{
	if (netvx_playing ()) { ls_reset_req = 1; fprintf (stderr, "[vectrex_emu] RESET (the app) - online: both players, in the same game frame\n"); return; }
	extern void vecx_reset_button (void); emu_wait_ext (); vecx_reset_button (); fprintf (stderr, "[vectrex_emu] RESET (the app)\n");
}
int vx_app_port2 (void) { return dk_p2; }        /* (port 2's device: 0 controller, 1 VecVox, 2 VecVoice, 3 3D Imager, 4 light pen) */
int vx_app_cart_out (void) { return ej_out; }
int vx_app_power_on (void) { return pw_on; }
int vx_app_paused (void) { return vx_paused; }
void vx_app_eject (void) { if (netvx_playing ()) { fprintf (stderr, "[vectrex_emu] eject: not during an online game\n"); return; } eject_toggle (); }   /* (as Ctrl+E: the switch at the next refresh) */
static int game_switch_now (const char *path)
{
	fprintf (stderr, "[vectrex_emu] switching to %s (from %s)\n", path, cur_game);   /* (a crash on a switch: its log says which) */
	emu_wait_ext (); emu_primed = 0;
	if (sd_img[0] && sd_folder[0]) sdc_sync_back (sd_img, sd_folder);
	flash_save (1);
	if (ov) stbi_image_free (ov); ov = NULL; ov_path[0] = 0; ov_made = 0;   /* (the last game's overlay out; on / off stays as the player set it - an eject turned it on again) */
	{ extern int vt_vb, vt_hb512, vt_multicart, vt_uvm2, vt_uvm2_cart, vt_pitrex; extern const char *vt_flash_file;   /* (the last cartridge's
	                                              boards and modes out: vtest's main only sets them - Vectorblade then Scramble ran Scramble on
	                                              Vectorblade's board: Mine Storm, the owner 2026-11-15) */
	  extern int vt_uvm2_intro, vt_uvm2_go; extern void uvm2_close (void); uvm2_close (); vt_vb = vt_hb512 = vt_multicart = 0; vt_uvm2 = vt_uvm2_cart = vt_uvm2_intro = vt_uvm2_go = vt_pitrex = 0; vt_flash_file = NULL; }
	const char *sys = NULL; env_cb (RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY, &sys);
	if (!game_open (path, sys, 0)) { fprintf (stderr, "[vectrex_emu] could not load %s\n", path);
		struct retro_message msg = { "Vectrex_EMU: this game could not be loaded", 180 }; env_cb (RETRO_ENVIRONMENT_SET_MESSAGE, &msg); return 0; }
	ph_made = 0;                                /* (the phosphor dark: a new game) */
	geo_tell ();   /* (2026-11-16: the new size only - SET_SYSTEM_AV_INFO set RetroArch's video up anew on each switch, and on the
	                *  Nova that stopped there: Pole Position to Scramble from the browser, RetroArch hung) */
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
	{ const int r = btn_id ("vectrex_emu_btn_reset"), e = btn_id ("vectrex_emu_btn_eject");   /* (RESET: L1 by default, eject none) */
	  if (r >= 0) d[n++] = (struct retro_input_descriptor) { 0, RETRO_DEVICE_JOYPAD, 0, (unsigned) r, "RESET button" };
	  if (e >= 0 && e != r) d[n++] = (struct retro_input_descriptor) { 0, RETRO_DEVICE_JOYPAD, 0, (unsigned) e, "Eject / reinsert the cartridge" }; }
#endif
	d[n] = (struct retro_input_descriptor) { 0 }; env_cb (RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS, d);
}

#ifdef __ANDROID__
/* (Android: a crash written into the log - the signal, the address, the calls with their places in this core - so a crash
 *  on a handheld can be found from its log; then the crash goes on as before. 2026-11-16) */
#include <signal.h>
#include <unwind.h>
static struct sigaction crash_old[32];
typedef struct { void **a; int n, max; } crash_bt;
static _Unwind_Reason_Code crash_step (struct _Unwind_Context *c, void *p)
{
	crash_bt *b = p; void *ip = (void *) _Unwind_GetIP (c);
	if (ip && b->n < b->max) b->a[b->n++] = ip;
	return b->n < b->max ? _URC_NO_REASON : _URC_END_OF_STACK;
}
static void crash_handler (int sig, siginfo_t *si, void *uc)
{
	char l[300]; Dl_info me; dladdr ((void *) crash_handler, &me);
	int n = snprintf (l, sizeof l, "\n[vectrex_emu] CRASH: signal %d at address %p (core base %p)\n", sig, si ? si->si_addr : NULL, me.dli_fbase);
	write (2, l, (size_t) n);
	void *a[32]; crash_bt b = { a, 0, 32 }; _Unwind_Backtrace (crash_step, &b);
	for (int i = 0; i < b.n; i++) { Dl_info d; const char *lib = "?"; void *base = NULL;
		if (dladdr (a[i], &d) && d.dli_fname) { lib = strrchr (d.dli_fname, '/') ? strrchr (d.dli_fname, '/') + 1 : d.dli_fname; base = d.dli_fbase; }
		n = snprintf (l, sizeof l, "  #%d %s +0x%lx %s\n", i, lib, (unsigned long) ((char *) a[i] - (char *) base), d.dli_sname ? d.dli_sname : ""); write (2, l, (size_t) n); }
	sigaction (sig, &crash_old[sig], NULL); raise (sig);   /* (then as before: RetroArch's own handling) */
}
static void crash_install (void)
{
	static int done; if (done) return; done = 1;
	struct sigaction sa; memset (&sa, 0, sizeof sa); sa.sa_sigaction = crash_handler; sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
	const int sigs[] = { SIGSEGV, SIGBUS, SIGFPE, SIGILL, SIGABRT };
	for (int i = 0; i < 5; i++) sigaction (sigs[i], &sa, &crash_old[sigs[i]]);
}
/* (a hang: the refreshes stopped 3 s - the thread that ran the last one shows where it is, its calls into the log, as a
 *  crash's; inside the core or in RetroArch. Once a stop, 8 at most. The Nova, 2026-11-16: a switch from the browser hung) */
static pthread_t wd_th, wd_run_th; static volatile int wd_quit, wd_have_run, wd_dumps;
static void wd_thr_set (void) { wd_run_th = pthread_self (); wd_have_run = 1; }   /* (each refresh: its thread) */
static void wd_sig (int sig, siginfo_t *si, void *uc)
{
	char l[300]; Dl_info me; dladdr ((void *) wd_sig, &me);
	int n = snprintf (l, sizeof l, "[vectrex_emu] WATCHDOG: no refresh for 3 s - the thread that ran them (core base %p):\n", me.dli_fbase);
	write (2, l, (size_t) n);
	void *a[40]; crash_bt b = { a, 0, 40 }; _Unwind_Backtrace (crash_step, &b);
	for (int i = 0; i < b.n; i++) { Dl_info d; const char *lib = "?"; void *base = NULL; d.dli_sname = NULL;
		if (dladdr (a[i], &d) && d.dli_fname) { lib = strrchr (d.dli_fname, '/') ? strrchr (d.dli_fname, '/') + 1 : d.dli_fname; base = d.dli_fbase; }
		n = snprintf (l, sizeof l, "  #%d %s +0x%lx %s\n", i, lib, (unsigned long) ((char *) a[i] - (char *) base), d.dli_sname ? d.dli_sname : ""); write (2, l, (size_t) n); }
}
static void *wd_main (void *u)
{
	unsigned last = wd_runs; int still = 0, told = 0;
	while (!wd_quit) {
		struct timespec t = { 0, 250000000 }; nanosleep (&t, NULL);
		if (wd_runs != last) { last = wd_runs; still = 0; told = 0; continue; }
		if (++still == 12 && !told && wd_dumps < 8 && wd_have_run) { told = 1; wd_dumps++; int e = pthread_kill (wd_run_th, SIGUSR2);
			if (e) fprintf (stderr, "[vectrex_emu] WATCHDOG: no refresh for 3 s (the thread not reached: %d)\n", e); }
	}
	return NULL;
}
static void wd_start (void)
{
	if (wd_th) return;
	struct sigaction sa; memset (&sa, 0, sizeof sa); sa.sa_sigaction = wd_sig; sa.sa_flags = SA_SIGINFO | SA_ONSTACK | SA_RESTART; sigaction (SIGUSR2, &sa, NULL);
	wd_quit = 0; if (pthread_create (&wd_th, NULL, wd_main, NULL)) wd_th = 0; else fprintf (stderr, "[vectrex_emu] watchdog on\n");
}
static void wd_stop (void) { if (wd_th) { wd_quit = 1; pthread_join (wd_th, NULL); wd_th = 0; } }   /* (before RetroArch unloads the core) */
#else
static void wd_start (void) {} static void wd_stop (void) {} static void wd_thr_set (void) {}
#endif
bool retro_load_game (const struct retro_game_info *g)
{
	emu_wait_ext (); emu_primed = 0;
	boot_id = VX_TICKS () | 1;   /* (v0.25: a new game booted) */
	if (!g || !g->path) return false;
	const char *sys = NULL; env_cb (RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY, &sys);
#ifndef VX_EMUVR
	dbg_start (sys);                            /* (the private iOS test build: the previous session's log, before it is written anew) */
#endif
#if defined(__ANDROID__) || VX_IOS
	/* (Android, iPhone / iPad: stderr goes nowhere - the core's messages to <system>/vectrex_emu/vectrex_emu.log, for a
	 *  game that will not start: the BIOS, the overlay found, the speech module, the GPU path) */
	if (sys) { char lp[1100]; snprintf (lp, sizeof lp, "%s/vectrex_emu/vectrex_emu.log", sys); FILE *lf = fopen (lp, "w");
		if (lf) { fflush (stderr); dup2 (fileno (lf), 2); fclose (lf); setvbuf (stderr, NULL, _IONBF, 0);
			fprintf (stderr, "[vectrex_emu] core 1.1, game %s\n", g && g->path ? g->path : "(none)"); } }
#endif
#ifdef __ANDROID__
	crash_install (); wd_start ();
#endif
	enum retro_pixel_format pf = RETRO_PIXEL_FORMAT_XRGB8888;
	if (!env_cb (RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &pf)) return false;
	input_names ();
	snprintf (cur_game, sizeof cur_game, "%s", g->path); ej_out = 0;
	netls_load (g->path);                       /* (a UVMC2 / PiTrex game: online in lockstep - netls.c) */
#ifndef VX_EMUVR
	snprintf (br_game, sizeof br_game, "%s", g->path); br_dir[0] = 0;
	if (!br_root[0]) br_parent (g->path, br_root, sizeof br_root);   /* (the ROM browser's top: Recent and the PiTrex there) */
	br_rec_note (g->path);
#endif
	return game_open (g->path, sys, 1);
}
