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
mkdir -p dist/pkg/system/vectrex_emu/gpu; cp gpu/case_scratch.png gpu/glass_light.bin dist/pkg/system/vectrex_emu/gpu/   # (the GPU path: make_gpu_assets.py)
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
# the owner's shared folder (Google Drive, for the Retroid Pocket Nova): the newest build there, unpacked - each file
# copied as it is to its place on the device (SHARE= empty: skipped)
SHARE=${SHARE-"/mnt/g/Google Drive/#Sharing/Vectrex/Android_Core"}
if [ -n "$SHARE" ] && [ -d "$SHARE" ]; then
	rm -rf "$SHARE/system" "$SHARE/ES-DE"
	cp -r dist/pkg/. "$SHARE/"
	date '+built %Y-%m-%d %H:%M' > "$SHARE/BUILD.txt"
	git log -1 --format='commit %h %s' >> "$SHARE/BUILD.txt" 2>/dev/null || true
	echo "copied to $SHARE"
	# the PiTrex and UVMC2 SD cards (FAT images the desktop Vectrex_EMU builds in its cache folder, 285 MB each), the
	# PiTrex loader and the UVMC2 games: in cards/, only when changed (not in the zip)
	mkdir -p "$SHARE/cards/system/vectrex_emu" "$SHARE/cards/ROMs"
	for f in ../cache/pitrex_sd.img ../cache/uvmc2_sd.img; do [ -f "$f" ] && cp -u "$f" "$SHARE/cards/system/vectrex_emu/"; done
	[ -f ../build/Vectrex_EMU/pitrex/pitrex.img ] && cp -u ../build/Vectrex_EMU/pitrex/pitrex.img "$SHARE/cards/ROMs/PiTrex.img"
	cp -u ../package/roms/*.um2 "$SHARE/cards/ROMs/" 2>/dev/null || true
fi
