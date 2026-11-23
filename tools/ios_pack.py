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
    bb = im.getchannel('A').point(lambda a: 255 if a > 8 else 0).getbbox()   # (the sheet itself: the .art keeps it in the middle
    if bb: im = im.crop(bb)                                     #  of a 1360 x 1080 canvas - squeezed whole, the tile showed it half as wide)
    bg = Image.new('RGBA', im.size, (5, 8, 10, 255)); bg.alpha_composite(im)   # (on the app's black: the sheet's clear parts dark)
    os.makedirs(os.path.join(th, 'overlays'), exist_ok=True)
    bg.convert('RGB').resize((324, 414), Image.LANCZOS).save(os.path.join(th, 'overlays', stem + '.png'), optimize=True)
    nt += 1
print('thumbs:', nt)
# the library's other two looks (the owner's choice in the app): each game's cartridge and its box front (the box-art
# pack there), named after the ROMs. The cartridge, on the real shell, the first there is of: the box art's title made
# into a label (G:\AI\Vectrex_Artwork\boxart_labels: the label project's way - silver grid, shared furniture, legal box
# - from the box front; the owner, 2026-11-23: better than the old ones); the owner's label template (print 4x\labels:
# Last Human, no box art); the real cartridge seen from above (original gce); the cartridge every platform draws
# (libretro/genart.c: the game's title in the blue pill)
import re                                                       # noqa: E402
from PIL import ImageDraw, ImageFilter                          # noqa: E402
LBL = 'G:/AI/Vectrex_Artwork'                                    # (was Vectrex_Labels: the folder renamed)
LAB, BOX = LBL + '/print 4x/labels', LBL + '/original gce/Vectrex 2D Front Cover Box Art - v1.3 Full'
BAL = LBL + '/boxart_labels/labels-png'                          # (<box front's name>.png, 3120 x 1700)
ALIAS = {'hyperchaseautorace': 'Hyperchase', 'mrbostoncleansweep': 'Clean Sweep Mr Boston',   # (a ROM's key -> the box
         'minestormiii': 'Mine Storm v3', 'lunarlander': 'Lunar Lander demo', 'zzap': 'Zzap Strikes Back',  # front's
         'testcartridgerev4': 'Vectrex Test Cartridge Rev.4', 'blitzactionfootball': 'Blitz Action Football (US)'}  # name)


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
    return Image.fromarray(dark_edge(a), 'RGBA')


def dark_edge(a):
    """the cartridge's outline in the shell's dark plastic, no light line round it on a dark background (the owner,
    2026-11-23): the half-clear pixels and the first four opaque rings capped 14 / 20 / 28 / 36 / 44 (the plastic ~40),
    the half-clear ones premultiplied - dark whether the picture is blended (the phones, RetroArch) or not (the PC)"""
    import numpy as np
    from PIL import ImageFilter as _F
    al = a[:, :, 3].astype(np.float32); rgb = a[:, :, :3].astype(np.float32)
    inside = al >= 128; pad = 5
    ring = np.full(al.shape, 99, np.int32); ring[(al > 0) & ~inside] = 0
    cur = Image.fromarray(np.pad(inside, pad).astype(np.uint8) * 255, 'L')
    for i in (1, 2, 3, 4):
        er = cur.filter(_F.MinFilter(3)); r = (np.asarray(cur) > 0) & ~(np.asarray(er) > 0)
        ring[r[pad:-pad, pad:-pad]] = i; cur = er
    mx = rgb.max(2)
    for i, cap in ((0, 14.0), (1, 20.0), (2, 28.0), (3, 36.0), (4, 44.0)):
        s = ring == i; rgb[s] *= np.minimum(1.0, cap / np.maximum(mx[s], 1.0))[:, None]
    semi = (al > 0) & (al < 255); rgb[semi] *= (al[semi] / 255.0)[:, None]
    a = a.copy(); a[:, :, :3] = np.clip(rgb + 0.5, 0, 255).astype(np.uint8)
    return a


