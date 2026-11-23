"""make_genart_h.py - libretro/genart_data.h: the data the generated library pictures are drawn from (libretro/genart.c -
a cartridge and a box for every game with no picture of its own, the same on the PC, RetroArch, iPhone and Android).

  GA_CART_PNG   the cartridge without a title (840 px): the real shell (original gce/Armor..Attack, the one the PiTrex's
                cartridge already ships with) + a label composed the label project's way - on the boxes' silver paper
                (silver_paper: grain, light, the highlight streak, the grid pure white, the legal box plain), the
                shared furniture v6 with the GCE logo + TM as the GCE labels have it (the owner, 2026-11-23), Last
                Human's legal box with one line - cut out as
                tools/ios_pack.py's shrink() / dark_edge(); GA_CART_X0 / Y0 / S: where the 3120 x 1700 label lies in it
                (the pill's frame); GA_CART_LY0 / LH: the label's top and height there exactly (the shrink's rows are
                rounded down: a little less than x k down) - the player's own labels go there (genart.c)
  GA_FURN_PNG   the box's furniture (the Vectrex logo + TM, ARCADE SYSTEM CARTRIDGE), drawn from the SVG at 840 px,
                placed as on his real Clean Sweep box: GA_FURN_X / Y its top left in the 800 px front's px
  GA_FOOT_PNG   the box's foot: the owner's finished artwork (art/GCE_bottom_text.tif) as one mask at 840 px, fitted to
                his real Clean Sweep box's foot (foot_fit); GA_FOOT_X / Y its top left in the 800 px front's px
  GA_FONT       Archivo Black (SIL OFL 1.1, libretro/fonts), cut to Latin-1 and the typographic quotes
  GA_TITLES     libretro/genart_titles.txt: titles set by hand

Needs the artwork folder (G:/AI/Vectrex_Artwork), PIL, numpy, resvg_py, fontTools. Run after any of them changes:
    python tools/make_genart_h.py"""
import io, os, re
import numpy as np
from PIL import Image, ImageDraw, ImageFont, ImageFilter
import resvg_py
from fontTools.ttLib import TTFont
from fontTools import subset
from fontTools.pens.svgPathPen import SVGPathPen
from fontTools.pens.transformPen import TransformPen

here = os.path.dirname(os.path.abspath(__file__)); root = os.path.dirname(here)
ART = 'G:/AI/Vectrex_Artwork'
W, H = 3120, 1700
BOX = (376.62, 1435.72, 2269.94, 184.28, '#aeaeae')                  # Last Human's legal box (= the GCE labels')
LEGAL = 'FOR USE WITH THE VECTREX ARCADE SYSTEM'
CART_W = 840                                                         # the cartridge tiles' width (the owner, 2026-11-23)
BOX_K = 840 / 800.0                                                  # the box: the pack's 800 x 1024 front, at 840 wide
FONT = os.path.join(root, 'libretro', 'fonts', 'ArchivoBlack-Regular.ttf')
REG = next(p for p in ('C:/Windows/Fonts/LiberationSans-Regular.ttf', 'C:/Windows/Fonts/arial.ttf',
                       '/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf') if os.path.exists(p))   # (the legal line: drawn into the pictures)


def text_path(s, font_file, cap, cx, base):
    f = TTFont(font_file); gs = f.getGlyphSet(); cmap = f.getBestCmap(); upm = f['head'].unitsPerEm
    caph = getattr(f['OS/2'], 'sCapHeight', 0) or 0.716 * upm; k = cap / caph
    adv = sum(f['hmtx'][cmap[ord(c)]][0] for c in s) * k; x = cx - adv / 2; d = []
    for c in s:
        g = cmap[ord(c)]; pen = SVGPathPen(gs); gs[g].draw(TransformPen(pen, (k, 0, 0, -k, x, base)))
        d.append(pen.getCommands()); x += f['hmtx'][g][0] * k
    return ' '.join(p for p in d if p)


def furniture_body(gce=False):
    """the Vectrex logo + TM, ARCADE SYSTEM CARTRIDGE - and with gce the GCE logo + TM above the legal box (the cartridge's
    label, as the GCE labels); the box's top has no GCE"""
    furn = open(ART + '/print 4x/reconstructed2/shared-svg/shared furniture v6.svg', encoding='utf-8').read()
    fb = furn[furn.index('<g id="vectrex-logo">'):furn.index('</svg>')]
    if not gce:
        fb = re.sub(r'<g id="gce-(?:symbol|tm)"[^>]*>.*?</g>', '', fb, flags=re.S)
        assert 'gce-' not in fb
    return fb


