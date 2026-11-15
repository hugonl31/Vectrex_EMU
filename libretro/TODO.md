# Vectrex_EMU RetroArch core - to do

The aim: the core behaves like the desktop Vectrex_EMU (the same ROMs, cartridge hardware, saves, UVMC2, PiTrex, the
same look), with files where RetroArch expects them (BIOS and data in `system/vectrex_emu`, saves in RetroArch's saves
folder, the ROM folder only read). Small items get done as they come up; bigger ones are agreed first.

## To test on the Nova (the owner)

- [ ] Last Human (.bin): ZERO REF 3 / TEXT 0 look right by default (the desktop's Vectrex unit, not an ideal machine)
- [ ] Last Human (.bin): its calibration and high scores kept after a restart (the DS2431 save chip)
- [ ] Vectorblade: its scores kept (the flash written to the saves folder)
- [ ] Last Human (UVMC2), PiTrex: full speed (Threaded emulation on, UVMC2 mode Fast)
- [ ] The GPU path in RetroArch itself: the glass, the ceiling light, the case
- [ ] START for the overlay remapped in Quick Menu > Controls; tate (Rotation 90 / 270)
- [ ] VecVoice / VecVox games talk (sp0256-al2.bin is on the Nova now)

## In progress (the owner, 2026-11-15: do them all)

Ports and input
- [ ] Port 2 device chosen in the core's options, as the desktop's Config > Ports: auto (by the game, as now: the
      3D Imager, the light pen, VecVoice / VecVox) or one device by name
- [ ] Touch screen: the light pen by touch
- [ ] README for the public release: the optional files - `sp0256-al2.bin` (speech), the 3D Imager needs none

The room light
- [ ] Strengths, as the desktop's sliders: the reflection on the glass, on the overlay, the case's reflections and wear
- [ ] The lamp's position (roomlight.maps in C: the light map made in the core)

The Vectrex unit (the desktop's Config > Unit / Circuit)
- [ ] The brightness knob, the size X / Y, the high voltage, centring X / Y, the yoke's rotation, the picture's shape
      (skew, keystone, pincushion, linearity); Random unit and Cranky

The look
- [ ] The overlay's effects on the GPU: scratches, wear and damage, the plastic glow, the ceiling light on the
      overlay and its plastic brightening around it
- [ ] The overlay's parallax
- [ ] The Ultra profile's defaults the desktop's Ultra look: every default compared with vecrender.py / vectrex_emu.py
- [ ] The options grouped like the desktop's Config tabs (where RetroArch's submenus allow)

## Low priority (the owner: ask first)

- [ ] The PC keyboard and mouse as the UVMC2's / PiTrex's USB device: RetroArch's keyboard and mouse
- [ ] A ROM browser like the desktop's: on a button (L1?) or touch, its folder set in the options (else the folder the
      game was loaded from)
- [ ] 16:9 mode: in tate the desktop's control strip (OSD) below the picture, clickable / touchable
- [ ] RetroArch netplay for two-player games (the other player as player 2): 6809 games have exact save states, which
      netplay needs; UVMC2 / PiTrex have none

## Done (2026-11-15)

- SD cards as folders, as the desktop (fatimg.py in C: sdcard.c) - the UVMC2's text past the left edge was an old dev
  uvm2.cfg (zero 35) on the Nova's card image
- Overlays as the desktop has them: MiSTer .art and PNG (vart.py in C: overlay_art.c)
- sync_nova.sh: the local desktop build's ROMs, overlays, cards and saves onto the Nova
- EmuVR built from the shared libretro_vx.c (poc_core/build.bat); GitHub workflow on Node.js 24; old exe backups removed

- Skip the Vectrex intro (System); Reset to defaults: RetroArch's own Quick Menu > Core Options > Reset Options
- UVMC2 / PiTrex at full speed on the Nova: Threaded emulation (emulates while drawing), UVMC2 mode Fast by default
- Saves in RetroArch's saves folder: Last Human's DS2431, flash cartridges' .sav; the desktop's Vectrex unit
- VecFever .v4e, Cartridge hardware, UVMC2 mode; the GPU path (glass, ceiling light, case), quality profiles
- Rotation (tate) with the controls turned, picture size by screen pixels, submenus, room light, trail as light
