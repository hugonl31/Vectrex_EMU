# Vectrex_EMU for Android

The same emulator on Android TV boxes (NVIDIA Shield), retro handhelds (Retroid, AYN Odin, Anbernic) and
phones with a controller: the desktop program's core, unchanged, and its picture and sound ported to the
graphics card and sound system of Android. Built to be started by a front end - HyperSpin, or any other that
starts an emulator with an Android intent - and to go straight back to it when you quit.

* [Install](#install) - [first start](#first-start) - [front ends (HyperSpin)](#front-ends-hyperspin) -
  [controls](#controls) - [settings](#settings)
* [What is ported](#what-is-ported) - [the renderer](#the-renderer) - [building](#building) -
  [testing](#testing)

## Install

Android 8.0 or later, OpenGL ES 3.1 (every Shield, every handheld of the last years); Vulkan is used where
the device has it. Install `Vectrex_EMU-android-release.apk` (from the build, see [Building](#building)):
copy it to the device and open it, or `adb install Vectrex_EMU-android-release.apk`. The BIOS (with Mine
Storm), the power switch's sounds and Mine Storm's overlay are inside the app.

## First start

Open **Vectrex_EMU** from the launcher (on a TV: the apps row). The settings screen:

* **Choose ROM folder** - the folder with your ROMs (subfolders included). The app does not ask for
  Android's broad *All files access*: you give it this one folder, once, through Android's own folder
  picker. A front end that passes a file path needs it (see below); one that passes a `content://` URI
  does not.
* **Choose overlay folder** (optional) - MiSTer `.art` overlays (the MiSTer Vectrex core's `overlays.zip`,
  unpacked) or PNGs, named like the ROMs, as on the desktop. An `overlays` folder inside the ROM folder,
  or overlays next to the ROMs, are found too.
* **Choose speech ROM** (optional) - `al2.bin`, the SP0256-AL2 ROM for the VecVoice / VecVox, a free
  download (see the main README's *Speech modules*); then set **Controller port 2** to the module.
* **Play Mine Storm** / **Play a ROM...** - to try it without a front end.

## Front ends (HyperSpin)

The game is started with an intent to the app's `LaunchActivity`:

| | |
|---|---|
| package | `io.github.hugonl31.vectrexemu` |
| activity | `io.github.hugonl31.vectrexemu.LaunchActivity` |
| action | `android.intent.action.VIEW` |
| data | the ROM: a path (`/storage/emulated/0/ROMs/Vectrex/Berzerk.bin`), `file://` or `content://` URI |

Instead of the data, the path may come in an extra named `ROM`, `rom`, `path` or `file` (and a few other
common names; any extra that ends in a ROM's name is tried too). Zipped ROMs are opened (the first ROM in
the zip). `.bin`, `.vec`, `.gam` and `.rom` as on the desktop. Without a ROM: Mine Storm.

A path is read directly where Android allows it; on Android 11 and later it is found in the ROM folder
granted in the settings - by its path, or else by its file name. If a game cannot be read, the screen
says why; Back returns to the front end.

From a computer, the same as a front end does:

```
adb shell am start -n io.github.hugonl31.vectrexemu/.LaunchActivity -a android.intent.action.VIEW -d "file:///sdcard/ROMs/Vectrex/Berzerk.bin"
adb shell "am start -n io.github.hugonl31.vectrexemu/.LaunchActivity --es ROM '/sdcard/ROMs/Vectrex/Scramble.zip'"
```

Every start is a fresh emulator: a game started while another one plays (or sits in the background)
ends that one first. **Back**, or **Select + Start** together, quits at once - the flash cartridges'
saves (Vectorblade's high scores) are written - and Android returns to the front end.

## Controls

| Vectrex | Controller | Keyboard (as the desktop) |
|---|---|---|
| stick | left stick (analog), D-pad | arrow keys |
| buttons 1-4 | X Y A B (Settings > Buttons 1-4: also A B X Y, Y X B A) | A W S D |
| quit | Back, or Select + Start | Esc |

The first controller that is used is player 1, the second player 2 (the second Vectrex port). A TV
remote's centre button is button 4 (fire, in most games).

## Settings

Everything in the desktop program's Tube, Overlay and Sound menus, with the same defaults and ranges (the
owner's choices, side by side with a real Vectrex) - the overlay's scratches (wear, wear type, scribbles,
glint strength and reach, **New random scratches**), the plastic glow, Even beam, the bending type - and:

* **Renderer** - Auto (Vulkan, else OpenGL ES 3.1), or one of them. If a device shows a broken picture
  with Vulkan, OpenGL ES is the way out.
* **Phosphor resolution** - the phosphor images at a lower resolution, for a slow graphics chip. The
  overlay's scratches and plastic glow cost a few extra passes; with both off the overlay costs next to
  nothing.
* **Screen** - landscape, or portrait: a phone or handheld held upright, the picture filling it (the
  desktop's screen turned 90 degrees). Some handhelds keep their screen landscape whatever an app asks.
* **Switch off when quitting** - off by default (Back returns at once); on: the tube's last zap or
  collapse and the switch's click, half a second, as on the desktop.
* **Games** - every game played lately: its overlay (automatic, another one, or none) and that overlay's
  position and size, as the desktop's Overlay menu keeps them per game and per overlay.

## What is ported

| | |
|---|---|
| the core | `src/core`, unchanged and complete: compiled into the app, the same per-frame step as `vtest --live` (`app/src/main/cpp/core_api.c`) - bank switching, the flash cartridges, the speech modules, the BIOS intro skip |
| the picture | the beam model, the phosphor in real time with the measured afterglow, its joining and overlap, bloom, the corner glow, the curved tube, the tone curve, the mains hum wobble, the brightness bending, Even beam, the tube warming up at power on and (optional) switching off |
| the overlay | MiSTer `.art` and PNG, tint, room light, print, parallax; forty years of scratches made on the graphics card (the same recipe and seeds as the desktop: an overlay gets the same scratches on both), their glint from the light around them, the damage cutting through the colour, the plastic glow; per game: which overlay, its position and size |
| the sound | the buzz that follows the picture, the built-in speaker, the speech module's amplifier and speakers, the power switch |

Not ported, on purpose: the control panel below the picture (volume knob, RESET, the ROM browser, the
on-screen display) - the front end is the menu here, and the log has the speed meter. Not yet:
per-controller mapping beyond the button layout and the dead zone.

## The renderer

`vecrender.py`'s passes, the same on Vulkan and OpenGL ES (`app/src/main/cpp`): each shader is written
once (`shaders/`) and compiled at build time to SPIR-V for Vulkan (the NDK's `glslc`) and kept as GLSL ES
source for OpenGL ES. Each display frame, with the numbers in `scene.cpp` straight from `vecrender.py`:

1. **fresh light** - the lines drawn since the last display frame, each a Gaussian capsule (one instance
   per line, the quad made in the vertex shader), light added, freshness kept as the maximum
2. **phosphor** - the phosphor image aged by the time passed and lit again (16-bit float, ping-pong)
3. **show** - what the phosphor shows now (afterglow overlap)
4. **bloom** - two separable blurs at quarter size
5. **screen** - curve, tone and bloom in one pass, then the overlay (multiply, then its own light)

With the overlay's scratches or plastic glow, step 5 draws into an image of the screen instead, and:

6. **the scratches** - once per overlay: ~200,000 scratches drawn as lines into three float textures,
   each made in the vertex shader from its index (no vertex data at all)
7. **their light** - the screen at quarter size blurred (the matte's halo), and at eighth size its angle
   harmonics (the glint: which scratches face the light around them)
8. **the sheet** - one pass onto the display: the screen, the overlay, the matte, the glint, the glow
   and the damage; the desktop needs four blended passes for it

Faster than a straight port:

* the phosphor images cover the picture (and a small margin), not the screen: on a 16:9 screen less than
  half the pixels are aged, shown and blurred
* the desktop's tone + bloom into a full-size image, then the curve: here one pass (the curve only moves
  where the picture is read) - one full-size image and pass less; likewise its quarter-size copy and the
  first blur
* the glint's harmonics at eighth size (the desktop: quarter) - its kernels are 16 and 48 screen pixels wide
  and the field is smooth: the same look for an eighth of the work
* every pass clears or overwrites its whole image (Vulkan: nothing is loaded from memory - cheap on the
  tiled graphics chips of phones and handhelds); all parameters are push constants; the line data goes
  into a persistently mapped buffer, two frames in flight; descriptor sets are made once
* Vulkan pre-rotation: on a handheld whose screen is a portrait panel held sideways, the picture is drawn
  turned into the display's own orientation, so Android's compositor does not turn it again
* the sound at the device's own rate (48 kHz mostly), stereo float, the core's 44.1 kHz resampled in the
  app: the format Android's low-latency path takes. (A device with a system-wide equalizer on its speaker,
  as the AYN Thor ships, gets the normal path all the same: switch the equalizer off for the lowest delay.)
* the display's refresh paces the picture (FIFO / swap interval 1); the emulation runs on its own thread,
  paced by real time and the sound queue as the desktop's, so 50 Hz games stay smooth on 60 Hz screens:
  every line still lights the phosphor at the moment the beam drew it

The log (`adb logcat -s VectrexEMU`) shows every 10 seconds the display and game frame rates, the
emulation speed and sound underruns.

## Building

Android Studio (open the `android` folder), or from the command line with JDK 17 and the Android SDK
(platform 35, NDK `29.0.14206865`, CMake `3.22.1`):

```
cd android
./gradlew assembleRelease        # app/build/outputs/apk/release/Vectrex_EMU-android-release.apk
```

The release APK is signed with the debug key (for sideloading); sign it with your own for a store.
The BIOS, the sounds and Mine Storm's overlay are copied from the repository's `bios`, `sounds` and
`overlays` folders at build time. `.github/workflows/android.yml` builds it on every push.

## Testing

* `tools/core_test.c` - the core library against the original core: the same input stream (both players,
  RESET, the line gap setting) through `vtest --live` and through `core_api.c` must give the same bytes,
  vectors and sound (`tools/make_test_input.py` writes the stream). The workflow runs it.
* Tested on the Android emulator (API 36, x86_64): Vulkan (with the Khronos validation layers, core and
  synchronization checks: clean, the scratches, the sheet, Even beam, crisp lines and switching off
  included) and OpenGL ES 3.1; launches by path, `file://` URI and extra, zipped ROMs, the ROM folder
  grant, a new game while one runs, Back, Home and back, portrait, per-game overlay settings.
* On an AYN Thor (Snapdragon 8 Gen 2, Adreno 740, Android 13, a portrait panel held sideways): Vulkan and
  OpenGL ES, the scratches made in 0.4 s at the start, 60 display fps, the game at full speed, no sound
  underruns; the picture pre-rotated by the app.
