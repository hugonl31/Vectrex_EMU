# Vectrex_EMU release notes

The newest version is at the top. `VERSION` holds the version every push to GitHub publishes: the
build workflow replaces that release (title = the version, marked Latest) with the new build.

## v1.0.8

**The game room: find someone to play with** (2026-10-28)
- **Online > Host public game...**: your game goes into the meeting point's game room under your gamer name (up to 12
  of A-Z, 0-9, _ and -, kept for next time). Anyone can see it and join as player 2. A name someone in the room already
  uses is refused ("That name is in use. Pick another."). The listing ends when someone joins.
- **Online > Find players...**: the public games waiting, with the gamer, the game, how long they have waited and
  whether you have that game (by its contents, any file name). Join starts it as a code would.
- Host this game stays the private way: a five-letter code only the player you tell can use.
- The meeting point keeps the room small and fast: at most 100 public games waiting, 400 codes and 100 games joined at
  once (a full room says so); a full room costs it well under 1 % of one processor core. The list is asked at most a few
  times a second per PC.
- Tested between Windows, a Linux mini PC and a Raspberry Pi 5 through the meeting point, direct and relayed, with a
  sync check every frame: Armor Attack and PiTrex Space Duel identical on both sides.

**Smooth by default**
- Reset all (and a new installation) gives the monitor's full rate with Smooth motion on: the same on a 60 Hz screen,
  smoother on 120 / 144 / 165 / 240 Hz - where a fixed 60 can only be paced by the clock. The first-time setup keeps
  Smooth motion on with the full rate too.

**Get games & extras**
- The first-time setup's "Download everything missing" opens the window with what is missing, and waits for Download
  all; nothing starts by itself.
- The Site column names where each set comes from (archive.org; archive.org and GitHub; vide.malban.de;
  spatula-city.org) - the downloads come from the project's own copies first.

## v1.0.7

**The Vectrex's trim pots** (2026-10-28)
- Every adjustment of a real Vectrex, from the service manual and the boards' parts lists, now in Config:
  - **Unit** tab (what an owner turns): **R509** the brightness knob at the back, **R401 / R408** the X and Y size
    (60-140 %), **R525** the high voltage (4.6-7.0 kV; higher: a smaller, brighter, sharper picture), the yoke's
    **centering magnets** and the **yoke rotation**.
  - **Circuit** tab (the service trim pots): **R302** the DAC offset on the logic board, which moves the picture on the
    diagonal, changes the dimmest lines and makes the joystick read a little off; **R335 / R333** the X / Y
    integrators' rate offset (the old Drift rows, now with the real range of +-0.585 DAC steps), which bends long
    lines; **R526** the high voltage's ringing (off its minimum the voltage drops by up to 8 %); and the existing zero
    reference, beam and DAC models.
- A real Vectrex has no focus or position pot: the picture is centred with the yoke's ring magnets. The defaults are every pot in
  its factory position (100 % size, 5.8 kV, R302 as the manual sets it), and there the core is bit-identical to v1.0.6
  (lines and state, tested on the Test Cartridge, Mine Storm, Armor Attack and HyperChase).
- **Random unit** and **Cranky** make a new unit on every press: Random is slightly off in every part, as real units
  differ; Cranky is a tired one. Both stay readable and playable. The brightness knob stays where you set it.

**Online play: fixes from tests between Windows, Linux and a Raspberry Pi 5**
- A Windows PC and a Linux or Mac computer went out of sync in about a quarter of all online games, plain cartridges
  too: the power-on seed was computed with 64-bit arithmetic on Linux and macOS. Now the same on every system
  (Windows exactly as before).
- PiTrex games through the meeting point's relay could not start: the SD card's file list arrived cut short. It is now
  sent in smaller pieces, a damaged list is collected again, and the relay passes longer packets (so v1.0.6 hosts work
  through it too).
- R302 moves the joystick comparator, so online both PCs use the host's R302 (it cannot be changed during an online
  game). The 512K cartridge's latch also powers up the same on both sides.
- An online UVMC2 / PiTrex game's card image (about 300 MB in the cache folder) is now deleted when the game ends.
- A Raspberry Pi starts at the Low quality profile the first time (at Ultra its graphics driver draws about 300 ms a
  picture and crashed now and then, which also ended online games).
