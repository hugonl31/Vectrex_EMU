"""ios_pack.py - the content pack the owner's private iOS test app downloads at its first start (and again when the pack
changes): everything the PC has, laid out as the core's folders - system/vectrex_emu (BIOS, the GPU path's pictures,
the overlays, the UVMC2's SD card folder with Doom, Quake, the movies and Last Human, the PiTrex's SD card files with the
owner's calibration, the speech ROM) and roms (every game). The app stays a few MB, each new build quick; the pack sits
on the project's server behind the log token (vxreport.py GET /iosextra/pack, /iosextra/packver).
  python ios_pack.py <out folder>   -> <out>/pack.tgz, <out>/pack.ver
Copyright (c) 2026 Hugo Bokhorst. Part of Vectrex_EMU; see LICENSE.md."""
import os, sys, tarfile, time

R = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PC = os.path.join(R, 'build', 'Vectrex_EMU')                  # (the PC app as the owner runs it)
PKG = os.path.join(R, 'libretro', 'dist', 'pkg', 'system', 'vectrex_emu')   # (the core's own files, as package_retroarch makes them)
SKIP = ('printf.log', 'settings.json')

out = sys.argv[1]
os.makedirs(out, exist_ok=True)
parts = [(os.path.join(PKG, 'bios.bin'), 'system/vectrex_emu/bios.bin'),
         (os.path.join(PKG, 'gpu'), 'system/vectrex_emu/gpu'),
         (os.path.join(PKG, 'overlays'), 'system/vectrex_emu/overlays'),
         (os.path.join(PC, 'uvmc2'), 'system/vectrex_emu/uvmc2'),
         (os.path.join(PC, 'pitrex'), 'system/vectrex_emu/pitrex'),
         (os.path.join(PC, 'system', 'sp0256-al2.bin'), 'system/vectrex_emu/sp0256-al2.bin'),
         (os.path.join(PC, 'roms'), 'roms')]
n = 0


def keep(ti):
    global n
    if os.path.basename(ti.name) in SKIP:
        return None
    if ti.isfile():
        n += 1
    ti.uid = ti.gid = 0; ti.uname = ti.gname = ''
    return ti


ver = time.strftime('%Y%m%d-%H%M')
with tarfile.open(os.path.join(out, 'pack.tgz'), 'w:gz', compresslevel=6) as t:
    for src, arc in parts:
        if not os.path.exists(src):
            sys.exit('missing: ' + src)
        t.add(src, arc, filter=keep)
    v = os.path.join(out, 'pack_version.txt'); open(v, 'w').write(ver + '\n')
    t.add(v, 'system/vectrex_emu/pack_version.txt')
open(os.path.join(out, 'pack.ver'), 'w').write(ver + '\n')
print('pack %s: %d files, %.1f MB' % (ver, n, os.path.getsize(os.path.join(out, 'pack.tgz')) / 1e6))
