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
scp -q -P 8022 $P/system/vectrex_emu/bios.bin $NOVA:$SD/BIOS/vectrex_emu/
# (the overlays: only those the Nova lacks - sync_nova.sh puts the desktop build's own there, which stay)
tar -C $P/system/vectrex_emu/overlays -cf - . | ssh -o BatchMode=yes -p 8022 $NOVA "tar -C $SD/BIOS/vectrex_emu/overlays -xf - --skip-old-files --no-same-owner --no-same-permissions 2>/dev/null || true"
ssh -o BatchMode=yes -p 8022 $NOVA "mkdir -p $SD/BIOS/vectrex_emu/gpu"
scp -q -P 8022 $P/system/vectrex_emu/gpu/* $NOVA:$SD/BIOS/vectrex_emu/gpu/
scp -q -P 8022 $P/vectrex_emu_libretro_android.so $P/vectrex_emu_libretro.info $P/INSTALL.txt $NOVA:$SD/Download/
# (the info file also where RetroArch can read it, once its Core Info folder is set there - its own folder is app-private:
#  the core list then shows "Vectrex_EMU" instead of the file's name)
ssh -o BatchMode=yes -p 8022 $NOVA "mkdir -p /storage/emulated/0/RetroArch/info"
scp -q -P 8022 $P/vectrex_emu_libretro.info $NOVA:/storage/emulated/0/RetroArch/info/
# the PiTrex and UVMC2 SD cards as folders (the core builds each card from its folder and copies the games' writes
# back): only when the Nova has none - the games' saves are in them
for d in ../package/uvmc2 ../build/Vectrex_EMU/pitrex; do
	[ -d "$d" ] || continue; b=$(basename "$d")
	if ssh -o BatchMode=yes -p 8022 $NOVA "test -d $SD/BIOS/vectrex_emu/$b"; then echo "$b: on the Nova already (its saves kept)"
	else echo "sending $b"; scp -q -r -P 8022 "$d" $NOVA:$SD/BIOS/vectrex_emu/; fi
done
# the speech ROM (VecVoice / VecVox games) from the desktop install, when there - the owner's own copy, not in the package
[ -f ../build/Vectrex_EMU/system/sp0256-al2.bin ] && scp -q -P 8022 ../build/Vectrex_EMU/system/sp0256-al2.bin $NOVA:$SD/BIOS/vectrex_emu/
[ -f ../build/Vectrex_EMU/pitrex/pitrex.img ] && scp -q -P 8022 ../build/Vectrex_EMU/pitrex/pitrex.img "$NOVA:$SD/ROMs/vectrex/PiTrex.img"
for g in ../package/roms/*.um2; do [ -f "$g" ] && scp -q -P 8022 "$g" "$NOVA:$SD/ROMs/vectrex/"; done
ssh -o BatchMode=yes -p 8022 $NOVA "ls -l $SD/Download/vectrex_emu_libretro_android.so; ls $SD/BIOS/vectrex_emu; ls $SD/BIOS/vectrex_emu/overlays | wc -l; ls $SD/ES-DE/custom_systems"
echo "on the Nova: RetroArch > Load Core > Install or Restore a Core > the SD card's Download > vectrex_emu_libretro_android.so"
