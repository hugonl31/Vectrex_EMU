# Photo matching scripts (2026-11-21)

The working scripts of the session that fitted the dots and the beam-on-phosphor law to the owner's raw photos
(`G:\Google Drive\#Sharing\Vectrex\Testcarts`, the Dot Test photos in `...\Dot test`). Kept as they were used: they
expect to run from one folder that also holds their caches (`*_raw.npy` from the DNGs, `oct/*.npy`, the snapshots) and
the test core DLLs (`cb/`). To be made into one tool; until then, what each does:

| Script | What |
|---|---|
| `dng.py` | DNG (ProRAW, linear) -> `.npy` caches + previews |
| `pcpages.py`, `pcvb.py`, `pcgrab.py` | the PC app from source, minimised, focus kept, skip intro, no wobble / bending, G-Sync: Dot Test pages 1-4 / Vectorblade's logo (frame 520) / one page as F12 snapshots at the window's size. Env: `LOOK`, `BEAM=1` (beam trace), `VX_BT_EXPO`, `VX_BT_SPOT`, `VR` (another vecrender.py) |
| `renall.py`, `rend3.py` | the core (tools/lrhost.py) on every test cart and page |
| `compose.py`, `beamcmp.py` | raw photo next to render -> `testcart/compare/*.png` |
| `cmpd.py`, `pcsweep.py` | page 1: every dot's radius at 90 / 50 / 25 / 12 % of white, photo vs render |
| `erat.py` | page 1: every dot's light / the reference line's light a unit (linear; exposure- and blur-free) |
| `cut.py`, `cutemu.py` | page 4: how far each corner's tip stays inside the sharp corner - photo / the core's own beam path (`VT_RAMPRC`, `VT_VLAG`, ...) |
| `lagsim.py` | a 90 degree corner through 1-4 stage spot lags: none makes an 8-cycle rest as round as none |
| `ridge.py`, `vbrest.py` | Vectorblade's logo: joint dots / line level; the beam's time by speed |

Test rules (the owner): skip intro on, wobble and brightness bending off, G-Sync on, minimised, the owner's window
keeps the focus, pictures by F12 at the window's size.

## The integrators' switch: what the photos say (2026-11-21, to fit)

Three independent measurements of how RAMP really starts and stops the integrators on the owner's Vectrex:

1. **Travel during a RAMP-off pulse** (`joints.py`; Dot Test page 3, the spacing of the joints' dots, four lines, the two
   at the same speed alike within 0.2 cycle). Cycles of travel during a pulse of 5 / 8 / 11 / 16 / 24 / 32 cycles:
   **1.5 / 4.9 / 7.4 / 5.9 / 2.1 / 0.1** (speed $50; $7F: - / 5.0 / 5.5 / 5.0 / 2.0 / 0.6). The emulator: 2 for every pulse
   (start 5, stop 7). So: a short pulse (<= ~11 cycles) hardly stops the beam (it loses ~3.3 cycles), a long one stops it
   for its whole length; the restart comes later the longer the stop was (lost travel: 3.5 3.1 3.6 10.1 21.9 31.9).
2. **Corner rounding** (`cut.py` / `cutemu.py`; Dot Test page 4, 90 degrees, rests 0 / 8 / 16 / 32 / 64): the tip stays
   **1.71 / 1.82 / 0.84 / ~0 / ~0** units inside the sharp corner; the emulator 1.83 / 0.95 / 0.26 / 0.02 / 0.00. No lag of
   the spot (1-4 stages, `lagsim.py`) makes a rest of 8 as round as none.
3. **Line length by scale** (`sq.py`; Gap Test page 3, squares at scales $06-$13): the photo's small squares 4-8 %
   larger against scale $13 than the emulator's - the real lines ~0.5-1.2 cycles longer than ours, no more. A model must
   not lengthen ordinary lines (VT_RAMPRC=14,0.4 does: +3.7 cycles) nor shrink short ones (8.6,0.443 does: Gap Test -22 %).

