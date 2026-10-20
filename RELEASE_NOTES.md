# Vectrex_EMU release notes

The newest version is at the top. `VERSION` holds the version every push to GitHub publishes: the
build workflow replaces that release (title = the version, marked Latest) with the new build.

## v1.0.0

The first release for Windows, macOS and Linux - ready to run on all three: `Vectrex_EMU-windows.zip`,
`Vectrex_EMU-mac-app.zip` (Intel + Apple Silicon) and `Vectrex_EMU-linux-bin.zip`. Each has the emulator,
the Vectrex BIOS (with Mine Storm) and Mine Storm's overlay (inside the Windows exe, in the `bios` folder
on Mac / Linux), and `roms`, `roms/overlays` and `bios` folders with a note on what goes there: your ROMs,
overlays (MiSTer's `overlays.zip`) and the speech ROM (a free download, see README.md). The PNG and log
files are the build's own checks.

- macOS: the picture at a Retina screen's full resolution; Command shortcuts (⌘E eject, ⌘R RESET, ⌃⌘F
  full screen, ⌘Q); runs from Gatekeeper's hidden copy of a downloaded app; 8-bit images on Apple's
  software renderer
- Linux: OpenGL through GLX (X11 / XWayland), controllers and sound through SDL2
- Windows: `build_exe.bat` builds the core and installs PyInstaller when missing

Updated 2026-10-20 (the same version, rebuilt):

**Release**
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
  in `roms/overlays` (or `overlays`), found by the ROM's name (also without its `(...)` parts and version
  number); the ROM browser skips that folder. Mine Storm's overlay comes with the emulator (`bios/bios.art`,
  credit and disclaimer in the README).
- Overlay menu: on / off (O), choose automatically / choose / none per game, tint, room light, printed
  art, parallax, position and size per overlay, scratches.
- Flat over the curved tube; the sheet in its real shape (162 x 207 mm).
- Room light lights the whole sheet: the plastic steeply, the print more gently.
- Scratches (on by default): fine wear, longer and deeper ones down to a single deep scratch, made on the
  graphics card (about 50 ms), a matte halo and glints around the lit lines; Wear, Damage, Glint strength,
  New random scratches; each overlay its own pattern.

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
- Sliders: the default in the middle, 0 to twice the default; buzz default 0.152.
- Keys: + / - / 0 window size, 9 real size, P pause, L ROM browser, O overlay - without Ctrl; only Ctrl+E
  (eject) and Ctrl+R (RESET) need Ctrl. These keys can not be given to a player.
- Player 1's buttons default to A W S D.
- Cartridge hardware: a 512 KB latch flash cartridge; the 1 MB multi-cart board.
- The Vectrex's DAC mixed as on MiSTer; a random seed at power-on.
