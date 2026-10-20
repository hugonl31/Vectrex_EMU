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
      "(Page Up, the mouse wheel over the knob below the picture, or a click on its right half) and the "
      "Vectrex clicks on, the tube warms up and the BIOS plays its start screen. Turned all the way down "
      "it clicks off again. Vectrex > Start switched on skips the knob."),
('h', 'Cartridges'),
('p', "Without a cartridge the Vectrex runs its built-in game, Mine Storm. Put your ROM files in the "
      "folder *roms* next to Vectrex_EMU.exe and click the cartridge below the picture (or L): the "
      "ROM browser lists them. Choosing one power-cycles the Vectrex with it, as if you had swapped the "
      "cartridge. The emulator starts again with the cartridge used last."),
('h', 'Playing'),
('p', "Player 1 plays with the arrow keys and A W S D (buttons 1 to 4), Shift for a half push of the "
      "stick, or with a game controller; Vectrex > Controls sets both players up. Keys that run the "
      "emulator: F11 full screen, Esc back / quit, P pause, L ROM browser, O overlay, Ctrl+R RESET, Ctrl+E eject, Page Up / Down volume, Ctrl+Page Up / Down speech module volume, "
      "+ / - / 0 size, 9 real size."),
('more', 'Why the menus are not there in full screen', "In full screen with G-Sync, Windows hides its own "
      "menus and pop-ups behind the picture, so everything you need there is drawn into the picture "
      "itself: the volume knob, the RESET button, the cartridge and the ROM browser. Press Esc or F11 to "
      "get back to the window and its menus."),
]),

('vectrex', 'Menu: Vectrex', [
('item', 'ROM browser (L)', "Lists the ROMs in the roms folder next to Vectrex_EMU.exe (subfolders "
         "too, but not the overlays folder). Up / Down choose, Left / Right page, Enter or a double click loads, Esc closes. The game "
         "keeps running behind it but gets no input."),
('item', 'Open ROM file...', "The Windows file dialog, for a ROM anywhere on your PC."),
('item', 'Eject cartridge (Ctrl+E)', "No cartridge: the Vectrex runs Mine Storm from its own ROM."),
('item', 'RESET button (Ctrl+R)', "The RESET button on the console. The RAM is kept and the BIOS restarts "
         "the cartridge without its intro, exactly as on the real machine. Some games use this: they "
         "keep their high scores through a RESET."),
('item', 'Volume (0 % = off)', "The volume knob. As on the Vectrex it is also the power switch: at 0 % it "
         "clicks off, turned up from 0 % it switches on (a cold start). While turning it crackles a little, "
         "like the worn potentiometers of most Vectrexes do."),
('item', 'Pause (P)', "Stops the Vectrex; the afterglow on the screen stands still too."),
('item', 'Controls...', "Keys and game controllers for both controller ports, the stick's dead zone and "
         "travel, and a test field that shows what the Vectrex gets."),
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
('item', 'Controller port 2', "The second joystick, or a speech accessory plugged in there: the VecVoice "
         "(Richard Hutchinson; a GI SP0256-AL2 speech chip) or the VecVox (Alex Herbert; a SpeakJet chip). "
         "The SpeakJet itself can not be emulated exactly (its workings are unknown), so the VecVox speaks "
         "with the SP0256's voice at the SpeakJet's pitch and lengths: songs keep their tune. Both need the "
         "SP0256-AL2 ROM, which is not included ((c) Microchip Technology Inc.): download al2.bin from Joe "
         "Zbiciak's page (spatula-city.org/~im14u2c/sp0256-al2/, shared with Microchip's permission) and put "
         "it in the bios folder next to the program as sp0256-al2.bin."),
('item', 'Start switched on', "Starts the Vectrex at once instead of waiting for you to turn the knob."),
('item', 'Skip the Vectrex intro (start screen)', "On switching on, the BIOS's start screen (the VECTREX "
         "logo, its tune, then the game's name) runs unseen in a fraction of a second and the cartridge "
         "starts straight away - in exactly the state it would have after the real intro."),
('more', "What the start screen does", "When a Vectrex is switched on, its 8 KB ROM (the 'Executive' or "
         "BIOS) clears the RAM, sets up the sound chip and the I/O chip, draws the growing VECTREX logo "
         "with its tune, then reads the cartridge's header - a copyright line, a tune and the game's name - "
         "and shows that title for a few seconds before it jumps into the game. A cartridge without a valid "
         "header gets Mine Storm instead. All together this takes about 13 seconds. The RESET button skips "
         "the logo too, but keeps the RAM."),
('item', 'Reset Vectrex settings to default', "Cartridge hardware Auto, port 2 the joystick, start switched "
         "off, with the intro. The controls keep theirs (the Controls window has its own reset), the volume "
         "knob stays."),
('item', 'Quit (Esc)', "Closes the emulator; the settings are kept for next time."),
]),