Tried and not it: a speed low-pass while the beam is lit (rounds the corners, bends and shortens the second leg); one RC
with a threshold, symmetric or with a soft threshold (`swfit.py`: rms 1.3 cycles on 1., and short lines vanish). Next: run
the core with candidate switch models and score them against 1-3 together (plus the October photos' gaps and text).

## Vectorblade's logo (2026-11-21)

`prof.py` cuts the lines along their normal: the photo's line is 0.40 / 0.70 / 1.00 / 1.30 / 1.70 units wide at 90 / 75 / 50 /
25 / 10 % of its peak (flatter and steeper than a Gaussian: exp(-(d/a)^2.6) in the shader). `logofit.py` lays the
emulator's lines on the photo (a similarity transform + the tube's own height: the owner's picture ~10 % taller) and
gives the mean distance between the line sets: October's timing 1.28 units, the short-pause rule (VT_RAMPSW=1) 1.43 -
worse, so it stays off. Vectorblade's RAMP-off pulses (VT_RSWLOG=file): mostly 8, 9 and 12 cycles, then 18-22, 34, 39.
Its joints: 1.2x the line's level in the photo (90th percentile along the lines), 1.9x here - the beam lingers 6-10 lit
cycles at each (the pulse less 2); the Dot Test's page 3 says the real beam loses only ~4 cycles of travel to such a
pulse. Open: what the beam and the DAC do during a short pause (Switch Test, pages 1 and 3).

## The automatic fit, first run (2026-11-21): nothing beats October's timing

`autofit.py` runs the core with candidate settings and scores them on three photo measurements at once: the logo's shape
(`logofit.py`), the corners' cut (Dot Test page 4) and the travel during RAMP-off pulses (page 3). 18 settings
(`autofit1.log`): the X / Y arrival delay 5 / 7 / 9 / 11 cycles, each with no short-pause rule and three windows of it
(VT_RAMPSW_TAB), and the speed slide (VT_VLAG 4 and 8, two stages, only while the beam is lit - VT_VLAGLIT).
- The logo's shape is best with October's settings (1.28 units); every short-pause window makes it 1.47-1.83, the speed
  slide 1.54-1.84, an X / Y delay of 11 makes it 1.43.
- The speed slide (tau 12, two stages) gives the corners of the photo (8 cycles: 1.78 / 1.82, 16: 0.73 / 0.84) - and
  breaks the logo (1.90).
- `defl.py`: instant integrators and any linear deflection (1-3 lags, a second order): no travel during a pulse at all.
So one mechanism rounds the corner test's corners (the X speed flipped at RAMP off, the beam lit) without touching
Vectorblade's vectors (X and Y set inside pauses of 8, 9, 12 cycles). The candidates differ in what they predict for
the Switch Test: page 2 (an X step with RAMP held on: a slide shows there, a switch effect does not) and page 3 (the
flip K cycles before / after the RAMP edges: where the dot sits against the tip).

## The phosphor's decay (2026-11-21): Switch Test page 4, ProRes video

The owner's clips (Blackmagic Camera, iPhone 18 Pro Max 24 mm, ProRes 422 HQ 4K, Apple Log, 100 fps, shutter 1/200 s,
6400 K): `A001_11211859_C007.mov` ISO 64, `A001_11211901_C009.mov` ISO 800 (nothing clips: the brightest code 0.67, black
0.15 = Apple Log's black). `decay1-4.py`: Apple Log -> linear, the light in a box at every position of the eight rows
in every frame, sorted by the position's age (the fresh one: the brightest; two camera frames a Vectrex frame, so two
samples 10 ms apart), the fit.

- **One decay law: light ~ (t + 1 ms)^-1.38** - the stepping lines at intensity 7F and 5F, 10 to 230 ms after the
  beam, rms 2 % and 6 % (7F: 318 at 10 ms, 128 at 20, 50 at 40, 19.6 at 80, 7.6 at 160; the frame with the beam in it
  4635 - the 5 ms window that holds the excitation). The dots follow it as far as they rise above the noise (to ~80 ms).
- A line no longer drawn, as a share of the same line redrawn every frame (the light of one whole 20 ms frame, linear):
  6.1 % after 1 frame, 2.8 % after 2, 1.7 % after 3, 0.9 % after 5 (100 ms), 0.48 % after 8, 0.28 % after 12, 0.14 % after 20
  (400 ms), 0.04 % after 50 (1 s).
- The afterglow's strength is not the prompt light's: line 5F / 7F 0.69 fresh, 0.5 at every age (the slow part of the
  light law: 0.67^1.5 = 0.55); a dot resting 16 cycles leaves the same trail as one resting 64 (0.9-1.1); dots at 5F as
  7F (0.9-1.2), at 40 0.4-0.5. It saturates with the dose.
