# Vectrex_EMU

**A Vectrex emulator that doesn't try to be perfect. It tries to be a Vectrex.**

<p align="center"><img src="docs/images/title.png" width="460" alt="Scramble in Vectrex_EMU, with the phosphor afterglow trailing behind the ship and the scrolling landscape"></p>

The Vectrex (GCE / Milton Bradley, 1982) is the only home console that ever drew its games with a real
vector beam: one electron beam, steered by analog circuits, tracing every line straight onto the
phosphor of its own built-in 9-inch screen. Most emulators show what the games *meant* to draw: clean,
sharp lines on black. A real Vectrex never looks like that. Its lines are made of moving light; corners
glow where the beam rests; shapes don't quite close because the analog circuits drift; text has gaps;
the picture wobbles with the mains hum; everything leaves a fading trail; and the speaker buzzes along
with whatever is on screen.

**Vectrex_EMU is built to show all of that** - measured on a real Vectrex, not guessed.

Windows, macOS (Intel and Apple Silicon) and Linux. Downloads: [Releases](../../releases)
(Mac and Linux), or build the Windows exe with one double-click (see [Building](#building)).

**Author:** Hugo Bokhorst ([@hugonl31](https://github.com/hugonl31)) - the idea, the real Vectrex, every
photo, recording and measurement, and every call on how it should look, sound and feel.<br>
**Development assistant and programmer:** Claude (Anthropic).

---

## The story: measuring a Vectrex

**First, a word about the photos.** Glowing phosphor is very hard to photograph faithfully. The eye
sees a CRT differently from a camera: the exposure decides how much of the glow a photo catches, so its
bloom, its brightness and its fading afterglow are never quite what you see in the room. Keep that in
mind with every comparison below. The photos are still valuable - they showed us *where* things happen
and how long they last - but getting the bloom and the decay to *look* right took more than photos. In
the end the most important tool was A/B testing by eye: many rounds with the real Vectrex and the
emulator on an IPS monitor side by side, until the two looked very much alike in real life.

The project started from **vecx**, a long-standing open-source Vectrex emulator, and from one rule:
*nothing gets changed because it "looks nicer" - only because a real Vectrex does it.* So every step was
the same loop: look at the real machine, measure it, change the emulator, compare again.

### 1. Photographing the beam

The first surprise was how different a real Vectrex looks up close. We took long series of close-up
photos of the real tube, game by game, and laid them next to the emulator. The lines of Mine Storm's
ship stop short of their corners; the mines open up at their inner points; the corners where the beam
rests for a moment shine as bright dots; fast lines are thinner than slow ones.

An ideal emulator - one that runs the 6809 exactly but treats the rest as perfect - draws none of that:
every line meets its neighbour, every shape closes.

<p align="center">
<img src="docs/images/real_ship_mine.jpg" width="380" alt="Mine Storm's ship and a mine on a real Vectrex">
<img src="docs/images/emu_ship_mine_perfect.png" width="380" alt="the same in an ideal emulation, without gaps"><br>
<em>Left: a real Vectrex (one with unusually wide gaps). Right: an ideal emulation - the CPU exact, the
analog side perfect: no gaps at all.</em>
</p>

So where do the gaps come from? They turned out to be timing. The 6809 CPU, the 6522 VIA and its timer,
and the analog integrators that move the beam do not switch together: they start and stop a few cycles
apart. A new value from the DAC reaches the X integrator about 15 cycles late and the Y sample about 14;
the integrators start and stop some 12 cycles after their RAMP signal switches; the timer runs a little
longer than its count (as Malban measured these delays for his VIDE emulator, checked here against our
photos). A line therefore starts a moment late and stops a moment late, and a short line at a corner
can end before the beam got there. We made the emulator run every instruction's bus access in the exact
cycle the real chips do and added those analog delays - and the gaps appeared, in the same places as on
the tube:

<p align="center">
<img src="docs/images/real_ship_mine.jpg" width="380" alt="Mine Storm's ship and a mine on a real Vectrex">
<img src="docs/images/emu_ship_mine_gaps.png" width="380" alt="the same in Vectrex_EMU with wide gaps"><br>
<em>Left: the real Vectrex. Right: Vectrex_EMU with the delays, set to this machine's wide gaps. Note the
short lines, the open corners and the bright resting dots.</em>
</p>

<p align="center">
<img src="docs/images/real_mothership.jpg" width="380" alt="the mothership on a real Vectrex">
<img src="docs/images/emu_mothership.png" width="380" alt="the mothership in Vectrex_EMU"><br>
<em>Mine Storm's mothership: real (left) and emulated (right).</em>
</p>

### 2. Filming the glow

A vector screen has no frames; it has *phosphor*. We filmed the Vectrex title fading to black and measured
the light frame by frame: the glow drops to ~15 % at once, then fades over about two seconds along two
curves. The emulator lights the phosphor at the exact moment the beam passes each spot and lets it fade
along that measured curve, sixty times a second, whatever frame rate the game runs at. A moving object
leaves a real trail, not a stack of copies - checked against Mine Storm's big mothership, which on the
real tube leaves one smooth trail.

<p align="center">
<img src="docs/images/real_phosphor_decay.png" width="400" alt="the mothership's fading trail on a real Vectrex">
<img src="docs/images/emu_phosphor_decay.png" width="400" alt="the mothership's fading trail in Vectrex_EMU"><br>
<em>Mine Storm's mothership on the move: the trail of fading phosphor it leaves on a real Vectrex (left) and in Vectrex_EMU (right).</em>
</p>

Even the brightness was measured: a test page of bars and dots at 16 intensities showed exactly where a
dim beam disappears on the real tube (10 lines and 10 dots still visible).

### 3. Listening

The Vectrex is not quiet. The beam's currents leak into the audio amplifier, so the speaker **buzzes**
along with what is drawn - louder with more lines, at the game's frame rate, never quite silent. We
recorded it, together with the small built-in speaker's sound, the click of the power switch and the
crackle of a worn volume knob. All of it is in: the buzz is generated from the actual vectors each frame,
the sound plays through a model of that speaker, and turning the knob (which is also the power switch)
scratches, crackles and sometimes cuts out, as an old potentiometer does.

### 4. Asking the chips directly

Some things can't be seen in a game, so we wrote **test cartridges**: small 6809 programs that ask the
hardware a question and print the answer on screen. They were flashed to a real cartridge, run on the
real Vectrex, photographed - and then the emulator was changed until it printed the same numbers.

<p align="center">
<img src="docs/images/real_cputime.jpg" width="250" alt="CPU timing test on a real Vectrex">
<img src="docs/images/real_openbus.jpg" width="250" alt="open bus test on a real Vectrex">
<img src="docs/images/real_bright.jpg" width="250" alt="brightness test on a real Vectrex"><br>
<em>Test cartridges on the real Vectrex: CPU and timer cycles, open bus reads, beam brightness.</em>
</p>

* **CPU timing**: 32 measurements of instruction cycles and the 6522's timers and shift register. vecx
  was off in several places (a timer running one cycle long, a flag set too early, a timer that stopped
  when read). All 32 now match.
* **Open bus**: what the CPU reads where no chip answers. The real Vectrex returns the last byte on the
  data bus; vecx returned garbage. That tiny detail decides a famous glitch (below).
* **Sample and hold**: how fast the Y axis follows a jump - measured, modelled.
* **Brightness**: where a dim beam disappears - see above.

### 5. The Mine Storm bug

Mine Storm, the game built into every Vectrex, has a rare bug: when a mine splits at the wrong moment,
the game reads a stray list of vectors. On the emulator this froze the game for seconds and drew a long
diagonal streak. We logged sessions until it happened, traced it to the exact instruction - and then
tested the real hardware: on the real Vectrex, the built-in Mine Storm shrugs it off, while the same
game on a cartridge glitches for a second or two. The difference is the open bus. With the measured
open-bus behaviour, Vectrex_EMU now does exactly what the real machine does - in both cases.

### 6. The analog side

Things no digital emulator has, because they are analog:

* **Drift**: the integrators' zero point wanders, so a shape drawn as one loop doesn't quite close -
  calibrated against photos of test patterns on the real tube.
* **Mains hum**: the power supply's 50 Hz hum pushes the beam a little. A game that finishes each frame
  within the Vectrex's budget of 30,000 CPU cycles runs at 50 frames a second, in step with the hum:
  every frame is pushed the same way and the picture stands still. But a busy scene - lots of vectors on
  screen - can need more cycles than that. The frame takes longer, the game slows to, say, 48 fps, and
  its frames slip against the hum: the picture starts to sway (a scene far over budget wobbles faster).
  At 25 fps, exactly two hum waves per frame, it is in step again and stands still. Vectrex_EMU works out
  the hum at the exact moment each line is drawn, so its games wobble exactly when a real Vectrex's do.

  <p align="center"><img src="docs/images/wobble_sync.svg" width="760" alt="the game's frames against the 50 Hz mains hum at 50, 48 and 25 fps"></p>
  <p align="center"><img src="docs/images/wobble.gif" width="640" alt="a still test picture at 50, 48 and 25 fps"><br>
  <em>A still test picture (testcart/wobble_test.asm) at 50, 48 and 25 fps. For demonstration only: the
  wobble is exaggerated about ten times and the animation runs at half speed, not in real time.</em></p>
* **Brightness bending**: the high voltage sags with a busy screen; the picture breathes.
* **The tube**: curved glass, glow around resting spots, a soft round spot - and switching off makes the
  picture shrink away with a last flash of the beam, as the real one does.

The result is not a "filter" laid over a clean picture: the imperfections come out of the emulation.

### 7. One choice: how wide the gaps are

Everything above copies the real machine. One thing is a choice. How *wide* the gaps in the lines are
(chapter 1) varies from one Vectrex to the next: component tolerances and age shift the analog timing,
so some machines show wide gaps and others barely any. The Vectrex in our photos has exceptionally wide
ones. Rather than copy this one machine, Vectrex_EMU keeps the gaps where they belong but, by default,
at the size of a better tuned - or less worn - Vectrex: a tighter tube. **Tube > Line gaps** sets them
to taste: the integrators' stop delay, 9.3 cycles (corners nearly closed) to 15.3 (wide, like the
machine in the photos), 12.3 by default.

<p align="center">
<img src="docs/images/emu_ship_mine.png" width="380" alt="the ship and a mine in Vectrex_EMU with the default gaps"><br>
<em>The default (12.3 cycles): the same gaps in the same places as on the real machine in chapter 1, but tighter.</em>
</p>

### 8. Speech: two rare modules

A handful of Vectrex games and demos talk - or sing - through a speech module plugged into the second
controller port. There are two: the **VecVoice** (Richard Hutchinson), built around General Instrument's
SP0256-AL2 speech chip, and the **VecVox** (Alex Herbert), built around Magnevation's SpeakJet. Both were
hobbyist boards made in small numbers, long out of production; today they are very hard to find.

**How they work on a real Vectrex.** The game sends bytes over a serial line it bit-bangs on the sound
chip's I/O port (bit 4 of port A, 9615 baud); the module answers "ready for the next one" on port 2's
button lines. The VecVoice takes SP0256 *allophones* - 64 small speech sounds from the chip's 2 KB ROM,
put together by a 12-pole digital filter. The VecVox takes SpeakJet codes: 72 phonemes plus codes for
pitch (in Hz), speed, stress and volume, which is how VecVox X-Mas sings its tunes. The speech comes out
of the module's own amplifier, not the Vectrex's speaker.

**How we built them.** Without the modules on the desk, we worked from what is documented and from
recordings of the real ones, comparing A/B by ear:

* **VecVoice**: the SP0256 is fully documented, and MAME has a faithful emulation of it (Joe Zbiciak
  and Tim Lindner); Vectrex_EMU runs a C port of it, with the serial link as the MiSTer core models it.
* **VecVox**: how the SpeakJet makes its sounds was never published - it is a programmed chip that has
  never been read out. So the VecVox speaks with the SP0256's voice: every SpeakJet phoneme becomes the
  nearest SP0256 sound(s), stretched to the SpeakJet's length and sung at the SpeakJet's pitch. Its
  speed law was fitted to a recording of VecVox X-Mas: 25.9 s against the recording's 25.2 s, most
  notes landing on the beat. A formant synthesizer built from the same recording was tried as well; side
  by side, the SP0256-based voice sounded more like the real thing, so that is the one that stayed.
* **The level** follows the MiSTer core's speech mix (Sorgelig's compressor), 50 % louder, with an
  arcade-style bass for big speakers - most owners plug better speakers into the module than the
  Vectrex's own.

The SP0256-AL2's ROM is (c) Microchip and not included: it is a free download (see
[Speech](#speech-vecvoice-and-vecvox)), and so is the list of games that talk.

### 9. Overlays - and forty years of scratches

Every Vectrex game came with a printed plastic **overlay** for the screen: see-through colour tints the
vectors behind it, solid print adds the borders, the artwork and the game's name. The emulator reads the
MiSTer core's overlay files (`.art`) and plain PNG images, and lays the sheet over the finished picture:
the coloured plastic multiplies the light that comes through it, the print shines in the room's light,
and the sheet stays **flat** - the curved tube bends the vectors behind it, not the overlay. It sits
just in front of the glass, so seen from the usual height the picture sits a little higher on it
(**parallax**, as on MiSTer). With **Display > Real size** the sheet is exactly as big as a real one,
162 x 207 mm.

<p align="center">
<img src="docs/images/overlay_onoff.png" width="640" alt="Mine Storm without and with its overlay"><br>
<em>Mine Storm without and with its overlay (the one overlay that comes with the emulator).</em>
</p>

**Room light** lights the sheet itself: in an evening room the plastic is nearly black and only the
vectors show; in daylight the colours and the print come up. The plastic's light follows the setting
steeply, the print more gently, and **Printed art brightness** sets one against the other.

<p align="center">
<img src="docs/images/room_light.png" width="760" alt="room light: half, default, maximum"><br>
<em>Room light at half, at the default (an evening room) and at the maximum (daylight).</em>
</p>

**The scratches.** A real overlay is forty years old. Decades of being handled, wiped and stored have
left it slightly **matte**, criss-crossed by countless fine scratches - and, here and there, a longer,
deeper one. In the dark you hardly see them, until a vector lights them from behind: then the fine ones
make a soft matte halo around the line, and the scratches that happen to run *across* the light catch it
and glint, so around a bright spot they form a little ring of dashes, like a sun drawn by a child.

Getting that right took about thirty rounds of pictures, each compared with real sheets. The first
scratches were far too big - they looked like damage. Real wear is finer: a matte buff, millions of
scratches too small to see one by one, 5 to 35 mm long, in every direction, crossing every spot many
times over; it makes the plastic duller in its own colour, never whiter. On top of that sit fewer,
longer, deeper scratches - and those follow a rule: each step has about 4.6 times fewer, a little longer
and deeper, down to one single deep scratch. The recipe that matched best:

* 1 million fine scratches (5 - 35 mm; counted over MiSTer's whole frame, about 620,000 on the sheet itself), then nine steps of 215,000, 46,000, 10,000, 2,150, 465, 100, 22, 5
  and 1 scratch, each step's lengths spread from 0.4 to 2.5 times its typical length (up to 100 mm);
* their depth stays low for a long time and then rises fast, so only the last few really stand out;
* every one slightly bent - drawn by a grain of sand under the sheet, not by a machine - at a random
  angle, and more of them where hands hold a sheet (the middle, the lower half, the edges) than in the
  corners.

<p align="center">
<img src="docs/images/scratch_layers.png" width="820" alt="the scratch layers"><br>
<em>The scratch layers as the graphics card holds them, white = deeper: the fine wear, the nine longer
steps, both together (each a 5 x 5 cm patch), and the longer steps on the whole sheet - denser where the
sheet is held.</em>
</p>

The graphics card draws all of them - almost 900,000 lines - into two textures in about 50 ms, once,
when an overlay is loaded; per scratch it keeps its depth and its direction, so the shader can work out
the glint for any direction of light. After that they cost about a millisecond per frame. Each overlay
has its own pattern, as every real sheet is worn its own way, and **New random scratches** deals a new set.

<p align="center">
<img src="docs/images/scratches_onoff.png" width="640" alt="scratches off and on"><br>
<img src="docs/images/scratches_zoom.png" width="640" alt="scratches off and on, zoomed in"><br>
<em>The same moment without and with the scratches (on by default): the matte halo around the lines,
the glints around the bright spots.</em>
</p>

<p align="center">
<img src="docs/images/scratches_example_01.png" width="200" alt="the Vectrex start screen through a worn overlay">
<img src="docs/images/scratches_example_02.png" width="200" alt="Bedlam through a worn overlay">
<img src="docs/images/scratches_example_03.png" width="200" alt="Web Warp's start screen through a worn overlay">
<img src="docs/images/scratches_example_04.png" width="200" alt="Pole Position through a worn overlay"><br>
<em>Worn sheets in play: the start screen, Bedlam, Web Warp and Pole Position - the scratches light up
only around what is drawn.</em>
</p>

**Overlay > Scratches** switches them; **Wear** sets the number and depth of the fine ones, **Damage**
the larger ones, **Glint strength** how strongly they catch the light:

<p align="center">
<img src="docs/images/scratch_sliders.png" width="720" alt="the scratch sliders"><br>
<em>Mine Storm's ship and a mine (2x zoom) with Wear, Damage and Glint strength at 0, 1 (default) and 2.</em>
</p>

---

## What it looks like

<p align="center">
<img src="docs/images/games.png" width="820" alt="GCE games in Vectrex_EMU"><br>
<em>Original GCE games in Vectrex_EMU.</em>
</p>

Below the picture sits the Vectrex's own control panel, drawn in green outlines: the **on-screen display**
(the game's frame rate, the monitor's, the emulation speed, the volume), a **cartridge** you click to
open the **ROM browser**, the **RESET** button and the **volume knob**, which - as on the Vectrex - is
also the power switch.

<p align="center">
<img src="docs/images/panel.png" width="820" alt="the on-screen panel"><br>
<em>The panel below the picture: on-screen display, cartridge, RESET and the volume knob.</em>
</p>

<p align="center">
<img src="docs/images/browser.png" width="400" alt="the ROM browser"><br>
<em>Click the cartridge: the ROM browser, drawn into the picture (it works in full screen too).</em>
</p>

## Features

* Cycle-exact 6809 bus timing, the 6522 as measured, Malban's analog delays (as measured for VIDE)
* Real-time phosphor with the measured afterglow, bloom, glow, curved glass, mains wobble, drift, bending
* The buzz, the built-in speaker, the power switch and a worn volume knob
* Fixed 60 Hz (tear-free on any monitor), the monitor's full rate (120 / 144 / 240 Hz), or G-Sync /
  FreeSync; always at the monitor's full resolution (4K, Retina)
* Two players, keyboard and any controllers at once (Xbox / PlayStation / Switch pads, the Vectrex USB
  controller, flight sticks), mapped per controller with dead zones and endpoints
* Bank-switched cartridges: 64K (PB6), Malban's Vectorblade cartridge with its flash save (as on MiSTer),
  a 512K flash cartridge with a bank latch, 1 MB multi-carts (as Sean Kelly's Multi-Cart)
* Sampled sound and speech through the Vectrex's DAC, and the VecVoice / VecVox speech accessories
* Overlays (MiSTer `.art` and PNG), flat over the curved tube, lit by the room, with forty years of
  scratches made on the graphics card
* Real size (as big as a Vectrex on any monitor), a start size that fits the desktop, the whole screen
  turned in 90 degree steps for a monitor on its side
* Every look and sound setting adjustable and explained in the built-in Help

---

## Getting started

* **Mac** (Intel and Apple Silicon): download `Vectrex_EMU-mac-app.zip` from [Releases](../../releases),
  unzip it and open `Vectrex_EMU.app` (the first start needs one extra click, see [Mac and Linux](#mac-and-linux)).
* **Linux** (x86-64): download `Vectrex_EMU-linux-bin.zip` from [Releases](../../releases), unzip it and
  start `./Vectrex_EMU`.
* **Windows**: download this project (Code > Download ZIP), unzip it and double-click `build_exe.bat` -
  it builds `Vectrex_EMU.exe` (see [Building](#building)).

Then put your cartridge ROMs (`.bin`) in a folder called `roms` next to the program, and turn the volume
knob up to switch the Vectrex on. Without a cartridge it plays its built-in game, Mine Storm.

**On Windows** `Vectrex_EMU.exe` is all you need: the emulator core, the BIOS (with Mine Storm) and the
power-on/off sounds are inside it. Copy it anywhere; next to it only a `Vectrex_EMU.conf` appears.
`Vectrex_EMU.exe GAME.bin` starts with a cartridge, L opens the ROM browser; it starts again with the cartridge
used last (if that file is gone: Mine Storm).

## Mac and Linux

The same emulator runs on macOS and Linux. Everything in this README applies; only the start differs.
On both, the BIOS is not built into the program: it sits in the **`bios` folder next to it**
(`bios/bios.bin`, in the zip). The `roms` folder and `Vectrex_EMU.conf` sit next to the program,
as on Windows; if that folder can not be written to, the settings go to `~/Library/Application Support/Vectrex_EMU`
(Mac) or `~/.config/Vectrex_EMU` (Linux).

**Mac** (Intel and Apple Silicon, macOS 10.13 or later): unzip and keep `Vectrex_EMU.app`, `bios` and
`roms` together in one folder. The app is not signed by Apple, so the
first start is blocked. Right-click (or Ctrl+click) the app > **Open** > **Open**. On macOS 15 or later:
double-click it once, then **System Settings > Privacy & Security > Open Anyway**. After that it starts normally.
Or in Terminal, once: `xattr -dr com.apple.quarantine Vectrex_EMU.app`. (Until then macOS may run a downloaded app from a hidden copy that can not see the `bios` and
`roms` folders; the app finds its real folder by itself, and if it ever says the BIOS is missing: drag
`Vectrex_EMU.app` out of its folder in Finder and back in.) The Ctrl shortcuts work with Command as well:
⌘E eject, ⌘R RESET, ⌃⌘F full screen (also fn+F11); L, O, P and + / − / 0 / 9 need no modifier,
⌘Q quits (settings saved); Page Up / Down are fn+↑ / fn+↓ on a MacBook. The right mouse button is
Ctrl+click on a one-button mouse or trackpad. On a Retina screen the picture is drawn at the screen's full resolution (its pixels, not
macOS's points: twice as sharp), found by itself; it follows the window to another screen. Display > Screen at the full rate uses the monitor's rate
(120 Hz ProMotion); the fixed 60 Hz is paced by the clock there.

**Linux** (x86-64, glibc 2.35 or later: Ubuntu 22.04, Debian 12, Fedora 36 and later...): unzip and start `./Vectrex_EMU` (or double-click it). It needs an X11 or Wayland desktop
(through XWayland) with OpenGL (Mesa or a GPU driver) and PulseAudio or PipeWire for sound - every
normal desktop has them. If the unzipping tool dropped the program's run permission: `chmod +x Vectrex_EMU`.

Controllers work on both through SDL2: an Xbox, PlayStation or Switch pad shows up like the Xbox pad
on Windows; any other joystick (the Vectrex USB controller, flight sticks) by its own name, with
its axes and buttons, mapped in Vectrex > Controls as on Windows. Settings files move between all three
systems as they are.

## Switching on

Like a real Vectrex it **starts switched off**: turn the volume knob up (Page Up, the mouse wheel over
the knob below the picture, or a click on its right half) and it clicks on, the tube warms up and the
BIOS start screen plays. **Vectrex > Start switched on** skips the knob; **Vectrex > Skip the Vectrex
intro** runs the BIOS start screen (the VECTREX logo, its tune and the game's title, ~13 s) unseen in a
fraction of a second at every power on, so the cartridge starts at once - in the same state as after the
real intro (the core's live flag bit 4 with a power on: it runs until the CPU first executes code below
$F000). Both are saved.

## Help

**Help** in the menu bar opens a help window: every menu option explained, with **More** buttons that
unfold the history and the mechanics behind it (the phosphor, the mains wobble, the buzz, bending, the
analog drift), plus the Vectrex itself (CPU and cycles, RAM, the 6522, the sound chip and its limits),
cartridges, bank switching and saving. The text lives in `src\frontend\help_text.py`.

## Menu bar

Every option is in the menu bar (Vectrex, Display, Tube, Overlay, Sound, Help); options that are on have a tick,
the few that also have a key show it. Values (volume, bloom, afterglow length, wobble, bending, buzz)
open a small slider window: drag or click on the bar to set it, **right click for the default**, Esc
closes it. The defaults are the faithful values - measured on a real Vectrex or chosen side by side with
one - so each slider has its default **in the middle**: from 0 on the left to twice the default on the
right, for personal adjustments; it shows how far you are from the default. (The volume knob goes
0-100 %: 0 % is the power switch.) Each menu ends with its own **Reset ... settings to default**
(Vectrex, display, tube, overlay, sound): it resets only that menu's settings. In full screen there is no menu (with G-Sync Windows menus are hidden there): the keys
below, the knob and the cartridge work there.

**Volume knob on screen** (Display menu, default on): the Vectrex's own panel as simple green
outlines in the strip right below the picture, lined up with its right edge - in the window and in
full screen alike: the volume knob with its position marker, OFF/ON and VOLUME, and the RESET button.
Mouse wheel over the knob or a click on its right / left half turns it up / down (down to 0 % switches
the Vectrex off); a click on RESET presses it.

**Cartridge on screen** (Display menu, default on): a Vectrex cartridge below the middle of the
picture. Click it (or L): the ROM browser opens over the picture with all ROMs in the folder
**`roms` next to `Vectrex_EMU.exe`** (subfolders too; `.bin`, `.vec`, `.gam`, `.rom`; no folder or no
ROMs: the list is empty). The game runs on behind it but gets no input. Up / Down choose, Left / Right
a page, the mouse wheel or the arrow buttons scroll; Enter, a double click or the plug loads the chosen
ROM (a power cycle with it); Esc, the X or a click outside closes it. Drawn in the picture, so it works
in full screen with G-Sync too.

**Display > Real size (9):** as big as on a real Vectrex: the overlay sheet 162 x 207 mm, as the real ones (the picture as tall as the sheet), worked out from your
monitor's size and resolution. Windows usually knows the monitor's size (the monitor reports it); if
it does not, or a TV or projector reports it wrong, set **Display > Monitor size** (the diagonal in
inches; right click: as the monitor reports it). Check it with a ruler: an overlay should be 162 mm wide
and 207 mm high. On macOS and Linux set the monitor size first.

**Display > Screen:** fixed 60 Hz (default: smooth and tear-free on any monitor), the monitor's full
rate (vsync on every refresh - 120, 144, 165, 240 Hz...: the afterglow fades in finer steps and moving
things update more often; on a 60 Hz monitor the same as 60 Hz), or G-Sync / FreeSync (one screen
frame per game frame: no tearing, the wobble stands still at 50 fps; needs such a monitor). Always at
the monitor's own resolution (e.g. 4K).

**On-screen display** (Display menu, default on): INTERNAL (the game's frames per second), MONITOR
(the screen's refresh), EMU RATE (green 100 %, orange / red when the PC can not keep up), VOLUME, and
SPEECH VOL when a speech module is plugged in - below the picture on the left.

## Keys

Only what full screen needs has a key, so the rest of the keyboard is free for the players:

| Key | |
|---|---|
| F11 | full screen on/off |
| Esc | closes the ROM browser; in full screen: back to the window; in the window: quit |
| L | the ROM browser (the Windows file dialog: Vectrex > Open ROM file) |
| O | the overlay on / off |
| Ctrl+E | eject the cartridge (Mine Storm) |
| Ctrl+R | the RESET button (RAM kept: the BIOS warm-starts the cartridge, no intro) |
| P | pause |
| Page Up / Page Down | the volume knob in 5 % steps - and, as on the Vectrex, the power switch: turned down to 0 % it clicks off (the tube fades to black), turned up from 0 % it switches on (a cold start, also for carts that ignore RESET), with the real power-off / power-on sounds. While turning it scratches like a worn potentiometer; now and then it stops on a dead spot: the game goes silent, the buzz goes on - turn it again. |
| Ctrl+Page Up / Ctrl+Page Down | the speech module's own volume (VecVoice / VecVox) in 5 % steps, 0 - 200 % (100 % = the MiSTer core's Normal level) |
| + / - / 0 | window size / start size (as big as fits the desktop, above the task bar); in full screen: the picture size |
| 9 | real size: the picture as big as on a real Vectrex, in the window and in full screen (until + / - / 0) |

These keys (and everything with Ctrl) can not be given to a player: F11, Esc, Page Up / Down, + - 0 9, P L O.
Only Ctrl+E and Ctrl+R need Ctrl - they end the game.

(The mouse pointer hides in full screen 2 s after it last moved.)

## Controls

**Vectrex > Controls** (in the window): both players - the 1st and the 2nd controller port. Each
player picks a **controller** from the ones connected, listed by their own names (e.g. "Vectrex USB
Controller", "Logitech Extreme 3D"; Xbox-type XInput pads and other USB gamepads / joysticks), or
**Keyboard only**. A controller belongs to one player: the other player's list does not offer it.
A controller first chosen is mapped at once: its X / Y axes = the Vectrex stick, its buttons 1-4 = the
Vectrex buttons 1-4 (an Xbox pad: the left stick and X Y A B); a hat / d-pad is the stick too.

**The keyboard always works as well**, next to the controller: the buttons add up, and a direction
key overrides the stick on its own axis (holding the stick up while pressing Right = up-right; pressing
A while the stick is held = button 1 with the stick still held). No switching needed to try a game on the keyboard.

Click a box, then press a key / a controller button, or push the stick the way the row says (Up: push
up...: that picks the axis and its direction); Shift+click adds a second; right click clears (a stick
box: back to the default axis). The emulator's own keys (F11, Esc, Page Up / Down, Ctrl+...) can not be
taken. Each controller keeps its own mapping: switching controllers needs no new mapping.

**Stick:** per controller **analog stick** or **buttons**. Analog: a bar per axis (X, Y) shows the real
stick (ring) and what the Vectrex gets (fill); drag the inner handles for the **dead zone** and the outer
ones for the **endpoint** - the stick position that already gives full deflection, so a joystick with a
long travel does not need to be pushed all the way (right click: back to 20 % dead zone, full travel).
**Link X and Y** (on by default) moves both axes' handles together; untick it to set them apart.
Buttons: the Up / Down / Left / Right (and Half stick) rows take controller buttons instead of axes -
full deflection, half with the half button. A hat / d-pad works as the stick in both modes. The **test
field** per player shows the stick (ring), the dead zone (dark square), the endpoints (dashed), what the
Vectrex gets (dot, with the x / y values) and buttons 1-4 lighting up. Reset to defaults.

Defaults: player 1 - arrows, A W S D = buttons 1-4, Shift = half stick, and the first controller found.
Player 2 - keyboard only, no keys: pick a second controller, or keys of your own. A chosen controller
that is unplugged is shown as "not connected" (the keyboard still works) and is used again when it is
plugged back in. The keys only count while the emulator's window is in front.

**Settings** (window size, full-screen picture size, look, sound, volume, controls, the last ROM) are saved in
`Vectrex_EMU.conf` next to `Vectrex_EMU.exe`, so the folder is portable: copy it and the settings
come along. If that folder cannot be written (e.g. under Program Files) they go to
`%APPDATA%\Vectrex_EMU\Vectrex_EMU.conf`. Nothing else is written: no logs, no recordings.

## Cartridge hardware

**Vectrex > Cartridge hardware** (saved): **Auto** (the default) models the bank-switching cartridge
boards, chosen by the ROM's size; or pick one board for any ROM:

* **up to 64K** - PB6 drives the EPROM's A15 (3.3K pull-up: an input PB6 reads high, bank 1 at power-on).
* **over 64K, up to 256K** - Malban's Vectorblade cartridge (SST39SF020A flash): two bank lines, PB6 and
  the cartridge port's /IRQ (low while an enabled VIA interrupt flag is set), bank = PB6 + 2 x (/IRQ
  high), 64K per bank of which the cartridge shows $0000-$BFFF; power-on bank 3. The flash is modelled
  (program, sector / chip erase, ID): the game's high scores and settings are saved to `<rom>.sav` next
  to the ROM, the dump itself is never changed.
* **exactly 512K** - a 512K flash cartridge with a bank latch (Hugo Bokhorst's v2 board, SST39SF040):
  A15 = PB6, A16-A18 = a 74HC377 that a write to $0000-$3FFF loads with the data's bits 0-2 (clock =
  /CART OR A14), 16 banks of 32K. The latch has no reset: it powers up in any bank, the RESET button
  keeps it. Flash saves as above (`<rom>.sav`); the flash's commands load the latch with $55 on their
  $2AAA write, so a game saves in $4000-$7FFF of a bank with A16-A18 = 101.
* **other images over 256K** - Sean Kelly's Multi-Cart: a latch written at $7FFF picks the 4K page the cartridge window
  starts at; power-on page 0 (the menu); the RESET button keeps the page.

**Off**: a bone-stock Vectrex - every ROM is a plain 32K cartridge (the first 32K of the file, no bank
switching), so bank-switched games do not start. Changing it puts the cartridge in again (power cycle).

## Speech: VecVoice and VecVox

**Vectrex > Controller port 2** plugs a speech accessory into the second controller port instead of a
joystick. Games and demos made for them send bytes over a serial line on the sound chip's port A; the
accessory answers "ready" on port 2's button lines.

* **VecVoice** (Richard Hutchinson): a General Instrument SP0256-AL2. The synthesizer is a C port of
  MAME's SP0256 emulation by Joe Zbiciak and Tim Lindner; the link follows the MiSTer core (Dar,
  Videodr0me).
* **VecVox** (Alex Herbert): a Magnevation SpeakJet - its 72 phonemes and its codes for pitch (in Hz),
  speed, volume, pauses and repeats. How the SpeakJet makes its sounds is not known (a programmed chip,
  never read out), so here it speaks with the SP0256's voice: each phoneme as the nearest SP0256
  sound(s), stretched to the SpeakJet's length (its speed law fitted to a recording of a real one), at
  the SpeakJet's pitch - songs such as VecVox Xmas keep their tune and timing, in a different voice.

**The speech ROM is not included.** The SP0256-AL2's built-in ROM is (c) Microchip Technology Inc.
Joe Zbiciak distributes it with Microchip's permission - download it from his page:

1. Get `al2.bin` (2 KB) from [spatula-city.org/~im14u2c/sp0256-al2](http://spatula-city.org/~im14u2c/sp0256-al2/).
2. Put it in the **`bios` folder next to the emulator** as `sp0256-al2.bin` (`al2.bin` works too; so
   does MAME's `sp0256-al2.bin`, the same data with each byte's bits reversed).
3. Choose **Vectrex > Controller port 2 > VecVoice** or **VecVox**. The Vectrex is switched off and on.

Both accessories need this file (the VecVox too: it speaks with the SP0256's voice). Without it the
setting stays on Joystick. While an accessory is plugged in, port 2 has no joystick.

The modules have their own amplifier and audio out, and most owners plug better speakers into them than
the Vectrex's own. So their sound has its own volume and does not go through the Vectrex's knob, buzz
or speaker: **Sound > Speech module** - **Volume** (also Ctrl+Page Up / Down; the on-screen display
shows SPEECH VOL), **Full-range speakers** (big speakers with an arcade cabinet's warm bass: +6 dB below ~150 Hz) or
**Small speaker** (no deep bass, rounded treble - still better than the Vectrex's 1982 speaker).

**Port 2's buttons:** the modules answer "ready" on controller port 2's button 2 (low = ready, what
the speech programs wait for) - to the Vectrex that looks like player 2 holding button 2. A game that
waits for every button of both controllers to be released before it starts sees that button held, as
it would with a real module plugged in: **GCE's original Berzerk** does that and will not start -
choose Joystick for it. **Berzerk Ultimate** (Malban) starts and talks. Two-player games lose their
second player while a module is plugged in.

### Games that talk

Pick the device the game was written for: **VecVox** for SpeakJet games, **VecVoice** for SP0256 games
(a real VecVox or AtariVox+ has a VecVoice mode for those, chosen with a button held at power-up or a
DIP switch). With the wrong one the speech comes out garbled, as with the wrong real module.

| Game | Device | Tested here |
|---|---|---|
| Berzerk Ultimate (Malban) | VecVox | talks |
| Brecher (2015) | VecVox | |
| Color Clash | VecVox | |
| Debris (Revisited) | VecVox | |
| RushHour | VecVox | |
| Shifted | VecVox | |
| Space Frenzy (John Dondzila, 2006) | VecVox | talks |
| Spike's Circus | VecVox | |
| VecFahren (Der Luchs, 2013) | VecVox | talks |
| Vectoblox | VecVox | |
| Vectorblade (Malban) | VecVox | talks ("level one") |
| V:Hockey | VecVox | |
| VecVox X-Mas (2005) | VecVox | sings |
| VecTalk (Binary Star, 2018), VecVox Demo, VecVox Driver Demo | VecVox | talk |
| B.E.T.H. (Manu) | VecVoice | talks |
| Deathchase | VecVoice | |
| NOX | VecVoice | |
| Pythagorean Theorem (Rob Mitchell, 2002) | VecVoice | |
| Verzerk (Alex Herbert, 2002) - a talking Berzerk | VecVoice | talks |
| Y.A.S.I. (John Dondzila; on "Protector & YASI") | VecVoice | |
| VecVoice Demo (Richard Hutchinson, 2002) | VecVoice | talks |

From the AtariVox+ list ([AtariAge](https://atariage.com/store/index.php?l=product_detail&p=1045),
[review](https://www.atari7800forever.com/autovox.html)), [vectrex.co.uk](http://www.vectrex.co.uk/news-2009.htm)
and [Revival Studios](https://www.revival-studios.com/vectrex.php); the lists date from 2009-2015 and
partly disagree (Space Frenzy is filed under VecVoice in places - its speech is SpeakJet codes). Malban's
two games are on none of them; newer games may be missing too.

## Overlays

On a real Vectrex every game came with a printed plastic sheet in front of the screen: the coloured,
see-through parts tint the vectors behind them, the solid print gives borders, pictures and the game's
name. The emulator shows overlays in the **MiSTer core's `.art` format** ([Vectrex_MiSTer](https://github.com/Videodr0me/Vectrex_MiSTer):
its `overlays.zip`, and its Overlay Tools make them from an 846 x 1080 PNG) and as **PNG** files:

* put the `.art` or `.png` files in **`roms/overlays`** (or `overlays` next to the program), named like
  the ROM: `Armor Attack.bin` finds `Armor Attack.art`. Without an exact match it takes the same name
  without its `(...)` parts and version number, so hacks and versions find the game's overlay
  (`Berzerk (Hack - Speed, 2016).bin` finds `Berzerk.art`, `Thrust v1.2.bin` finds `Thrust.art`). Other
  overlays for a game are named with a tag, `Spike (red).art`: pick them with **Choose overlay...**. The
  ROM browser does not list the overlays folder.
* **PNG**: an image with transparency (8-bit RGBA, like the input of MiSTer's Overlay Tools), the sheet
  846 x 1080 (or the same proportions), or the whole 1360 x 1080 MiSTer frame with the sheet in its
  middle. Alpha 255 is solid print, 1 - 254 coloured see-through plastic (MiSTer's tools use 51), 0 clear.
  A PNG without transparency is read as see-through plastic, black as clear.
* only the normal packages are needed: the `_90CW` / `_90CCW` ones (in MiSTer's `Tate` folder) are the
  same artwork turned for MiSTer's tate mode, which turns only the game picture. The emulator turns the
  whole screen, overlay included (**Display > Rotate screen**, for a monitor on its side).
* **Overlay** menu: on / off (O), choose automatically or pick one (remembered per game), tint
  strength, room light, printed art brightness, parallax, scratches, and per overlay a fine position / size.
* **Room light** lights the whole sheet, plastic and print: low for an evening (the plastic almost black,
  only the vectors), high for daylight. The plastic's light rises steeper than the setting (half = a
  quarter, the right end four times the default), the print more gently (half = half as bright, the
  right end twice); Printed art brightness sets the print against the plastic.
* the overlay is flat, like the sheet: the curved tube bends the vectors behind it, not the overlay.
* **Scratches** (on by default): a 40-year-old sheet is worn. Millions of fine scratches make it slightly
  matte; fewer, longer and deeper ones (about 4.6x fewer per step, up to 100 mm, slightly bent, more
  where hands hold the sheet) run across it, down to a single deep one. In the dark they show only where
  the vectors light them: a soft matte halo around the lines, and the scratches that run across the light
  glint - little rings around bright spots, as on a real sheet. In room light the whole sheet looks more
  matte, in its own colours (not whiter). **Wear** sets the fine ones, **Damage** the larger ones
  (number and depth), **Glint strength** how strongly they catch the light, **New random scratches** another
  pattern (each overlay its own variation). They are made on the graphics card in a fraction of a second
  and cost about a millisecond per frame.

One overlay comes with the emulator: **Mine Storm's**, for the game built into the console
(`bios/bios.art`, also packed into the exe). It is shown when no cartridge is in, unless your overlays
folder has one of its own (`bios`, `Mine Storm (built-in game)` or `Mine Storm`). It is the
[Libretro overlay-borders](https://github.com/libretro/overlay-borders) Mine Storm artwork as converted
to `.art` by the [Vectrex_MiSTer](https://github.com/Videodr0me/Vectrex_MiSTer) project (its
`overlays.zip`, `Mine_Storm.art`; see its `CREDITS.md`).

*Disclaimer: the overlay artwork is not part of this project and not covered by its licence; it belongs
to its creators and is included unchanged as a courtesy, for non-commercial use. Vectrex is a trademark
of its owners; this project is not affiliated with them, Libretro or MiSTer. On request it will be
removed. Other overlays do not come with the emulator: the artwork has several authors and terms - get
them from MiSTer's `overlays.zip` (see its `CREDITS.md`).*

## What it models (in detail)

* **Analog delays** (as Malban measured them for VIDE): the X integrator gets a new DAC value 15 cycles
  late, Y its sample 14, the integrators start 12 / stop 12.3 cycles after RAMP switches (Malban: 15), timer 1 runs N+2 cycles (6522 data sheet: N+1.5), the beam zeroes
  5 cycles after /ZERO. Cycle-timed code (Vectorblade's sprites and star field) needs them, and they put
  the BIOS raster text where photos of a real Vectrex show it. `VT_ANALOGDLY=0` switches them off;
  `VT_RAMPDLY=on,off` and `VT_XYZDLY=x,y,zero` change single delays.
* **Bus timing**: a 6809 reads and writes the VIA in the instruction's last cycle. The core runs each
  instruction once as a dry run to learn its length, runs the VIA and the analog side up to its last
  cycle, then executes it. With Malban's delays this gives the dark line ends of a real Vectrex (photos
  of a real Vectrex: Mine Storm's mines open at their inner corners, the ship's lines ~20 % short).
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
* **Afterglow length**: the measured decay (see the phosphor section) runs at x0.6 by default (Tube >
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
  real Vectrex: green 100 %; orange below 99 % (the PC can not keep up: the game runs
  slow, the sound breaks up); red below 90 %.
* **Resolution:** always the monitor's real pixels (Windows display scaling such as 150 % does not
  matter): full screen renders at the desktop resolution (e.g. 3840 x 2160), the window at its own
  size.

## Building

download the project (GitHub: Code > Download ZIP), unzip, double-click
`build_exe.bat`: it builds the core (`build_core.bat`, needs Visual Studio Build Tools with C++), installs
PyInstaller if needed and makes `Vectrex_EMU.exe` (needs Python 3 from python.org). `run_from_source.bat`
runs the emulator straight from the sources (after `build_core.bat`). The BIOS (`bios/bios.bin`, with Mine
Storm) is in the project: Smith Engineering allows it to be copied freely for non-commercial use.

**macOS and Linux:** `build_mac.sh` (on a Mac, the python.org Python 3.12) and `build_linux.sh`; the GitHub
workflow `.github/workflows/build.yml` builds both, and on each it checks the OpenGL picture with
`tools/snapshot_test.py`. The platform side (OpenGL context, keys, controllers, sound) is
`src/frontend/osdep.py`; on Windows the program uses the Windows APIs as before.

## Folder

| Path | |
|---|---|
| `Vectrex_EMU.exe` | the emulator, self-contained (front end, core, BIOS and sounds packed with PyInstaller) |
| `core\vectrex_core.exe` | the emulator core (vecx, GPL), packed into the exe when it is built |
| `bios\bios.bin` | the Vectrex BIOS (with Mine Storm) |
| `bios\bios.art` | Mine Storm's overlay (see Overlays) |
| `bios\sp0256-al2.bin` | the VecVoice's speech ROM - not included: you download it (see VecVoice) |
| `roms\` | your cartridges: the ROM browser lists them (the cartridge on screen, L) |
| `roms\overlays\` | your overlays (`.art`, `.png`), named like the ROMs |
| `sounds\` | the power-on / power-off sounds, recorded on a real Vectrex |
| `src\core\` | core sources (C): `vecx.c` CPU bus + analog model, `e6809.c` CPU, `e8910.c`/`psg.c` AY sound, `live.c` the frame pipe, `vtest.c` main (also a scripted test harness), `ds2431.c` (a 1-Wire save chip), `sp0256.c` / `speakjet.c` (the speech accessories), `bot.c` (a test player) |
| `src\frontend\` | `vectrex_emu.py` window, input, sound, settings; `vecrender.py` OpenGL beam renderer |
| `build_core.bat` | rebuild the core with Visual C++ 2019 or newer |
| `build_exe.bat` | rebuild the exe with PyInstaller (`python -m pip install pyinstaller`); packs `core\`, `bios\` and `sounds\` into it |
| `run_from_source.bat` | run from the Python sources (Python 3.12 with tkinter) |

The core is the vecx emulator (GPL, see `src\core\LICENSE.md`). The SP0256 synthesizer in
`src\core\sp0256.c` is ported from MAME's sp0256.cpp (BSD-3-Clause, Joseph Zbiciak and Tim Lindner).
The SP0256-AL2 ROM is (c) Microchip Technology Inc., distributed by Joe Zbiciak with Microchip's
permission; it is not part of this project. Several details (the DAC sound mix, the power-on seed, the
VecVoice link, the speech level with Sorgelig's audio compressor table, GPL-2.0-or-later) follow the MiSTer
Vectrex core (Dar, Sorgelig, alanswx, Videodr0me).

Vectrex_EMU by Hugo Bokhorst, developed with Claude (Anthropic) as development assistant and programmer.
