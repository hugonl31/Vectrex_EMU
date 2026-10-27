"""The Help window's text (Help menu). Each topic: a title and a list of blocks:
    ('p', text)              a paragraph
    ('h', text)              a sub-heading
    ('item', name, text)     a menu item and what it does
    ('more', title, text)    a deep dive, folded away behind a button
Paragraph text may use *word* for bold."""

TOPICS = [
('start', 'Getting started', [
('p', "Vectrex_EMU emulates a Vectrex, the vector-display games console of 1982, and tries to look and "
      "sound like a real one rather than a perfect one: the soft glowing beam, the afterglow of the "
      "phosphor, the slight wobble from the mains, the buzz from the speaker."),
('h', 'Switching it on'),
('p', "Like a real Vectrex it starts *switched off*. The volume knob is also the power switch: turn it up "
      "(Page Up, the mouse wheel over the knob below the picture, or grab the knob and turn it round: drag "
      "clockwise; a click on its right half steps it up) and the "
      "Vectrex clicks on, the tube warms up and the BIOS plays its start screen. Turned all the way down "
      "it clicks off again. Start switched on (Config's Vectrex unit tab, and the setup) skips the knob."),
('h', 'Cartridges'),
('p', "Without a cartridge the Vectrex runs its built-in game, Mine Storm. Put your ROM files in the "
      "folder *roms* next to the program and click the cartridge below the picture (or L): the "
      "ROM browser lists them. Choosing one power-cycles the Vectrex with it, as if you had swapped the "
      "cartridge. The emulator starts again with the cartridge used last. A right click on the cartridge "
      "pulls it out of the slot (Mine Storm); another right click puts the last cartridge back. Ctrl+E does the same."),
('h', 'Playing'),
('p', "Player 1 plays with the arrow keys and A S D F (buttons 1 to 4, in a row as on the controller; W A S D as a cross in Controls), Shift for a half push of the "
      "stick, or with a game controller; Controls > Keyboard and controllers sets both players up. Keys that run the "
      "emulator: F11 full screen, Tab the Config window, Esc back / pause, P pause, L ROM browser, O overlay, C case, Ctrl+R RESET, Ctrl+E eject, Page Up / Down volume, Ctrl+Page Up / Down speech module volume, "
      "+ / - / 0 size, 9 Authentic size, Alt the menus."),
('more', 'The menus in full screen', "In full screen (F11) the menu bar is hidden: move the mouse to the top "
      "edge of the screen, or press Alt, and it appears over the picture - it goes again when the mouse moves "
      "down. Tab opens the Config over the picture; it and the other windows stay in front of it. The knob, "
      "RESET, the cartridge and the ROM browser are drawn into the picture itself. Esc or F11: "
      "back to the window."),
]),

('vectrex', 'Menu: Vectrex', [
('p', "The cartridge, the console's own buttons and the setup. Hover over an item in the menu for a short explanation."),
('item', 'ROM browser (L)', "Lists the ROMs in the roms folder next to the program; its subfolders are "
         "shown as folders above the games - open one, and BACK at the top (or Backspace) goes back up. A Finder "
         "alias (Mac), a shortcut (Windows) or a symbolic link of a folder shows as a subfolder, of a ROM as "
         "that ROM: an alias of the folder you build your games in, nothing to copy. Up / "
         "Down choose, Left / Right page, Enter or a double click loads a ROM or opens a folder, Esc closes. "
         "With a controller: the stick chooses (left / right: a page), button 4 loads or opens, button 1 goes "
         "back to the roms folder (there it closes the browser). Hold the stick up and all four buttons for a "
         "second to open it with the controller (TV mode shortcuts). "
         "The game keeps running behind it but gets no input."),
('item', 'Recent games', "The last games you played, newest first: hover over it and click one to start it."),
('item', 'Open ROM file...', "The Windows file dialog, for a ROM anywhere on your PC. VecFever .v4e files "
         "load too: unpacked into the folder v4e next to the settings (their saves are kept there), run with the "
         "cartridge board they name; one for hardware the emulator does not have is refused with a message."),
('item', 'Open UVMC2 game...', "A game for the Ultimate Vectrex Multicart 2 (.um2) anywhere on your PC: it runs on "
         "the cartridge's own computer (see Config: Cartridge)."),
('item', 'Eject / insert cartridge (Ctrl+E)', "Out of the slot: the Vectrex runs Mine Storm from its own ROM. Already out: the last cartridge goes back in (a right click on the cartridge does the same)."),
('item', 'RESET button (Ctrl+R)', "The RESET button on the console. The RAM is kept and the BIOS restarts "
         "the cartridge without its intro, exactly as on the real machine. Some games use this: they "
         "keep their high scores through a RESET."),
('item', 'Volume (Page Down / Page Up)', "The volume knob. As on the Vectrex it is also the power switch: at 0 % it "
         "clicks off, turned up from 0 % it switches on (a cold start). While turning it crackles a little, "
         "like the worn potentiometers of most Vectrexes do."),
('item', 'Pause (P)', "Stops the Vectrex; the afterglow on the screen stands still too."),
('item', 'Setup wizard...', "The setup from the first start again: what was found (the BIOS, your games, the UVMC2 "
         "SD folder, the speech ROM), the quality for this PC with a quick test, the display mode for your screen, "
         "player 1's keys and whether the Vectrex starts switched on. Its Reset Vectrex settings button puts back "
         "the cartridge hardware Auto, port 2 the joystick, start switched off with the intro (the controls keep "
         "theirs, the volume knob stays)."),
('item', 'Quit', "Closes the emulator; the settings are kept for next time."),
]),

('view', 'Menu: View', [
('p', "What you see: the window's size, the overlay, the case, the control deck below the picture, the quality "
      "and the room light."),
('item', 'Full screen (F11)', "The picture on the whole monitor at its full resolution (for example 4K)."),
('item', 'Bigger / Smaller / Fit screen', "The window size (+ / - / 0); Fit screen is as big as fits the "
         "desktop, above the task bar. In full screen: the picture size."),
('item', 'Authentic (9)', "In the window and in full screen (until + / - / 0): as big as on a real Vectrex: the overlay 162 x 207 mm like the real sheets, worked out "
         "from the monitor's size and resolution."),
('item', 'Overlay (O)', "The game's printed plastic sheet in front of the screen on or off (see Config: Overlay)."),
('item', 'Choose overlay...', "Any overlay for this game, remembered for it."),
('item', 'Case (C)', "The Vectrex's black plastic case round the tube, mirroring the picture - shown when there is "
         "no overlay (see Config: Case)."),
('item', 'Control deck', "The strip below the picture - the on-screen display, P2 PORT, the cartridge, RESET and the "
         "volume knob - on or off at once. Off, the window gets smaller by it; on again, the parts come back as they were. "
         "Each part on its own: Config > Display."),
('item', 'Quality', "Low, Medium, High or Ultra (Custom when changed by hand): how many of the picture's effects this PC draws; "
                    "Test this PC... suggests one (see The Config window)."),
('item', 'Room light', "Dark (half the default), Evening (the default) or Daylight (twice): how much the room lights up "
         "the overlay, the glass and the case. Finer: Config, the Room light tab."),
]),

('menu_controls', 'Menu: Controls', [
('p', "How you play: your keys and controllers, the mouse, the TV mode shortcuts, and what is plugged into "
      "controller port 2."),
('item', 'Keyboard and controllers...', "For both controller ports: which game controller and which keys each "
         "player uses (both work at once), the stick's dead zone and travel, and a test field that shows what the "
         "Vectrex gets (see Controllers)."),
('item', 'Mouse steers player 1', "The pointer's place on the picture is the stick, from the middle (a third of the "
         "picture's width away = all the way); the left button is button 4, the right one button 1."),
('item', 'TV mode shortcuts', "On (default), for playing from the sofa: the stick in one direction and all "
         "four buttons held for a second (player 1's controller or keys) - up: the ROM browser, down: the RESET "
         "button, left: controller port 2 VecVox / joystick, right: the overlay on / off. Every game with a "
         "controller only. Switch them off for a game that uses that combination (one switch for all)."),
('item', 'Port 2: Light pen', "GCE's light pen (1983) for Art Master, AnimAction and Melody Master: a light sensor in its tip "
         "that the game reads when the beam passes under it - so the game knows where it points. Here the mouse is the pen: "
         "the pointer becomes a small pen, and over the picture the pen is held to the screen. The mouse buttons are "
         "player 1's controller buttons: left = button 1 (Art Master: draw), right = button 2 (a straight line), middle = "
         "button 3 (the menu). The pen sees only where the game draws: to drag the cursor, start on it and move steadily. AnimAction needs its cartridge's "
         "own RAM, found by itself (Cartridge hardware: Auto). With the joystick in port 2, these games get the pen by "
         "themselves, for that game only."),
('item', 'Port 2: 3D Imager', "GCE's 3D Imager (1983): goggles with a spinning disc in front of the eyes - half "
         "of it black, half red, green and blue filters - that turned the white lines into colour (and 3D: each eye saw "
         "every other picture). For 3D Mine Storm, 3D Narrow Escape and 3D Crazy Coaster. The game drives the disc's "
         "motor itself and times its pictures from a hole in the disc: at the start the game waits, the screen dark, "
         "while the disc spins up (about 13 seconds), then plays in colour - as on the real goggles. A note on the "
         "picture shows the disc's speed meanwhile, and the motor is heard: a small electric motor whirring up "
         "(Config > Cartridge: the motor sound, its volume, the note). Shown in colour; the two eyes' pictures "
         "together (no 3D yet). Click the P2 PORT pictogram to plug it in - or just start the game: with the joystick in "
         "port 2, a game that needs the goggles (the three 3D games, and Rocket Sledge, an unreleased one) gets them by "
         "itself, for that game only."),
('item', 'Port 2: Joystick / VecVoice / VecVox', "The second joystick, or a speech accessory plugged in there: the VecVoice "
         "(Richard Hutchinson; a GI SP0256-AL2 speech chip) or the VecVox (Alex Herbert; a SpeakJet chip). With the "
         "joystick in port 2 and the speech ROM in the system folder, a game that talks (Vectorblade, Berzerk Ultimate, "
         "Space Frenzy, Verzerk and the others in the README's list) gets its module by itself, for that game only - "
         "said so on the picture. Choosing port 2 by hand while it is on is for that game only (remembered for it; the other "
         "games keep theirs). Config > Cartridge > Auto-select "
         "port 2 switches all of this off (the 3D Imager and the light pen too): port 2 then changes only when you "
         "change it, as on a real Vectrex. "
         "The SpeakJet itself can not be emulated exactly (its workings are unknown), so the VecVox speaks "
         "with the SP0256's voice at the SpeakJet's pitch and lengths: songs keep their tune. Both need the "
         "SP0256-AL2 ROM, which is not included ((c) Microchip Technology Inc.): download al2.bin from Joe "
         "Zbiciak's page (spatula-city.org/~im14u2c/sp0256-al2/, shared with Microchip's permission) and put "
         "it in the system folder next to the program as sp0256-al2.bin. A module signals 'ready' by holding "
         "player 2's button 2, as a real one does: games without speech that wait for every button to be "
         "released (GCE's original Berzerk) will not start - choose Joystick for them."),
]),

('online', 'Menu: Online', [
('p', "Two players, two PCs, one Vectrex: the host is player 1 and chooses the game, the other joins as player 2, and "
      "both Vectrexes switch on together. Only the controllers travel over the network: both PCs compute the same game."),
('item', 'Host this game...', "Starts waiting for player 2 with the game in the slot now, and opens the Online window. "
         "The meeting point gives a five-letter code: tell it to player 2 (Copy puts it on the clipboard)."),
('item', 'Join a game...', "Type the host's code (or an address) and press Join. The host's game starts here when you "
         "have the same ROM in your roms folder (found by its contents, whatever its file name)."),
('item', 'Leave the online game', "The game goes on here alone; the other player is told. While still waiting: stops waiting."),
('item', 'Online settings...', "The meeting point (both players need the same one; Default puts back the emulator's own), "
         "hosting without it - on a LAN, or with UDP port 7744 forwarded to this PC - and the ONLINE row in the on-screen "
         "display (the round trip in milliseconds, RELAY when the meeting point passes the game on)."),
('more', 'How the two PCs stay in step', "Each side sends its controller every frame and runs a frame only when it has "
         "both: a game never differs between the two. The host measures the round trip and sets an input delay that hides "
         "it (3 frames on a LAN, up to 12 far away). Most home routers let the two PCs talk directly once the meeting "
         "point has introduced them; when two strict routers will not, the meeting point relays the packets."),
]),

('deck', 'The Config window', [
('p', "Tab (or Config on the menu bar) opens one window over the running game with every display, sound and hardware "
      "setting, in tabs: Tube, Room light, Case, Overlay, Sound, Vectrex unit, Display and Cartridge. Every switch and "
      "slider acts on the game at once. Right click a slider: its default. Reset this tab puts back only that tab; Reset all puts back every tab (to the Quality profile's defaults, Custom: High's). "
      "Hold H (or the Before button) to see the look from before the Config window opened. Tab or Esc closes it."),
('p', "With a game controller: LB / RB switch tabs, up / down pick a row, left / right change it, A switches, "
      "Y holds the look from before, B closes."),
('h', 'Quality'),
('item', 'Low', "For weak graphics (a Raspberry Pi 5, old laptops with integrated graphics): Medium's cuts, the phosphor "
                "drawn at half the window's size while the vectors stay at full size, and 30 pictures a second instead "
                "of 60. The two switches are also in Display > Weak graphics, for any profile."),
('item', 'Medium', "The Vectrex look for laptops: the afterglow, the glow, the corner dots, closed corners (bend "
                   "smoothing) and the overlay's colours stay; the tube curve, glass edge, scratches, plastic glow, parallax, "
                   "room light, reflections and dither are off, and the glow is drawn at three-quarter size."),
('item', 'High', "All but the room light and its reflections; a lighter glow."),
('item', 'Ultra', "Every effect on."),
('item', 'Custom', "A profile you changed: any picture setting (Tube, Room light, Case, Overlay) set by hand in the tabs below."),
('item', 'Test this PC...', "Draws a heavy scene at each profile for a moment, measures the processor and the "
                           "graphics card, and suggests the profile that suits this PC - to accept or not. The first-start "
                           "setup runs it. Nothing changes by itself while you play."),
('p', "Quality only changes the picture effects, never the emulation. A profile sets the effects' switches; you "
      "can change any of them afterwards."),
('h', 'This game'),
('item', 'Save', "This game keeps the look and controls it has now and starts with them every time. "
      "Nothing is saved for a game unless you press this."),
('item', 'Revert', "Back to this game's saved settings (after changing them)."),
('item', 'Remove', "The game forgets its own settings and uses the everyday ones again."),
('item', 'Make default', "The settings now become the everyday settings, for every game without its own."),
('p', "A game without its own settings uses the everyday settings; what you change there is kept for all games."),
]),

('unit', 'Config: Vectrex unit', [
('p', "No two Vectrexes draw alike: their analog parts have aged and were trimmed differently. These are those "
      "parts, live, as in VIDE's analog settings - to see how a game looks on a good, a tired or a cranky Vectrex."),
('item', 'Unit', "Which Vectrex the parts below come from: the owner's (measured), an ideal one, a "
         "cranky one or a random one. Changing a part by hand makes it Custom."),
('item', "Owner's Vectrex", "Measured on the emulator author's own Vectrex (photos of its test screens): the default."),
('item', 'Ideal', "A perfect machine: the zero reference holds, nothing drifts, the Y hold settles at once."),
('item', 'Cranky', "A tired unit: a weak zero reference, drift, slow settling, long beam delays."),
('item', 'Random unit', "A new believable Vectrex somewhere between a good and a tired one, each time."),
('item', 'Zero hold', "How well the zero reference charges when a game re-centres the beam: lower, and shapes drawn after "
      "a re-centre start a little off - text lines slant, closed boxes do not meet."),
('item', 'Zero calibration shift', "Where the calibrated zero ends up after the BIOS's calibration pulse."),
('item', 'Drift X / Drift Y', "The integrators' offset: while a line is drawn the beam creeps sideways or up and down."),
('item', 'Zero offset', "The trimmers R333 / R335: the whole picture's centre against the zero reference."),
('item', 'Y hold settling', "How quickly the Y sample-and-hold reaches a new value (1 = at once)."),
('item', 'Beam switch-off delay', "How long the beam stays lit after a game blanks it: longer, and lines get tails."),
('item', 'Zeroing time X / Y', "How fast ZERO brings the beam back to the middle (an RC: the integrators' capacitors "
      "emptied through a switch: X 4.5, Y 3.3 cycles). Games that zero only briefly leave "
      "the beam a little short of the middle; a ZERO drawn with the beam on curves, Y first. 0 = at once (the old model)."),
('item', 'Offset shift (zero reference)', "The zero reference moves the beam by itself: ZERO goes to the point it "
      "sets, not to the middle - on the diagonal, a positive offset left and down (the trick behind old fast text "
      "routines). 166 = measured; 0 = the reference only matters while the beam moves."),
('item', 'Deflection lag X / Y', "How far the spot lags behind the integrators: rounder corners, curved ends, and the "
      "short gaps where joined lines meet (the spot still moving while the beam is dark). 6 = measured."),
('item', 'DAC bit 0-3 error', "The DAC's ladder: each bit's current off by this share. Text drawn right and back at a "
      "width has different bits each way, so its slant changes from one width to the next. The owner's: measured."),
('item', 'Line gaps (stop delay)', "When the integrators stop after a line: the gaps at line ends and corners."),
('item', 'Brightness (the knob at the back)', "As the knob on the back of a Vectrex: 0 = a black screen, 1 = as bright as a real "
      "one goes, 0.71 = well set (right click on the slider). Down: the faintest lines go first, a line's corner dots with "
      "it (how a game looks on a dim Vectrex). Up: brighter, and the beam glows even when a game switches it "
      "off - its moves and its returns to the middle show as a faint web, the 'spider web' of a Vectrex turned up too far, and "
      "a spot where it rests. The test cartridge's screens show it well."),
('item', 'Export... / Import...', "Save the unit as a .vxunit file to share, or load one."),
('h', 'Starting up'),
('item', 'Start switched on', "Starts the Vectrex at once instead of waiting for you to turn the knob."),
('item', 'Skip the Vectrex intro', "On switching on, the BIOS's start screen (the VECTREX "
         "logo, its tune, then the game's name) runs unseen in a fraction of a second and the cartridge "
         "starts straight away - in exactly the state it would have after the real intro."),
('more', "What the start screen does", "When a Vectrex is switched on, its 8 KB ROM (the 'Executive' or "
         "BIOS) clears the RAM, sets up the sound chip and the I/O chip, draws the growing VECTREX logo "
         "with its tune, then reads the cartridge's header - a copyright line, a tune and the game's name - "
         "and shows that title for a few seconds before it jumps into the game. A cartridge without a valid "
         "header gets Mine Storm instead. All together this takes about 13 seconds. The RESET button skips "
         "the logo too, but keeps the RAM."),
]),

('display', 'Config: Display', [
('p', "The screen: its rate, its turn, and the parts below the picture (all at once: View > Control deck)."),
('item', 'Monitor size (for Authentic)...', "The monitor's diagonal in inches, if it does not report its size "
         "(or reports it wrong): right click on the slider for the reported size. Check with a ruler: the "
         "overlay should be 162 mm wide and 207 mm high."),
('item', 'Rotate screen', "The whole screen turned in 90 degree steps - for a monitor turned on its side "
         "(tate). The overlay, the knob and the display turn with it."),
('item', 'On-screen display', "Below the picture: INTERNAL (the game's own frames per second), MONITOR "
         "(your screen's refresh), EMU RATE (green at 100 %; orange or red when the PC can not keep up), "
         "VOL, and SPEECH VOL when a speech module is plugged into port 2."),
('item', 'CPU and GPU meters (how much of each frame is used)', "Two bars below the on-screen display: how much of each "
         "frame's time the processor (CPU) and the graphics card (GPU) need - green below 70 %, orange to 90 %, red above. "
         "Near 100 % the emulator can not keep up: lower the Quality (or "
         "Optimize) for the GPU; for the CPU, a lighter game or a faster PC. CPU is the busier of the emulation and the drawing."),
('more', "Why games run at different frame rates", "A Vectrex has no picture memory: the CPU steers the "
         "beam along every line, every frame. Most games redraw 50 times a second, but a busy screen takes "
         "longer to draw, so the frame rate drops - and the picture flickers more. INTERNAL shows that "
         "rate: it is part of the game, not a fault of the emulator."),
('item', 'Volume knob and RESET button', "The Vectrex's panel in simple outlines below the picture: the "
         "knob (wheel or click to turn it) and the RESET button."),
('item', 'Cartridge', "A cartridge below the picture: click it for the ROM browser."),
('h', 'Screen'),
('item', '30 pictures a second (half the screen rate)', "A new picture on every second screen refresh: half the graphics "
         "work, for weak graphics. The game runs on at its own speed; only the screen shows fewer pictures, the afterglow "
         "fading in bigger steps. Quality Low sets it."),
('item', 'Phosphor size', "The glow and the afterglow drawn at a share of the window's size and scaled up, the lines "
         "themselves always at full size (sharp). 3/4: about half the glow's graphics work (Quality Medium); Half: a quarter "
         "(Quality Low) - softer glows, hardly visible on a laptop screen."),
('item', 'Screen', "How the pictures meet your monitor's refreshes - hover over each choice."),
('item', 'Matched to the monitor', "The default: 60 Hz on a 60 / 120 / 240 Hz monitor, else the monitor's rate divided down near the game's 50 Hz (144 Hz: 72, 165 Hz: 55) - always in step with the refresh, so smooth and tear-free."),
('item', 'Smooth motion', "For 60 Hz screens without G-Sync / FreeSync (most laptops): a Vectrex game makes 50 pictures a second, a 60 Hz "
      "screen shows 60, so every fifth refresh repeats one and moving things stutter slightly. Smooth motion gives every refresh a picture "
      "of its own, with the vectors where they are between the two game frames around it - the game keeps its own speed and sound, the "
      "picture comes about 20 ms later. Where two frames do not match line for line (things appearing, other lines), the earlier one is "
      "shown as is. Off by default (the purist 50 pictures a second)."),
('item', "The monitor's full rate", "Every refresh of a 120, 144 or 240 Hz monitor: the afterglow fades in "
         "finer steps and moving things update more often."),
('item', 'G-Sync / FreeSync', "One screen picture per game frame, with no tearing - needs such a monitor. "
         "At 50 frames a second the mains wobble stands still, as on a real Vectrex."),
('item', 'Reset display settings to default', "Fit screen (not Authentic; the monitor size as it "
         "reports it), not turned, the on-screen display, the knob and the cartridge on, fixed 60 Hz."),
]),

('tube', 'Config: Tube', [
('p', "The Tube tab is about the picture tube and the analog parts that drive it. Each effect is measured "
      "on a real Vectrex (photos, a video and sound recordings) and can be switched off."),
('item', 'Phosphor (afterglow and bloom)', "The glow of the screen's coating. Off: plain lines."),
('item', 'Bloom', "How far the brightest lines light up the glass around them."),
('more', 'Why the lines glow', "The electron beam excites a coating of phosphor on the inside of the "
         "glass. A spot the beam lingers on gets very bright: dots and line ends glow, fast thin lines stay "
         "faint. Light scattered in the thick glass front (halation) makes a soft halo around bright "
         "spots: the bloom. The emulator draws each line as a soft round spot whose brightness and width "
         "follow how long the beam spent on each point, as the tube does."),
('item', 'Afterglow / Afterglow length', "The phosphor keeps glowing after the beam has passed. "
         "x1 is the measured length; the default x0.6 was matched to a real Vectrex side by side."),
('more', 'The measured afterglow', "Filmed on a real Vectrex: the moment the beam leaves, a spot drops to "
         "about 15 % of its brightness, to 4 % after 0.15 s and 1.5 % after half a second; it is gone after "
         "about 2 seconds. That is why fast objects leave fading copies or a faint trail, and why a game "
         "running at 25 frames a second flickers less than you might expect. A spot drawn again is lit "
         "again; it does not add up."),
('item', 'Afterglow joining', "How the afterglow of a moving object joins up (0.5 by default): each "
         "fading spot spreads a little every frame, so the copies a fast object leaves (one per game frame) "
         "merge into one smooth trail, as on the tube - sharp, not blurred. The glow only spreads for its "
         "first moments, so a fading shape keeps its own size. 0: separate copies."),
('item', 'Afterglow overlap', "The fading glow of neighbouring copies overlaps and averages out, as "
         "on the tube (0.8 by default, in pixels): the trail becomes one smooth gradient, and the corner "
         "dots a shape leaves join into a line. Only the fading light; lines being drawn stay sharp."),
('item', 'Corner glow (halo where the beam rests)', "Where the beam stops for a moment - every corner, every "
         "dot such as a shot or a star - the screen glows softly around the spot, as the light scatters in the "
         "tube's thick glass. It also softens the tiny dark gaps a Vectrex leaves at some corners."),
('item', 'Corner glow strength...', "How strong that glow is (right click on the slider: the default)."),
('item', 'Corner dot brightness...', "How much brighter the spot is where the beam stops - corners, dots, the joins "
         "in a dashed line - than the line itself. x1 = as measured; 0 = no brighter than the line."),
('item', 'White balance (colour temperature)', "The middle is neutral: the lines and their glow the screen's own "
         "pure white. Towards cool (up to 16000 K) the glow turns colder while the lines' bright cores stay pure white - "
         "the screen's brightest - and from about 9600 K the cores follow. Towards warm (down to "
         "5600 K) the same. 12000 K by default: the bluish white of a real Vectrex - a cold glow, and the brightest cores "
         "bluish too; further right even bluer. "
         "Only the lines change colour - the overlay and the case keep theirs."),
('item', 'Beam trace (the spot lights the phosphor)', "Off by default. The core sends the spot's own path - every "
         "cycle where it was - and the phosphor lights up by how long the beam stayed in each place: slow is bright, a "
         "stopped beam builds a dot, fast is faint, and curves are smooth curves, not pieces of straight line. "
         "One measured rule instead of the line renderer's corner dots and glow."),
('item', 'Even beam (an ideal vector display)', "Off by default. Every point as bright and as thin as the lines: "
         "no brighter, fatter corners and dots where the beam rests, no corner glow, no fainter fast lines, and "
         "where lines overlap they do not add up. A vector display without the tube's character - dots such as "
         "stars stay visible, just no brighter than a line. The gaps stay: they come from the timing."),
('item', 'Line gaps (analog delay)...', "The small gaps at line ends and corners: on a real Vectrex the beam "
         "stops moving a few cycles after the game switches it off, and how many differs from one machine to "
         "the next. 7 cycles (the default) is a well-tuned Vectrex; 4 nearly closes the corners, 10 "
         "gives the wide gaps of a worn one."),
('item', 'Tube geometry (a real Vectrex)', "Where the beam really lands, measured on the owner's Vectrex (and "
         "matching a second one): the picture a little smaller and narrower, more so towards the sides; vertical "
         "lines bowed slightly to the left, horizontal ones down; the top a little narrower than the bottom; the "
         "top right corner pulled up. So the Test Cartridge's screens look as on the real tube. Overlays follow: "
         "the sheet is made a little narrower so their playgrounds still sit on the game's lines."),
('item', 'Tube geometry strength...', "From an ideal, straight picture (left) through the owner's Vectrex (the "
         "middle) to twice as much."),
('item', 'Bend smoothing (curves without facets)', "A curve a game draws as a chain of short lines is drawn as one smooth "
         "bend, as the tube shows it: no corners where the short lines meet. Off: the lines exactly as the game sends them."),
('item', 'Second glow pass (a softer, wider glow)', "A second, wider glow on top of the first: softer, closer to the real "
         "tube's halo. Off saves graphics time on weak graphics."),
('item', 'Dither (no bands in soft glows)', "A faint noise in the soft glows, so they fade out smoothly instead of in visible "
         "steps of brightness (bands)."),
('item', 'Glass edge (rounded corners)', "The tube's glass face ends in rounded corners with a soft edge: what "
         "is drawn beyond it disappears there, as on the real screen."),
('h', 'Mains hum wobble'),
('item', 'Mains hum wobble / Wobble strength', "The picture sways a tiny bit with the mains."),
('item', 'Mains: 50 Hz / 60 Hz', "Europe and most of the world / USA, Canada and others."),
('more', 'Where the wobble comes from', "The Vectrex's power transformer and its supply carry a little of "
         "the 50 or 60 Hz mains into the deflection circuits, so the beam is pushed slightly one way and "
         "back again with the mains. The game's frame rate is not tied to the mains: a game running at "
         "exactly 50 frames a second on 50 Hz mains catches the wobble at the same point every frame and "
         "the picture stands still; any other frame rate catches it a little later each frame and the "
         "picture slowly ripples. On 60 Hz mains a 50 frames-a-second game ripples at the 10 Hz "
         "difference. The emulator follows the mains phase at the moment each line is drawn."),
('h', 'Brightness bending'),
('item', 'Brightness bending / Bending type / Bending strength', "A busy, bright picture grows and shifts a little."),
('more', 'Why the picture bends', "The tube's high voltage comes from a small transformer that can only "
         "deliver so much current. A lot of bright drawing loads it, the voltage sags, the slower electrons "
         "are deflected further and the picture grows ('grow'); the load also shifts the deflection "
         "('shift'). Bright explosions make a Vectrex picture breathe."),
('item', 'Reset tube settings to default', "Back to the measured values - only this menu's settings."),
('more', 'Analog drift and calibration', "The Vectrex draws with analog integrators: the CPU sets a speed "
         "for X and Y and opens the integrators for a number of cycles; the beam moves while they charge. "
         "Their offsets drift a little, so closed shapes may not quite close and text slants slightly; every "
         "Vectrex is a bit different and has two trimmers for it on its main board. Some newer games have a "
         "calibration screen. The emulator models the integrators' drift and their delays: the X and Y "
         "speeds reach the integrators 14-15 CPU cycles late, the beam starts moving 12 cycles and stops 15 "
         "cycles after it was told to - games that time their drawing to the cycle depend on that."),
]),

('overlay', 'Config: Overlay', [
('p', "On a real Vectrex every game came with a printed plastic sheet in front of the screen: coloured, "
      "see-through parts tint the vectors behind them, the solid print gives borders, pictures and the "
      "game's name. The emulator shows the MiSTer core's overlay files (.art) and PNG images with "
      "transparency (an 846 x 1080 sheet): put them in the folder *overlays* next to the program, "
      "named like the ROM - Armor Attack.bin finds Armor Attack.art (MiSTer's own names such as "
      "ArmorAttack.art or Armor_Attack.art too: underscores count as spaces). Mine Storm's overlay comes with the "
      "emulator as overlays/Mine Storm (built in).art (from the MiSTer project's overlays, Libretro artwork): "
      "a Mine Storm.art of your own is chosen instead, or replace that file."),
('item', 'Overlay (O)', "The overlay on or off."),
('item', 'Choose automatically (by ROM name)', "The overlay with the ROM's name; else the same name without "
         "its (...) parts and version number, so hacks and versions find the game's overlay; Mr. Boston "
         "Clean Sweep finds Clean Sweep."),
('item', 'Choose overlay... / No overlay for this game', "Any overlay for this game, or none - remembered for "
         "the game."),
('item', 'Tint strength...', "How strongly the coloured plastic colours the vectors."),
('item', 'Room light...', "How much the room lights up the sheet - the coloured plastic and the print: low for "
         "an evening (the plastic almost black, only the vectors), high for daylight. The plastic's light rises "
         "steeper than the setting (half gives a quarter, the right end four times the default); the print "
         "follows it more gently (half: half as bright, the right end twice)."),
('item', 'Printed art brightness...', "How bright the overlay's printed artwork (its white and coloured print) "
         "shows in the room's light."),
('item', 'Scratches (decades of handling)', "Decades of handling: countless fine scratches make the sheet "
         "slightly matte, and fewer, deeper ones - thousands of short scuffs, down to a few deep ones. In the dark "
         "you see them only where the vectors light them up: a soft matte halo around the lines, and the "
         "scratches that lie square to the light glint - rings around a bright dot, random hairlines along a "
         "long line (each part of it lights other ones). In room "
         "light the whole sheet looks more matte, in its own colours. On by default; made on the graphics "
         "card in a fraction of a second, each overlay with its own pattern."),
('item', 'Wear (how much handled)...', "How worn the sheet is: all the scratches together, fine and large - "
         "from almost new (left) to twice as worn (right)."),
('item', 'Fine scratches / Medium scratches / Damage / Wear type...', "Three sliders for the kind of wear: the fine scratches (the matte buff), the medium ones "
         "and the damage - the few deep ones that cut through the colour layer where they are widest, so a "
         "little white light comes through. Each from none to twice as many. Right click on a slider: its default."),
('item', 'Scribbles...', "The share of curly scratches - short sweeps ending in a little curl, as from a grain of "
         "sand dragged under the sheet."),
('item', 'Glint strength...', "How strongly the scratches catch the light."),
('item', 'Glint reach...', "How far from the lines the scratches still catch their light: to the left only "
         "close to them, to the right farther out."),
('item', 'Plastic glow', "The plastic itself spreads a little of the light: a soft, wide glow in the "
         "sheet's colour around bright lines and text, as on a real overlay in the dark."),
('item', 'Plastic glow strength...', "How strong the plastic glow is (x1 = as on the owner's sheet in the dark)."),
('item', 'New random scratches', "Another pattern of scratches (each overlay gets its own variation of it)."),
('item', 'Parallax (seen from the usual height)', "The sheet sits a little in front of the tube, so seen from "
         "the usual height the picture sits a little higher on it (as on MiSTer)."),
('item', 'Position and size (this overlay)', "Fine adjustment for the overlay of this game, kept for it. The sheet "
         "stays on the case's frame, as on a real Vectrex: the game's picture behind it moves and is sized the other "
         "way. With the overlay off, the picture is back to normal."),
('item', 'Reset overlay settings to default', "On, chosen automatically, tint, room light, print, parallax "
         "and the scratches back to the defaults; this game's chosen overlay and this overlay's position and size "
         "too (other games keep theirs)."),
]),

('light', 'Config: Room light', [
('p', "The room around the Vectrex: how much light there is, and the office ceiling light above and in front of "
      "you, mirrored in the tube's glass and on an overlay."),
('item', 'Room light...', "How much the room lights up the overlay's sheet - the coloured plastic and the print: low for "
         "an evening (the plastic almost black, only the vectors), high for daylight. The plastic's light rises "
         "steeper than the setting (half gives a quarter, the right end four times the default); the print "
         "follows it more gently (half: half as bright, the right end twice)."),
('item', 'Glass catches room light (without an overlay)', "Without an overlay the glass turns grey in the room's "
         "light - hardly at the default, clearly at the slider's right end - with the phosphor layer's fine bumps "
         "behind it; off: always black. With an overlay the glass stays black, for the contrast."),
('item', 'Ceiling light reflected on the glass', "The ceiling light mirrored sharply in the glass (a little "
         "softened: the glass is not perfectly clean), and behind the thick glass the phosphor layer lit up in a wide, "
         "soft glow - larger than the light and offset from it, more so towards the edges where the glass is "
         "thicker. Without an overlay, with the room light: faint at the default, bright at the right end."),
('item', 'Glass reflection strength...', "How bright the light on the glass is."),
('item', 'Ceiling light reflected on the overlay', "On an overlay the light is mirrored a little blurred (plexiglass), "
         "the plastic around it gets brighter in its own colour, and the scratches around it glint - the more worn "
         "the sheet, the more of them."),
('item', 'Overlay reflection strength...', "How bright the light on the overlay is."),
('item', 'Light position...', "Drag the dot to move the ceiling light: left and right along the top of the screen, "
         "and up and down - from just visible at the top edge to its middle a quarter of the way down."),
('item', 'Reset light settings to default', "The room light, the glass and the ceiling light as they came."),
]),

('case', 'Config: Case', [
('p', "The Vectrex's black plastic case round the tube, shown when there is no overlay: glossy walls sloping down to "
      "the glass, which mirror the picture - softly, a little warped, as moulded plastic does - and a 5 mm matte front. "
      "The room light and the ceiling light (the Room light tab) light it too. With an overlay it is hidden: the "
      "overlay has a frame and lighting of its own."),
('item', 'Case  (C)', "The case on or off (also View > Case)."),
('item', 'Reflections...', "How strongly the picture is mirrored in the case's walls."),
('item', 'Wear...', "Decades of use: the fine scratches on the glossy walls and the band on the matte front where "
         "overlays were slid in and out. Left end: as new."),
('item', 'Reset this tab', "The case on, its reflections and wear as they came."),
]),

('addons', 'Config: Cartridge', [
('p', "Cartridges with a computer of their own. The Ultimate Vectrex Multicart 2 (UVMC2) carries an RP2350 - two ARM "
      "Cortex-M33 cores at 150 MHz - and runs native games (.um2 files) on it: the game holds the Vectrex's 6809 "
      "stopped and drives the bus itself, writing the VIA once per bus cycle, exactly as the 6809 would. The emulator "
      "runs the RP2350 (both cores, its PIO state machines, DMA, timers, PSRAM) and lets it drive the emulated Vectrex, "
      "so the picture, the tube and the sound chip behave as with any cartridge - the mains wobble and the buzz too, "
      "as on a real Vectrex with a UVMC2."),
('h', 'The cartridge slot and port 2'),
('item', 'Controller port 2', "What is plugged into the second controller port: the second joystick, "
         "a speech module, the light pen or the 3D Imager (as Controls > Port 2)."),
('item', 'Cartridge hardware', "Auto (the default): the emulator builds the extra cartridge board a newer "
         "game needs to be bigger than 32 KB or to save high scores - chosen by the ROM's size: 64 KB = PB6 "
         "bank switching, 256 KB = Vectorblade's flash (as on MiSTer), exactly 512 KB = the 512 KB "
         "latch flash, other big images = the 1 MB multi-cart. Or pick one board for any ROM. Off: a bone-stock "
         "Vectrex, every ROM a plain cartridge of up to 32 KB."),
('more', 'How bigger cartridges work', "The Vectrex shows a cartridge only 32 KB of address space ($0000 "
         "to $7FFF). Larger games switch which part of their memory sits in that window ('bank "
         "switching'). The first trick used a spare line of the Vectrex's 6522 I/O chip, port B bit 6, "
         "which happens to reach the cartridge slot (pin 35): wired to the EPROM's top address line it "
         "picks one of two 32 KB halves of a 64 KB chip. Malban's Vectorblade adds the cartridge port's "
         "interrupt line as a second bank bit and uses a flash chip, which the game also writes to - so it "
         "keeps its high scores and settings without a battery. The HB v2 board takes a 512 KB flash: "
         "PB6 plus a small latch that a write to $0000-$3FFF sets - 16 banks of 32 KB, with saves too. Sean "
         "Kelly's Multi-Cart holds dozens of games in 1 MB and picks a 4 KB page with a small latch. The "
         "emulator models them all; the flash carts' saves go to a .sav file next to the ROM, the ROM itself "
         "is never changed."),
('h', 'Ultimate Vectrex Multicart 2'),
('item', 'PC mouse and keys on USB', "On (the default): a UVMC2 game or a PiTrex program sees the PC's mouse as a USB mouse (move it over the picture; left / right click, the wheel) and, after Ctrl+K, the keyboard as a USB keyboard - VecOS uses both, the PiTrex's Elite the keyboard. While typing, every key goes to the cartridge (the emulator's own keys and the controller keys rest); Ctrl+K again gives them back. The mouse steering player 1 (Vectrex menu) takes the mouse instead."),
('item', 'Auto-select port 2 (plug in what a game needs)', "On (the default): a game that needs the 3D Imager, "
         "the light pen or a speech module gets it in port 2 when it starts (a game that talks only with the speech ROM in the "
         "system folder), and the joystick back after it. Choosing port 2 by hand then counts for that game only. Off: port 2 "
         "holds what you chose, for every game, as on a real Vectrex."),
('item', 'UVMC2 games in the ROM list', "On: .um2 files in the roms folder appear in the ROM browser "
         "(marked UVMC2) and in Open ROM file."),
('item', 'Open UVMC2 game...', "A file dialog for .um2 files anywhere on your PC."),
('item', 'Headphone jack sound', "The UVMC2's own audio (its 3.5 mm jack, a 16-bit DAC at 32 kHz): clean and full "
         "range beside the Vectrex's sound (as a line out to an amplifier), through the Vectrex's own small speaker "
         "and its volume knob, or off. Games that use the Vectrex's sound chip are heard as always."),
('item', 'Open the SD folder / Choose SD folder...', "The folder that is the cartridge's SD card: open it in the file "
         "manager, or choose another folder for it."),
('item', 'Start the PiTrex', "A Raspberry Pi in the cartridge slot: its loader starts, the menu of its games and emulators "
         "(button 4 skips the intro)."),
('item', 'Open PiTrex program...', "One PiTrex program (.img / .pit) on its own, without the loader."),
('item', 'Headphone volume...', "The jack's level (100 % = as the game sends it)."),
('item', 'The SD card folder', "A real UVMC2 reads its games' extra files from a microSD card; here the card is a folder: uvmc2, next to the program (or in %APPDATA%/Vectrex_EMU when the program's folder cannot be written). Put files there as you would on the card - VecDoom: a folder doom with a WAD file in it (uvmc2/doom/DOOM.WAD, or DOOM1.WAD, the free shareware episode); Quake: a folder quake with pak0.pak (and pak1.pak) - and start the game. A game that needs such a file and does not find it says what and where, and opens the folder. The Config window's Cartridge tab: Open the SD folder, or choose another folder. What a game writes (saves, settings) is kept in the folder."),
('item', 'Reset add-on settings to default', "UVMC2 games listed, the jack clean at 100 %, the SD card folder uvmc2."),
]),

('sound', 'Config: Sound', [
('item', 'Vectrex buzz / Buzz volume', "The famous Vectrex buzz from the speaker."),
('more', 'Where the buzz comes from', "The deflection amplifiers that steer the beam draw large, fast "
         "currents. Some of that leaks into the audio amplifier, which sits on the same board and supply, "
         "so the speaker hums along with whatever is being drawn: louder and harsher with more and brighter "
         "lines, repeating at the game's frame rate. It is not the mains: it never stops, because even an "
         "empty screen is recalibrated every frame by the BIOS."),
('item', 'Vectrex speaker', "All sound through a model of the small built-in speaker: no deep bass, a "
         "little harsh at full volume."),
('item', 'Disc motor sound (its whir) / Motor volume', "The 3D Imager's disc motor: its whir while the goggles spin "
         "(only with the 3D Imager in port 2)."),
('item', 'Speech module (VecVoice / VecVox)', "The speech modules have their own amplifier and audio out, "
         "so their sound skips the Vectrex's knob, buzz and speaker. Volume: its own (Ctrl+Page Up / Ctrl+Page "
         "Down), 0 - 200 %; 100 % is the MiSTer core's Normal level. Full-range speakers: big speakers with an arcade cabinet's warm bass (most owners plug better "
         "speakers in). Small speaker: no deep bass, rounded treble - still better than the Vectrex's own."),
('item', 'Reset sound settings to default', "Back to the measured values - only this menu's settings "
         "(the volume knob stays)."),
]),

('help_controls', 'Controllers', [
('p', "The Vectrex controller has an analog stick and four buttons in a row. In Controls > Keyboard and controllers each "
      "player gets a game controller and keys; both work at the same time. Click a box, then press a key "
      "or a controller button, or push the stick the way the box says; Shift+click adds a second one; "
      "right click clears."),
('p', "Analog stick: drag the inner handles of the bars for the dead zone and the outer ones for the "
      "endpoint (how far you must push for a full deflection). Buttons as stick: for a d-pad or keys - "
      "full deflection, half with the half button."),
('more', "How the Vectrex reads the stick", "The stick has two potentiometers. The Vectrex has no "
         "analog-to-digital converter: the BIOS compares the stick's voltage with values from its one 8-bit "
         "DAC and searches for the match, a few steps each frame. Many games only ask for left / centre / "
         "right, so an analog stick and a d-pad play the same; others use the full range."),
]),

('system', 'The Vectrex', [
('p', "The Vectrex came out in November 1982, made by General Consumer Electronics (GCE) and designed by "
      "Smith Engineering. It was the only home console with its own vector monitor: a 9-inch black-and-"
      "white tube in an upright cabinet, with a controller that stores underneath. Colour came from plastic "
      "overlays that slid in front of the screen. It cost $199. Milton Bradley bought GCE in 1983; the "
      "video game crash ended the Vectrex in 1984. It has kept a lively homebrew scene ever since."),
('h', 'Inside'),
('item', 'CPU', "Motorola 6809 at 1.5 MHz."),
('more', 'Cycles: the budget of every frame', "1.5 million cycles a second, and most games aim for 50 "
         "frames a second: 30,000 cycles per frame for everything - the game, the sound and, above all, "
         "drawing. The CPU draws every line itself: it sets the line's speed, opens the integrators, waits "
         "while the beam moves, closes them, and the next. A long line costs as many cycles as it takes to "
         "draw. When a frame needs more than 30,000 cycles the game runs slower and the picture flickers "
         "more. The 6809 itself is a capable 8-bit CPU with 16-bit registers and good addressing - and the "
         "emulator counts its cycles exactly, down to which cycle of an instruction a write reaches the "
         "chips."),
('item', 'RAM', "1 KB ($C800-$CBFF), shared by the game and the BIOS."),
('item', 'ROM', "8 KB in the console: the Executive (BIOS, 4 KB) and Mine Storm (4 KB)."),
('item', 'I/O', "A 6522 VIA: two timers, a shift register and two ports that drive everything else."),
('more', 'How a line is drawn', "The 6522's port A feeds one 8-bit DAC. Through an analog multiplexer its "
         "voltage is sampled and held for the X speed, the Y speed, the brightness, the zero reference or "
         "the sound. Port B opens the integrators ('RAMP') and timer 1 times how long; the shift register "
         "switches the beam on and off in patterns of 8 bits - that is how dotted lines and the BIOS's "
         "raster text are drawn. /ZERO pulls the beam back to the centre. All of this is analog: it lags "
         "a little behind the CPU, which is what the emulator's delays model."),
('item', 'Sound', "A General Instrument AY-3-8912: three square-wave channels, a noise generator and an "
         "envelope. Its I/O port also reads the controller buttons."),
('more', 'The limits of the sound chip', "Three voices, each a square wave at one pitch and one of 16 "
         "volume levels, plus one noise source that any voice can mix in. The envelope (a hardware volume "
         "shape) is shared by all three. Games make drums from noise bursts and chords from fast arpeggios. "
         "Speech and samples are possible by writing the DAC directly, but that takes all of the CPU's time "
         "- the screen has to wait."),
('item', 'Screen', "A 9-inch tube driven by analog X/Y integrators: no pixels, no picture memory."),
('item', 'Controller', "An analog stick and four buttons; two controller ports. Accessories included a "
         "3D imager (spinning-disc colour and 3D goggles) and a light pen - both emulated: Controls > Port 2."),
]),

('carts', 'Cartridges', [
('p', "The original cartridges held a 4 KB or 8 KB ROM; the console shows a cartridge up to 32 KB "
      "($0000-$7FFF). Every cartridge starts with a header the BIOS reads at power on: a copyright line "
      "(without it the console plays Mine Storm), a tune and the game's title for the start screen."),
('h', 'Later discoveries'),
('p', "Homebrew authors found ways past the 32 KB limit and the missing save memory:"),
('item', 'Bank switching (64 KB)', "Port B bit 6 of the 6522 reaches the cartridge slot; wired to the "
         "EPROM's top address line it switches between two 32 KB banks."),
('item', 'Vectorblade (256 KB)', "Malban's flash cartridge: the cartridge interrupt line as a second bank "
         "bit, and high scores written back into the flash chip."),
('item', 'Multi-carts', "Many games on one board, picked from a menu (Sean Kelly's Multi-Cart: 1 MB)."),
('item', 'Save chips', "A tiny EEPROM such as the DS2431, talked to over one wire through the same port B "
         "bit, keeps high scores without a battery."),
('more', 'Why it is tricky', "The Vectrex never had a bank-switch signal. PB6 was simply a spare I/O line "
         "brought out to the slot, and the /IRQ line was never meant for switching memory. Games that use them have to switch banks only from code that sits at the same place "
         "in every bank, keep the signal steady while they draw, and expect a stock console to boot into "
         "the bank the pull-up resistors choose."),
]),

('accuracy', 'About this emulator', [
('p', "The core is vecx (GPL), much extended: exact 6522 timer and shift-register behaviour, the analog "
      "delays Malban measured on real Vectrexes, the bus timing of the 6809's writes, the integrators' "
      "drift, the AY sound chip at its own clock, and the bank-switching cartridges. The front end draws "
      "the beam with OpenGL in real time - at your monitor's full resolution - and models the tube, the "
      "phosphor and the speaker from measurements of a real Vectrex."),
('p', "The settings live in system\\settings.json next to Vectrex_EMU.exe (or in %APPDATA%\\Vectrex_EMU "
      "when that folder can not be written)."),
]),
]