# the box laid out on the owner's real Clean Sweep box (his photo, 2022 x 2576 px: box px = 400 + (photo x - 997) x S,
# photo y x S - 6, S = 800 / 2022 - the content 6 px higher than on the photo, the owner's framing; genart.c's GA_BX_*):
# the furniture (Vectrex + TM, ARCADE SYSTEM CARTRIDGE from shared furniture v6) where the photo has it - ARCADE SYSTEM
# CARTRIDGE across photo x 611..1411, the ink from photo y 90 (the SVG's ink x 821..2299, y 89..); the foot: the owner's
# finished artwork (art/GCE_bottom_text.tif: THIS PACKAGE ... / GCE / ENTERTAINING NEW IDEAS) as one piece, scaled
# uniformly and moved so its FOR USE line (the photo's own words) and its GCE symbol sit on the photo's (least squares
# on their ends and baselines). The Last Human box (make_lh_box.py) takes the same paper and foot from genart.c.
S_PH = 800 / 2022.0
def PX(xp): return 400 + (xp - 997.0) * S_PH
def PY(yp): return yp * S_PH - 6.0
FURN_S = (PX(1411) - PX(611)) / (2299 - 821 + 1); FURN_T = (PX(611) - 821 * FURN_S, PY(90) - 89 * FURN_S)
FOOT_ART = os.path.join(root, 'art', 'GCE_bottom_text.tif')
PH_FORUSE = (549, 1464, 2387); PH_GCE = (869, 1138, 2507)          # (photo px: the FOR USE line's ends and baseline; the GCE's sides and bottom)
SH = ART + '/print 4x/reconstructed2/shared-svg'


def foot_fit():
    """the artwork's mask (ink 0..1, its soft edges kept) and its fit art px -> photo px: (mask, s, tx, ty)"""
    a = np.asarray(Image.open(FOOT_ART).convert('RGBA')).astype(np.float32)
    m = (a[..., 3] / 255) * (1 - a[..., :3].mean(2) / 255)
    rows = (m > 0.5).any(1); bands = []; i = 0
    while i < len(rows):
        if rows[i]:
            j = i
            while j < len(rows) and rows[j]: j += 1
            bands.append((i, j)); i = j
        else: i += 1
    assert len(bands) == 5, bands
    y0, y1 = bands[2]; sub = m[y0:y1] > 0.5; xs = np.nonzero(sub.any(0))[0]
    base = y0 + float(np.percentile([np.nonzero(sub[:, c])[0].max() for c in xs], 60)) + 1
    l3 = (int(xs.min()), int(xs.max()) + 1, base)
    y0, y1 = bands[3]; xs = np.nonzero((m[y0:y1] > 0.5).any(0))[0]; gaps = np.nonzero(np.diff(xs) > 4)[0]
    sx1 = int(xs[gaps[-1]]) + 1 if len(gaps) else int(xs.max()) + 1
    sr = np.nonzero((m[y0:y1, xs.min():sx1] > 0.5).any(1))[0]; sym = (int(xs.min()), sx1, y0 + int(sr.max()) + 1)
    ax = np.array([l3[0], l3[1], sym[0], sym[1]], float); px = np.array([PH_FORUSE[0], PH_FORUSE[1], PH_GCE[0], PH_GCE[1]], float)
    ay = np.array([l3[2], sym[2]], float); py = np.array([PH_FORUSE[2], PH_GCE[2]], float)
    A = np.vstack([np.column_stack([ax, np.ones(4), np.zeros(4)]), np.column_stack([ay, np.zeros(2), np.ones(2)])])
    (sf, tx, ty), *_ = np.linalg.lstsq(A, np.concatenate([px, py]), rcond=None)
    return m, sf, tx, ty


def box_foot(kb):
    """the box's foot at kb x the 800 px front, a mask: (L, its x, y in the front's px)"""
    m, sf, tx, ty = foot_fit(); k = sf * S_PH * kb
    x0, y0 = PX(tx) * kb, PY(ty) * kb
    im = Image.fromarray((m * 255).astype(np.uint8)).resize((max(1, int(round(m.shape[1] * k))), max(1, int(round(m.shape[0] * k)))), Image.LANCZOS)
    print('foot: art px x %.5f -> photo px at (%.1f, %.1f): %d x %d at the box\'s (%.2f, %.2f)' % (sf, tx, ty, im.width, im.height, PX(tx), PY(ty)))
    return im, int(round(x0)) / kb, int(round(y0)) / kb