('display', 'Menu: Display', [
('item', 'Full screen (F11)', "The picture on the whole monitor at its full resolution (for example 4K)."),
('item', 'Bigger / Smaller / Start size', "The window size (+ / - / 0); the start size is as big as fits the "
         "desktop, above the task bar. In full screen: the picture size."),
('item', 'Real size (9)', "In the window and in full screen (until + / - / 0): as big as on a real Vectrex: the overlay 162 x 207 mm like the real sheets, worked out "
         "from the monitor's size and resolution."),
('item', 'Monitor size (for Real size)...', "The monitor's diagonal in inches, if it does not report its size "
         "(or reports it wrong): right click on the slider for the reported size. Check with a ruler: the "
         "overlay should be 162 mm wide and 207 mm high."),
('item', 'Rotate screen', "The whole screen turned in 90 degree steps - for a monitor turned on its side "
         "(tate). The overlay, the knob and the display turn with it."),
('item', 'On-screen display', "Below the picture: INTERNAL (the game's own frames per second), MONITOR "
         "(your screen's refresh), EMU RATE (green at 100 %; orange or red when the PC can not keep up), "
         "VOLUME, and SPEECH VOL when a speech module is plugged into port 2."),
('more', "Why games run at different frame rates", "A Vectrex has no picture memory: the CPU steers the "
         "beam along every line, every frame. Most games redraw 50 times a second, but a busy screen takes "
         "longer to draw, so the frame rate drops - and the picture flickers more. INTERNAL shows that "
         "rate: it is part of the game, not a fault of the emulator."),
('item', 'Volume knob and RESET button', "The Vectrex's panel in simple outlines below the picture: the "
         "knob (wheel or click to turn it) and the RESET button."),
('item', 'Cartridge', "A cartridge below the picture: click it for the ROM browser."),
('h', 'Screen'),
('item', 'Fixed 60 Hz', "The default: smooth and tear-free on any monitor."),
('item', "The monitor's full rate", "Every refresh of a 120, 144 or 240 Hz monitor: the afterglow fades in "
         "finer steps and moving things update more often."),
('item', 'G-Sync / FreeSync', "One screen picture per game frame, with no tearing - needs such a monitor. "
         "At 50 frames a second the mains wobble stands still, as on a real Vectrex."),
('item', 'Reset display settings to default', "The start size (no real size; the monitor size as it "
         "reports it), not turned, the on-screen display, the knob and the cartridge on, fixed 60 Hz."),
]),

