# UVMC2 movie player (`movies.um2`)

A Ultimate Vectrex Multicart 2 program for Vectrex_EMU that plays the Vectrex vector movies made for the PiTrex /
VecFever (BadApple.bin, LaLinea.bin, BlaBla.bin, ...) from the cartridge's SD card. Shipped as
`package/roms/UVMC2 Movie Player.um2`. Written from scratch (no UVMC2 SDK code); tested in Vectrex_EMU only.

**Using it:** put the movies in the folder `movies` on the SD card (in the emulator: `sd/movies` next to the program) and
start "UVMC2 Movie Player" from the ROM browser. It shows each movie's name, plays them one after another (in a loop);
button 4 skips to the next. The movies are not included - they belong to their makers.

**Sound:** a file with the movie's name and `.pcm` next to it (raw 16-bit mono, 44100 Hz, starting with the movie)
is the good sound. By default it goes through the Vectrex's own speaker - at 22050 Hz through its DAC (two samples
averaged, 8 bits with a little dither, each at its time from the movie's start) - so it plays on any Vectrex, no
headphones needed. **Button 3** switches it to the UVMC2's headphone jack (its PT8211, 16 bits) and back. Without a
`.pcm` the movie's own sound plays (8 bits, ~11 kHz, as the original player did). For Bad Apple, from a copy of the
video:

    ffmpeg -i badapple.mp4 -t 219.0667 -ac 1 -ar 44100 -f s16le BadApple.pcm

(the movie's sound was made from that recording: it lines up from 0 s, checked to 2 ms over the whole length).
Measured in Vectrex_EMU: the speaker sound matches the recording at 0.95 correlation (the original's ~0.2), the jack's
at 0.98; picture and sound within ~20 ms of each other, no drift.

**The movie format** (from the 6809 VIDEOPLAYER that plays them): 1536-byte chunks, one a frame at 30 frames a second.
Bytes 0-1023 vector commands, 1024-1535 sound samples for the DAC ($A5: none). A command byte: `$80` the beam to the
middle; `$20` the end of the frame; else the low nibble n = n beam moves by one (dy, dx) - [count][dy][dx] then count
line pairs (0 = 256) - or, n = 0, [count] then count + 1 line pairs (dy, dx); the high nibble `$10` more commands
follow, `$F0` the frame goes on in the next chunk, else the frame ends. Lines: a 24-cycle ramp each.

**How it works:** the RP2350 holds the 6809 in /HALT and drives the bus itself, one access per E cycle, writing the VIA
as the 6809 player did. Frames are timed by the chip's microsecond timer. The SD card: SPI0 (GP34-36, CS GP39), its own
small FAT32 reader (long names; with or without a partition table). The jack: a PIO program on PIO2 (GP43-45) clocking
32-bit stereo words at 44100 Hz, its FIFO fed between bus accesses from a ring the SD card fills.

Build: `python make_um2.py` (needs `arm-none-eabi-gcc` in WSL).

License: MIT - free to use, change and share, with this notice. (c) 2026 Hugo Bokhorst.