def svg_layer(body, k, t, w, h):
    """an SVG part (its 3120 x 1700 units) scaled k and moved t (px), on a clear w x h canvas"""
    svg = ('<svg xmlns="http://www.w3.org/2000/svg" width="%d" height="%d" viewBox="0 0 %d %d">'
           '<g transform="translate(%.4f %.4f) scale(%.6f)">%s</g></svg>' % (w, h, w, h, t[0], t[1], k, body))
    return Image.open(io.BytesIO(bytes(resvg_py.svg_to_bytes(svg_string=svg)))).convert('RGBA')


def box_top(kb):
    """the box's furniture at kb x the 800 px front: (RGBA cropped, its x, y in the front's px)"""
    f = open(SH + '/shared furniture v6.svg', encoding='utf-8').read(); body = f[f.index('<g id="vectrex-logo">'):f.index('<g id="gce-symbol">')]
    im = svg_layer(body, FURN_S * kb, (FURN_T[0] * kb, FURN_T[1] * kb), int(800 * kb), int(200 * kb)); bb = im.getchannel('A').getbbox()
    return im.crop(bb), bb[0] / kb, bb[1] / kb


def render_svg(body, w=W, h=H):
    svg = ('<svg xmlns="http://www.w3.org/2000/svg" width="%d" height="%d" viewBox="0 0 %d %d">\n' % (w, h, W, H) + body + '\n</svg>')
    return Image.open(io.BytesIO(bytes(resvg_py.svg_to_bytes(svg_string=svg)))).convert('RGBA')


def silver_paper(box=BOX):
    """the label's paper as the drawn boxes' silver (the owner, 2026-11-23): (190, 192, 200) with a fine grain, lit from
    the upper left (+-14), the boxes' S1 highlight streak scaled to the label (32 levels, 0.08 W across, fading along
    0.25 H); no bend, no wear. The label's own grid (silver-grid.svg) in pure white; box: the legal box, plain (no grid).
    RGB 3120 x 1700"""
    grid = open(ART + '/print 4x/reconstructed/shared-svg/silver-grid.svg', encoding='utf-8').read()
    gb = grid[grid.index('<g id="silver-grid">'):grid.index('</svg>')]
    g = np.asarray(render_svg(gb).convert('L'), np.float32)
    lo = np.percentile(g, 5); c = np.clip((g - lo) / (255.0 - lo), 0, 1)               # (the white lines' coverage)
    if box:
        bx, by, bw, bh = box[:4]; xx = np.arange(W) + 0.5; yy = np.arange(H) + 0.5
        c[np.ix_((yy > by) & (yy < by + bh), (xx > bx) & (xx < bx + bw))] = 0
    yv, xv = np.mgrid[0:H, 0:W].astype(np.float32) + 0.5
    light = (0.5 - (xv / W * 0.35 + yv / H * 0.65)) * 2 * 14
    px, py = xv - 0.22 * W, yv - 0.20 * H; u = px * 0.39 + py * 0.92; v = px * 0.92 - py * 0.39
    light += 32 * np.exp(-(v / (0.08 * W)) ** 2) * np.exp(-(u / (0.25 * H)) ** 2)
    yi, xi = np.mgrid[0:H, 0:W].astype(np.uint32)                  # (the grain: the boxes' hash; ~4x stronger here, the label is made
    q = (xi * np.uint32(73856093)) ^ (yi * np.uint32(19349663)); q ^= q >> np.uint32(13); q *= np.uint32(0x5bd1e995); q ^= q >> np.uint32(15)
    light += ((q & np.uint32(0xffff)).astype(np.float32) / 65535 * 2 - 1) * 6 * 3.2   #  ~4x smaller on the cartridge)
    p = np.array((190, 192, 200), np.float32)[None, None, :] + light[..., None]
    return Image.fromarray(np.clip(p * (1 - c[..., None]) + 255 * c[..., None], 0, 255).astype(np.uint8))