('tube', 'Menu: Tube', [
('p', "The Tube menu is about the picture tube and the analog parts that drive it. Each effect is measured "
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
('item', 'Line gaps (analog delay)...', "The small gaps at line ends and corners: on a real Vectrex the beam "
         "stops moving a few cycles after the game switches it off, and how many differs from one machine to "
         "the next. 12.3 cycles (the default) is a well-tuned Vectrex; 9.3 nearly closes the corners, 15.3 "
         "gives the wide gaps of a worn one."),
('item', 'Curved tube', "The Vectrex's screen face is curved; this bends the picture a little, as you see "
         "it on the glass."),
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
('item', 'Brightness bending / type / strength', "A busy, bright picture grows and shifts a little."),
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

('overlay', 'Menu: Overlay', [
('p', "On a real Vectrex every game came with a printed plastic sheet in front of the screen: coloured, "
      "see-through parts tint the vectors behind them, the solid print gives borders, pictures and the "
      "game's name. The emulator shows the MiSTer core's overlay files (.art) and PNG images with "
      "transparency (an 846 x 1080 sheet): put them in roms/overlays (or overlays next to the program), "
      "named like the ROM - Armor Attack.bin finds Armor Attack.art. Mine Storm's overlay comes with the "
      "emulator (from the MiSTer project's overlays, Libretro artwork)."),
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
('item', 'Printed art brightness...', "The solid print (borders, pictures, the game's name) against the plastic; "
         "it follows the room light too, more gently than the plastic."),
('item', 'Scratches (decades of handling)', "A 40-year-old sheet is worn: millions of fine scratches make it "
         "slightly matte, and fewer, longer, deeper ones run across it, down to a single deep one. In the dark "
         "you see them only where the vectors light them up: a soft matte halo around the lines, and the "
         "scratches that run across the light glint, so they form little rings around bright spots. In room "
         "light the whole sheet looks more matte, in its own colours. On by default; made on the graphics "
         "card in a fraction of a second, each overlay with its own pattern."),
('item', 'Wear (fine scratches)...', "How worn the sheet is: the number and depth of the fine scratches."),
('item', 'Damage (larger, deeper scratches)...', "The number and depth of the larger scratches - from none to a "
         "well-used sheet."),
('item', 'Glint strength...', "How strongly the scratches catch the light."),
('item', 'New random scratches', "Another pattern of scratches (each overlay gets its own variation of it)."),
('item', 'Parallax (seen from the usual height)', "The sheet sits a little in front of the tube, so seen from "
         "the usual height the picture sits a little higher on it (as on MiSTer)."),
('item', 'Position and size (this overlay)', "Fine adjustment for the overlay of this game, kept for it."),
('item', 'Reset overlay settings to default', "On, chosen automatically, tint, room light, print, parallax "
         "and the scratches back to the defaults; this game's chosen overlay and this overlay's position and size "
         "too (other games keep theirs)."),
]),

('sound', 'Menu: Sound', [
('item', 'Vectrex buzz / Buzz volume', "The famous Vectrex buzz from the speaker."),
('more', 'Where the buzz comes from', "The deflection amplifiers that steer the beam draw large, fast "
         "currents. Some of that leaks into the audio amplifier, which sits on the same board and supply, "
         "so the speaker hums along with whatever is being drawn: louder and harsher with more and brighter "
         "lines, repeating at the game's frame rate. It is not the mains: it never stops, because even an "
         "empty screen is recalibrated every frame by the BIOS."),
('item', 'Vectrex speaker', "All sound through a model of the small built-in speaker: no deep bass, a "
         "little harsh at full volume."),
('item', 'Speech module (VecVoice / VecVox)', "The speech modules have their own amplifier and audio out, "
         "so their sound skips the Vectrex's knob, buzz and speaker. Volume: its own (Ctrl+Page Up / Ctrl+Page "
         "Down), 0 - 200 %; 100 % is the MiSTer core's Normal level. Full-range speakers: big speakers with an arcade cabinet's warm bass (most owners plug better "
         "speakers in). Small speaker: no deep bass, rounded treble - still better than the Vectrex's own."),
('item', 'Reset sound settings to default', "Back to the measured values - only this menu's settings "
         "(the volume knob stays)."),
]),

('help_controls', 'Controllers', [
('p', "The Vectrex controller has an analog stick and four buttons in a row. In Vectrex > Controls each "
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
         "3D imager (spinning-disc 3D glasses) and a light pen."),
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
         "bit, keeps high scores without a battery (Robot Arena)."),
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
('p', "The settings live in Vectrex_EMU.conf next to Vectrex_EMU.exe (or in %APPDATA%\\Vectrex_EMU "
      "when that folder can not be written)."),
]),
]
