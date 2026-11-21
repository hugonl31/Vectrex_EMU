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
- The October tests with the slew (`renall2.py`, `compose3.py` -> `testcart/compare/slew_*.png`: photo | today | candidate):
  X delay 5 leaves a kink before the dot in the Corner Test's corners with a rest (the X change, written one cycle after
  RAMP off, starts a cycle before the stop; the tube shows none) - **X delay 6, slew 18, lag 7** has none: the 14 corners'
  cut rms 0.28 (today 0.65), the logo 1.26 (1.28), the Dot Test's 90 degree corners 2.05 / 1.39 / 0.42 at rests 0 / 8 / 16
  (photo 1.71 / 1.82 / 0.84, today 1.83 / 0.95 / 0.26); the Corner Test round at rests 0-12 and sharp from 24 as the photo;
  the Gap Test's lines and squares, the Lag Test and Vectorblade's logo as today. `VT_XYZDLY=6,9,8 VT_XSLEW=18 VT_DEFLAG=7`.
- **Correction (the same evening)**: `VT_XSLEW` sat only in the branch without the DAC's bit errors; the app and the libretro
  core run the other one, so every `slew_*.png` render before this line showed "X delay 5 / 6, no slew" - the X flip a cycle
  or two before the stop: the hump before the dot (the owner saw it), and the rounding that looked like the photo's. Fixed
  (both branches), all `slew_*.png` and `corners_proposed.png` rendered again: no hump; on the Corner Test the slew's
  change is small (rest 0 and 5 a little rounder). The numbers above (Switch Test cuts, logo, Dot Test corners) came from
  the headless core, where the slew was active - they stand. Lesson: check a core switch in the build that makes the
  pictures, and look at corners at full size.

## Corner roundness on rendered frames (2026-11-21, late)

