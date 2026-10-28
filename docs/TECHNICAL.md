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
  Since 2026-10-27 the dry run is tentative: RAM writes go through with an undo log, and an instruction that touched
  nothing but RAM, the BIOS and the cartridge (most of them - what it read cannot change during its cycles) is done in
  that one run; one that reads the VIA or writes anything else is undone and run again as before. The same results to
  the bit, ~10 % faster; `VT_ONERUN=0` runs every instruction twice again.
* **Speed of the analog side** (2026-10-27): the per-cycle analog model (`alg_sstep`) was 62 % of the 6809 core's time.
  Most of a frame nothing in it moves (the game waits for the next frame, the beam at rest): a cycle that left the whole
  state as it found it 64 times in a row (longer than any of its histories, which then hold one value) with the inputs
  unchanged would do the same again, so it is only counted - until a write, a setting or one of the lines the VIA moves
  by itself (timer 1's PB7, the shift register's CB2, the CA2 / CB2 pulses) changes. The whole state is compared only
  once the beam stands still, so a moving beam costs next to nothing extra. Off with the 3D Imager, the light pen, the
  beam-trace mode or the deflection protect on. `VT_ALGSKIP=0` off; `stats` prints the share skipped (`algskip`: 35-63 %
  in games). The CPU also reads the BIOS and the RAM itself instead of through the memory function. Together with the
  single run: 1500 frames of five games 6.2 -> 4.5 s on the 9800X3D (-27 %), every one of the 68 6809 cartridges and test
  cartridges bit-identical (the vectors, the RAM, the cycles).
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
* **The UVMC2's JIT** (`src/core/cm33_jit.c`, 2026-10-27): the RP2350's two Cortex-M33 cores run their Thumb code as
  native x86-64 or ARM64 code, a block at a time, **giving exactly the interpreter's result** - checked frame by frame
  (the vectors drawn at three points of 20 s of play) on VecDoom, Veccy Road and the UVMC2 Demo, on Windows, Linux x86-64
  and a Raspberry Pi 5. The interpreter already decoded every instruction once into a handler; the JIT compiles the
  common ones inline (moves, adds, compares, logic, shifts, extends, loads and stores with a fast path for SRAM and
  PSRAM - keeping the spin detector's bookkeeping exactly as the memory functions do - PUSH / POP / LDM / STM / LDRD,
  the branches) and calls the handler for the rest, with the state the interpreter's loop would give it. The cycle count
  lives in a host register and goes to memory only where C code can see it; the PC likewise. Blocks end on the loop's
  own conditions (the PC elsewhere, attention, the slice's end), and a small dispatcher goes straight on into the next
  block, doing between blocks what the loop does between instructions (the spin detection, `run_attn`). Every
  instruction's code is an entry point (the RP2350 runs in 64-cycle slices, so blocks are entered in the middle all the
  time). A write over compiled instructions drops the blocks around them only (a game's variables often share a page
  with its code). `VT_M33JIT=0` off; `VT_M33JIT=calls` every instruction through its handler (the reference);
  `VT_M33NOINL=hex` switches inline groups off (to find a wrong one); `VT_M33STATS=1` statistics; `VT_PERFMAP=1`
  (Linux) a `/tmp/perf-PID.map` so `perf` names the blocks. Measured, 20 s of game time each (one host thread / two):
  VecDoom on the 9800X3D 20.0 -> 14.8 s / 15.4 -> 9.9 s; on a Ryzen 7840HS laptop chip at 28 W 21.9 -> 17.2 s / -> 10.8 s;
  on a Raspberry Pi 5 47.9 -> 41.4 s (one thread). What remains is mostly the RP2350's peripherals: the PIO state
  machines, the DMA, the pins - a game that bit-bangs the Vectrex's bus from the CPU (the UVMC2 Demo) spends its time
  in GPIO reads and the spin detector, not in its instructions.
  Fixed 2026-10-27: the flag "a core holds an exclusive monitor" (LDREX) was never cleared, so after a program's first
  LDREX every compiled store and PUSH took the slow way for good (h_push alone was 4 % of VecDoom's time); it now
  follows the monitors (with the cores on two host threads it stays set once set, as before - the other core may take a
  monitor while this one clears the flag). And the PIO skips its clock divider's two 64-bit divisions when the divider
  is 1. The PIO's run loop (`pio_run_sm`) now works out a state machine's settings once per run and keeps its registers
  (PC, X, Y, the shift registers, the delay) in a local for the run, so the host keeps them in registers; while the PIO
  has its part of a slice, the pin levels are kept until one of its own pin writes. PIO 17 -> 15.6 % of VecDoom's time; with it
  VecDoom on the 7840HS at 28 W 17.1 -> 15.5 s, the UVMC2 Demo 14.6 -> 13.5 s (the JIT; today's changes together).
  Before the PIO change, VecDoom with the JIT, one thread: 9800X3D 14.8 -> 14.4 s, Ryzen 7840HS at 28 W 17.2 -> 16.2 s, Raspberry Pi 5
  41.4 -> 38.6 s; the three games' frames the same as the interpreter's on all three machines. The PIO itself is now the largest piece (17 %: the bus
  state machine waits on the Vectrex's E clock, ~5 instructions an edge).
  Tried and dropped (no gain within 2 %): the known PIO programs (VecDoom's bus player, the PT8211 audio program) with
  their instructions as compile-time constants, and skipping state machines stalled on an empty FIFO before the call.
  The PIO's cost is its 93 M runs in 20 s of VecDoom (once per half E period per active state machine), not decoding.
  How the games drive the bus (20 s): VecDoom through that PIO bus player, 27.6 M writes and 24 k reads; Veccy Road
  from the CPU's own pins (SIO), 28.7 M writes and 17 k reads; no routine is shared. A high-level bus - the RP2350
  running ahead of the Vectrex, the Vectrex caught up whenever the RP2350 needs one of its inputs (read data, /IRQ,
  PB6) - would stay exact and save the lockstep's own ~15-20 %, at the price of turning the vecx / RP2350 control
  flow around (not done).
* **The PiTrex's USB keyboard and mouse** (`pitrex_usb.c`: the Pi's DWC OTG host controller as USPi drives it, with
  usbhost.c's keyboard + mouse device on its root port, on with `VT_USBHID=1` - the frontend sets it for `.img` / `.pit`
  as for `.um2`): checked 2026-10-27 headless with the script's `usbkey`: GS BASIC finds the keyboard, Ctrl+C breaks
  its menu and `print 12345 + 3` answers 12348; Elite (newkind) finds it and takes N at "Load new commander (Y/N)?".
* **The PiTrex's JIT** (`src/core/arm11_jit.c`, x86-64; `arm11_jit_a64.c`, ARM64): fixed 2026-10-27 - on Linux and
  macOS x86-64 the PiTrex never started with the JIT on. A short backward branch writes the cached ARM registers back,
  calls the loop check, and wrote them back again on its way out: by then the call had changed RSI / RDI (kept across
  calls on Windows, not on Linux / macOS), so the BSS clear at the start ran away with r1 = garbage. The second write
  goes now (and a conditional branch not taken leaves the block's state as it was). The ARM64 backend got the VFP and
  counter instructions in place (K_COP, as x86-64 since 2026-10-26): the loader's interpreter entries 35.6 M -> 0.27 M.
  The PiTrex loader, 583 frames: Linux x86-64 (Ryzen 7840HS) interpreter 3.8 s, JIT 2.0 s (before: never booted); a
  Raspberry Pi 5 interpreter 9.7 s, JIT 5.9 -> 5.25 s; Windows unchanged (2.35 s). `VT_JIT=exact` matches the interpreter
  frame for frame on all three. (The IRQ handler's return, `LDM sp!, {..., pc}^`, ~27 000 a second, still runs in the
  interpreter: too rare to matter.)
* **The PiTrex's timer delay loops** (`pitrex_cart.c` px_solve, `arm11.c` arm11_trial, 2026-10-27): a loop like
  `while (CLO - t0 < d)` on the Pi's 1 MHz system timer was seen as a wait, but the timer changes every microsecond, so
  the ARM woke a thousand cycles apart to go round once more - 33 M times in the loader's 14 s. Now each such wait is
  solved once: the loop's round is run on a copy of the CPU with the timer forced to later values (a store, a read of
  any other peripheral, a coprocessor instruction or a call stops the try: unsure means waking as before), doubling then
  halving in to the first value it leaves at; the CPU then sleeps until the timer gets there. The same loop usually
  waits as long as last time, or (woken by an interrupt in the middle) for the same end: those are tried first, so a
  wait costs 2.5 tries on average. The loader: Windows JIT 1.82 -> 1.62 s, Linux 2.08 -> 1.90 s, Pi 5 5.21 -> 4.58 s
  (the interpreter 9.75 s: the JIT is now 2.1x on the Pi). The vectors and the Pi's registers, cycles and peripheral
  accesses stay the same; one thing differs: the loop's own timer register holds the value from the start of the wait,
  not the latest, so an interrupt's saved copy of it (a word under the IRQ stack, never read again - the loop reloads
  the register before it looks at it) differs. Interpreter and `VT_JIT=exact` still match each other exactly.
  `VT_PXSOLVE=0` off (the old way, bit for bit); the digest prints the waits solved, the wake-ups saved and the tries.
* **Deflection protect** (the Vectrex's spot killer, 2026-10-27): the power board rectifies the Y deflection
  amplifier's swing (C409 0.47 uF, D401 / D402, C410 0.47 uF drained by R530 820K: RC 0.385 s, clamped at 6.2 V) into
  the base of Q503, the transistor the beam current flows through. No swing on Y for a while and the beam fades; bright
  drawing drains the capacitor faster; a Y swing of some size pumps it up again. The BIOS's Recalibrate sweeps the
  whole screen every frame as a "keep alive", so games never trip it; the test cartridge's BEAM CUT OFF test does
  (the service manual: the shrinking cube "will decrease in size, then disappear, then reappear in about 2-3 seconds"
  - ours vanishes below ~12 % of the screen height and fades back in as it grows), and a crashed game's resting dot
  goes dark in about half a second instead of burning. The model only scales each line's Z: the game is unchanged.
  **Off by default until it is calibrated**: as modelled it would also dim Vectorblade's loading bar (a row of dots
  spanning 3 % of the screen height, drawn for over a second) - whether the real console does that is the first thing
  to check. `VT_PROTECT=1` switches it on; `VT_PROTECT=vpp,ib,v0,v1,shrink` also sets the Y swing per full-screen
  deflection (14 V), the capacitor's drain at full beam (12 V/s), the base voltages where the beam is off / fully on
  (0.7 / 2.2 V) and how much the deflection shrinks (the J-FET switches; 0, not modelled). To calibrate against a real
  Vectrex: a video of Test Rev. 4's BEAM CUT OFF test (when the cube vanishes and when it is back) and of
  Vectorblade's start. The core's stats line reports `protmin` (the lowest the model took the beam) and `protdim`
  (lines it dimmed) for any run.
* **16-bit registers** (2026-10-27): the 6809's registers wrap at 16 bits (vecx let S run past 0 into a 32-bit value;
  `VT_REGMASK=0` for the old way), an illegal opcode costs 2 cycles (0 cycles stopped time: the core hung on a
  cartridge full of $10 prefix bytes - found by fuzzing with garbage cartridges), and the core reports an illegal
  opcode or post-byte 20 times, not millions.
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

### The tube's geometry and the case: real sizes (2026-10-28)

The renderer's unit is half the picture height of the 35000 x 41000 drawing area (`ALG_MAX_Y / 2`). The overlay
sheet is `ART_TUBE_K` = 0.924 units high on each side of the centre = 103.5 mm, so 1 unit = `MM_UNIT` = 112.0 mm;
everything else is in real millimetres from the owner's trace (3x real size, one layer each for the frame, the
glass's edge and the Test Cartridge grid).

- `casemodel.CASE_LUT`: the case's opening, a rounded rectangle 155 x 200 mm (corner r 5.13 mm), its radius every
  degree; `FRONT_W` 3.5 mm of matte front round it. The shader's `sdC` draws the opening exactly (a rounded-rectangle
  distance), the front as a parallel band - outer edge 162 x 207 mm, corner r 8.63 mm.
