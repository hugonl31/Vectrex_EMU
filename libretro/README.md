# Vectrex_EMU libretro core

Vectrex_EMU's emulator (`src/core`, unchanged) as a libretro core for RetroArch, with the CPU port of the desktop's
tube look (beam, phosphor trail, glow, tube geometry, overlays, buzz and speaker). One source, two flavours:

- **RetroArch** (default): Android arm64 now; the same code builds for Linux. 60 Hz, the overlay painted in.
- **EmuVR** (`-DVX_EMUVR`): the VR build, as `Vectrex_EMU_EmuVR/poc_core` (90 Hz, the overlay drawn by the mod).

Plan and background: `Vectrex_EMU_EmuVR/libretro_port_plan.md`.

## Build (WSL / Linux, with the Android NDK r27c in `~/android-ndk-r27c`)

```sh
sh build_libretro.sh android     # vectrex_emu_libretro_android.so (arm64-v8a, Android 9+)
sh build_libretro.sh linux       # vectrex_emu_libretro.so (x86-64 test build)
sh package_android.sh            # dist/Vectrex_EMU_RetroArch_Android.zip (core, info, BIOS, overlays, ES-DE, INSTALL.txt)
sh build_libretro.sh native      # this system's clang: Linux x86-64 / arm64 (.so), macOS universal (.dylib)
build_libretro_win.bat           # Windows x64 with Visual C++ (vectrex_emu_libretro.dll, no OpenMP: no runtime DLLs)
python package_retroarch.py <system> <core> <zip>   # a zip as RetroArch's folders (cores, info, system) + INSTALL.txt
```

The release (GitHub Actions, `.github/workflows/build.yml`, job `retroarch`) builds and packs all five.

## Test without RetroArch

`bench/vxhost.c` is a minimal front end (`gcc -O2 -o bench/vxhost bench/vxhost.c -ldl`):

```sh
bench/vxhost ./vectrex_emu_libretro.so <system dir> <game> bench 600        # ms per frame
bench/vxhost ./vectrex_emu_libretro.so <system dir> <game> shot 400 out.ppm # a picture
bench/vxhost ./vectrex_emu_libretro.so <system dir> <game> state            # save states (run-ahead) exact?
```

The system dir holds `vectrex_emu/bios.bin` (and `overlays/*.png`, `on.wav`, `off.wav`).

## Save states

The emulator's files are compiled with their data in sections of their own (`src/core/state/state_*.c`): Visual C++
`data_seg` / `bss_seg`, clang `#pragma clang section` (ELF sections `vxd` / `vxb`, bounded by `__start_` / `__stop_`).
6809 games have save states (run-ahead, rewind, netplay): each variable by name in one order (`vars_*.h`, made by
`tools/gen_state_vars.py`), so a state is the same on Windows, Android and Linux. UVMC2 / PiTrex games have none.

## Netplay

- 6809 games: RetroArch's own netplay. For the sticks, the host sets Port 1 and Port 2 to **Vectrex controller**
  (Settings > Input > Max Users 2 first; Quick Menu > Controls > Port N Controls > Device Type; save the core remap).
- UVMC2 / PiTrex games: lockstep, as the desktop's online play (`netls.c`): load the game through RetroArch, not the
  ROM browser; it starts again when player 2 joins. Both play in the slower side's UVMC2 mode; the SD card holds only
  the files both have alike.
- Both sides need the same core and the same game file.

## Optional files (in `system/vectrex_emu`)

- `sp0256-al2.bin` - the speech chip's ROM, for VecVoice / VecVox games
- `uvmc2/`, `pitrex/` - the UVMC2's and PiTrex's SD card folders

## Files

- `libretro_vx.c` - the core (from the EmuVR core v0.36, made portable 2026-11-15)
- `libretro.h`, `stb_image.h`, `stb_truetype.h` - third-party headers (libretro API: MIT; stb: public domain / MIT)
- `genart.c`, `genart_data.h`, `genart_titles.txt` - the library's cartridge and box for a game with no picture (its title
  in the blue pill), the cartridge with the player's own label (`thumbs/labels`); `genart_data.h` is made by `tools/make_genart_h.py`, titles set by hand go in `genart_titles.txt`;
  the font: `fonts/ArchivoBlack-Regular.ttf` (SIL Open Font License 1.1, `fonts/OFL.txt`)
- `tubemap_tab.h`, `glass_cut_lut.h` - the measured tube and the glass's edge (generated tables)
- `vectrex_emu_libretro.info` - RetroArch's core info
- `netls.c` - UVMC2 / PiTrex netplay (lockstep); `vars_vx.h`, `tools/gen_state_vars.py` - the portable save state
- `android/` - ES-DE's custom system file and the install notes for the package