def base_label():
    cx = BOX[0] + BOX[2] / 2; cy = BOX[1] + BOX[3] / 2
    lab = silver_paper().convert('RGBA')
    lab.alpha_composite(render_svg(furniture_body(gce=True) + '<path d="%s" fill="black"/>' % text_path(LEGAL, REG, 47.0, cx, cy + 23.5)))
    return lab.convert('RGB')


def dark_edge(a, k=1):
    """the outline in the shell's dark plastic (tools/ios_pack.py's): k, each ring's width in pixels (1 at 420 wide, 2 at
    840 - the same edge at either size, as ios_pack.py's)"""
    al = a[:, :, 3].astype(np.float32); rgb = a[:, :, :3].astype(np.float32)
    inside = al >= 128; pad = 4 * k + 1
    ring = np.full(al.shape, 99, np.int32); ring[(al > 0) & ~inside] = 0
    cur = Image.fromarray(np.pad(inside, pad).astype(np.uint8) * 255, 'L')
    for i in (1, 2, 3, 4):
        er = cur.filter(ImageFilter.MinFilter(2 * k + 1)); r = (np.asarray(cur) > 0) & ~(np.asarray(er) > 0)
        ring[r[pad:-pad, pad:-pad]] = i; cur = er
    mx = rgb.max(2)
    for i, cap in ((0, 14.0), (1, 20.0), (2, 28.0), (3, 36.0), (4, 44.0)):
        s = ring == i; rgb[s] *= np.minimum(1.0, cap / np.maximum(mx[s], 1.0))[:, None]
    semi = (al > 0) & (al < 255); rgb[semi] *= (al[semi] / 255.0)[:, None]
    a = a.copy(); a[:, :, :3] = np.clip(rgb + 0.5, 0, 255).astype(np.uint8)
    return a


def shrink(im, Wd):
    """tools/ios_pack.py's shrink(), also giving where the picture's (0, 0) went and the scale across and down:
    (picture, left, top, k, ky)"""
    src = np.asarray(im.convert('RGBA')).copy(); left = top = 0
    def light_line(px):
        op = px[px[:, 3] > 128]; return len(op) > 0 and op[:, :3].mean() > 100
    for _ in range(12):
        if light_line(src[-1]): src = src[:-1]
        else: break
    for _ in range(12):
        if light_line(src[0]): src = src[1:]; top += 1
        else: break
    for _ in range(12):
        if light_line(src[:, 0]): src = src[:, 1:]; left += 1
        else: break
    for _ in range(12):
        if light_line(src[:, -1]): src = src[:, :-1]
        else: break
    clear = Image.fromarray(((src[:, :, 3] < 128) * 255).astype(np.uint8), 'L').filter(ImageFilter.MaxFilter(21))
    ring = np.asarray(clear) > 0; lightpx = src[:, :, :3].mean(2) > 95
    src[ring & lightpx] = 0
    im = Image.fromarray(np.ascontiguousarray(src), 'RGBA'); k = Wd / im.width
    a = np.asarray(im.resize((Wd, int(Wd * im.height / im.width)), Image.LANCZOS)).copy()
    al = a[:, :, 3]; semi = (al > 0) & (al < 250)
    a[semi, :3] = np.minimum(a[semi, :3], (40, 42, 46)); a[al < 48] = 0
    return Image.fromarray(dark_edge(a, max(1, round(Wd / 420))), 'RGBA'), left, top, k, a.shape[0] / im.height


def shell_box(sh):
    """the label's place on the shell (tools/ios_pack.py's)"""
    w, h = sh.size; px = sh.load()
    def light(x, y):
        r, g, b, a = px[x, y]; return a > 200 and r > 150 and g > 150 and b > 150
    y0 = int(h * 0.35)
    rows = [y for y in range(y0, h) if sum(light(x, y) for x in range(0, w, 3)) > w / 3 * 0.6]
    yb = max(y for y in range(h) if (np.asarray(sh)[y, :, 3] > 128).mean() > 0.5)
    runs = []
    for y in rows:
        if runs and y == runs[-1][1] + 1: runs[-1][1] = y
        else: runs.append([y, y])
    rows = [y for r0, r1 in runs if yb - r1 > 12 for y in range(r0, r1 + 1)]
    cols = [x for x in range(w) if sum(light(x, y) for y in range(rows[0], rows[-1], 3)) > (rows[-1] - rows[0]) / 3 * 0.6]
    return cols[0], rows[0], cols[-1], rows[-1]