SHELL = None                                                    # (the real shell, its label's place: for a label alone)
LAB_ASPECT = 3120 / 1700                                        # (every label of print 4x\labels)


def shell_box(sh):
    """the label's place on the shell: the rows and columns mostly light grey - not the light scan line along the
    shell's bottom (Armor..Attack rows 773-776: taken as label, every label ran over the bottom plastic to the edge)"""
    import numpy as np
    w, h = sh.size; px = sh.load()
    def light(x, y):
        r, g, b, a = px[x, y]; return a > 200 and r > 150 and g > 150 and b > 150   # (the clear surround is white too)
    y0 = int(h * 0.35)
    rows = [y for y in range(y0, h) if sum(light(x, y) for x in range(0, w, 3)) > w / 3 * 0.6]
    al = np.asarray(sh)[:, :, 3]; yb = max(y for y in range(h) if (al[y] > 128).mean() > 0.5)   # (the shell's last row)
    runs = []
    for y in rows:
        if runs and y == runs[-1][1] + 1: runs[-1][1] = y
        else: runs.append([y, y])
    rows = [y for r0, r1 in runs if yb - r1 > 12 for y in range(r0, r1 + 1)]
    cols = [x for x in range(w) if sum(light(x, y) for y in range(rows[0], rows[-1], 3)) > (rows[-1] - rows[0]) / 3 * 0.6]
    return cols[0], rows[0], cols[-1], rows[-1]                 # (Armor..Attack: 39, 338, 769, 727)


def cart(label):
    global SHELL
    if SHELL is None:
        sh = Image.open(os.path.join(GCE, 'Armor..Attack (World).png')).convert('RGBA')
        SHELL = (sh, shell_box(sh))
    sh, (x0, y0, x1, y1) = SHELL; im = sh.copy()
    lw = x1 - x0 + 1; lh = int(round(lw / LAB_ASPECT))           # (the label at its own aspect, top-aligned: as Robot Arena, the owner's)
    lab = (label if isinstance(label, Image.Image) else Image.open(label)).convert('RGBA').resize((lw, lh), Image.LANCZOS); im.paste(lab, (x0, y0))
    return shrink(im)


def drawn_cart(name):
    """a cartridge for a game with no label of its own: the one every platform draws (libretro/genart.c - the blue pill with
    its title on our own label, no GCE), through the PC's native library (src/frontend/native: build_win.bat); None
    without it - the apps then draw it themselves"""
    sys.path.insert(0, os.path.join(R, 'src', 'frontend'))
    import genart
    r = genart.make('carts', name)
    return Image.frombytes('RGBA', (r[0], r[1]), r[2]) if r else None


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
    bal = {}                                                    # (the box-art labels, by the box front's key)
    if os.path.isdir(BAL):
        bal = {key(os.path.splitext(f)[0]): os.path.join(BAL, f) for f in os.listdir(BAL) if f.lower().endswith('.png')}
    tc, tb = os.path.join(th, 'carts'), os.path.join(th, 'boxes'); os.makedirs(tc, exist_ok=True); os.makedirs(tb, exist_ok=True)
    nc = nb = 0; srcs = {}
    for f in sorted(os.listdir(os.path.join(PC, 'roms'))):
        stem, ext = os.path.splitext(f)
        if ext.lower() not in ('.bin', '.vec', '.um2', '.gam'):
            continue
        k = key(stem)
        gp, lp, bp = find(gce, stem), find(labs, stem), find(boxes, stem)
        ap = bal.get(key(ALIAS[k])) if k in ALIAS else bal.get(k)
        if ap: im, srcs[stem] = cart(ap), 'box-art label ' + os.path.basename(ap)
        elif lp: im, srcs[stem] = cart(lp), 'label template ' + os.path.basename(lp)
        elif gp: im, srcs[stem] = shrink(Image.open(gp).convert('RGBA')), 'GCE photo ' + os.path.basename(gp)
        else: im, srcs[stem] = drawn_cart(stem), 'drawn (genart.c)'
        if im is not None: im.save(os.path.join(tc, stem + '.png'), optimize=True); nc += 1
        if bp:
            b = Image.open(bp).convert('RGB'); b.resize((390, int(390 * b.height / b.width)), Image.LANCZOS).save(os.path.join(tb, stem + '.png'), optimize=True); nb += 1
    print('cartridges:', nc, 'boxes:', nb)
    import json as _json                                        # (each cartridge's source, beside the pack: not in it)
    _json.dump(srcs, open(os.path.join(out, 'carts_sources.json'), 'w', encoding='utf-8'), indent=1)
