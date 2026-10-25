#!/bin/sh
# Build dist-linux/Vectrex_EMU (one file) on Linux: the core with cc, the program with PyInstaller
# (python3 -m pip install pyinstaller pysdl2 pysdl2-dll; Python with Tk). The BIOS is not built in:
# it goes in a system folder next to the program (system/bios.bin).
set -e
cd "$(dirname "$0")"
PY=${PY:-python3}
mkdir -p core build
cc -O2 -w -ffp-contract=off -fgnu89-inline -o core/vectrex_core src/core/vtest.c src/core/vecx.c src/core/e6809.c src/core/e8910.c src/core/cm33.c src/core/rp2350.c src/core/host.c src/core/arm11.c src/core/usbhost.c -lm -pthread
"$PY" -m PyInstaller.utils.cliutils.makespec --onefile --windowed --name Vectrex_EMU \
  --specpath build --paths src/frontend --collect-all sdl2dll --exclude-module sdl2.test --exclude-module sdl2.examples \
  --add-binary "$PWD/core/vectrex_core:core" --add-data "$PWD/sounds/on.wav:sounds" \
  --add-data "$PWD/sounds/off.wav:sounds" --add-data "$PWD/icons/vectrex_emu.png:icons" src/frontend/vectrex_emu.py
"$PY" tools/linux_spec.py build/Vectrex_EMU.spec        # (the system's libstdc++ / X / GL: GitHub issue 3)
"$PY" -m PyInstaller --noconfirm --distpath dist-linux --workpath build/pyi-linux build/Vectrex_EMU.spec
echo built dist-linux/Vectrex_EMU
