"""make_genart_h.py - libretro/genart_data.h: the data the generated library pictures are drawn from (libretro/genart.c -
a cartridge and a box for every game with no picture of its own, the same on the PC, RetroArch, iPhone and Android).

  GA_CART_PNG   the cartridge without a title (840 px): the real shell (original gce/Armor..Attack, the one the PiTrex's
                cartridge already ships with) + a label composed the label project's way - the exact silver grid, the
                shared furniture v6 with the GCE logo + TM as the GCE labels have it (the owner, 2026-11-23), Last
                Human's legal box with one line - cut out as
                tools/ios_pack.py's shrink() / dark_edge(); GA_CART_X0 / Y0 / S: where the 3120 x 1700 label lies in it
                (the pill's frame); GA_CART_LY0 / LH: the label's top and height there exactly (the shrink's rows are
                rounded down: a little less than x k down) - the player's own labels go there (genart.c)
  GA_FURN_PNG   the box's furniture (the Vectrex logo + TM, ARCADE SYSTEM CARTRIDGE), drawn from the SVG at 840 px,
                placed as the Last Human box's (make_lh_box.py): GA_FURN_X / Y its top left in the 800 px front's px
  GA_FOOT_PNG   the box's foot: the small print and ENTERTAINING NEW IDEAS from the owner's picture
                (gce_box_bottom_source.webp), its GCE + TM drawn from the label project's SVG ('gce v5'), a mask at
                840 px, placed as on the box-art pack's GCE fronts (ink 223..577 x 896..1005 of 800 x 1024) and as the
                Last Human box's; GA_FOOT_X / Y its top left in the 800 px front's px
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
FOOT = ART + '/boxart_labels/gen/gce_box_bottom_source.webp'            # (the owner's: the small print and GCE, black on clear)
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


# the box's top and foot as the Last Human box's (G:/AI/Vectrex_Artwork/last_human/make_lh_box.py - the same numbers,
# in the 800 x 1024 front's px): the furniture (Vectrex + TM, ARCADE SYSTEM CARTRIDGE from shared furniture v6) with its
# ink at x 212..587, y 20..122 (the SVG's ink x 821..2299, y 89..); the foot: the owner's block image scaled by its width
# to the ink Simon's fronts have (223..577 x 896..1005), its GCE + TM rows (440..570) taken out and drawn from the
# 'gce v5' SVG instead - the GCE symbol as wide as the image's (x 692..1226), standing on its line (y 561)
FURN_S = (587 - 212 + 1) / (2299 - 821 + 1); FURN_T = (212 - 821 * FURN_S, 20 - 89 * FURN_S)
GCE_INK = (223, 896, 577, 1005); GCE_ROWS = (440, 570); GCE_SYM = (692, 448, 1226, 561); SVG_SYM = (1213, 1283, 1805, 1400)
SH = ART + '/print 4x/reconstructed2/shared-svg'


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


def box_foot(kb):
    """the box's foot at kb x the 800 px front, a mask: (L cropped, its x, y in the front's px)"""
    b = Image.open(FOOT).convert('RGBA'); bx0, by0, bx1, by1 = b.getchannel('A').getbbox()
    s = (GCE_INK[2] - GCE_INK[0]) / (bx1 - bx0)
    a = np.asarray(b).copy(); a[GCE_ROWS[0]:GCE_ROWS[1], :, 3] = 0; b = Image.fromarray(a)
    ow = b.width; b = b.resize((int(round(b.width * s)), int(round(b.height * s))), Image.LANCZOS); s = b.width / ow
    cx, cy = (GCE_INK[0] + GCE_INK[2]) / 2, (GCE_INK[1] + GCE_INK[3]) / 2
    x = int(round(cx - (bx0 + bx1) / 2 * s)); y = int(round(cy - (by0 + by1) / 2 * s))
    Wd, Hd = int(round(800 * kb)), int(round(1024 * kb))
    lay = Image.new('L', (Wd, Hd), 0)
    bb = b.getchannel('A').resize((int(round(b.width * kb)), int(round(b.height * kb))), Image.LANCZOS) if kb != 1 else b.getchannel('A')
    lay.paste(bb, (int(round(x * kb)), int(round(y * kb))))
    g = open(SH + '/gce v5 (GCE + TM bottom-aligned).svg', encoding='utf-8').read(); gbody = g[g.index('<g id="gce-symbol">'):g.rindex('</svg>')]
    k = s * (GCE_SYM[2] - GCE_SYM[0] + 1) / (SVG_SYM[2] - SVG_SYM[0] + 1)
    tx = x + GCE_SYM[0] * s - SVG_SYM[0] * k; ty = y + (GCE_SYM[3] + 1) * s - (SVG_SYM[3] + 1) * k
    gl = svg_layer(gbody, k * kb, (tx * kb, ty * kb), Wd, Hd).getchannel('A')
    lay = Image.fromarray(np.maximum(np.asarray(lay), np.asarray(gl)))
    bb = lay.getbbox(); return lay.crop(bb), bb[0] / kb, bb[1] / kb


def render_svg(body, w=W, h=H):
    svg = ('<svg xmlns="http://www.w3.org/2000/svg" width="%d" height="%d" viewBox="0 0 %d %d">\n' % (w, h, W, H) + body + '\n</svg>')
    return Image.open(io.BytesIO(bytes(resvg_py.svg_to_bytes(svg_string=svg)))).convert('RGBA')


def base_label():
    grid = open(ART + '/print 4x/reconstructed/shared-svg/silver-grid.svg', encoding='utf-8').read()
    gb = grid[grid.index('<g id="silver-grid">'):grid.index('</svg>')]
    cx = BOX[0] + BOX[2] / 2; cy = BOX[1] + BOX[3] / 2
    legal = ('<rect x="%.2f" y="%.2f" width="%.2f" height="%.2f" fill="%s"/>' % BOX
             + '<path d="%s" fill="black"/>' % text_path(LEGAL, REG, 47.0, cx, cy + 23.5))
    return render_svg(gb + furniture_body(gce=True) + legal).convert('RGB')


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
