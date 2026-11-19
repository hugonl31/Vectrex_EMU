# What's new

The short list the emulator shows when a new version is out (the update message): a few lines per version, the
newest on top, each line short enough for that small window (at most about 70 characters). Every new version adds
its lines here before it is pushed. RELEASE_NOTES.md has the whole story.

## v1.7.5
- iPhone / iPad: play nearby with no network (peer-to-peer Wi-Fi)

## v1.7.4
- Online through the meeting point: full speed (the delay was guessed)
- Online: no picture stalls while waiting for the other player
- Nearby: Join the nearby host only on a host's own network
- iPhone: hosting on the hotspot checks that it is on

## v1.7.3
- Online: your player name is asked once and kept for every mode
- Online: a fresh install on the same device gets its name back
- Apps: the name is reserved at the first start

## v1.7.2
- PC: HDR on an HDR monitor (View > HDR) - as on phones
- Player names: A-Z and 0-9 only; needed to host or join
- iPhone / Android: Reset all settings; a long press resets one
- iPhone: double-tap a slider for its default; it opens there

## v1.7.1
- Online > Nearby: play without a router - the host makes the Wi-Fi
- Nearby: the network's name and password stay the same each time
- Nearby: Join the nearby host - one click on the host's network
- Online: a game seen on every network, ad-hoc ones too
- Online: through the meeting point both start anew (no 15 s wait)
- New: the Android app (test build) - as the iPhone / iPad app

## v1.7.0
- Online: the host can change the game for both players
- Online: no ending after a dropout - waits until a player leaves
- Online: the same power-on on both sides (no early out of step)
- RetroArch: a modern control deck - panels, words, a volume bar
- RetroArch: the overlay drawn faster on phones and handhelds

## v1.6.9
- Phosphor trail on black: as long as through an overlay
- RetroArch: the overlay no longer brightens the trail
- RetroArch: the trail on black as on the PC

## v1.6.8
- Last Human: 16 shots, slower shield bots, centipede cages, fixes
- RetroArch: slow games (Vectrexians) no longer flicker
- RetroArch: Fill skipped lines option (off: as a real Vectrex)
- RetroArch: no black flashes in Last Human with many shots
- RetroArch: GPU meter; EMU RATE steady at 100% when keeping up
- RetroArch browser: Recent and PiTrex at the top, as on the PC
- RetroArch deck: no accidental RESET or port 2 change by touch
- New icon: a render of our 3D Vectrex model
- RetroArch: the PiTrex files fetched from the ROM browser
- RetroArch: Keyboard as on the PC option (arrows, A W S D, P L O)
- RetroArch: the sharpest picture on upright screens too

## v1.6.7
- Fast forward: hold Space, the game runs 4x as fast (no sound)
- Fixed: Always on top, as saved, did not work after a start

## v1.6.6
- ROM browser pauses the game while it is open (PAUSED)
- Paused: EMU RATE shows 0% in green (was -- or a red 0%)
- RetroArch: the ROM browser always opens on the running game
- RetroArch: RESET on L1 and Pause on L2 by default
- Fixed: RetroArch hung after a game switch with run-ahead on
- Fixed: RetroArch on Android crashed loading from the browser
- Fixed: RetroArch kept the last game's overlay faintly after a switch

## v1.6.5
- Last Human keeps its calibration and high scores (save chip)
- Fixed: room light made the trail and glow look brighter
- Phosphor trail 1.5x brighter: the same with or without overlay

## v1.6.4
- Get games & extras: the items download side by side
- Setup: the window grows with its rows (nothing cut off)

## v1.6.3
- Online > Test VecLink on this PC: a 2nd Vectrex window, by cable
- VecLink: Berzerk Arena's link game; test your own VecLink code
- Online > Host VecLink game: two consoles online, a screen each
- Online > Host on local network (LAN): no internet, game listed
- Online menu: clearer names, grouped
- P2 PORT: hold it for a menu of all devices; VecLink has its icon
- Update: faster download; asks before it installs, says what stays
- Fixed: VIA timer 1 started at $FFFF ran out at once
- Online: both players need v1.6.3

## v1.6.2
- Full screen: the strip below the picture lines up as in the window
- A game loaded while the Vectrex is on skips the logo, as after RESET
- Buttons held while loading reach the game (Last Human: 2+3 = calibration)
- Fixed: UVMC2 music too low after RESET or a ROM swap (Last Human)
- Config > Ports: Reset UVMC2 calibration to the shipped one
- UVMC2 and PiTrex: the zero reference of a real Vectrex (UVMC2 zero 3)
- Last Human v1.4.0: ZERO REF +03 and TEXT +00 out of the box, both editions

## v1.6.1
- Last Human v1.3.9: calibrated for a standard Vectrex
- Last Human: closed ship outlines and straight text on a stock console

## v1.6.0
- UVMC2 games now power on through the Vectrex intro, like a real one
- RESET in the intro or in play goes straight to the game's own start
- Online: both players need v1.6.0
- Last Human v1.3.8: the UVMC2 title runs at a steady 50 fps
- Last Human: stick left/right sets difficulty, up/down the arena
- Last Human: TEXT calibration straightens the small text
- Last Human: co-op high scores marked 2P, no longer slow the page

