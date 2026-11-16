#!/bin/sh
# The Android package: the core, its info file, system/vectrex_emu (the BIOS, the overlays as PNG, the power sounds),
# ES-DE's custom system file and the install notes -> dist/Vectrex_EMU_RetroArch_Android.zip. The overlay PNGs come
# from OVERLAYS (default: the repository's own, as the desktop release has them - MiSTer .art, the core reads them).
set -e
cd "$(dirname "$0")"
OVERLAYS=${OVERLAYS:-../overlays}
sh build_libretro.sh android
rm -rf dist/pkg; mkdir -p dist/pkg/system/vectrex_emu/overlays
cp vectrex_emu_libretro_android.so vectrex_emu_libretro.info dist/pkg/
cp ../system/bios.bin dist/pkg/system/vectrex_emu/   # (the switch-on / off sounds are built into the core)
cp "$OVERLAYS"/*.art "$OVERLAYS"/CREDITS.md dist/pkg/system/vectrex_emu/overlays/ 2>/dev/null; cp "$OVERLAYS"/*.png dist/pkg/system/vectrex_emu/overlays/ 2>/dev/null || true
mkdir -p dist/pkg/system/vectrex_emu/gpu; cp gpu/case_scratch.png gpu/glass_light.bin dist/pkg/system/vectrex_emu/gpu/   # (the GPU path: make_gpu_assets.py)
# what the desktop release has as well (its package folder): Last Human's overlay, the UVMC2's SD card folder (its
# config, Bad Apple - Malban's, with his permission), the included games (Last Human, its UVMC2 edition, the UVMC2 demo
# and movie player) and the BIOS / speech notes
cp ../package/overlays/*.png dist/pkg/system/vectrex_emu/overlays/ 2>/dev/null || true
cp -r ../package/uvmc2 dist/pkg/system/vectrex_emu/
cp ../package/system/README-speech.txt dist/pkg/system/vectrex_emu/   # (README.txt: the desktop's - ffmpeg, settings.json)
mkdir -p dist/pkg/roms; cp ../package/roms/*.bin ../package/roms/*.um2 dist/pkg/roms/   # (its README: the desktop's ROM browser and folders)
cp -r android/ES-DE dist/pkg/
cp -r retroarch_config dist/pkg/config   # (this core: Max Users 2, ports 1 and 2 the Vectrex controller)
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
	# the PiTrex and UVMC2 SD cards as folders (the core builds the card from them, as the desktop): the release's UVMC2
	# folder, the desktop install's PiTrex folder; the PiTrex loader and the UVMC2 games - in cards/, not in the zip
	mkdir -p "$SHARE/cards/system/vectrex_emu" "$SHARE/cards/ROMs"
	rm -f "$SHARE/cards/system/vectrex_emu/pitrex_sd.img" "$SHARE/cards/system/vectrex_emu/uvmc2_sd.img"
	cp -ru ../package/uvmc2 "$SHARE/cards/system/vectrex_emu/"
	[ -d ../build/Vectrex_EMU/pitrex ] && cp -ru ../build/Vectrex_EMU/pitrex "$SHARE/cards/system/vectrex_emu/"
	[ -f ../build/Vectrex_EMU/pitrex/pitrex.img ] && cp -u ../build/Vectrex_EMU/pitrex/pitrex.img "$SHARE/cards/ROMs/PiTrex.img"
	cp -u ../package/roms/*.um2 "$SHARE/cards/ROMs/" 2>/dev/null || true
fi
# the Retroid Pocket Nova itself (the owner, 2026-11-15): the newest core in the SD card's Download folder - Windows'
# scp (this PC's key, Termux's sshd); the Nova off or away: skipped (NOVA_DL= empty: skipped)
NOVA=${NOVA:-u0_a117@10.0.2.221}
NOVA_DL=${NOVA_DL-/storage/F929-FA97/Download}
SCP=/mnt/c/Windows/System32/OpenSSH/scp.exe; command -v "$SCP" >/dev/null 2>&1 || SCP=scp
if [ -n "$NOVA_DL" ]; then
	if "$SCP" -q -o BatchMode=yes -o ConnectTimeout=8 -P 8022 dist/pkg/vectrex_emu_libretro_android.so dist/pkg/vectrex_emu_libretro.info "$NOVA:$NOVA_DL/" 2>/dev/null
	then echo "copied to the Nova: $NOVA_DL"; else echo "the Nova not reached: not copied there"; fi
fi