- Tested this PC (Windows) against a Linux mini PC and a Raspberry Pi 5, direct on the LAN and relayed through the
  meeting point, with a sync check every frame: VecDoom, Veccy Road, Space Duel, Tempest, Vectorblade and Armor Attack
  all identical. On a Raspberry Pi 5 the PiTrex games run online; the UVMC2 games are too heavy for it on one thread.

## v1.0.6

**Online play for UVMC2 games and PiTrex programs** (2026-10-28)
- VecDoom, Veccy Road, Tempest, Space Duel, Vectorblade and the rest of the UVMC2 and PiTrex library can now be played
  online, like a plain cartridge: Online > Host this game... on the host, the code on player 2's side.
- The SD card: the host sends its card's file list (name, size, checksum) and player 2 answers which files it has the
  same. Both play from those files only, from an identical card image built on each PC (fixed file dates, nothing is
  copied between the PCs and nothing is written back to the card folder). When some files differ the chat says how
  many of the host's files both have.
- Identical to the bit on both PCs: while online the UVMC2's two cores run on one PC thread and the PiTrex's JIT in its
  exact mode (the threaded and fast modes can differ by a few instructions between two PCs); the PC's keyboard and
  mouse do not reach the cartridge. Measured on a desktop Ryzen 7 9800X3D: VecDoom 1.4x real time, Veccy Road 2.0x, the PiTrex
  games 3.6 to 6.6x - a much slower PC may not keep VecDoom at full speed online.
- A sync check for every game: twice a second both sides compare a checksum of the emulated machine (the Vectrex's
  RAM and cycle count, the UVMC2's or PiTrex's processor state). Should the two games ever part, ONLINE: OUT OF SYNC
  shows and the chat says at which frame. Tested two PCs on one machine with a check every frame: Space Duel (PiTrex)
  1017 checks, Veccy Road (UVMC2) 1486, Armor Attack 391 - all the same.
- Both players need v1.0.6 for UVMC2 and PiTrex games online (with v1.0.5 the host says the SD cards could not be
  compared); plain cartridges still play between v1.0.5 and v1.0.6.

## v1.0.5

