#!/bin/sh
# The local desktop build's files onto the owner's Retroid Pocket Nova, so both run the same (Termux's sshd, this PC's
# key): the ROMs (and their test cartridges), the overlays (.art / .png - the core reads both), the PiTrex and UVMC2 SD
# card folders, the saves (flash .sav, Last Human's .ds2431) - from build/Vectrex_EMU. The PC is the master: its files
# replace the Nova's of the same name; nothing on the Nova is deleted. What was there is kept in one backup first.
# Run from Windows' Git Bash or WSL:  sh sync_nova.sh
#   NOVA=user@host (default u0_a117@10.0.2.221), SD=/storage/<card> (default F929-FA97)
set -e
cd "$(dirname "$0")"
NOVA=${NOVA:-u0_a117@10.0.2.221}
SD=${SD:-/storage/F929-FA97}
B=../build/Vectrex_EMU
V=$SD/BIOS/vectrex_emu                          # (RetroArch's system folder on the Nova: BIOS)
RA=/storage/emulated/0/RetroArch/saves/Vectrex_EMU   # (its saves, sorted by core)
X="ssh -o BatchMode=yes -p 8022 $NOVA"
put () {   # put <local folder> <Nova folder> [tar options]: the folder's contents streamed in (new times: newer than any card)
	src=$1; dst=$2; shift 2
	$X "mkdir -p \"$dst\""
	tar -C "$src" "$@" -cf - . | $X "tar -C \"$dst\" -xmf - --no-same-owner --no-same-permissions 2>/dev/null || true"
	echo "$src -> $dst"
}
# a backup of the Nova's card folders and saves (the games' own writes): one, replaced each time
$X "mkdir -p $V/backup && tar -C $V -cf $V/backup/before_sync.tar uvmc2 pitrex 2>/dev/null; tar -C $RA -cf $V/backup/before_sync_saves.tar --exclude=vectrex_emu_cards . 2>/dev/null; true"
# the overlays: the Nova's earlier PNG set (from EmuVR) aside once - the desktop's .art / .png from now on
$X "cd $V && if [ -d overlays ] && [ ! -d overlays_emuvr_old ]; then mv overlays overlays_emuvr_old; fi; mkdir -p overlays"
put "$B/roms" "$SD/ROMs/vectrex" --exclude='*.sav' --exclude='*.ds2431' --exclude='README.txt'
put "$B/overlays" "$V/overlays"
put "$B/pitrex" "$V/pitrex"
put "$B/uvmc2" "$V/uvmc2"
# the saves: next to the ROMs on the PC, in RetroArch's saves folder on the Nova
$X "mkdir -p $RA"
( cd "$B/roms" && ls *.sav *.ds2431 2>/dev/null ) | while read f; do scp -q -P 8022 "$B/roms/$f" "$NOVA:$RA/"; echo "save $f -> $RA"; done
# the cards built from the old folders: gone, made again from these at the next game
$X "rm -f $RA/vectrex_emu_cards/*.img $RA/vectrex_emu_cards/*.list"
$X "ls $SD/ROMs/vectrex | wc -l; ls $V/overlays | wc -l; ls $RA"