`cornerm.py`: a corner's cut as a share of its leg, measured the same way on the photo and on the emulator's rendered
frame (`renall2.py`: the libretro core through tools/lrhost.py; `cornerm_run.py`, `cgrid.py`: grids; `gapm.py`: the Gap
Test's joins - dip index and dots). In the libretro core and the app the lag comes from the unit option (analog 9), not
from VT_DEFLAG: `LROPT=vectrex_emu_unit_lagx=..,vectrex_emu_unit_lagy=..`; VT_DEFLAGN (1-6 stages) and VT_LAGBOX are read.

- Corner Test, cut / leg at rests 0 / 5 / 8 / 12 / 16 / 24: **photo 0.122 / 0.120 / 0.105 / 0.073 / 0.044 / 0.003**; today
  0.092 / 0.058 / 0.052 / 0.036 / 0.026 / 0.019. The roundness follows the rest's length (the Dot Test's page 4 alike; the
  Switch Test's corners with a rest of 32 are sharp whenever the flip was written): the spot settles slowly after a stop.
- A lag of 4 equal stages, 14 cycles in all, X delay 8, the beam 8 cycles later (both edges): 0.120 / 0.110 / 0.090 / 0.064 /
  0.045 / 0.023, the legs' ratio and the Gap Test's dips as the photo's - **but the dots at the joins are gone** (the photo,
  `testcart/compare/gap_joins_zoom.png`: a bright dot at each piece's START, the line fading before the gap).
- The beam's switch-off alone later (6 cycles): corners, legs, dips and dots all fit - **but the BIOS font smears into
  bars** (the owner saw it). Rejected.
- No beam delay, lag 10-14 in 3-5 stages: joins and font right, corners at rests 5-16 still too sharp (0.06-0.07 at rest 5).
- A box-shaped response (VT_LAGBOX): no better than the stages.
So no setting of this model (stages + pure delays) gives the corners, the joins' dots and the font together. Nothing is
the default; VT_XSLEW, VT_DEFLAGN > 2, VT_LAGBOX are test switches.

## The Settle Test's photos (2026-11-21, night): pages 1 and 2

The owner's eight DNGs (main camera 6.93 mm, ISO 64, 1/50 and 1/25 s; the red channel free at 1/50). `st1.py`, `st2.py`:
every line's lit ends; the whole line (the last row) = L + 2 cycles gives the px a cycle. `st3.py`: the fit.

- Page 1, the travel still to come when the beam goes off K cycles after RAMP off (speed $7F; cycles):
  K 5 / 8 / 12 / 16 / 20 / 24 / 32: **9.39 / 6.77 / 4.03 / 2.24 / 1.13 / 0.49 / 0.11** ($50: 9.90 / 7.29 / 4.34 / 2.64 / 1.37 / 0.78 / 0.20;
  before the stop the $50 line: K + travel = 14.25).
- Page 2, the travel done when the beam comes on K cycles after RAMP on ($7F): K 5 / 8 / 12 / 16 / 20 / 24 / 32:
  **0.81 / 1.41 / 3.00 / 5.42 / 8.51 / 12.00 / 19.92** (a baseline of ~0.35); the spot ends 12.1 cycles behind.
- So start 12.1, stop 14.25 cycles in all (the core today 11 and 12.5): stop - start 2 as page 1 of the Switch Test - the
  beam's switch is the same both ways (no on / off asymmetry).
- Fits (rms, cycles; the core today 1.10): a delay + one lag: start 5.2, stop 6.95, lag 8.0 - 0.25; two stages: start 1.9,
  stop 3.6, lag 11.2 - 0.20; **three stages: start -0.6, stop 1.1, lag 13.65 - 0.18** (no pure delay left: the switch is
  instant, all the lateness is the spot's).
- Rendered frames with it (`cgrid.py`: lag 13.7 in 3 stages, VT_RAMPDLY=0,2, X delay 3): the Corner Test's cut / leg
  0.117 / 0.104 / 0.093 / 0.070 / 0.053 / 0.030 (photo 0.122 / 0.120 / 0.105 / 0.073 / 0.044 / 0.003; today 0.092 / 0.058 / 0.052 /
  0.036 / 0.026 / 0.019), the legs' ratio rms 0.024 (today 0.065). **But the Gap Test's joins (BIOS lines: timer 1's RAMP,
  the shift register's beam) then gape** (dip index 0.22-0.34, photo 0.06-0.17): the timer / shift-register path is several
  cycles off against the hand-timed one. A later beam for the shift register alone (VT_SRDELAY, a test) closes the gaps at
  6-8 cycles but takes the joins' dots - not it. Owed: the BIOS path's own timing (timer 1's PB7, the shift register's
  first bit) measured the same way - a page with timer-driven ramps and shift-register beam; pages 3 and 4 not yet read.
- The second set of Settle Test photos (21:41, ISO 80; `st5.py`, `st4.py`) repeats the first to ~0.1 cycle; the line ends as
  sharp (10-90 % in 5.1 px, 5.3 before). X ($7F), travel to come at beam-off K 5..32: 9.36 / 6.82 / 4.07 / 2.20 / 1.13 / 0.51 / 0.12;
  travel done at beam-on: 0.88 / 1.51 / 3.09 / 5.50 / 8.60 / 12.12 / 20.02. Fits (rms, cycles): 1 stage 0.26 (start 5.0, stop 7.0,
  lag 7.8), 2: 0.19, 3: 0.16 (start -0.7, stop 1.2, lag 13.3), 4: 0.14 (start -2.8, stop -0.95, lag 15.4: the beam ~2.8 late);
  in all 12.6 cycles after a start, 14.45 after a stop.
- **Page 4, Y** (speed $50): to come 8.08 / 5.77 / 3.15 / 1.66 / 0.61 / 0.27 / 0.00; done 0.89 / 1.86 / 3.91 / 6.29 / 9.55 / 13.11 / 20.76 -
  the same lag (13.4 in 3 stages) but 1.1 cycles earlier at the start, 1.65 at the stop (11.5 / 12.8 in all): the X speed
  is written a cycle after RAMP on; stop - start 1.3 in Y, 1.85 in X.
- **Page 3, pulses** (speed $7F): a lit dash of 4 / 8 / 16 cycles is 3.84 / 8.27 / 17.03 long, a dark gap 3.48 / 7.85 / 16.81;
  a 4-cycle dash reaches 0.91 of the line's level, a 4-cycle gap falls to 0.01: the beam's switch is done well inside a
  cycle or two and the same both ways (off - on = +0.2 cycles).

## The Timer Test's photos (2026-11-21, 22:xx): `tt1.py`, `tt2.py` (the tube), `tt3.py` (the core's own lines)

Eight DNGs (main camera, ISO 64); the same lines as the Settle Test, px a cycle from the hand-RAMP pages. Both speeds agree:

| | the tube | the core today |
|---|---|---|
| a timer-driven line of N cycles (a hand RAMP of L: L + 2) | N + 4.8 | N + 7.5 |
| timer 1's RAMP on, against a port write in the starting write's cycle | 1.3 cycles earlier | 4.9 earlier (the start delay of 5 is not applied to timer 1) |
| timer 1's RAMP off, against a port write at t0 + N | 1.1 later | 0.0 |
| the shift register's beam OFF against a PCR write | 2.2 later | 0.0 |
| the shift register's beam ON against a PCR write | 1.7 later | 0.0 |

So in the core the BIOS's lines run 2.7 cycles too long and the shift register switches the beam 2 cycles too early.
With all of it set (lag 13.3 in 3 stages, VT_RAMPDLY=1,3,4, VT_ZDELAY=2, VT_SRDELAY=2-3, X delay 3) the Corner Test's
roundness matches (cut / leg 0.124 / 0.104 / 0.091 / 0.064 / 0.054 / 0.030) but the Gap Test's joins still gape more than the
photo (dip index 0.09-0.19 against 0.06-0.17) and the dots at the small scales are missing: the BIOS's short pieces (6-13
cycles) behave as if the spot were 2-3 cycles earlier than the long hand-timed lines say. Open: whether a short move
settles faster than a long one (the Settle Test's lines are 24-48 cycles) - a page with ramps of 6-13 cycles is owed.

## The Short Test's photos (2026-11-21, 22:xx): `sh1.py` (the tube), `sh2.py` (the core), `st6.py` (X and Y together)

- **A short move settles as a long one**: the travel still to come K cycles after RAMP off, ramps of 6 / 8 / 10 / 13 cycles
  ($7F): K 5: 4.82 / 6.37 / 7.43 / 8.45, K 12: 2.70 / 3.35 / 3.72 / 4.19, K 20: 0.96 / 1.16 / 1.22 / 1.34 - what the long line's curve
  (9.36 / 4.07 / 1.13) gives for a ramp that short (the long curve less itself L cycles later: 4.7 / 2.5 / 0.8 for L 6), if
  anything a little MORE to come, not less. The same at four speeds (page 4: $40 / $60 / $7F within 0.3 cycles). The spot's
  response is linear - no faster settling for the BIOS's short pieces.
- Timer 1 with N = 6-13 (page 3): as page 1's curves 1.5-2.2 cycles later, as the Timer Test's long lines.
- In my hand-timed X lines the X speed is written a cycle after RAMP on: the line does not move until it arrives.
  `st6.py` (X and Y in px, the scales free, stop - start 2 from the Switch Test's page 1): **the X speed arrives 2.2 cycles
  after the RAMP's start**; Y in all 11.0-11.6 cycles behind after a start, 13.0-13.6 after a stop; the split between a
  pure delay and lag stages is NOT settled by these lines (1 stage: start 2.3, lag 9.4; 2: -1.6, 12.7; 3: -4.3, 15.3 - all
  0.36-0.40 cycles rms): only the corners' roundness by rest tells them apart (it wants the stages).
- Found on the way, in the core: a 16-bit store to the VIA (STX / STY / STD <$00) writes both bytes in the instruction's
  last cycle; a 6809 writes the first one (port B: RAMP, the mux) a cycle earlier. Every cart of mine switches RAMP that
  way, the BIOS mostly with 8-bit writes: a cycle of the mismatch between the hand-timed lines and the BIOS's. To fix.

## The Join Test's photos and the setting that fits everything timed (2026-11-21, late night)

- `jt1.py` (the tube), `jt2.py` (the core): the BIOS's line by hand (shift register + timer 1, N 6-13). The tube, N 13, travel
  to come when the shift register is cleared K = 2 / 5 / 8 / 11 / 14 / 17 / 20 after t0 + N: 10.78 / 8.37 / 6.04 / 4.12 / 2.58 / 1.43 / 0.78;
  travel done when it is set K = 4 / 8 / 12 / 16 / 20 after t0: 0.96 / 2.41 / 4.72 / 7.68 / 10.64 (at the BIOS's own -4: 0.1).
  The core with lag 13.3 in 3 stages, VT_RAMPDLY=1,3,5, VT_ZDELAY=2, VT_SRDELAY=2, X delay 3: 10.46 / 8.02 / 5.79 / 3.96 / 2.59 /
  1.63 / 0.97 and 0.81 / 2.47 / 4.97 / 8.08 / 11.24 - within 0.3-0.6 cycles; the core today 10.31 / 7.74 / 5.01 / 3.00 / 1.78 / 1.05 /
  0.60 and 0.00 / 0.83 / 3.18 / 6.32 / 9.87.
- Two rows of page 1 show a hook (N 8 / K 5, N 11 / K 2): the shift register cleared 17 cycles after it was set does not
  blank - the 6522's shift register written as its eight bits end. Noted, not modelled.
- Pages 3 and 4 (the Y hold): the second leg starts right for every K from 5 to 24 and with the mux open 7 or 12 cycles -
  the Y speed is in its hold in time; nothing to change there.
- **A harness trap**: the frontend's "line gaps" setting (vt_set_rampoff) arrives after VT_RAMPDLY and set both stop
  delays back to 7 - every `cgrid.py` row with `ramp=` before this line ran with stop 7. Now VT_RAMPDLY, when given, stands.
- With that fixed, rendered frames (`cgrid.py`; lag 13.3 in 3 stages, VT_RAMPDLY=1,3,4, VT_ZDELAY=2, VT_SRDELAY=3, X / Y delay 3):
  the Corner Test's cut / leg 0.124 / 0.104 / 0.091 / 0.064 / 0.054 / 0.030 (photo 0.122 / 0.120 / 0.105 / 0.073 / 0.044 / 0.003; today
  0.092 / 0.058 / 0.052 / 0.036 / 0.026 / 0.019); the Gap Test's dip index 0.066 / 0.066 / 0.076 / 0.086 / 0.107 / 0.129 / 0.133 / 0.047
  (photo 0.060 / 0.051 / 0.058 / 0.075 / 0.099 / 0.132 / 0.171 / 0.067: rms 0.018, today 0.024); the squares' size nearer the
  photo's; the font crisp. **What is left is light, not timing**: the tube shows a bright dot at every piece's start (the
  beam on while the spot only creeps away); the core's rule renderer makes a dot from a rest only, so with the slow
  start there is none (`testcart/compare/gap_joins_zoom.png`). The beam-on-phosphor light (dwell = light) is the fix.

## The Photo Test's photos (2026-11-21, 22:48): sixteen pages, three passes

28 DNGs (main camera): pages 1-12 at 1/50 s f/1.48 ISO 64 and again at f/4.0 ISO 50 (red free of clipping on nearly
every page), pages 13-16 at 1/5 s f/4.0. `pt0.py` (loading, the lines of a page), `pt1.py` (pages 1-2), `pt7.py` (pages
5, 7, tube and core), `pt_render.py` (the core's pages), pictures `testcart/compare/photo_p*.png` (tube | proposed | today).

- **Edge focus**: the text at the screen's top and bottom is 12-13 px tall at f/1.48 AND at f/4.0, 6-7 px in the middle -
  the tube's own (a camera's blur would shrink with the aperture); left / middle / right lines the same width.
- **Pages 1-2, light against dwell** (cycles a unit, 1.05 to 33): light a unit = c d^p - **p 0.70 at intensity $7F, 0.73 at
  $5F, 0.83 at $40, 0.90 at $30** (rms 2-5 %; a + b d does 7-10 %). At $7F the slowest line (32 x the dwell) is 11.7 x
  the fastest's light, at $40 17.6 x. $5F / $7F at the same speed 0.49-0.56.
- **Page 3**: a line drawn 1 / 2 / 4 / 8 times in the frame: 1 : 1.99 : 3.84 : >= 7.2 (the last clips) - **hits apart in time
  add up**; the crossings 1.00 +- 0.02 of the two lines' sum. The saturation is within one pass of the beam only.
- **Page 4**: light by intensity $3F / $5F / $6F / $7F = 0.12 / 0.51 / 0.76 / 1 ($1F does not show); the line's width the same
  (6 px at f/4: the growth at f/1.48, 7 to 9 px, is the lens); a dot of 20 cycles = 14-17 px of that line's light at
  every intensity, its width growing 6 -> 10 px with the intensity while its peak stays the line's (0.45): **a dot gets
  wider, not brighter**; left / middle / right within 10 %.
- **Page 5, the shift register's gap**: $00 written G cycles after $FF: the dash G 10..24 = 11.7 11.7 13.8 13.8 15.8 15.8
  17.7 (never off: 42) 19.7 20.8 21.8 22.7 23.6 24.5 25.6 cycles - **while the eight bits are still going out the
  switch follows their 2-cycle clock; a write 17 cycles after the last one is lost; otherwise 1.7 cycles late.** The
  core: G - 0.9 for every G: none of the three.
- Page 6, the bytes: most significant bit first, 2 cycles a bit, the last bit stays on the line - as the core.
- **Page 7, timer 1's flag in the BIOS's wait loop**: the line's end steps between N 9 / 10 and 16 / 17 - the core's
  steps at the same N: the flag's timing is right.
- Page 8: with the X speed in the DAC before RAMP on the eight rays are straight (X and Y start together); written
  with RAMP on the diagonals start bent (X ~2 cycles late), as the Settle Test said.
- Pages 9-12, the BIOS's shapes: the tube closes its corners with a dot at scales $08-$30; the core today too; **the
  "proposed" timing leaves gaps at the squares' corners** - not right for the BIOS's routines yet.
- Pages 13-16, the rate ($5F line, 1/5 s): 40 / 30 / 25 Hz = 0.78 / 0.60 / 0.51 of 50 Hz - the frames' count: a frame's light
  does not depend on the rate.
Owed next: the 6522's shift register as page 5 shows it (in place of the VT_SRDELAY stand-in), then the timing fitted
again on the BIOS's shapes; the light law (d^p by intensity, hits add, dots widen) into the renderer.

## The shift register in the core (VT_SRMODEL=1, a test switch) - 2026-11-21, night

As the Photo Test's page 5: a byte written while the last one's bits still leave goes out on their 2-cycle clock (the
second boundary after the write); written 17 cycles after the last it does not go out; up to VT_SRW (16) cycles after
the bits ended its first bit leaves 4 cycles after the write, later 2 (VT_SRD0). `pt5c.py`, the dash by G 10..24: tube
11.7 11.7 13.8 13.8 15.8 15.8 17.7 (lost) 19.7 20.8 21.8 22.7 23.6 24.5 25.6; the core with it 11.2 11.2 13.1 13.1 15.1 15.1
17.1 (lost) 19.0 20.0 21.0 22.0 23.0 24.0 25.0; without 9.1 10.1 ... 22.8.
- With it and today's timing the Gap Test's gaps at scales 10-12 vanish (dip 0.063 / 0.070 / 0.066, photo 0.099 / 0.132 /
  0.171): today's gaps came from the wrong shift register. With lag 13.3 in 3 stages, VT_RAMPDLY=0,2,3, VT_ZDELAY=1, X / Y 3:
  corners 0.117 / 0.104 / 0.090 / 0.065 / 0.056 / 0.030, dips rms 0.018 - but the BIOS's squares (Photo Test page 10) still
  open ~2 units at their corners where the tube closes them with a dot.
- The BIOS (Draw_VLp: the pattern to the shift register 6 cycles before timer 1 starts, a 7-cycle wait loop, then $00)
  clears the register 22 + 7k cycles after setting it - beyond what page 5 measured (24). The SR Test's pages 1-2 measure
  18 .. 60, its pages 3-4 the BIOS's own line by scale.

## The SR Test's photos (2026-11-22): `srt1.py` / `srt2.py` (the tube), `srt3.py` / `srt4.py` (the core), `sqtrace.py`

Four DNGs (1/50 s, f/1.48, ISO 64, focus on the left). All 15 rows a page by a projection (`srt2.py`).
- **Pages 1-2, $00 written G = 18 .. 60 cycles after $FF**: the dash grows evenly, ~7 px a cycle, no step and no lost
  write anywhere (G 18 19 20 .. 48: 117 124 132 138 144 152 158 165 172 178 186 192 199 207 212 220 227 234 242 248 . 264 270
  277 . 292 298 308 . 321 328; 54: 370; 60: 414 px). The "2 cycles more for 16 cycles after the bits" of the first
  reading (it made a step at G 32) is out: **a write goes out a fixed time later; while the bits still leave, on
  their 2-cycle clock (an odd gap one cycle more); 17 cycles after the last write it is lost.** The Photo Test's
  page 5 fits the same (even G <= 16 on one line with G >= 18). VT_SRMODEL=1 now does that (VT_SRD0, 3: the write's
  delay; VT_SRW=16 brings the first reading back).
- A photo's absolute lengths are good to ~1.5 cycles only: the scale runs 6.6 px a cycle at the left to 7.1 further
  right (from rows a wait-loop pass apart: 7 cycles more). Differences and levels are what to read.
- **Pages 3-4, the BIOS's Draw_VLp, one vector at scale N = 6 .. 35**: the lit length steps where the core's does
  (N 12/13, 19/20, 26/27, 33/34), by 3.1 cycles (the core with the proposed timing 3.2, today 2.9); within a pass it
  grows 0.56 a cycle of N (proposed 0.58, today 0.65). With the scale from the rows themselves the lengths fit
  VT_SRD0=3 to 0.1-0.3 cycles (N 6 / 13 / 20 / 27 / 35: tube 10.0 16.9 24.0 30.9 38.6, core 10.1 16.9 23.9 30.9 38.5).
  The light of the line's last 2 units against its middle (the beam lingering before the blank; a dash cut at speed:
  0.72): tube 1.13 .. 0.82 within a pass, core 1.05 .. 0.84 (VT_SRD0 2), 1.11 .. 0.88 (3), 1.15 .. 0.92 (4). The start: tube
  1.6-1.9 x the middle (peak 2.6-3.2 x), the core as dwell^0.7 2.1 (peak 4.0) - the dot's light wants the renderer's
  dot (wider, not brighter), not this stand-in.
- The BIOS's Draw_VLp from the ROM ($F410): the pattern to the register, CLR T1 high 6 cycles later (t0), the flag
  read at t0 + 7 + 7j, the register cleared 9 cycles after the read that saw it: K = 12 .. 17 cycles after t0 + N.
- **The Photo Test's page 10 again, by the light along a square's sides (f/4)**: the tube's lines do NOT reach the
  corner - each side brightens at its end (the beam slowing), dips, then comes the next side's start dot, 5 x the
  line's level and ~9 px wide. The line's end to the dot's centre at scale $20: 1.5-1.6 units (the core: today 2.0,
  proposed with VT_SRD0 3 / 4 / 5: 2.4 / 2.1 / 1.8). The squares look closed because the dot is wide and bright; the
  core's rule renderer draws no such dot from 3 cycles of rest. So the corners' "gaps" in the proposed renders are
  mostly the missing dot, not the timing.
- Gap Test / Corner Test on rendered frames with the new register: the joins' dips follow VT_ZDELAY + VT_SRD0 (5:
  rms 0.018, 4: 0.024, 3: 0.041); the corners as before (cut / leg 0.117 0.104 0.090 0.065 0.056 0.030). The Timer Test
  says 2 for the register after a PCR write, the line's end 3, the joins 4: 3 taken, whole cycles only so far.
Proposed (tests, nothing default): VT_SRMODEL=1 (VT_SRD0 3), lag 13.3 in 3 stages (VT_DEFLAGN=3), VT_RAMPDLY=0,2,3,
VT_ZDELAY=1, VT_XYZDLY=3,3,8. Owed: the renderer's light (dwell^p by intensity, hits add, a resting dot wider not
brighter, the slow start's light) - without it the BIOS's corners cannot look like the tube's; fractional delays.

## The light law in the PC's beam-trace renderer (2026-11-22): `lawfit.py`, `bt2_check.py`, `bt2_meas.py`, `pcany.py`

- Read again: the Photo Test's page 4 line is a slow one (Draw_Line_d, 16 units at scale $7F: 8.3 cycles a unit). A
  20-cycle dot = 2.0 units of THAT line, and its peak (0.44-0.49) is a line's of that dwell (page 1: 0.42 at 16.6 cycles
  a unit) - **the dot follows the lines' law, with the dwell taken over the spot's footprint (~1 unit)**; "wider, not
  brighter" was a wrong reading. The line's peak grows with the dwell (0.058 -> 0.65, x 11 from 1 to 33 cycles a unit),
  its width hardly (6.4-8.8 px).
