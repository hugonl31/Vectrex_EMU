#!/bin/bash
# build_cart.sh <stem> <Name on the cartridge> : generate, assemble (lwasm in WSL), pack, copy to the emulator's roms and the Drive
set -e
cd /g/AI/Vectrex_EMU/testcart
python make_$1.py
MSYS_NO_PATHCONV=1 wsl -e bash -lc "cd /mnt/g/AI/Vectrex_EMU/testcart && ~/tools/lwtools-4.24/lwasm/lwasm --format=raw --list=$1.lst -o $1_raw.bin $1.asm && echo assembled"
python make_$1.py --pack
cp $1_64k.bin "../build/Vectrex_EMU/roms/Tests/$2.bin"; cp $1_64k.bin "/g/Google Drive/#Sharing/Vectrex/Testcarts/$2.bin"
