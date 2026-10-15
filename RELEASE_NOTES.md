# Vectrex_EMU release notes

The newest version is at the top. `VERSION` holds the version every push to GitHub publishes: the
build workflow replaces that release (title = the version, marked Latest) with the new build.

## v1.0.0

The first release for Windows, macOS and Linux. Downloads: the Mac app (Intel + Apple Silicon) and the
Linux program, each with the Vectrex BIOS in its `bios` folder; Windows: build `Vectrex_EMU.exe` with
`build_exe.bat` (see README.md). Put your ROMs in a `roms` folder next to the program. The PNG and log
files are the build's own checks.

- macOS: the picture at a Retina screen's full resolution; Command shortcuts (⌘O, ⌘E, ⌘R, ⌘P, ⌘+ / ⌘− /
  ⌘0, ⌃⌘F full screen, ⌘Q); runs from Gatekeeper's hidden copy of a downloaded app; 8-bit images on
  Apple's software renderer
- Linux: OpenGL through GLX (X11 / XWayland), controllers and sound through SDL2
- Windows: `build_exe.bat` builds the core and installs PyInstaller when missing