# the circuit behind each Vectrex unit part: the second paragraph of its hover info in the Config window (deck.py)
UNIT_CIRCUIT = {
    'zhold': "The circuit: the one 8-bit DAC is shared; the multiplexer (a 4052) passes its voltage to a hold capacitor "
             "for the zero reference only while it is selected. A short select charges it part of the way: this value "
             "is the share it reaches. 0.38 measured on the owner's Vectrex.",
    'zcal': "The circuit: the BIOS calibrates the zero reference with a short pulse at each frame's start; the held "
            "voltage settles a little off the value written. This is how far, in DAC steps.",
    'drift_x': "The circuit: the X integrator is an op-amp charging a capacitor. Its input offset and the capacitor's "
               "leakage add a small constant speed while the integrator runs, so long lines bend away.",
    'drift_y': "The circuit: the Y integrator is an op-amp charging a capacitor. Its input offset and the capacitor's "
               "leakage add a small constant speed while the integrator runs, so long lines bend away.",
    'zofs': "The circuit: two trimmers on the main board (R333 X, R335 Y) set the integrators' centre against the "
            "zero reference. Set at the factory, moved by age and by repairs.",
    'ysettle': "The circuit: the Y speed is not fed straight in - the multiplexer charges a sample-and-hold "
               "capacitor, which needs a moment to reach a new value. A slow one: the start of a line after a change "
               "in Y speed is drawn at the old speed for a moment.",
    'beamoff': "The circuit: the brightness (Z) amplifier on the power board drives the tube's cathode through two "
               "transistors; switching the beam off is not instant, so a line runs on a little past its end.",
    'knob': "The circuit: the knob at the back is a 250K pot between +50 V and -30 V that biases the tube's control "
            "grid (G1) through 220K: a fixed offset on every line's brightness - and on the beam when a game blanks it.",
    'zerotx': "The circuit: /ZERO closes an analog switch (a 4066) across the X integrator's capacitor; it empties "
              "through the switch's resistance, an RC of a few cycles, not at once.",
    'zeroty': "The circuit: /ZERO closes an analog switch (a 4066) across the Y integrator's capacitor; it empties "
              "through the switch's resistance, an RC of a few cycles, not at once.",
    'ofsk': "The circuit: the zero reference is held on its own capacitor and added at the integrators' inputs; while "
            "/ZERO holds the beam it still pulls on it, so the beam rests where the reference puts it.",
    'lagx': "The circuit: the X deflection amplifier drives the yoke's coil; the coil's inductance and the amplifier's "
            "bandwidth make the spot follow the integrator a few cycles late.",
    'lagy': "The circuit: the Y deflection amplifier drives the yoke's coil; the coil's inductance and the amplifier's "
            "bandwidth make the spot follow the integrator a few cycles late.",
    'dac': "The circuit: the DAC (an MC1408) sums a current per bit through a resistor ladder; each resistor's "
           "tolerance makes its bit a little too strong or too weak, so equal steps are not quite equal.",
    'gap': "The circuit: the 6522's port B switches the integrators (RAMP) and the beam; the analog side reacts some "
           "cycles after the CPU's write, so each line ends a little after the program asked.",
}