- The law (`vecrender.py` BT2_*, `bt2_light`): the measured light a unit against the dwell at $30 / $40 / $5F / $7F as a
  table (log-log, between intensities by the drive), by intensity at 8.3 cycles a unit 0.022 / 0.12 / 0.51 / 0.76 / 1.
  `bt2_prepare`: along every run of lit pieces the beam's cycles within 1 unit of a place = its dwell; a long piece
  gives its ends as pieces of their own, a run's ends get a cap. Separate runs add. VX_BT_LAW=1: the law before.
- Rendered (beam trace, the proposed timing, low exposure, measured as the photos): page 1 light by speed / the second
  row $7F 1.33 1.58 2.04 2.42 3.13 5.42 9.51 (tube 1.32 1.60 2.06 2.43 3.18 5.51 9.78), $40 1.44 1.89 2.64 3.30 4.56 8.02
  13.5 (1.44 1.89 2.67 3.33 4.63 8.19 13.7); page 4 lines by intensity 0.116 / 0.56 / 0.75 / 1, a dot = 1.6-2.0 units of
  its line (2.0); page 10, the square's corner dot 5.8 x the line (tube 5), the line's end rising to 1.4 (1.4).
  Still off: the tube's line starts brighter for longer (1.3-1.5 x over ~3 units, ours 1.1 after 2.5), the dip before
  the corner dot is missing, the dot is rounder / smaller than the tube's (10.5 x 7.6 px against a 6.2 px line).
