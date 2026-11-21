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
