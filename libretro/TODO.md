# Vectrex_EMU RetroArch core - to do

The aim: the core behaves like the desktop Vectrex_EMU (the same ROMs, cartridge hardware, saves, UVMC2, PiTrex, the
same look), with files where RetroArch expects them (BIOS and data in `system/vectrex_emu`, saves in RetroArch's saves
folder, the ROM folder only read). Small items get done as they come up; bigger ones are agreed first.

## Tested on the Nova (the owner, 2026-11-16): all of it

## In progress (the owner, 2026-11-15: do them all; the full look and UVMC2 at full speed on the Nova)

- [ ] Every option's default and wording compared with the desktop's once more
- [ ] README for the public release: the optional files - `sp0256-al2.bin` (speech), the 3D Imager needs none

## Next (the owner, 2026-11-15)

- [x] The control deck on the Nova: touch on the knob, the ports, the cartridge; 16:9 tate

## Netplay (the owner, 2026-11-15)

- [x] 6809 games: RetroArch's own netplay (rollback with save states); one side alone changes nothing (the browser,
      eject, RESET, pause, the deck's power and port 2); simulated in vxhost netsim - the states the same
- [x] Two RetroArch instances on the PC: the join works and stays (the state held pointers; fixed 2026-11-15)
- [x] Between systems: the state by variable name (vars_*.h, VX15) - Windows <-> Nova byte-identical, 7 games
- [x] The real test: the PC hosting, the Nova joining - works (the owner, 2026-11-16); the sticks need both ports
      as Vectrex controller on the host (Max Users 2)
- [x] Two Android devices (the owner, 2026-11-16)
- [x] UVMC2 / PiTrex: lockstep (netls.c, RetroArch's netpacket interface), the desktop's agreements (the mode,
      the JIT, the seed, R302, the card's common files); PC <-> PC tested; Windows <-> Nova the same (solo test)

## Low priority (the owner: ask first)


## The desktop, from the core's work

- [ ] The glint harmonics in one pass per direction with their weights made on the CPU (gpu_ovfx.c): the same picture,
      fewer passes - for the Raspberry Pi 5, where the overlay's effects were slow

## Done (2026-11-15)

- The tube's light on the GPU (gpu_phos.c: the lines, the afterglow, the glow - the CPU's model, pixel for pixel) and
  the overlay's effects (gpu_ovfx.c: scratches, plastic glow, the ceiling light on the sheet, as the desktop's);
  parallax; the Nova at 960 px: the full Ultra look 3.3 ms a picture (Medium 1.0), Last Human (UVMC2) at full speed
- Port 2 device in the options, the light pen by touch; the room light's strengths and the lamp's position
- The options in submenus as the desktop's Config tabs
- Quality profiles as the desktop's (PROFILE_OFF, switch for switch); Optimize (pctest.py) as the auto profile
- The ROM browser as the desktop's (SELECT, mappable; touch); RESET and eject / reinsert buttons (Ports: none by
  default, or L / R / L2 / R2 / L3 / R3)
- The control deck as the desktop's (deck.c: OSD, PAUSED, P2 / P1 ports, cartridge, RESET, volume knob - the desktop's
  geometry, checked against its screenshot), Display > Control deck (off / full / slim / pop-up) and Screen shape 4:3 / 16:9; a Pause button;
  the ROM browser on L3 (Ports > ROM browser button)
- RetroArch's keyboard (Game Focus) and mouse as the UVMC2's / PiTrex's USB devices (usbkm.c) - not tested with a
  real keyboard yet
- The Vectrex unit (Unit / Circuit tabs): owner's, ideal, random and cranky units, the knob, the pots, the yoke, the
  picture's shape, the circuit's parts (unit.c)
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