- `testcart/compare/photo_p10_vlp_beam.png`: the photo next to it. Tests switch the tube's warm-up off (`pcany.py`).
Owed: the same law in the core's GPU renderer (phones, RetroArch); Corner / Gap Test and Vectorblade in beam trace.

## Beam trace in the shared renderer (libretro_vx.c, a test: VX_BEAM=1) - 2026-11-22

`bt2_light` / `bt2_prepare` in C (as vecrender.py); the core sends the spot's path (vt_set_analog 11). This renderer adds
display values, not light, so pieces must not lie on each other: a run's caps go into its first / last moving piece,
joined pieces are cut at their bisector. Corner Test (rendered, proposed timing): cut / leg 0.121 0.087 0.093 0.069 0.056
0.045 0.031 0.011 (photo 0.122 0.120 0.105 0.073 0.044 0.003 0 0); Gap Test: one dot a join (10 a row, as the photo), dips
0.055-0.068 on display values, 0.09-0.12 as light (photo 0.05-0.17, growing with the scale - ours flat).
`cmp_show.py` -> `testcart/compare/now_corners.png`, `now_vectorblade.png` (photo | today | new).
Open: **Vectorblade's logo is 8 % wider / 10 % taller with the proposed timing than today** - to check against the photo
with a scale reference before anything becomes default; the lines' level by intensity is lower than before ($5F 0.51 of
$7F, was 0.67): the exposure wants setting again; start dots are short bright pieces (comet-like at 2x); no glass glow.

