#!/bin/sh
# the save state's size, a save / load test and a netplay rollback test (netsim) for some games - Linux build, WSL
cd "$(dirname "$0")"
mkdir -p /tmp/vxns/vectrex_emu; cp -n ../../system/bios.bin /tmp/vxns/vectrex_emu/ 2>/dev/null
R=../../build/Vectrex_EMU/roms
for g in "Space Wars.bin" "Scramble.bin" "Vectorblade.bin" "Last Human.bin" "Armor Attack.bin"; do
	echo "== $g"
	VXPOC_STATELOG=1 VXH_SAVEDIR=/tmp/vxns ./vxhost ../vectrex_emu_libretro.so /tmp/vxns "$R/$g" state 2>&1 | grep -i "at most\|^state"
	VXH_SAVEDIR=/tmp/vxns ./vxhost ../vectrex_emu_libretro.so /tmp/vxns "$R/$g" netsim 600 6 2>/dev/null | grep netsim
done
