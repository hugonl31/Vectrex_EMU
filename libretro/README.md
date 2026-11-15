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
```

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
6809 games have save states (run-ahead, rewind); UVMC2 / PiTrex games have none.

## Files

- `libretro_vx.c` - the core (from the EmuVR core v0.36, made portable 2026-11-15)
- `libretro.h`, `stb_image.h` - third-party headers (libretro API: MIT; stb_image: public domain / MIT)
- `tubemap_tab.h`, `glass_cut_lut.h` - the measured tube and the glass's edge (generated tables)
- `vectrex_emu_libretro.info` - RetroArch's core info
- `android/` - ES-DE's custom system file and the install notes for the package