## The PC app under the test switches (2026-11-22) - `gaptrace.py`, `gaptrace2.py`

- **The PC app replaced VT_DEFLAG by the unit's lag (6)**: every PC beam-trace picture before this (page 10's dots, the
  20-cycle dot, "corner dot 5.8 x") had lag 6 with the proposed delays - the beam lit up to each line's end, no gaps. Now a
  VT_DEFLAG in the environment stands (Core) and forces Y too. The libretro pictures (LROPT) always had 13.3.
- The Gap Test's photo along a row: each line fades out, a dark gap (S10 / S11 / S12: ~12 / 18 / 30 px of 13.6 px a unit;
  none at S6, S13), then the next line's start dot (2.1-2.8 x the line across ~1.2 units) and a slow start (above the line
  for ~4 units). The core's path with the proposed timing: gaps 1.2 1.5 1.8 2.1 2.5 2.9 3.3 1.4 units (S6 .. S13; with the
  owner's unit 0.9 .. 2.9) - the tube's gaps where they show, but S13's is not on the tube.
- With the real lag the PC beam trace shows the gaps; the start dots hardly show - the lines already sit at white (the
  exposure), a dot can only get wider. Owed with the exposure.
- The switch-on bulge sent the frame through _build_py (screen values into the light shader: bright, bold, half a second
  after a load): _build_np scales now. Tube > Line gaps under VT_RAMPDLY: moves the test's stop delays by (setting - 7).
