#!/bin/sh
# Vectrex_EMU's libretro core with clang (the Android NDK's): for RetroArch on Android (arm64), and the same source for
# Linux x86-64 (a test build: the bench and the save-state test run it on this PC, under WSL).
#   sh build_libretro.sh android      -> vectrex_emu_libretro_android.so   (arm64-v8a, Android 9+ / API 28)
#   sh build_libretro.sh linux        -> vectrex_emu_libretro.so           (x86-64, glibc; links with the system's gcc)
#   sh build_libretro.sh ios          -> vectrex_emu_libretro_ios.dylib    (iPhone / iPad arm64, on a Mac: Xcode's clang)
# NDK: $ANDROID_NDK (default ~/android-ndk-r27c). Copyright (c) 2026 Hugo Bokhorst. Part of Vectrex_EMU; see LICENSE.md.
set -e
cd "$(dirname "$0")"
NDK=${ANDROID_NDK:-$HOME/android-ndk-r27c}
BIN=$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin
C=../src/core
SRC="libretro_vx.c $C/state/state_vecx.c $C/state/state_e6809.c $C/state/state_e8910.c $C/state/state_core.c
     $C/cm33.c $C/rp2350.c $C/host.c $C/arm11.c $C/usbhost.c"
# (as the desktop core: exact floating point - no fused multiply-add, the same results as the Windows build)
FLAGS="-O3 -fPIC -w -ffp-contract=off -fgnu89-inline -fvisibility=hidden -I$C -I. -DVX_RETROARCH"
EXPORTS="-Wl,--version-script=exports.map"
mkdir -p obj
case "${1:-android}" in
android)
	CC="$BIN/aarch64-linux-android28-clang"
	OUT=vectrex_emu_libretro_android.so
	# (OpenMP: the drawing in bands on the big cores; libomp linked in statically, the core pinned - see retro_init)
	$CC $FLAGS -fopenmp -static-openmp -shared -o $OUT $SRC $EXPORTS -lm -llog -ldl -Wl,-z,max-page-size=16384
	cp $OUT obj/vectrex_emu_libretro_android.sym.so   # (with its symbols: a crash log's offsets to function names, llvm-addr2line)
	$BIN/llvm-strip --strip-unneeded $OUT
	;;
linux)
	OUT=vectrex_emu_libretro.so
	for f in $SRC; do o=obj/$(basename $f .c).o; $BIN/clang --target=x86_64-linux-gnu $FLAGS ${OMP:+-fopenmp} -c $f -o $o; done
	gcc -shared -o $OUT obj/*.o $EXPORTS -lm -ldl -lpthread ${OMP:+-lgomp}
	;;
ios)
	# (iPhone / iPad, arm64, iOS 13+: RetroArch for iOS takes it as a framework - .github/workflows/ios.yml. iOS gives an
	#  app no executable memory: the UVMC2's and the PiTrex's JITs out, their interpreters run - measured 2026-11-17 on
	#  the PC: VecDoom 68 % of a frame, a PiTrex arcade game 98 %; no OpenMP, as the macOS core)
	SDK=$(xcrun --sdk iphoneos --show-sdk-path)
	OUT=vectrex_emu_libretro_ios.dylib
	${CC:-clang} $FLAGS -target arm64-apple-ios13.0 -isysroot "$SDK" -DCM33_NO_JIT -DARM11_NO_JIT -dynamiclib \
		-install_name @rpath/$OUT -o $OUT $SRC -Wl,-exported_symbol,_retro_* -lm
	;;
native)
	# (the release cores: this system's own clang - Linux x86-64 / arm64 (Raspberry Pi 4 / 5, 64-bit), macOS (Intel and
	#  Apple Silicon in one); no OpenMP, so the core needs nothing the system does not have)
	CC=${CC:-clang}
	if [ "$(uname)" = Darwin ]; then
		OUT=vectrex_emu_libretro.dylib
		$CC $FLAGS -arch arm64 -arch x86_64 -mmacosx-version-min=10.13 -dynamiclib -o $OUT $SRC -Wl,-exported_symbol,_retro_* -lm
	else
		OUT=vectrex_emu_libretro.so
		$CC $FLAGS -shared -o $OUT $SRC $EXPORTS -lm -ldl -lpthread
		strip --strip-unneeded $OUT 2>/dev/null || true
	fi
	;;
esac
ls -l $OUT
