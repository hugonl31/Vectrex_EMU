"""make_shipped_art.py - the games list's pictures that come with the emulator (every public build: the PC zips, the
RetroArch packages, the iPhone and Android apps): the remade collection (the owner, 2026-11-24 - boxart_labels/
make_new_art.py: every label and box on our own template - the silver paper, the real labels' small print, gloss c - with
each game's title artwork and picture from Simon Quincey's Vectrex 2D Front Cover Box Art pack (v1.3 Full), used with his
permission; none of his own pictures), for the games whose cover has real title artwork - the pack's generic covers (a
blue pill with the typed name, G:/AI/Vectrex_Artwork/boxart_labels/placeholders.txt) are left out: for those the
emulator draws its own cartridge and box (libretro/genart.c), which look the same. Beside them: the owner's own titles
(OWN - games with nothing in the pack: his label and box).

  python tools/make_shipped_art.py [art folder] [out folder]
      art folder: G:/AI/Vectrex_Artwork (default); out: package/system/vectrex_emu/thumbs/shipped (default)
  python tools/make_shipped_art.py --full <zip> [art folder]
      the full set for those who want every game's pictures (a separate download, the GitHub release artwork-1, never in
      the repository): the whole remade collection, the plain covers too, laid out as the folder it is unzipped
      into - system/vectrex_emu/thumbs/labels, .../boxes (the player's own folders: found by the box art's name through the
      shipped index.txt), the owner's titles, an extras folder (the owner's artwork: EXTRAS) and a README.txt

Out (the folder made afresh):
  labels/<box art's name>.jpg   the remade label (boxart_labels/labels-new, 3120 x 1700), 840 x 458 - the emulator puts it
                                on its cartridge itself (genart.c)
  boxes/<box art's name>.jpg    the remade box front (boxart_labels/boxes-new), 800 x 1024
  index.txt                     which game takes which picture: "<key>\t<box art's name>", one a line - every box art's
                                own name's key (the full set's too) and the keys of ROM names spelt otherwise (ALIAS)
  CREDITS.txt
The key (every platform the same: library.py, libretro/library.c via genart.c's vx_genart_key, the apps): the name with
every "(...)" and "[...]" part left out, in lower case, only its letters a-z and digits 0-9 - "Armor..Attack (World)",
"Armor Attack" and "armor attack [b1]" are all "armorattack".
Copyright (c) 2026 Hugo Bokhorst. Part of Vectrex_EMU; see LICENSE.md."""
import os, re, shutil, sys
from PIL import Image

R = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ARGS = list(sys.argv[1:]); FULL = None
if '--full' in ARGS:                                             # (--full <zip>: the full set instead)
    i = ARGS.index('--full'); FULL = ARGS[i + 1] if i + 1 < len(ARGS) else 'Vectrex_EMU-artwork-full.zip'; del ARGS[i:i + 2]
ART = ARGS[0] if len(ARGS) > 0 else 'G:/AI/Vectrex_Artwork'
OUT = ARGS[1] if len(ARGS) > 1 else os.path.join(R, 'package', 'system', 'vectrex_emu', 'thumbs', 'shipped')
BOX = os.path.join(ART, 'boxart_labels', 'boxes-new')               # (the remade collection: boxart_labels/make_new_art.py)
LAB = os.path.join(ART, 'boxart_labels', 'labels-new')
PLACEHOLDERS = os.path.join(ART, 'boxart_labels', 'placeholders.txt')
LABEL_SIZE, BOX_SIZE, QUALITY = (840, 458), (800, 1024), 85
# ROM names the box art spells otherwise (a ROM's name -> the box art's), as tools/ios_pack.py's ALIAS and
# boxart_labels/map_roms.py's (the last three: the same name family, probably the same game). Names whose key is the box
# art's already (Blitz! Action Football, Web Warp, Star Trek - The Motion Picture) need none.
ALIAS = {'HyperChase Auto Race': 'Hyperchase', 'Mr. Boston Clean Sweep': 'Clean Sweep Mr Boston',
         'Test Cartridge Rev. 4': 'Vectrex Test Cartridge Rev.4', 'Blitz! Action Football': 'Blitz Action Football (US)',
         'Heads-Up Action Soccer': 'Heads-Up Action Soccer (UK)', 'Star Trek - The Motion Picture': 'Star Trek The Motion Picture',
         'Web Warp': 'Web Warp (UK)', 'Web Wars': 'Web Wars (US)',
         'Mine Storm III': 'Mine Storm v3', 'Lunar Lander': 'Lunar Lander demo', 'ZZAP': 'Zzap Strikes Back'}
# the owner's own titles, games with nothing in the pack: name -> (its label, 3120 x 1700; its box front or None - the
# drawn box then), remade as the rest (make_new_art.py: Last Human's box with his cover's picture, Robot Arena's with the
# drawn box's scene under his title). Keys: the name's - "Last Human (UVMC2)" and the Tests' "Last Human (autopilot)" are
# "lasthuman" too
OWN = {'Last Human': ('boxart_labels/labels-new/Last Human.png', 'boxart_labels/boxes-new/Last Human.png'),
       'Robot Arena': ('boxart_labels/labels-new/Robot Arena.png', 'boxart_labels/boxes-new/Robot Arena.png')}