else:
    print('labels / box art not found: none in the pack')
# the pack in parts, each chosen in the app (the core's own and the games always): each part's version is its contents'
# hash, so a part that did not change is never fetched again. Out: pack-<id>.tgz per part, pack.json (the parts, their
# names, sizes and versions); the old single pack.tgz / pack.ver too, for the apps before this
import hashlib, json                                            # noqa: E402
J = 'system/vectrex_emu/'
PARTS = [
    ('core', 'The Vectrex: BIOS, overlays, picture files, speech ROM', True,
     [(os.path.join(PKG, 'bios.bin'), J + 'bios.bin'), (os.path.join(PKG, 'gpu'), J + 'gpu'), (os.path.join(PKG, 'overlays'), J + 'overlays'),
      (os.path.join(PC, 'system', 'sp0256-al2.bin'), J + 'sp0256-al2.bin'), (os.path.join(th, 'overlays'), J + 'thumbs/overlays')]),
    ('games', 'Games (ROMs)', True, [(os.path.join(PC, 'roms'), 'roms')]),
    ('boxes', 'Box art', False, [(os.path.join(th, 'boxes'), J + 'thumbs/boxes')]),
    ('carts', 'Cartridges with their labels', False, [(os.path.join(th, 'carts'), J + 'thumbs/carts')]),
    ('uvmc2', 'UVMC2: Last Human, Doom, Quake, vector movies', False, [(os.path.join(PC, 'uvmc2'), J + 'uvmc2')]),
    ('pitrex', 'PiTrex SD card (calibrated)', False, [(os.path.join(PC, 'pitrex'), J + 'pitrex')]),
]


def files_of(src, arc):
    if os.path.isfile(src):
        yield src, arc; return
    for r, ds, fs in os.walk(src):
        ds.sort()
        for f in sorted(fs):
            if f in SKIP:
                continue
            q = os.path.join(r, f); yield q, arc + '/' + os.path.relpath(q, src).replace(os.sep, '/')


man = {'made': ver, 'parts': []}
for pid, name, req, srcs in PARTS:
    h = hashlib.sha1(); fl = []
    for src, arc in srcs:
        if not os.path.exists(src):
            sys.exit('missing: ' + src)
        for q, a2 in files_of(src, arc):
            fl.append((q, a2)); h.update(a2.encode()); h.update(open(q, 'rb').read())
    pv = h.hexdigest()[:12]; tp = os.path.join(out, 'pack-%s.tgz' % pid)
    with tarfile.open(tp, 'w:gz', compresslevel=6) as t:
        for q, a2 in fl:
            ti = t.gettarinfo(q, a2); ti.uid = ti.gid = 0; ti.uname = ti.gname = ''; ti.mtime = 0
            with open(q, 'rb') as fh:
                t.addfile(ti, fh)
    man['parts'].append({'id': pid, 'name': name, 'required': req, 'version': pv, 'size': os.path.getsize(tp), 'files': len(fl)})
    print('part %-6s %-55s %4d files %7.1f MB  %s' % (pid, name, len(fl), os.path.getsize(tp) / 1e6, pv))
json.dump(man, open(os.path.join(out, 'pack.json'), 'w'), indent=1)
print('pack %s: %d parts, %.1f MB in all' % (ver, len(man['parts']), sum(x['size'] for x in man['parts']) / 1e6))
