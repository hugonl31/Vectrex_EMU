"""ios_pack.py - the content pack the owner's private iOS test app downloads at its first start (and again when the pack
changes): everything the PC has, laid out as the core's folders - system/vectrex_emu (BIOS, the GPU path's pictures,
the overlays, the UVMC2's SD card folder with Doom, Quake, the movies and Last Human, the PiTrex's SD card files with the
owner's calibration, the speech ROM) and roms (every game). The app stays a few MB, each new build quick; the pack sits
on the project's server behind the log token (vxreport.py GET /iosextra/pack, /iosextra/packver).
  python ios_pack.py <out folder>   -> <out>/pack.tgz, <out>/pack.ver
Copyright (c) 2026 Hugo Bokhorst. Part of Vectrex_EMU; see LICENSE.md."""
import os, shutil, sys, tarfile, time

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
# the game library's tiles: each overlay as a small picture (system/vectrex_emu/thumbs/<game>.png, 324 x 414 - the
# overlay's 162:207, sharp at 2x on an iPhone's tile); the .art overlays read as the PC app does (src/frontend/vart.py)
sys.path.insert(0, os.path.join(R, 'src', 'frontend'))
import vart                                                     # noqa: E402
from PIL import Image                                           # noqa: E402
th = os.path.join(out, 'thumbs'); shutil.rmtree(th, ignore_errors=True); os.makedirs(th, exist_ok=True)   # (fresh: none left from before)
nt = 0
for f in sorted(os.listdir(os.path.join(PKG, 'overlays'))):
    stem, ext = os.path.splitext(f)
    try:
        if ext.lower() == '.art':
            w, h, px = vart.load(os.path.join(PKG, 'overlays', f)); im = Image.frombytes('RGBA', (w, h), px)
        elif ext.lower() == '.png':
            im = Image.open(os.path.join(PKG, 'overlays', f)).convert('RGBA')
        else:
            continue
    except Exception as e:                                      # noqa: BLE001
        print('thumb skipped:', f, e); continue
    bg = Image.new('RGBA', im.size, (5, 8, 10, 255)); bg.alpha_composite(im)   # (on the app's black: the sheet's clear parts dark)
    bg.convert('RGB').resize((324, 414), Image.LANCZOS).save(os.path.join(th, stem + '.png'), optimize=True)
    nt += 1
print('thumbs:', nt)
# the library's other two looks (the owner's choice in the app): each game's cartridge - the real cartridge seen from
# above (original gce), else the owner's reconstructed label (print 4x\labels) on that real shell - and
# its box front (the box-art pack there); named after the ROMs, exact names first, then the closest
import re                                                       # noqa: E402
from PIL import ImageDraw, ImageFilter                          # noqa: E402
LBL = 'G:/AI/Vectrex_Labels'
LAB, BOX = LBL + '/print 4x/labels', LBL + '/original gce/Vectrex 2D Front Cover Box Art - v1.3 Full'


def key(s):
    s = re.sub(r'\s*4x$', '', s); s = re.sub(r'\(.*?\)', '', s); s = s.lower().replace('minestorm', 'mine storm')
    return re.sub(r'[^a-z0-9]', '', s)


def find(d, n):
    return d.get(key(n))                                        # (exact names only: "Berzerk" took a homebrew's box)


GCE = LBL + '/original gce'                                     # (the real cartridges seen from above, label and all)


def shrink(im, W=420):
    """the cartridge at the tile's size, a clean cutout: only the cartridge on a clear background - the edge's half-clear
    pixels in the shell's dark colour (the sources left a light grey line along the bottom), the faintest ones clear"""
    import numpy as np
    src = np.asarray(im.convert('RGBA')).copy()
    # (the sources' own leftover: light rows / columns along the outer edge - a scan's cut, not the dark shell)
    def light_line(px):
        op = px[px[:, 3] > 128]; return len(op) > 0 and op[:, :3].mean() > 100
    for _ in range(12):
        if light_line(src[-1]): src = src[:-1]
        else: break
    for _ in range(12):
        if light_line(src[0]): src = src[1:]
        else: break
    for _ in range(12):
        if light_line(src[:, 0]): src = src[:, 1:]
        else: break
    for _ in range(12):
        if light_line(src[:, -1]): src = src[:, :-1]
        else: break
    # (and any light pixel within 10 px of the clear surround: the shell's rim is dark all round, the label well inside)
    from PIL import ImageFilter as _F
    clear = Image.fromarray(((src[:, :, 3] < 128) * 255).astype(np.uint8), 'L').filter(_F.MaxFilter(21))
    ring = np.asarray(clear) > 0
    lightpx = src[:, :, :3].mean(2) > 95
    src[ring & lightpx] = 0
    im = Image.fromarray(np.ascontiguousarray(src), 'RGBA')
    a = np.asarray(im.resize((W, int(W * im.height / im.width)), Image.LANCZOS)).copy()
    al = a[:, :, 3]; semi = (al > 0) & (al < 250)
    a[semi, :3] = np.minimum(a[semi, :3], (40, 42, 46))
    a[al < 48] = 0
    return Image.fromarray(a, 'RGBA')