# the full set's extras folder: the owner's artwork (his own) - its folder in the zip, the files, how
EXTRAS = [('extras/labels', 'print 4x/labels/*.png', 'label'),                    # (his 34 labels, 1560 x 850 PNG)
          ('extras/label reconstructions (SVG)', 'print 4x/reconstructed2/deliver/*.svg', 'copy'),
          ('extras', 'GCE_Vectrex.svg', 'copy')]
EXTRA_LABEL = (1560, 850)
CREDITS = ("Title artwork and pictures: Simon Quincey (his Vectrex 2D Front Cover Box Art pack, v1.3), used with permission.\n"
           "Box and label design by Hugo Bokhorst; his own titles: Last Human, Robot Arena.\n"
           "Games without real artwork get the emulator's own drawn cartridge and box. Your own pictures (thumbs/carts,\n"
           "thumbs/labels, thumbs/boxes) always come first.\n")


def key(s):
    s = re.sub(r'\(.*?\)|\[.*?\]', '', s).lower()
    return re.sub(r'[^a-z0-9]', '', s)


def placeholders():
    out = set()
    for ln in open(PLACEHOLDERS, encoding='utf-8').read().splitlines()[1:]:
        m = re.match(r'^(.*?)\s+[0-9.]+\s*$', ln)
        if m:
            out.add(m.group(1).strip())
    return out


def sources():
    """the pack's games, remade: name -> (box front, label)"""
    boxes = {os.path.splitext(f)[0]: os.path.join(BOX, f) for f in sorted(os.listdir(BOX)) if f.lower().endswith('.png')}
    return {n: (b, os.path.join(LAB, n + '.png')) for n, b in boxes.items()
            if n not in OWN and os.path.exists(os.path.join(LAB, n + '.png'))}


def jpegs(src, n, out):
    """the label (840 x 458) and the box front (800 x 1024, when it has one) of one game -> out/labels, out/boxes; their bytes"""
    box, lab = src[n]
    im = Image.open(lab).convert('RGB').resize(LABEL_SIZE, Image.LANCZOS)
    im.save(os.path.join(out, 'labels', n + '.jpg'), quality=QUALITY, optimize=True)
    size = os.path.getsize(os.path.join(out, 'labels', n + '.jpg'))
    if box:
        b = Image.open(box).convert('RGB')
        if b.size != BOX_SIZE:
            b = b.resize(BOX_SIZE, Image.LANCZOS)
        b.save(os.path.join(out, 'boxes', n + '.jpg'), quality=QUALITY, optimize=True)
        size += os.path.getsize(os.path.join(out, 'boxes', n + '.jpg'))
    return size


def own():
    """the owner's titles: name -> (box front or None, label), as sources()"""
    out = {}
    for n, (lab, box) in OWN.items():
        lab = os.path.join(ART, lab); box = os.path.join(ART, box) if box else None
        for p in (lab, box):
            if p and not os.path.isfile(p):
                sys.exit('missing: ' + p)
        out[n] = (box, lab)
    return out


def index(src, mine=None):
    """every game's key -> its picture's name: the pack's every game (the full set's too, in the player's thumbs folder),
    the ROM names spelt otherwise (ALIAS) and the owner's titles (never over the pack's own); a key two names share: the
    first (Berzerk, not Berzerk (Bugfixed))"""
    idx = {}
    for n in sorted(src):
        if not key(n):                                           # (.--. .- -. --.. . .-.: no letters - found by its own name only)
            continue
        if key(n) in idx:
            print('same key as %s: %s (still in the full set, found by its own name)' % (idx[key(n)], n)); continue
        idx[key(n)] = n
    for rom, n in ALIAS.items():                                 # (never over a name's own)
        if n in src and key(rom) not in idx:
            idx[key(rom)] = n
    for n in sorted(mine or {}):
        if key(n) in idx:
            sys.exit('%s: the pack has %s for it already' % (n, idx[key(n)]))
        idx[key(n)] = n
    return idx


