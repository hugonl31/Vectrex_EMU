# Vectrex_EMU - technical details

What the emulator models, and how it was fitted to a real Vectrex - for the curious. The story behind
it is in the [README](../README.md#the-story-measuring-a-vectrex). Variables written `VT_...` are
environment variables of the core, for experiments.

## The core

* **Analog delays** (as Malban measured them for VIDE): the X integrator gets a new DAC value 15 cycles
  late, Y its sample 14, the integrators start 12 / stop 12.3 cycles after RAMP switches (Malban: 15), timer 1 runs N+2 cycles (6522 data sheet: N+1.5), the beam zeroes
  5 cycles after /ZERO. Cycle-timed code (Vectorblade's sprites and star field) needs them, and they put
  the BIOS raster text where photos of a real Vectrex show it. `VT_ANALOGDLY=0` switches them off;
  `VT_RAMPDLY=on,off` and `VT_XYZDLY=x,y,zero` change single delays.
* **Bus timing**: a 6809 reads and writes the VIA in the instruction's last cycle. The core runs each
  instruction once as a dry run to learn its length, runs the VIA and the analog side up to its last
  cycle, then executes it. With Malban's delays this gives the dark line ends of a real Vectrex (on the
  wide-gapped Vectrex in the photos Mine Storm's mines open at their inner corners and the ship's lines
  are ~20 % short; the default, a tighter tube, makes them ~15 % short).
  `VT_WRITETIME=1` = only the writes late, `0` = the old instant reads and writes.
* **Line ends** (fitted to photos of the test cart `testcart\timing_test.bin`, 2026-10-12):
  the BIOS polls timer 1 every 7 cycles, so /BLANK comes 10-16 cycles after a line's timer ends,
  depending on the line's length. With timer 1 running N+2 cycles (`VT_T1EXTRA=1`) and the
  integrators stopping 12.3 cycles after it (`VT_RAMPOFF`; a fraction = one more cycle at that share
  of the speed) the dark line ends fall where the real Vectrex has them: at scales $0B-$0C and
  $12-$13 (one period of the poll loop apart; 11.8 matched the test lines' sizes best, 12.3 gives
  Mine Storm's ship - drawn at $0C - its ~15 % short lines); Mine Storm's mothership closed but for
  its wing corners now and then.
  Wherever the beam rests the glass glows softly around the spot (Tube > Corner glow, strength 0.014, chosen side by side with a real tube
  by default; 1.6 game px wide, full from 18 cycles of rest). (`VT_BEAMFADE`, a fade-out trace
  after /BLANK, is off: it gave all text a shadow.)
* **Open bus**: a read where no chip answers (the $C000-$C7FF hole, $8000-$BFFF, an empty cartridge
  slot) returns the last byte on the data bus, as measured on a real Vectrex with
  `testcart\openbus_test.bin` (2026-10-14): $00 - the BIOS byte at $FFFF, read in the 6809's idle
  cycle just before the access - except after `,R` indexing (no idle cycle), which gets the next
  opcode. (vecx returned garbage in the hole and $FF elsewhere.) It matters for Mine Storm's own bug:
  a split mine can land in the shot mine's slot, which then gets type 4 and a stray vector list
  address ($3408); with no cartridge Draw_VLp now reads the next opcode ($30 / $2F) as the end of the
  list and nothing shows, as on the real Vectrex - before, $FF bytes drew a long diagonal streak and
  froze the game for seconds. (MAME has the same symptom open, bug 05784.)
* **Afterglow length**: the measured decay (see Phosphor in real time, below) runs at x0.6 by default (Tube >
  Afterglow length; chosen side by side with a real tube).
* **Afterglow joining** (Tube > Afterglow joining, default 0.5, chosen side by side with a real tube): a fading spot (older than the hold)
  spreads to its neighbours every screen frame, keeping half of its brightness, so the copies a moving
  object leaves - one per game frame - join into one smooth trail, as on the tube (Mine
  Storm's big mothership leaves "a singular trail, not a bunch of snapshots"); sharp,
  not blurred. Light brighter than a line (corner spots) spreads only as a line does. One step reaches
  1 px in an 820 px high window at 60 Hz (scaled with the window and the refresh rate). Lines being drawn
  and the measured fade times are unchanged. (A screen frame per game frame - G-Sync - did not change the
  copies.) The glow only grows during its first 35 ms after the hold (2 screen frames): it joins the
  copies but keeps the vector's size - unlimited, a fading bullet or text swelled into a blob.
  **Afterglow overlap** (Tube > Afterglow overlap, default 0.8 px in an 820 px window, scaled; chosen side by side with a real tube): the fading
  light - not the lit lines - is averaged over a small spot, as overlapping light adds up on the tube: the
  copies' steps and the corner dots at a shape's tips (Mine Storm's mothership wings) blend into a smooth
  gradient.
* **Dim beams, the cutoff** (measured with `testcart\bright_test.bin`, a photo and a count by
  eye on a real Vectrex): from intensity 63 up the beam's brightness is 0.55 + 0.45 x Z/127, as before; below,
  the tube's cutoff: lines are dark up to Z = 27 and grow as ((Z - 27) / 36.5)^1.7 ($20 very faint, $18
  dark: 10 of the page's 16 bars, as counted on the tube); a spot resting long (40+ cycles) has a sharper
  onset from Z = 23 ($20 clear, $18 dark: 10 dots). Short rests keep the line cutoff, so Vectorblade's
  faintest background stars (intensity 15 and 31, 3-cycle dots) stay (almost) dark, its 47 ones dim.
  Faint beams are also thinner (down to 0.45x).
* **Bloom of long bright rests**: a spot's energy = rest cycles x its intensity's brightness. Corners
  in every game stay at or below ~18 (Spinball's dots 26); above 20 the spot widens (up to 2x) and
  glows more (up to 2.5x), fully at 38 - Vectorblade's enemy shots (intensity 127, 38 cycles in one
  spot) are glowing dots as bright as the player's shot bars, as on the real tube.
* **6522, measured** with `testcart\cputime_test.bin` on a real Vectrex (all 32
  readings now equal): instruction cycles as the 6809 datasheet; timer 1 runs N+2 cycles (flag and PB7);
  after running out in one-shot mode timer 1 shows $FFFF for a cycle and reloads from its latch (vecx
  kept counting down; `VT_T1RELOAD=0/1/2`); the shift register's flag rises at the end of the 8th bit,
  2 cycles after it leaves (vecx: as it left); reading T2's low counter only clears its flag (vecx
  stopped timer 2: a game reading it froze in Wait_Recal).
* **Y sample-and-hold**: the Y hold capacitor charges through the multiplexer, closing 70 % of the gap
  to the DAC per cycle (`VT_YSHK`, 1 = instant as vecx): measured with `testcart\sh_test.bin` on a
  real Vectrex - connected for 4 cycles after a 191-step jump it stays ~1.5 steps short,
  from 6 cycles on it has settled. Normal games keep it connected longer: no line moves.
* **6522 details**: reading T1's low counter only clears the timer flag (timer 1 keeps running); the
  shift register's first bit leaves the cycle after it is written.

## The picture and the sound

All fitted to photos, a video and a sound recording of a real Vectrex:

* **Beam:** a soft round spot; brightness and width follow the time the beam spent per pixel
  (fast sweeps thin and a little fainter; wherever the beam rests - every corner, every dot such as a shot or a star - 100 % white, the lines just under it (~87 %), the same width - the difference is in brightness only, as in photos of a real Vectrex); everything pure white, phosphor afterglow, bloom.
* **Analog drift:** the integrator zero reference (closed shapes drawn as one loop do not quite
  close), the BIOS raster text offset, the 6522 shift register at one bit per two cycles (the BIOS
  characters as wide as on the tube).
* **Mains hum wobble:** the beam follows the 50 Hz mains phase at the moment each line is drawn; a
  game running at exactly 50 or 25 fps stands still, any other frame rate wobbles at its beat.
  Tube > Mains picks 50 Hz (Europe, the default) or 60 Hz (USA, Canada): on 60 Hz mains a 50 fps game
  ripples at the 10 Hz beat instead of standing almost still.
* **Brightness bending:** the high voltage sags with the beam load, the picture grows / shifts.
* **Sound:** the Vectrex buzz (follows what is drawn, never stops: the BIOS recalibrates every
  frame), all sound through a model of the small built-in speaker. The DAC reaches the sound line when
  the multiplexer selects it (how games play samples and speech, e.g. Spike's voice): its value is held
  there and mixed 6 : 1 against the sound chip's 10 bits, the balance the MiSTer core measured on a real
  Vectrex's line-out.
* **Power-on:** a real Vectrex's RAM wakes up random, and the BIOS keeps its random number seed as it
  finds it, so Mine Storm's first star field differs every time: the emulator gives the seed a new
  value at every power-on (as the MiSTer core does).
* **Phosphor in real time:** the screen runs at a fixed 60 Hz whatever the game's frame rate. Every
  line lights the phosphor at the moment the beam drew it, to its commanded brightness; then the spot
  fades as measured on a real Vectrex (a video of the title text going dark): at once to ~15 %, 4 %
  after 0.15 s, 1.5 % after 0.5 s, gone after ~2 s. A spot drawn again is lit again, it does not add up: an
  object is just as bright standing still as moving, only where it was a trail fades. Only what the
  beam draws lights up: a fast object at a low frame rate leaves separate fading copies, a slow one an
  almost smooth trail, as on the tube. On a 60 / 120 / 240 Hz monitor it uses vsync (no tearing); on other refresh rates it
  is paced by the clock, which a G-Sync / FreeSync monitor shows without tearing.
* **Emulation speed:** EMU RATE in the on-screen display shows how fast the emulation runs against a
  real Vectrex: green 100 %; orange below 99 % (the PC cannot keep up: the game runs
  slow, the sound breaks up); red below 90 %.
* **Resolution:** always the monitor's real pixels (Windows display scaling such as 150 % does not
  matter): full screen renders at the desktop resolution (e.g. 3840 x 2160), the window at its own
  size.