def png(im):
    b = io.BytesIO(); im.save(b, 'PNG', optimize=True); return b.getvalue()


def c_bytes(name, data):
    out = ['static const unsigned char %s[%d] = {' % (name, len(data))]
    for i in range(0, len(data), 40):
        out.append(','.join(str(x) for x in data[i:i + 40]) + ',')
    out.append('};'); return out


def c_str(s):
    return '"' + s.replace('\\', '\\\\').replace('"', '\\"') + '"'


if __name__ == '__main__':
    # the cartridge, no title
    sh = Image.open(ART + '/original gce/Armor..Attack (World).png').convert('RGBA')
    x0, y0, x1, y1 = shell_box(sh); lw = x1 - x0 + 1; lh = int(round(lw * H / W))
    im = sh.copy(); im.paste(base_label().convert('RGBA').resize((lw, lh), Image.LANCZOS), (x0, y0))
    cart, left, top, k, ky = shrink(im, CART_W)
    cx0, cy0, cs = (x0 - left) * k, (y0 - top) * k, lw / W * k     # (label px -> cartridge px: x = cx0 + lx * cs)
    cart_png = png(cart)
    # the box's furniture and legal line
    fu, fux, fuy = box_top(BOX_K); furn_png = png(fu)               # (as the Last Human box's: drawn at 840 from the SVGs)
    lg, ftx, fty = box_foot(BOX_K); legal_png = png(lg)
    # the font: Latin-1 and the typographic quotes / dashes
    opt = subset.Options(); opt.layout_features = ['kern']; opt.name_IDs = ['*']; opt.notdef_outline = True
    ft = TTFont(FONT); sub = subset.Subsetter(opt)
    sub.populate(unicodes=list(range(0x20, 0x7f)) + list(range(0xa0, 0x100)) + [0x2018, 0x2019, 0x201c, 0x201d, 0x2013, 0x2014, 0x2026])
    sub.subset(ft); b = io.BytesIO(); ft.save(b); font = b.getvalue()
    cap, upm = ft['OS/2'].sCapHeight, ft['head'].unitsPerEm
    # the titles set by hand
    titles = []
    for ln in open(os.path.join(root, 'libretro', 'genart_titles.txt'), encoding='utf-8'):
        ln = ln.strip()
        if not ln or ln.startswith('#') or '=' not in ln:
            continue
        a, v = ln.split('=', 1); titles.append((a.strip().upper(), v.strip()))
    L = ['/* genart_data.h - generated by tools/make_genart_h.py: the data libretro/genart.c draws the library\'s generated',
         ' * cartridges and boxes from. Do not edit: change the tool, genart_titles.txt or the pictures and run it again.',
         ' * The font: Archivo Black, (c) 2017 The Archivo Black Project Authors, SIL Open Font License 1.1 (libretro/fonts/OFL.txt). */',
         '#define GA_CART_X0 %.4ff' % cx0, '#define GA_CART_Y0 %.4ff' % cy0, '#define GA_CART_S %.6ff' % cs,
         '#define GA_CART_LY0 %.4ff' % ((y0 - top) * ky), '#define GA_CART_LH %.4ff' % (lh * ky),
         '#define GA_FONT_CAP %d' % cap, '#define GA_FONT_UPM %d' % upm,
         '#define GA_FURN_X %.3ff' % fux, '#define GA_FURN_Y %.3ff' % fuy, '#define GA_FOOT_X %.3ff' % ftx, '#define GA_FOOT_Y %.3ff' % fty]
    L += c_bytes('GA_CART_PNG', cart_png) + c_bytes('GA_FURN_PNG', furn_png) + c_bytes('GA_FOOT_PNG', legal_png) + c_bytes('GA_FONT', font)
    L.append('static const char *const GA_TITLES[][2] = {')
    L += ['\t{ %s, %s },' % (c_str(a), c_str(v)) for a, v in titles] + ['\t{ 0, 0 }', '};', '']
    io.open(os.path.join(root, 'libretro', 'genart_data.h'), 'w', encoding='utf-8', newline='\n').write('\n'.join(L))
    print('cartridge %d x %d (%d B), label at %.2f, %.2f x %.5f; furniture %s (%d B); foot %s (%d B); font %d B; %d titles'
          % (cart.width, cart.height, len(cart_png), cx0, cy0, cs, fu.size, len(furn_png), lg.size, len(legal_png), len(font), len(titles)))
