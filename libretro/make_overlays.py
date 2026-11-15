"""The desktop's overlays (MiSTer .art, PNG) as PNGs for the RetroArch core: each loaded as the desktop loads it
(vart.load_overlay: the 1080p plane, its soft edge) and cut to the overlay sheet (the centre 846 x 1080 of an .art
plane) - the core lays a PNG over the whole sheet. Named as the desktop's file: the core finds it by the game's name.

    python make_overlays.py [IN_FOLDER] [OUT_FOLDER]     (default: build/Vectrex_EMU/overlays -> dist/overlays_png)
Copyright (c) 2026 Hugo Bokhorst. Part of Vectrex_EMU; see LICENSE.md."""
import os, sys, struct, zlib
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'src', 'frontend'))
import vart

src = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, '..', 'build', 'Vectrex_EMU', 'overlays')
dst = sys.argv[2] if len(sys.argv) > 2 else os.path.join(HERE, 'dist', 'overlays_png')
os.makedirs(dst, exist_ok=True)

def png(path, w, h, rgba):
    rows = b''.join(b'\0' + rgba[y * w * 4:(y + 1) * w * 4] for y in range(h))
    def chunk(t, d): return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
    with open(path, 'wb') as f:
        f.write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 6, 0, 0, 0))
                + chunk(b'IDAT', zlib.compress(rows, 6)) + chunk(b'IEND', b''))

n = 0
for name in sorted(os.listdir(src)):
    if not name.lower().endswith(('.art', '.png')):
        continue
    try:
        w, h, px, sheet = vart.load_overlay(os.path.join(src, name))
    except (ValueError, OSError) as e:
        print('skipped', name, e); continue
    px = bytes(px)
    sw = int(round(sheet)); x0 = (w - sw) // 2                  # (the sheet: the centre columns)
    out = b''.join(px[(y * w + x0) * 4:(y * w + x0 + sw) * 4] for y in range(h))
    png(os.path.join(dst, os.path.splitext(name)[0] + '.png'), sw, h, out); n += 1
print(n, 'overlays ->', dst)
