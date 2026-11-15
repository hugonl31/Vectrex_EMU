#!/bin/sh
# The Android package: the core, its info file, system/vectrex_emu (the BIOS, the overlays as PNG, the power sounds),
# ES-DE's custom system file and the install notes -> dist/Vectrex_EMU_RetroArch_Android.zip. The overlay PNGs come
# from OVERLAYS (default: the EmuVR project's PNG set); they are not in the repository.
set -e
cd "$(dirname "$0")"
OVERLAYS=${OVERLAYS:-../../Vectrex_EMU_EmuVR/overlays}
sh build_libretro.sh android
rm -rf dist/pkg; mkdir -p dist/pkg/system/vectrex_emu/overlays
cp vectrex_emu_libretro_android.so vectrex_emu_libretro.info dist/pkg/
cp ../system/bios.bin ../sounds/on.wav ../sounds/off.wav dist/pkg/system/vectrex_emu/
cp "$OVERLAYS"/*.png dist/pkg/system/vectrex_emu/overlays/
cp -r android/ES-DE dist/pkg/
cp android/INSTALL.txt dist/pkg/
rm -f dist/Vectrex_EMU_RetroArch_Android.zip
(cd dist/pkg && python3 -c "
import os, zipfile
z = zipfile.ZipFile('../Vectrex_EMU_RetroArch_Android.zip', 'w', zipfile.ZIP_DEFLATED)
for d, _, fs in os.walk('.'):
    for f in fs:
        z.write(os.path.join(d, f), os.path.relpath(os.path.join(d, f), '.'))
")
ls -l dist/Vectrex_EMU_RetroArch_Android.zip
