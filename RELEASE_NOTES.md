# Vectrex_EMU release notes

The newest version is at the top. `VERSION` holds the version every push to GitHub publishes: the
build workflow replaces that release (title = the version, marked Latest) with the new build.

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
