# UVMC2 demo (`demo.um2`)

A small Ultimate Vectrex Multicart 2 program for Vectrex_EMU: a rotating 3D cube computed on the cartridge's
RP2350 and the title "UVMC2 DEMO", drawn on the Vectrex by the chip itself (it holds the 6809 in /HALT and drives
the bus one access per E cycle, writing the VIA as the BIOS's line routines do). Shipped as
`package/roms/UVMC2 Demo.um2`, so a new install shows a UVMC2 game working out of the box.

Written from scratch - no UVMC2 SDK code - in plain C for the RP2350 (bare metal, no libraries).
Tested in Vectrex_EMU; not yet on a real UVMC2.

Build: `python make_um2.py` (needs `arm-none-eabi-gcc` in WSL): makes `sintab.h`, `demo.elf`, `demo.bin` and
`demo.um2` (a 20-byte header - "2CMU", version 1, 1, load address 0x20000000, length in words - then the RAM
image, which starts with the ARM vector table).

License: MIT - free to use, change and share, with this notice. (c) 2026 Hugo Bokhorst.
