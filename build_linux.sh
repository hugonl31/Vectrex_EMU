#!/bin/sh
# Build dist-linux/Vectrex_EMU (one file) on Linux: the core with cc, the program with PyInstaller
# (python3 -m pip install pyinstaller pysdl2 pysdl2-dll; Python with Tk). The BIOS is not built in:
# it goes in a bios folder next to the program (bios/bios.bin).
set -e
cd "$(dirname "$0")"
PY=${PY:-python3}
mkdir -p core build
cc -O2 -w -fgnu89-inline -o core/vectrex_core src/core/vtest.c src/core/vecx.c src/core/e6809.c src/core/e8910.c -lm
"$PY" -m PyInstaller --noconfirm --onefile --windowed --name Vectrex_EMU --distpath dist-linux \
  --workpath build/pyi-linux --specpath build --paths src/frontend --collect-all sdl2dll --collect-submodules sdl2 \
  --add-binary "$PWD/core/vectrex_core:core" --add-data "$PWD/sounds/on.wav:sounds" \
  --add-data "$PWD/sounds/off.wav:sounds" src/frontend/vectrex_emu.py
echo built dist-linux/Vectrex_EMU