- Limits: the first sample is 10 ms after the beam (the first 5 ms window mixes excitation and decay); the dots beyond
  ~100 ms are below the clip's noise (the local background's subtraction, +-0.1).

## The Switch Test's stills, pages 1-3 (2026-11-21)

The owner's eight DNGs (ultra-wide, ISO 64, 1/50 and 1/25 s). Three things had to be put right before any number:

- **The picture's scale is not one number**: 6.7 px a unit at the left edge, 9.5 in the middle, 7.7 at the right (in X; Y
  8.8-10.3). `sw4.py` / `sw4c.py`: page 4's dots sit at known places (8 / 16 / 24 units apart, eight rows) - a map units <->
  px (rms 1.7 px); `swmap.py`, `swmap2.py`. One unit of that grid (Moveto at scale $FF) = 0.9627 of the emulator's unit of
  134 (measured on the core). **The Dot Test's "travel during a RAMP-off pulse" (1.5 / 4.9 / 7.4 / 5.9 / 2.1 / 0.1 cycles)
  was this distortion** - the joints with the middle pauses sat in the middle of the picture. With the map the pieces of
  page 1 are L + 2 cycles whatever the pause (5 to 48), at $40 and at $7F.
- **The blue channel clips on the lines' cores** (0.70-0.76 of full; the lines at 0.56): every bright spot was flattened in
  the earlier light measurements (they took the brightest channel). The red channel is free up to ~3.6 x a line's peak:
  `sw/*_red.npy`, `sw1.py`. The Dot Test's dot energies are owed a check in red.
- **The picture's response by place** (lens + tube): the left edge 0.4-0.5 of the middle - `swresp.py`, from page 3's
  fifteen figures (the same light each).

Results (`sw1c.py`, `swp1.py`, `swp1b.py`, `swfit1.py`: page 1; `swfig.py`, `swfig2.py`, `swcut.py`: pages 2 and 3 - the
core's path over the photo; `swmodel.py`, `swcutfit.py`: a small model; `swgrid.py`: the core over a grid; `swreg.py`: the
logo and the Dot Test's corners with a candidate):

- Page 1: the core's line 180.36 units, the photo's 180.4 (rows $40), 189.3 / 188.0 (rows $7F); the joints' places within
  ~1 unit. RAMP start 5 / stop 7 stands. A model fitted to the light along the four rows returns the core's own settings
  (start 5.3, stop 7.1, lag 6.2) and the light law's slow / fast share 0.37-0.42 (the law: 0.29 / 0.70 = 0.41).
- Pages 2 and 3, the corner's cut (the curve's nearest distance to the point where the legs' straight parts meet; 0 =
  sharp) - photo against the core today:
  X flips with RAMP on, speed $20 / $40 / $7F: 1.23 / 2.43 / 5.09 against 1.06 / 1.73 / 3.02;
  the flip 12 / 8 / 4 / 0 cycles before RAMP off: 3.19 / 2.82 / 1.70 / 0.07 against 2.32 / 2.51 / 1.56 / 0.03;
  the flip 0 / 5 cycles after RAMP on: 1.27 / 1.85 against 1.16 / 1.41. **The real turn is wider, the more so the bigger
  the step** (a delay and a lag alone: no setting beats the core's - `swgrid.py lag`, `xd`).
- **A slew limit fits**: the X speed follows the DAC at 16 DAC steps a cycle after a delay of 5 (instead of all at once
  after 9). The core's test switch `VT_XSLEW=16` with `VT_XYZDLY=5,9,8`, cut rms over the 14 corners: lag 6 0.32, lag 7
  0.24, lag 8 0.19 (the core today 0.65). The same settings on the standing tests: Vectorblade's logo 1.23 / 1.27 / 1.33
  (today 1.28); the Dot Test's 90 degree corners at rests 0 / 8 / 16: 2.04 / 1.46 / 0.37 (lag 6), 2.10 / 1.47 / 0.48 (lag 7),
  1.99 / 1.56 / 0.61 (lag 8) - the photo 1.71 / 1.82 / 0.84, today 1.83 / 0.95 / 0.26.
  `testcart/compare/switch_corners.png`: the photo with both paths. Not the default: the Y path (through the hold) is
  untested by this cartridge, and the October tests (gaps, text, axis) are owed a run with it.