**The real size of everything** (update, 2026-10-28)
- The front of a real Vectrex traced at three times real size (the matte frame, the edge of the glass, the Test
  Cartridge's grid, each line) and the emulator rebuilt from it. The overlay is the anchor: 162 x 207 mm, lying
  exactly on the case's matte frame.
- The frame: 162 x 207 mm outside with round corners (8.6 mm), 3.5 mm of matte; the opening 155 x 200 mm. The glass:
  the traced outline, 145.7 x 188.5 mm. The glossy walls keep their angle; the glass's reflections, the phosphor
  layer behind it and its soft edge keep their look.
- The picture: the emulator's grid warped onto the traced one (a smooth fit plus a mesh through every crossing),
  measured on the finished picture with brightness bending on: every line within 0.05 mm. Kept as a small table -
  as cheap as before.
- The drawing area is 1000 units wider on each side (35000): the grid's outer columns show, as on the tube. And the
  beam is drawn 6000 units past it on every side: with the measured glass the drawing area ended about 6 mm inside
  the glass's edge, so what a game drew there was missing; now it shows, as on a real Vectrex, up to the edge.
- Overlays lie exactly on the frame, 162 x 207 mm. Their position and size sliders move and size the picture behind
  the sheet instead; with the overlay off the picture is as measured. Earlier adjustments are reset. Parallax
  moves the picture (not the sheet) and is on in every quality.
- The black round the case is the same on every side and down to the on-screen display and the knob.

**Online play**
- Two PCs, one game: Online > Host this game... shows a five-letter code, Online > Join a game... takes it. A
  meeting-point server puts the PCs in touch directly (through most routers) or relays the packets; the input delay
  is set from the measured round trip. Also from the command line (`--net-online`, `--net-join CODE`; `--net-host`
  for a direct connection). The Online window shows the connection.
- Chat while you play (Online > Chat, Ctrl+T): Enter sends, Esc gives the keyboard back to the game; the window opens
  by itself when the other player writes, and their line shows in the picture for a moment (full screen too). Lines
  are resent until they arrive and shown in order.

**The Config window**
- Compact for 1080p laptops: short labels with an *i* for the details, two columns only when a tab is too tall;
  fits at 100, 125 and 150 %. The first-start setup is smaller too.
- Tabs with changed settings show their name in orange; the Quality row says what changed from (Low, Medium, High,
  Ultra or Optimal); hold H to compare with before. Reset this tab and Reset all reset everything, Start turned
  on included.
- Tabs switch at once, without white flashes or half-drawn controls; the window keeps the Display tab's width.
- Every window opens where it was last time (Online too).
- Full screen: the menu bar at the top edge of the screen or with Alt; the Config window and the other windows
  stay over the picture, also when full screen is switched while they are open.

**Picture and quality**
- Low quality for a Raspberry Pi 5 and old laptops: the phosphor at half size, 30 pictures a second; Medium at
  three quarters. Display > Phosphor size: Full, 3/4, Half. The bends' smoothing stays on in every quality.
- CPU and GPU meters below the on-screen display.
- White balance: 12000 K (the real Vectrex's cold white) is the default; the slider goes to 16000 K and to a warmer
  white past neutral.
- VecVox and VecVoice swapped their pictograms.

**Port 2**
- With Auto-select port 2 on, an accessory chosen by hand is kept for that game only; the 3D Imager and the light
  pen are never chosen for all games.

**Faster**
- The 6809 emulation about a third faster (bit-identical on 68 games); the PiTrex JIT faster (its floating point in
  the JIT) and now booting on Linux and macOS; ARM64 too.
- The display thread no longer spins while waiting for the screen; the sound filters and the line conversion
  cheaper.

**Fixes**
- Vectorblade (and others) no longer slow to a crawl after a game change.
- No false 999 % CPU or speed dips right after a game change; no false JIT note when starting VecDoom.
- A stopped core shows its exit code; garbage cartridges no longer hang the core.

**The 3D Imager**
- GCE's goggles on controller port 2 (click the P2 PORT pictogram, or Controls > Port 2): the disc's motor driven
  by the game through the AY's port A, spinning up and coasting as a motor with a flywheel does; the index hole
  pulses port 2's button 4 and the VIA's CA1 once a turn; each line coloured by the filter in front of the eye at
  that moment. 3D Mine Storm, 3D Narrow Escape and 3D Crazy Coaster in colour (both eyes together, no 3D yet).
- Smooth motion keeps the colours.
- The motor heard, on a channel of its own (not through the Vectrex's knob); Config > Sound and Config > Cartridge:
  on / off and its volume. Remade from recordings of real goggles (2026-10-28): not a whine but a soft rush
  (strongest at 500-1250 Hz) swelling once every turn of the disc, at the disc's own speed, about 23 dB under the
  game's sound.
- The game waits in the dark while the disc spins up: a note on the picture shows the disc's speed (can be
  switched off).

**Pausing**
- PAUSED is shown in the strip below the picture (large, orange), not on it: a paused game makes a clean
  screenshot. Without the strip it stays in the picture.

**The light pen**
- The mouse is the pen: it fires when the emulated beam's lit spot passes under it, once per pass (so Art Master's
  tracking circle is seen dot by dot and its cursor follows the pen). Over the picture the pen is held to the
  screen; the mouse buttons are player 1's buttons 1, 2 and 3 (Art Master: draw, straight line, menu).
- The pointer turns into a small green pen while the light pen is in port 2.
- Art Master, AnimAction and Melody Master run; AnimAction's cartridge RAM (8 KB at $2000) is found by its title.

## v1.0.4

**The beam, simulated**
- The spot itself follows the analog circuits: it runs about 6 cycles behind the integrators (the deflection's
  lag), which gives the gaps where lines join - growing with the scale as on the tube - and rounds fast corners.
- Rests by the spot's own speed: where the beam stops or slows, inside a stroke too, it lights a dot - a slight
  thickening at 12 cycles, a dot from 16 on, a blooming star for a long rest.
- Curves drawn smooth: a curve made of short lines bends as the spot does, without facets.
- Tube > Beam trace: the phosphor drawn from the spot's own path, the energy it leaves moving and resting.
- Fitted to new test cartridges photographed on a real Vectrex (in `testcart`): lag, corners, axes, gaps, the
  beam's ZERO and offset, and the DAC. The DAC turned out linear; lines meet the references placed by moves, as
  on the tube. Clean Sweep's maze lands within about a pixel of the real one.
- The Vectrex unit gained the ZERO circuit's timing, the offset, the lag and the DAC's bit errors (Config >
  Vectrex unit, in the presets too).
- The brightness knob at the back (Config > Vectrex unit): the tube's bias on the intensity drive, measured from
  black to a real Vectrex's brightest. Turned up, the blanked beam glows - the moves and returns to the middle
  become a faint "spider web".
- White balance: the slider's middle is neutral; towards cool or warm the glow takes the tint first, while the
  lines' bright cores stay the screen's full-bright white - they tint too only at the slider's far ends.

**The PiTrex**
- The Raspberry Pi cartridge emulated: the Pi Zero's ARM11 with a JIT compiler (x86-64 and ARM64), its timers,
  interrupts, the bus to the Vectrex and the SD card (a folder, `pitrex`). Its menu, the arcade emulators
  (Tempest, Battlezone, Black Widow, Asteroids, Space Duel, the Cinematronics games...), Vectorblade, Elite,
  GS BASIC and its own Vectrex emulators run.
- Vectrex > PiTrex: Start the PiTrex, Open PiTrex program..., SD card folder... The ROM browser lists it at the
  top; PiTrex programs are tinted red, UVMC2 games blue. Programs built for the Pi Zero 2 run their Pi Zero 1
  build.
- Arcade games need your own ROMs (MAME sets, non-merged or split, in `pitrex/roms`; AAE ones in
  `pitrex/roms/aae`).
- A USB keyboard and mouse on the PiTrex: the Pi's own USB controller emulated for the USPi library its programs
  use - Elite and GS BASIC find their keyboard. Ctrl+K types on it.
- Config > Cartridge: a PiTrex section (what is in the slot, start it, open a program, its SD card folder) and the USB
  keyboard and mouse setting of its own, for the UVMC2 and the PiTrex.
- `cache/core_log.txt`: the emulator core's messages of the last start, to send along when something is wrong (for
  the PiTrex it says whether its JIT runs; the window says so too when it cannot).

