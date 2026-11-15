#!/bin/sh
# The newest Android package (dist/pkg, from package_android.sh) onto the owner's Retroid Pocket Nova over SSH (Termux's
# sshd, port 8022, this PC's key): the BIOS, overlays and sounds into RetroArch's system folder (BIOS on the SD card),
# ES-DE's custom system file into ES-DE's folder (on the SD card), the core and its info file into Download - RetroArch
# installs the core from there itself (Load Core > Install or Restore a Core: its cores folder is its own, no other
# app may write there). Run from Windows' Git Bash or WSL:  sh install_nova.sh
#   NOVA=user@host (default u0_a117@10.0.2.221), SD=/storage/<card> (default F929-FA97)
set -e
cd "$(dirname "$0")"
NOVA=${NOVA:-u0_a117@10.0.2.221}
SD=${SD:-/storage/F929-FA97}
P=dist/pkg
[ -f $P/vectrex_emu_libretro_android.so ] || { echo "no package: sh package_android.sh first"; exit 1; }
ssh -o BatchMode=yes -p 8022 $NOVA "mkdir -p $SD/BIOS/vectrex_emu/overlays $SD/ES-DE/custom_systems"
if ssh -o BatchMode=yes -p 8022 $NOVA "test -s $SD/ES-DE/custom_systems/es_systems.xml && ! grep -q vectrex_emu_libretro $SD/ES-DE/custom_systems/es_systems.xml"; then
	echo "ES-DE/custom_systems/es_systems.xml exists with other systems: not replaced - copy the vectrex <system> in by hand"
else
	scp -q -P 8022 $P/ES-DE/custom_systems/es_systems.xml $NOVA:$SD/ES-DE/custom_systems/
fi
scp -q -P 8022 $P/system/vectrex_emu/bios.bin $P/system/vectrex_emu/on.wav $P/system/vectrex_emu/off.wav $NOVA:$SD/BIOS/vectrex_emu/
scp -q -P 8022 $P/system/vectrex_emu/overlays/*.png $NOVA:$SD/BIOS/vectrex_emu/overlays/
scp -q -P 8022 $P/vectrex_emu_libretro_android.so $P/vectrex_emu_libretro.info $P/INSTALL.txt $NOVA:/storage/emulated/0/Download/
ssh -o BatchMode=yes -p 8022 $NOVA "ls -l /storage/emulated/0/Download/vectrex_emu_libretro_android.so; ls $SD/BIOS/vectrex_emu; ls $SD/BIOS/vectrex_emu/overlays | wc -l; ls $SD/ES-DE/custom_systems"
echo "on the Nova: RetroArch > Load Core > Install or Restore a Core > Download > vectrex_emu_libretro_android.so"
