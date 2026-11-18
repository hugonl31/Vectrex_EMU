"""package_retroarch.py - a RetroArch core package (zip) for one system, laid out as RetroArch's own folders:
  cores/<the core>                 info/vectrex_emu_libretro.info
  system/vectrex_emu/              the BIOS, the overlays, the GPU path's pictures, the UVMC2's SD card folder, notes
  roms/                            Last Human, its UVMC2 edition, the UVMC2 demo and movie player
  INSTALL.txt
Android: the core and the info file at the top (RetroArch installs a core from a file there), ES-DE's system file.
  python package_retroarch.py <system name> <core file> <zip>      e.g.  windows-x64 vectrex_emu_libretro.dll out.zip
Copyright (c) 2026 Hugo Bokhorst. Part of Vectrex_EMU; see LICENSE.md."""
import glob, os, shutil, sys, zipfile

H = os.path.dirname(os.path.abspath(__file__)); R = os.path.dirname(H)
name, core, out = sys.argv[1], sys.argv[2], sys.argv[3]
android = name.startswith('android')
tmp = os.path.join(H, 'dist', 'pkg_' + name)
shutil.rmtree(tmp, ignore_errors=True)
sv = os.path.join(tmp, 'system', 'vectrex_emu')
os.makedirs(os.path.join(sv, 'overlays')); os.makedirs(os.path.join(sv, 'gpu')); os.makedirs(os.path.join(tmp, 'roms'))
cdir = tmp if android else os.path.join(tmp, 'cores'); idir = tmp if android else os.path.join(tmp, 'info')
os.makedirs(cdir, exist_ok=True); os.makedirs(idir, exist_ok=True)
shutil.copy(core, cdir); shutil.copy(os.path.join(H, 'vectrex_emu_libretro.info'), idir)
shutil.copy(os.path.join(R, 'system', 'bios.bin'), sv)
for f in glob.glob(os.path.join(R, 'overlays', '*.art')) + glob.glob(os.path.join(R, 'overlays', '*.png')) + \
         glob.glob(os.path.join(R, 'overlays', 'CREDITS.md')) + glob.glob(os.path.join(R, 'package', 'overlays', '*.png')):
    shutil.copy(f, os.path.join(sv, 'overlays'))
for f in ('case_scratch.png', 'glass_light.bin'): shutil.copy(os.path.join(H, 'gpu', f), os.path.join(sv, 'gpu'))
shutil.copytree(os.path.join(R, 'package', 'uvmc2'), os.path.join(sv, 'uvmc2'))
shutil.copy(os.path.join(R, 'package', 'system', 'README-speech.txt'), sv)
# (RetroArch's config folder: this core's override - Max Users 2, RetroArch's routine notices off (the override and
#  remap loaded, a controller set up, the display's rate) - and its remap - ports 1 and 2 the Vectrex controller,
#  the RetroPad with its sticks: netplay sends a port's sticks only with that type)
shutil.copytree(os.path.join(H, 'retroarch_config'), os.path.join(tmp, 'config'))
for f in glob.glob(os.path.join(R, 'package', 'roms', '*.bin')) + glob.glob(os.path.join(R, 'package', 'roms', '*.um2')):
    shutil.copy(f, os.path.join(tmp, 'roms'))
if android:
    shutil.copytree(os.path.join(H, 'android', 'ES-DE'), os.path.join(tmp, 'ES-DE'))
    shutil.copy(os.path.join(H, 'android', 'INSTALL.txt'), tmp)
else:
    shutil.copy(os.path.join(H, 'INSTALL-RetroArch.txt'), os.path.join(tmp, 'INSTALL.txt'))
if os.path.exists(out): os.remove(out)
with zipfile.ZipFile(out, 'w', zipfile.ZIP_DEFLATED) as z:
    for d, _, fs in os.walk(tmp):
        for f in fs: z.write(os.path.join(d, f), os.path.relpath(os.path.join(d, f), tmp))
print(out, os.path.getsize(out), 'bytes')