**Quality: profiles and Optimize**
- Low, Medium, High - and Custom, as in games: changing any picture setting under a profile makes it Custom;
  choosing a profile sets every picture setting to it.
- Optimize measures this PC once with a heavy scene, turns off only what it must for the full emulation speed
  (the costliest first, nothing that saves less than 2 %), says what it changed, and Undo puts it back. A new
  install runs it in the setup.
- Faster on weak PCs: the lines are drawn from display lists, the work measured on the graphics card itself.

**Get games & extras**
- The setup shows in red what is missing and gets it all in one click: the original GCE games, free homebrew
  games with overlays, the speech ROM and the PiTrex's SD card - from a chain of sources, newest first for the
  PiTrex; named as the ROM browser lists them, never overwriting what you have. A disclaimer is shown once.
- 65 overlays come with the emulator, named like the games (`overlays/CREDITS.md`: each design's author).

**Config and the strip below the picture**
- Reset all: every strip back to the chosen Quality profile (from Custom: High). The window's close button is
  back. Settings changed from their default are marked with a dot.
- Everything clickable below the picture shows a hand; a click on the on-screen display opens Config.
- The volume knob turns the moment the mouse moves; with the intro skipped, the power-on click and the knob's
  scratch play at once, and the picture keeps moving (G-Sync too).
- Recent games (the last ten, a clock in the ROM browser); a right click on the cartridge pulls it out of the
  slot (Ctrl+E), another puts it back.
- Messages appear in the middle of the picture.
- Fixed: the window could freeze while a Config slider was dragged (the brightness knob most often); the game
  went on underneath.
- Fixed: the setup, Config, Controls and Help windows ran off the bottom of smaller screens (1080p at 125 / 150 %);
  their middle now scrolls when needed, the buttons always in view.
- A PC too slow for the effects (over 25 ms a picture, weak graphics with every effect on) is pointed out once -
  Quality > Optimize - and `cache/render_log.txt` records the graphics card's time per picture, to send along.

## v1.0.3

**The picture**
- Tube geometry refitted to two real Vectrexes' grids ([issue #4](https://github.com/hugonl31/Vectrex_EMU/issues/4)):
  the outer columns are no longer pinched - they narrow gradually across the whole width, 85 % of the middle
  ones as on the real tubes (was 77 %, and more on the left than the right). The grid keeps its size and centre.
- The tube's glass traced from a real Vectrex: its rounded corners exactly as on the tube, the overlay centred on
  the glass, the picture placed so the overlays line up (Solar Quest's sun on its star).
- The plastic case round the tube, when there is no overlay: glossy black walls that mirror the picture - softly,
  a little warped, as moulded plastic does - fine scratches, and 5 mm of the case's matte front with the band
  where overlays were slid in. The room light and the ceiling light light it. View > Case (C), and the new
  Config > Case strip: Reflections and Wear.
- The screen is drawn in 16-bit colour and dithered to the monitor: the room light on overlays, the glass and the
  case falls off smoothly, without bands.

**Menus and windows: dark**
- Dark, like VS Code: the title bars, the menu bar, the drop-down menus, the Config window, Controls, the setup,
  Help, the messages, the ROM browser and Windows' own file dialogs.
- The menu bar is Vectrex, View, Controls, Config and Help. Alt (or F10) opens the menus, Alt + the underlined
  letter one of them; hover over an item for a short explanation of what it does.
- Vectrex: Recent games (the last ten, also games dropped onto the window). View: Fit screen, Authentic (as big as a real Vectrex), Choose
  overlay, Case, Control deck, Quality and Room light (Dark, Evening, Daylight). Controls: Keyboard and
  controllers, Mouse steers player 1, TV mode shortcuts, Port 2 (Joystick, VecVoice, VecVox). Config opens the
  Config window (Tab), the window that was called the control deck.
- The control deck is now the strip below the picture: the on-screen display, P2 PORT (a controller seen from
  above, the VecVox's speaker box or the VecVoice's speaking head - click it for the next one), the cartridge,
  RESET and the volume knob (with grip dots and a pointer). View > Control deck switches it all on or off at
  once; off, the window gets smaller. The window is cut evenly round the case and the strip.
- The Config window: settings in labelled boxes, what depends on a switch indented under it; it opens where you
  left it, on the strip you had open. Quality is Auto, Low, Medium or High (Ultra's full refresh rate is
  Config > Display's "Monitor's full rate").
- The Controls window and Help open where you left them; the ROM browser opens in the folder you were in, on the
  same game, scrolled as it was.

**Folders**
- Next to the program: `roms`, `overlays`, `system` (the BIOS, the speech ROM and the settings, now
  `settings.json`), `uvmc2` (the UVMC2's SD card, was `sd`) and `cache` (working files). Each has a note on
  what goes in it. An older folder layout is moved over by itself at the first start.
- A new icon: a little Vectrex.

**Bad Apple included**
- The Bad Apple vector movie comes with the emulator, in `uvmc2/movies`: load "UVMC2 Movie Player.um2" and it
  plays. By Malban (from his PiTrex SD card), bundled with his kind permission.

**Help**
- The help pages follow the new menus: Menu: Vectrex, Menu: View and Menu: Controls, then the Config window's
  strips.

## v1.0.2

**UVMC2 games** ([README](README.md#uvmc2-games-the-ultimate-vectrex-multicart-2))
- The Ultimate Vectrex Multicart 2 emulated: its RP2350 (two ARM Cortex-M33 cores, PIO, DMA, timers, PSRAM)
  runs `.um2` games that drive the Vectrex's bus themselves. Plug and play: `.um2` files in `roms` appear in
  the ROM browser; Vectrex > Open UVMC2 game... opens one from anywhere.
- The SD card is a folder, `sd` next to the program (made at the first start, with a note). A game that
  needs a file of its own (Doom's WAD, Quake's `pak0.pak`) and lacks it says which file and where, and opens
  the folder. Control deck > Cartridge: Open the SD folder, Choose SD folder...
- The headphone jack (clean, through the Vectrex's speaker, or off), and the PC's mouse and keyboard on the
  cartridge's USB port (Ctrl+K: type into it).
- The cartridge's two cores run on two of the PC's on four or more cores.
- Included, written from scratch for this emulator (MIT, sources in `testcart/`): **UVMC2 Demo** (a rotating
  cube drawn by the cartridge's chip) and **UVMC2 Movie Player** (the PiTrex / VecFever vector movies from
  `sd/movies`, with high-quality sound through the Vectrex's speaker or the jack; the movies are not included).

**The control deck and the setup** ([README](README.md#menus-and-the-control-deck))
- Tab opens the control deck over the running game: quality, this game's settings and seven strips (Tube,
  Room light, Overlay, Sound, Vectrex unit, Display, Cartridge). Hold H to compare with before; Reset this
  strip; a gamepad drives it too.
- The menu bar is now Vectrex, View and Help.
- Settings per game only when you ask: Save for this game, Revert, Remove game settings, Make default.
- The Vectrex unit: the analog parts that differ per Vectrex as sliders, with presets (Owner's Vectrex,
  Ideal, Cranky, Random unit), Export / Import.
- A setup at the first start (Vectrex > Setup wizard... runs it again): the folders found, the quality from a
  short test, the display mode explained with the best one chosen, player 1's keys, start switched on.

**Picture**
- Quality Low / Medium / High / Ultra or Auto (steps down by itself when the PC misses frames) - effects only.
- Smooth motion for 60 Hz screens: pictures drawn between the game's frames.
- G-Sync / FreeSync detected from the monitor's EDID, a G-Sync module too (it was missed before).
- No more flicker when a game runs below 50 frames a second, or draws half its lines each frame: the lines
  keep glowing between their redraws, as on the tube.
- No white block for a moment at the start.
- Linux: the program uses the system's own C++ runtime and X libraries (its older copies stopped Mesa's
  OpenGL from loading on new systems such as Fedora 44: no picture); without OpenGL the plain picture works
  (it crashed), says why, and starts switched on (issue #3).
- The screen's refresh is met by sleeping instead of spinning: much less CPU.

**Also**
- Esc never quits (in the window it pauses); a key or a click on the picture switches a Vectrex that is off on.
- Player 1's default buttons are a row, A S D F (W A S D in the setup); the mouse / touchpad can steer player 1.
- Touchpads scroll the menus and sliders.
- The ROM browser shows ROMs copied in while it is open, and follows links: a Finder alias (Mac) or a
  shortcut (Windows) of a folder in `roms` is a subfolder, of a ROM that ROM - no copying your own builds.
- The ROM browser with a controller: the stick chooses (left / right a page), button 4 loads a ROM or opens a
  folder, button 1 goes back to the roms folder (there it closes the browser).
- Controller shortcuts - the stick and all four buttons held for a second: up the ROM browser, down RESET,
  left port 2 VecVox / joystick, right the overlay on / off. Every game with a controller only; Vectrex >
  Controller shortcuts switches them off (one switch for all).
- Controller port 2 as a pictogram in the strip below the picture, next to the cartridge: P2 JOYSTICK, P2 VECVOX
  or P2 VECVOICE - click it for the next one. Every switch is announced in the middle of the picture.
- A controller button switches a Vectrex that is off on (a controller alone had no way to turn the knob).
- Steam Deck Game Mode: only one window is shown there, so the emulator skips the first-run setup and starts
  switched on (the game did not appear: the hidden setup window was waiting, the Vectrex switched off).
- Help > Copy system info, for a GitHub issue.
- Help updated for every new option.

## v1.0.1

**The tube's geometry, measured** ([README chapter 10](README.md#10-the-tubes-geometry-measured-not-invented))
- The generic barrel curve is gone: the picture now has a real Vectrex's own uneven shape, fitted to photos
  of the Test Cartridge grid, the start screen and Distortion 1 (a narrower top, a bowed bottom, leaning
  sides, the top right corner pulled up). A second Vectrex showed much the same shape.
- The vectors are bent line by line (long lines in pieces), so they stay sharp and the phosphor's glow and
  afterglow stay exact. Overlays are not bent - a flat sheet - only scaled onto the tube's picture.
- Tube > Tube geometry (on / off) and Tube geometry strength (x1 measured, 0 ideal); the picture keeps its
  size and place with the imperfections off.
- The glass: the screen opening's rounded shape with a soft edge (Tube > Glass edge), black above it as
  beside it; the on-screen display and the knob lined up with the glass.

**The room's light** ([README chapter 11](README.md#11-the-rooms-light-the-glass-the-phosphor-and-the-reflections))
- A new Light menu: an office ceiling light (a fluorescent fixture with white louvre cells, 5600 K) in the
  room, following Light > Room light.
- Its mirror image in the glass (a gentle dome: a little curved, in perspective, anti-aliased, slightly
  blurred); the glass turns slightly grey in a bright room without an overlay.
- A large soft glow on the phosphor layer behind the thick glass: enlarged, squarer, offset inwards by the
  glass's thickness, with a fine random grain.
- On an overlay: the lamp's reflection, the plastic brighter around it, and the scratches glinting around
  the whole lamp - every scratch close by, only the bigger ones further out.
- Light position... (a pad: drag the lamp), strengths for the glass and the overlay, each on / off, and
  Reset light settings to default.

**Also**
- VecFever `.v4e` files load like any ROM (ROM browser, Open ROM file): plain ROMs, a DS2431 save chip,
  2 x 48K and 4 x 48K banks; other VecFever types are refused with a message.
- The picture no longer flashes when the mouse moves over the window (seen on AMD drivers, issue #1).
- Help updated for every new option.

## v1.0.0

The first release for Windows, macOS and Linux - ready to run on all three: `Vectrex_EMU-windows.zip`,
`Vectrex_EMU-mac-app.zip` (Intel + Apple Silicon) and `Vectrex_EMU-linux-bin.zip`. Each has the emulator,
the same folders next to it on every system - `bios` (the Vectrex BIOS, with Mine Storm), `roms` (your
ROMs) and `overlays` (Mine Storm's overlay; MiSTer's `overlays.zip` goes here too) - each with a note on
what goes there; the speech ROM is a free download (see README.md). The PNG and log
files are the build's own checks.

- macOS: the picture at a Retina screen's full resolution; Command shortcuts (⌘E eject, ⌘R RESET, ⌃⌘F
  full screen, ⌘Q); runs from Gatekeeper's hidden copy of a downloaded app; 8-bit images on Apple's
  software renderer
- Linux: OpenGL through GLX (X11 / XWayland), controllers and sound through SDL2
- Windows: `build_exe.bat` builds the core and installs PyInstaller when missing

Updated 2026-10-22:

**Release**
- One layout on every system: the program, and next to it `bios` (`bios.bin`, also on Windows - no longer
  inside the exe), `roms` and `overlays`. Mine Storm's overlay is an ordinary file there,
  `Mine Storm (built in).art`: a `Mine Storm.art` of your own is chosen before it, or replace it. Overlays
  in `roms/overlays` are still found, and overlays chosen by hand are found again after a move.
- The ROM browser's "no ROMs" note in three short lines that fit.
- A new folder starts with the default settings: no settings are carried over from an early test version's
  file in %APPDATA% (it could set the buzz and the volume far too high).
- ROM browser: subfolders of `roms` as folders (a folder pictogram, above the games); inside one, BACK at
  the top (or Backspace) and the path in the title; it opens in the folder of the game in the slot.
- A speech module chosen without the speech ROM: a window says where to download it and where it goes,
  with buttons for the download page and the bios folder.
- Controller port 2 notes that a module holds player 2's button 2 (as a real one does): a game that will
  not start with a speech module (GCE's Berzerk) wants Joystick.
- A ready-to-run Windows build (`Vectrex_EMU-windows.zip`), built by the GitHub workflow like the Mac and
  Linux ones; the README restructured (download and play first, a contents list, the details in
  docs/TECHNICAL.md).

**Speech**
- VecVoice (SP0256-AL2, a C port of MAME's synthesizer; the MiSTer core's serial link) and VecVox
  (SpeakJet codes spoken with the SP0256's voice; its speed law fitted to a recording of VecVox X-Mas)
  on controller port 2. The speech ROM is a free download (README), not included.
- The speech on its own audio channel: its own volume (Ctrl+Page Up / Down, Sound > Speech module,
  SPEECH VOL on the on-screen display), full-range speakers (an arcade bass) or a small speaker; level as
  the MiSTer core's mix, 50 % louder; no extra delay against the game's sound.
- The modules' "ready" line on port 2's button 2 only; GCE's Berzerk waits for it to be released (choose
  Joystick for it), Berzerk Ultimate talks. README: the games that talk.

**Overlays**
- MiSTer's overlay files are found under their own names too (underscores as spaces, a trailing "Proto" or
  version ignored: `ArmorAttack.art`, `Dark_Tower_Proto.art`, `VectorbladeV1.13.art`).
- MiSTer `.art` overlays and PNGs (RGBA, the sheet or the whole MiSTer frame; any PNG kind and filter),
  in `overlays`, found by the ROM's name (also without its `(...)` parts and version
  number). Mine Storm's overlay comes with the emulator (`overlays/Mine Storm (built in).art`,
  credit and disclaimer in the README).
- Overlay menu: on / off (O), choose automatically / choose / none per game, tint, room light, printed
  art, parallax, position and size per overlay, scratches.
- Flat over the curved tube; the sheet in its real shape (162 x 207 mm).
- Room light lights the whole sheet: the plastic steeply, the print more gently.
- Scratches (on by default), modelled on photos of a real Mine Storm overlay in the dark: mostly short
  fine and medium scratches, a few scribbles, hardly any deep damage by default; a matte halo around the
  lit lines, and the scratches square to the light glint - rings around a bright dot, random hairlines along
  a line. Made on the graphics card when an overlay is loaded; each overlay its own pattern.
- Scratch settings: Wear (all of them), Wear type (fine / medium / damage, one window), Scribbles, Glint
  strength, Glint reach, New random scratches. Deep damage cuts through the colour layer: a little white
  light comes through.
- Plastic glow (on by default) and its strength: the sheet spreads a soft, wide glow in its own colour around
  bright lines.

**Games**
- Vectorblade paces itself without the BIOS's Wait_Recal: its frames now end at its own timer restart, so
  it runs at its 50 fps (the on-screen display showed 45) and the title banner rises smoothly instead of
  shaking.

**Display**
- Rotate screen: 0 / 90 / 180 / 270 degrees, the overlay and the panel included.
- Start size (0): as big as fits the desktop, above the task bar; it no longer changes when 0 is pressed
  again. Real size (9): the overlay 162 x 207 mm on any monitor, in the window and in full screen; the
  window centred vertically. Monitor size for monitors that do not report theirs.
- The on-screen display and the panel's labels lower, clear of an overlay's edge.
- The power-off flash stays inside the visible picture.

**Tube**
- Even beam (off by default): an ideal vector display - every point as bright and thin as a line, no corner
  glow, overlapping light not adding up; for those who want the perfect look instead of the tube's.
- Afterglow off no longer shows the last copy of a moving object next to the new one (it held 34 ms, now
  the game's frame + 2 ms).
- Line gaps: the integrators' stop delay (9.3 - 12.3 - 15.3 cycles), live.
- Afterglow softness taken out of the menu (it had no visible effect).

**Menus, keys, settings**
- Each menu has its own reset (Vectrex, display, tube, overlay, sound), resetting only that menu.
- Menu items greyed out while their feature is off (the afterglow's sliders with the afterglow off, the
  scratch sliders with the scratches off, Buzz volume with the buzz off, the speech module without one...).
- Sliders: the default in the middle, 0 to twice the default; buzz default 0.091.
- Keys: + / - / 0 window size, 9 real size, P pause, L ROM browser, O overlay - without Ctrl; only Ctrl+E
  (eject) and Ctrl+R (RESET) need Ctrl. These keys can not be given to a player.
- Player 1's buttons default to A W S D.
- Cartridge hardware: a 512 KB latch flash cartridge; the 1 MB multi-cart board.
- The Vectrex's DAC mixed as on MiSTer; a random seed at power-on.
