#!/bin/sh
# Build dist-mac/Vectrex_EMU.app on macOS for Intel and Apple Silicon (universal2): the core with clang
# for both, the program with PyInstaller from the python.org universal2 Python
# (python3 -m pip install pyinstaller pyobjc-framework-Cocoa pysdl2 pysdl2-dll). The BIOS is not built
# in: it goes in a bios folder next to the app (bios/bios.bin).
set -e
cd "$(dirname "$0")"
PY=${PY:-python3}
mkdir -p core build
clang -O2 -w -fgnu89-inline -arch arm64 -arch x86_64 -mmacosx-version-min=10.13 -o core/vectrex_core \
  src/core/vtest.c src/core/vecx.c src/core/e6809.c src/core/e8910.c -lm
"$PY" -m PyInstaller --noconfirm --windowed --target-arch universal2 --name Vectrex_EMU \
  --osx-bundle-identifier io.github.hugonl31.vectrexemu --distpath dist-mac --workpath build/pyi-mac \
  --specpath build --paths src/frontend --collect-all sdl2dll --collect-submodules sdl2 \
  --add-binary "$PWD/core/vectrex_core:core" --add-data "$PWD/sounds/on.wav:sounds" \
  --add-data "$PWD/sounds/off.wav:sounds" src/frontend/vectrex_emu.py
echo built dist-mac/Vectrex_EMU.app