FULL_README = """Vectrex_EMU - the games list's pictures, the full set
=====================================================
Box fronts (800 x 1024) and cartridge labels (840 x 458) for every game of Simon Quincey's Vectrex 2D Front Cover Box
Art pack (v1.3): %d games, each remade on the emulator's own box and label design with the game's title artwork and
picture from the pack - the emulator puts a label on its cartridge itself. The emulator comes with %d of them (the games
with real title artwork); this set has them all, the pack's plain covers too (with the emulator's own title pill) - and
Hugo Bokhorst's own titles (%s).

Title artwork and pictures: Simon Quincey (his Vectrex 2D Front Cover Box Art pack), used with permission.
Box and label design by Hugo Bokhorst. Label art and reconstructions by Hugo Bokhorst.

extras (not used by the emulator) - Hugo Bokhorst's artwork:
  labels                            his cartridge labels, 1560 x 850 PNG (print size: 66.04 x 35.98 mm): the GCE
                                    originals redrawn and his own designs (Last Human, Robot Arena, Patriots,
                                    Vectrexagon, PiTrex ...)
  label reconstructions (SVG)       his vector redraws of labels, 3120 x 1700 units (the same print size)
  GCE_Vectrex.svg                   his vector drawing of the Vectrex logo, free to use

Where to unzip it (it holds system/vectrex_emu/thumbs/labels and .../boxes) - Vectrex_EMU v1.8.5 or newer:
  Windows, Linux, Mac   next to the program (Vectrex_EMU.exe / Vectrex_EMU / Vectrex_EMU.app): the system folder there
  RetroArch             RetroArch's own folder - its system folder (Settings > Directory > System/BIOS) is the one
                        holding vectrex_emu
  iPhone / iPad         Files > On My iPhone > Vectrex_EMU (the app's folder)
  Android               the app's folder (the one holding system/vectrex_emu)
The games list (Cartridges and Boxes) then finds each game's picture by its name; your own pictures of the same name
(.png or .jpg in thumbs/carts, thumbs/labels, thumbs/boxes) come first.
"""


def full(src, mine, idx, zpath):
    """the full set as a zip laid out as the folder it is unzipped into, the owner's titles and the extras folder"""
    import glob, io, tempfile, zipfile
    tmp = tempfile.mkdtemp(prefix='vx_art_')
    os.makedirs(os.path.join(tmp, 'labels')); os.makedirs(os.path.join(tmp, 'boxes'))
    shipped = sum(1 for n in src if n not in placeholders() and idx.get(key(n)) == n)
    T = 'system/vectrex_emu/thumbs/'; every = dict(src, **mine); nx = 0
    with zipfile.ZipFile(zpath, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as z:   # (deflated: the boxes' grid paper repeats - a third smaller)
        for n in sorted(every):
            jpegs(every, n, tmp)
            for d in ('labels', 'boxes'):
                if os.path.isfile(os.path.join(tmp, d, n + '.jpg')):
                    z.write(os.path.join(tmp, d, n + '.jpg'), T + d + '/' + n + '.jpg')
        for folder, pat, how in EXTRAS:
            files = sorted(glob.glob(os.path.join(ART, pat)))
            if not files:
                sys.exit('extras: nothing at ' + os.path.join(ART, pat))
            for p in files:
                nm = os.path.basename(p)
                if how == 'label':                                  # ("Berzerk 4x.png" -> "Berzerk.png", 1560 x 850)
                    b = io.BytesIO(); Image.open(p).convert('RGB').resize(EXTRA_LABEL, Image.LANCZOS).save(b, 'PNG', optimize=True)
                    z.writestr(folder + '/' + re.sub(r' 4x\.png$', '.png', nm), b.getvalue())
                else:
                    z.write(p, folder + '/' + nm)
                nx += 1
        z.writestr('README.txt', FULL_README % (len(src), shipped, ', '.join(sorted(mine))))
    shutil.rmtree(tmp, ignore_errors=True)
    print('full set: %d games + %d of the owner\'s, %d extras -> %s, %.1f MB' % (len(src), len(mine), nx, zpath, os.path.getsize(zpath) / 1e6))


def main():
    mine = own()
    if FULL:
        src = sources(); full(src, mine, index(src, mine), FULL); return
    ph, src = placeholders(), sources()
    idx = index(src, mine)
    names = sorted(n for n in src if n not in ph and idx.get(key(n)) == n)
    missing_ph = sorted(ph - set(src))
    if missing_ph:
        print('placeholders not in the pack (names changed?):', missing_ph)
    shutil.rmtree(OUT, ignore_errors=True)
    os.makedirs(os.path.join(OUT, 'labels')); os.makedirs(os.path.join(OUT, 'boxes'))
    size = sum(jpegs(src, n, OUT) for n in names) + sum(jpegs(mine, n, OUT) for n in sorted(mine))
    with open(os.path.join(OUT, 'index.txt'), 'w', encoding='utf-8', newline='\n') as f:
        f.write('# which picture a game takes: <key>\\t<name> -> labels/<name>.jpg, boxes/<name>.jpg - here (the ones that come with\n'
                '# the emulator) or in thumbs/labels, thumbs/boxes (yours, the full set); every game of the box art pack and the ROM\n'
                '# names spelt otherwise. key: the game file\'s name without its (...) and [...] parts, lower case, only a-z and 0-9\n'
                '# (tools/make_shipped_art.py)\n')
        for k in sorted(idx):
            f.write('%s\t%s\n' % (k, idx[k]))
    open(os.path.join(OUT, 'CREDITS.txt'), 'w', encoding='utf-8', newline='\n').write(CREDITS)
    print('%d games shipped (%d placeholders left out of %d) + the owner\'s %s, %d keys in the index, %.1f MB' % (
        len(names), len(ph & set(src)), len(src), ', '.join(sorted(mine)), len(idx), size / 1e6))
    for rom, n in ALIAS.items():
        print('  alias %-32s -> %s%s' % (rom, n, '' if n in names else '  (not shipped: the full set)'))


if __name__ == '__main__':
    main()
