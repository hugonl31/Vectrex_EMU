#!/bin/sh
# Vectrex_EMU for Android - the app (no Gradle): the core and its host (jni/vxa.c) with the NDK (in WSL), the Java side
# with javac + d8, the package with aapt2, zipalign and apksigner (the Android SDK's build-tools). Run from Git Bash:
#   sh android/build.sh            -> android/out/Vectrex_EMU.apk  (arm64-v8a for phones and handhelds, x86_64 for the emulator)
# Paths: ANDROID_SDK (default C:/Android/sdk), JDK (C:/Android/jdk/...), the NDK in WSL (~/android-ndk-r27c). The debug
# key and the private test build's token live outside the repository (C:/Android/vxapp_private): never committed.
# Copyright (c) 2026 Hugo Bokhorst. Part of Vectrex_EMU; see LICENSE.md.
set -e
cd "$(dirname "$0")"
SDK=${ANDROID_SDK:-/c/Android/sdk}; BT=$SDK/build-tools/34.0.0; JAR=$SDK/platforms/android-34/android.jar
JDK=${JDK:-/c/Android/jdk/jdk-17.0.21+9}; PRIV=${PRIV:-/c/Android/vxapp_private}
VER=$(cat ../VERSION | tr -d 'v\r\n'); CODE=${CODE:-$(date +%y%m%d%H | sed 's/^0*//')}
O=out; rm -rf $O/classes $O/dex $O/apk; [ -n "$SKIP_NATIVE" ] || rm -f $O/obj/*/libvxa.so; mkdir -p $O/classes $O/dex $O/apk/lib/arm64-v8a $O/apk/lib/x86_64 $O/res $O/assets
# --- the native library: the core + the host, for both ABIs (WSL: the NDK's clang)
if [ -z "$SKIP_NATIVE" ]; then
WP=$(wsl -e wslpath -a "$(pwd -W)")
wsl -e bash -lc "set -e; cd '$WP'; NDK=\${ANDROID_NDK:-\$HOME/android-ndk-r27c}; BIN=\$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin; C=../src/core; L=../libretro
SRC=\"\$L/libretro_vx.c \$C/state/state_vecx.c \$C/state/state_e6809.c \$C/state/state_e8910.c \$C/state/state_core.c \$C/cm33.c \$C/rp2350.c \$C/host.c \$C/arm11.c \$C/usbhost.c\"
FL=\"-O3 -fPIC -w -ffp-contract=off -fgnu89-inline -fvisibility=hidden -I\$C -I\$L -DVX_RETROARCH -DVX_APP_ANDROID\"
for a in aarch64:arm64-v8a x86_64:x86_64; do t=\${a%%:*}; d=\${a#*:}; mkdir -p out/obj/\$d
  ( \$BIN/\${t}-linux-android28-clang \${FL/-fgnu89-inline/} -c -o out/obj/\$d/vxa.o jni/vxa.c && \$BIN/\${t}-linux-android28-clang \$FL -fopenmp -static-openmp -shared -o out/obj/\$d/libvxa.so \$SRC out/obj/\$d/vxa.o -lEGL -lGLESv3 -laaudio -landroid -llog -lz -lm -ldl -Wl,-z,max-page-size=16384 ) &
done; wait
# (the app's game process without its screen, for tests on a device nobody holds: jni/vxcon.c - Termux, detached)
\$BIN/aarch64-linux-android28-clang \${FL/-fgnu89-inline/} -c -o out/obj/vxcon.o jni/vxcon.c && \$BIN/aarch64-linux-android28-clang \$FL -fopenmp -static-openmp -pie -o out/vxcon \$SRC out/obj/vxcon.o -lEGL -lGLESv3 -landroid -llog -lz -lm -ldl && \$BIN/llvm-strip out/vxcon
for d in arm64-v8a x86_64; do test out/obj/\$d/libvxa.so -nt jni/vxa.c || { echo 'the native build failed'; exit 1; }; cp out/obj/\$d/libvxa.so out/apk/lib/\$d/; \$BIN/llvm-strip --strip-unneeded out/apk/lib/\$d/libvxa.so; done"
else
cp $O/obj/arm64-v8a/libvxa.so $O/apk/lib/arm64-v8a/ && cp $O/obj/x86_64/libvxa.so $O/apk/lib/x86_64/
fi
# --- the Java side
"$JDK/bin/javac" -nowarn -source 8 -target 8 -bootclasspath "$(cygpath -w "$JAR");$(cygpath -w "$BT/core-lambda-stubs.jar")" -encoding UTF-8 -d $O/classes src/com/hugob/vectrexemu/*.java 2>&1 | grep -v "^warning\|^Note\|bootstrap\|^1 warning" || true
test -s $O/classes/com/hugob/vectrexemu/Main.class
JAVA_HOME="$JDK" PATH="$JDK/bin:$PATH" "$BT/d8.bat" --release --min-api 28 --lib "$JAR" --output $O/dex $(find $O/classes -name '*.class')
# --- the resources (the icon), the assets (the games list's shipped pictures; the private build's token, when there), the package
mkdir -p $O/res/mipmap-xxxhdpi; python -c "
from PIL import Image; im = Image.open('../icons/ios_icon_1024.png').convert('RGBA'); im.resize((192, 192), Image.LANCZOS).save('$O/res/mipmap-xxxhdpi/ic_launcher.png')"
rm -f $O/assets/token.txt; [ -s "$PRIV/tok.txt" ] && cp "$PRIV/tok.txt" $O/assets/token.txt || true
rm -rf $O/assets/shipped   # (the shipped pictures: added below, not through aapt2 - its Windows build wrote "\" in their zip names)
"$BT/aapt2.exe" compile -o $O/res.zip --dir $O/res
"$BT/aapt2.exe" link -o $O/base.apk -I "$JAR" --manifest AndroidManifest.xml --min-sdk-version 28 --target-sdk-version 34 --version-code $CODE --version-name "$VER" -A $O/assets $O/res.zip
python - "$O" ../package/system/vectrex_emu/thumbs/shipped <<'EOF'
import sys, zipfile, os, shutil
o, ship = sys.argv[1], sys.argv[2]; src = os.path.join (o, 'base.apk'); dst = os.path.join (o, 'unsigned.apk')
shutil.copy (src, dst)
with zipfile.ZipFile (dst, 'a', zipfile.ZIP_DEFLATED) as z:
    z.write (os.path.join (o, 'dex', 'classes.dex'), 'classes.dex')
    for d in ('arm64-v8a', 'x86_64'):
        z.write (os.path.join (o, 'apk', 'lib', d, 'libvxa.so'), 'lib/%s/libvxa.so' % d)
    # the games list's pictures that come with the app (assets/shipped: Simon Quincey's box art, with his permission -
    # tools/make_shipped_art.py), stored: the app reads them whole (Library.java)
    for r, ds, fs in os.walk (ship):
        ds.sort ()
        for f in sorted (fs):
            q = os.path.join (r, f); z.write (q, 'assets/shipped/' + os.path.relpath (q, ship).replace (os.sep, '/'), zipfile.ZIP_STORED)
EOF
"$BT/zipalign.exe" -f -p 4 $O/unsigned.apk $O/aligned.apk
KS=$PRIV/debug.keystore
[ -s "$KS" ] || "$JDK/bin/keytool.exe" -genkeypair -keystore "$(cygpath -w "$KS")" -storepass android -keypass android -alias vx -keyalg RSA -keysize 2048 -validity 10000 -dname "CN=Vectrex_EMU debug" >/dev/null
JAVA_HOME="$JDK" PATH="$JDK/bin:$PATH" "$BT/apksigner.bat" sign --ks "$(cygpath -w "$KS")" --ks-pass pass:android --key-pass pass:android --out $O/Vectrex_EMU.apk $O/aligned.apk
ls -l $O/Vectrex_EMU.apk; [ -s $O/assets/token.txt ] && echo "(with the private build's token)" || echo "(no token: the files already on the device)"