SHELL = None                                                    # (the real shell, its label's place: for a label alone)


def cart(label):
    global SHELL
    if SHELL is None:
        sh = Image.open(os.path.join(GCE, 'Armor..Attack (World).png')).convert('RGBA'); w, h = sh.size; px = sh.load()
        def light(x, y):
            r, g, b, a = px[x, y]; return a > 200 and r > 150 and g > 150 and b > 150   # (the clear surround is white too)
        y0 = int(h * 0.35)                                      # (the label: the rows and columns mostly light grey)
        rows = [y for y in range(y0, h) if sum(light(x, y) for x in range(0, w, 3)) > w / 3 * 0.6]
        cols = [x for x in range(w) if sum(light(x, y) for y in range(rows[0], rows[-1], 3)) > (rows[-1] - rows[0]) / 3 * 0.6]
        SHELL = (sh, (cols[0], rows[0], cols[-1], rows[-1]))
    sh, (x0, y0, x1, y1) = SHELL; im = sh.copy()
    lab = Image.open(label).convert('RGBA').resize((x1 - x0 + 1, y1 - y0 + 1), Image.LANCZOS); im.paste(lab, (x0, y0))
    return shrink(im)


if os.path.isdir(LAB) and os.path.isdir(BOX):
    labs = {key(os.path.splitext(f)[0]): os.path.join(LAB, f) for f in os.listdir(LAB) if f.lower().endswith('.png')}
    boxes = {}
    for f in os.listdir(BOX):
        if not f.lower().endswith('.png'):
            continue
        st = os.path.splitext(f)[0]; up = '_upscayl' in st; k = key(st.split('_upscayl')[0])
        if k not in boxes or up:
            boxes[k] = os.path.join(BOX, f)
    gce = {}
    for f in os.listdir(GCE):
        if not f.lower().endswith('.png') or f.startswith('Robot'):
            continue
        st = os.path.splitext(f)[0]; up = '_upscayl' in st; st = st.split('_upscayl')[0]
        for k in {key(st), key(st.split(' - ')[0])}:          # (AnimAction - Advanced Animation: AnimAction)
            if k not in gce or up:
                gce[k] = os.path.join(GCE, f)
    tc, tb = os.path.join(th, 'carts'), os.path.join(th, 'boxes'); os.makedirs(tc, exist_ok=True); os.makedirs(tb, exist_ok=True)
    nc = nb = 0
    for f in sorted(os.listdir(os.path.join(PC, 'roms'))):
        stem, ext = os.path.splitext(f)
        if ext.lower() not in ('.bin', '.vec', '.um2', '.gam'):
            continue
        gp, lp, bp = find(gce, stem), find(labs, stem), find(boxes, stem)
        if gp:
            shrink(Image.open(gp).convert('RGBA')).save(os.path.join(tc, stem + '.png'), optimize=True); nc += 1
        elif lp:
            cart(lp).save(os.path.join(tc, stem + '.png'), optimize=True); nc += 1
        if bp:
            b = Image.open(bp).convert('RGB'); b.resize((330, int(330 * b.height / b.width)), Image.LANCZOS).save(os.path.join(tb, stem + '.png'), optimize=True); nb += 1
    print('cartridges:', nc, 'boxes:', nb)
else:
    print('labels / box art not found: none in the pack')
parts.append((th, 'system/vectrex_emu/thumbs'))
with tarfile.open(os.path.join(out, 'pack.tgz'), 'w:gz', compresslevel=6) as t:
    for src, arc in parts:
        if not os.path.exists(src):
            sys.exit('missing: ' + src)
        t.add(src, arc, filter=keep)
    v = os.path.join(out, 'pack_version.txt'); open(v, 'w').write(ver + '\n')
    t.add(v, 'system/vectrex_emu/pack_version.txt')
open(os.path.join(out, 'pack.ver'), 'w').write('%s\n%d\n' % (ver, os.path.getsize(os.path.join(out, 'pack.tgz'))))   # (the version, then the size: the app asks before it fetches)
print('pack %s: %d files, %.1f MB' % (ver, n, os.path.getsize(os.path.join(out, 'pack.tgz')) / 1e6))