- `vecrender.GLASS_LUT`: the traced glass edge every degree (the four corners averaged, mirrored). The room light's
  maps and reflections were made for the earlier outline (`GLASS_REF_AW/AH`): the shader looks them up in those
  coordinates (`gsc`), so they keep their look. The walls' depth formula was refitted to keep their angle.
- `tubemap.py`: the picture's map. The emulator's grid lines (the core's own vectors) were fitted to the traced
  ones: a cubic least-squares fit, plus a thin-plate correction through all 17 x 17 crossings and quarter points
  along every line between them, the short lines' ends included; the result sampled every 1/64 unit
  (129 x 161) as its straight part `LIN` plus int16 differences, bilinear between them. The brightness bending's
  growth at the grid's load (x1.0172, load 0.366) is divided out, so the finished picture matches. Measured on
  the rendered picture (wobble and glow off): every line within 0.3-0.4 window pixels (0.05 mm), mean 0.07.
  `tube_map(k)` (plain Python) and `_tube_f` (numpy) blend `LIN + k x` the table.
- `pic_adj()`: behind an overlay the picture is scaled and moved by the overlay's sliders (inverted) and the
  parallax; the sheet itself (`art_rect`) always lies on the frame. The .art plane is 1360 wide, its sheet the
  middle 846 columns: the plane is sized so the sheet is exactly 162 x 207 mm.
- `vecx.h` `ALG_MAX_X` 33000 -> 35000: the grid's outer columns were at -141 and 33007. Everything centres on
  `ALG_MAX_X / 2`, so the coordinates only shift.
- `vecx.c` `ALG_CLIP` (6000 units): the beam is drawn that far past the drawing area on every side - the glass
  reaches about 6 mm beyond it. The live protocol sends the coordinates + `LIVE_OFS` (8000, live.c) so they stay
  unsigned; the frontend's decoder takes it off (`LINE_DTS`, signed). `tubemap.py`'s table covers the same range
  (u +-1.19, v +-1.33).

### Line preparation cost (2026-10-27)
The renderer turns each game frame's lines into capsules on the drawing thread (`_build`: dwell rests, bend smoothing,
the tube map, joints, the vertex arrays - all NumPy). VecDoom, the densest game (about 6,700 pieces a frame), measured on
30 recorded frames: 5.0 ms a frame before, 4.6 ms after the bend-smoothing curve was computed only for the pieces that are
actually cut (output identical). The rest is spread over several hundred array operations of ~5 us each. A compiled
helper could take it under 1 ms, but on every machine fast enough to emulate VecDoom the drawing thread keeps headroom
(GEEKOM 7840HS: ~6 ms a frame, ~30 % of a core), while slower machines run out of emulation first; classic games stay
under 1 ms a frame. Not worth a compiled helper yet.
