#!/bin/sh
# Build dist-mac/Vectrex_EMU.app on macOS for Intel and Apple Silicon (universal2): the core with clang
# for both, the program with PyInstaller from the python.org universal2 Python
# (python3 -m pip install pyinstaller pyobjc-framework-Cocoa pysdl2 pysdl2-dll). The BIOS is not built
# in: it goes in a system folder next to the app (system/bios.bin).
set -e
cd "$(dirname "$0")"
PY=${PY:-python3}
mkdir -p core build
# SDL2 (controllers, sound) from pysdl2-dll, as build/sdllib/libSDL2.dylib: its own folder in the app (the
# pysdl2-dll package's SDL2 folder would clash with the sdl2 package on the Mac's case-insensitive disk)
mkdir -p build/sdllib
cp "$("$PY" -c "import sdl2dll, os; print(os.path.join(sdl2dll.get_dllpath(), 'SDL2.framework', 'Versions', 'A', 'SDL2'))")" build/sdllib/libSDL2.dylib
clang -O2 -w -fgnu89-inline -arch arm64 -arch x86_64 -mmacosx-version-min=10.13 -o core/vectrex_core \
  src/core/vtest.c src/core/vecx.c src/core/e6809.c src/core/e8910.c src/core/cm33.c src/core/rp2350.c src/core/host.c src/core/arm11.c src/core/usbhost.c -lm -pthread
"$PY" -m PyInstaller --noconfirm --windowed --target-arch universal2 --name Vectrex_EMU \
  --osx-bundle-identifier io.github.hugonl31.vectrexemu --distpath dist-mac --workpath build/pyi-mac \
  --specpath build --paths src/frontend --exclude-module sdl2dll --exclude-module sdl2.test --exclude-module sdl2.examples --add-binary "$PWD/build/sdllib/libSDL2.dylib:sdllib" \
  --add-binary "$PWD/core/vectrex_core:core" --add-data "$PWD/sounds/on.wav:sounds" \
  --add-data "$PWD/sounds/off.wav:sounds" --add-data "$PWD/icons/vectrex_emu.png:icons" src/frontend/vectrex_emu.py
echo built dist-mac/Vectrex_EMU.app