## v1.5.9
- Console defaults back to a typical Vectrex (zero offset and X drift 0)
- UVMC2 games no longer stall when another program shares the CPU

## v1.5.8
- Display > Run-ahead: a frame less input lag in many games (off by default)
- Run-ahead: Windows only for now, 6809 games, never online

## v1.5.7
- Last Human v1.3.7: the UVMC2 edition draws exactly like the cartridge
- Last Human: smoother roster page, arena fitted to the overlay, co-op HUD

## v1.5.6
- Small BIOS text now as tall as on a real Vectrex (was too flat)

## v1.5.5
- View > Always on top: the window stays above other programs

## v1.5.4
- Last Human UVMC2: the cinematics' text in the new real-console way too, centred

## v1.5.3
- Last Human UVMC2: text drawn the way that works on real consoles (was garbled on some)

## v1.5.2
- Last Human: no more long blank stretches when started from a multicart
- Last Human UVMC2: the calibration screen opens on a card without one
- The UVMC2 card comes with this emulator's calibration

## v1.5.1
- Last Human: with Arena 2 chosen, the title shows Arena 2's robots

## v1.5.0
- Online: joining through the meeting point no longer fails now and then

## v1.4.9
- Last Human co-op: both ships in the cinematics while both players are in
- Last Human ending: the ship flies to the middle and away, not through the text

## v1.4.8
- Last Human UVMC2 edition back: clean text, closed boxes
- Last Human: the Architect shows in his own cage before the last fight
- UVMC2: RESET no longer crashes or freezes the emulator
- A crash now offers to send a debug report
- Port 2 devices can be changed while a game runs, as on a real Vectrex

## v1.4.7
- UVMC2 Fast and Dev modes for slower PCs (Config > Ports)
- Optimize picks the UVMC2 mode this PC keeps up with
- Online UVMC2 games: the slower PC decides the mode, for that game
- CPU / GPU meters turn orange only above 90 %
- UVMC2 and PiTrex: whole pictures, no more missing or empty frames
- Online play needs v1.4.7 on both sides
- Picture-shape sliders moved to Config > Unit

## v1.4.6
- Last Human UVMC2 edition out for now: back once its text is fixed

## v1.4.5
- UVMC2 Centipede runs 1.7x faster (thank you for the debug reports)
- UVMC2 Galaga a little faster; Galaga and Moon Cresta: more to come
- Recording shows REC and the time in the title bar and control deck
- Random and Cranky units now differ in shape too: skew, keystone, bow
- Debug report: add your email (optional) to hear when a fix is out

## v1.4.4
- Mac: new installs find their UVMC2 and PiTrex SD card folders again

## v1.4.3
- Last Human: a new closing line in the Arena 2 ending
- Last Human UVMC2 edition included: cinematics, 2-player co-op
- Its co-op also works online (the other player on port 2)

## v1.4.2
- The included game is now called Last Human
- Robots now burst out of their cages in all directions
- Updates now always bring the newest Last Human (saves are kept)

## v1.4.1
- Fix: BCD scores no longer lose a carry (6809 DAA instruction)
- Online play needs v1.4.1 on both sides

## v1.4.0
- Record menu: videos with sound (F9) and big snapshots (F12)
- Saved as MP4 / PNG, named by date and time, in one folder

## v1.3.1
- The included game Robot Arena is now called Last Human

## v1.3.0
- Last Human is now included, free: a new game with its overlay

## v1.2.9
- Debug report: a timeline of what happened, and a picture of the screen

## v1.2.8
- Much faster: PiTrex menus and busy screens run at full speed again

## v1.2.7
- Updates keep your settings; only untouched ones get new defaults

## v1.2.6
- Auto update: the new version now starts by itself after updating

## v1.2.5
- Custom overlays: black outside the shape, soft edges fade to black
- Overlay on/off and its look are the same for every game

## v1.2.4
- Update message shows if online play still works with the new version

## v1.2.3
- Debug reports now include the program's own error log
- Reports also show the PC model, memory, late frames and sound dropouts

## v1.2.2
- Slower than real time? The emulator tells you why: battery, power mode
- Debug report: one click sends it, you get a number for the author
- Windows no longer slows the emulator down as background work

## v1.2.1
- UVMC2 games and PiTrex programs run 10 to 30 % faster
- PiTrex online play up to 30 % faster (Speedy real time on a Pi 5)
- Fixes: Basic quality with Beam trace, the updater on Linux

## v1.2.0
- Updates in one click: installs the new version, keeps your games
- See what's new before you update, right in this message
- Glass curvature: now 1 is your Vectrex, 0 even squares

## v1.1.9
- New phosphor engine: razor-sharp fast objects, trails that add up
- Smooth, rock-solid glow on moving lines
- Head tracking: its own tab, reflections move with your head
- Basic quality for a Raspberry Pi 5 with busy games
- Settings survive a restart, overlay position kept per game

## v1.1.8
- Head tracking over USB: iPhone and Android, no extra tools

## v1.1.7
- Head tracking: the picture slides behind the overlay as you move

## v1.1.6
- Glass curvature slider (Config > Tube)
