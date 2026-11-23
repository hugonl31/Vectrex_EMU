"""make_genart_h.py - libretro/genart_data.h: the data the generated library pictures are drawn from (libretro/genart.c -
a cartridge and a box for every game with no picture of its own, the same on the PC, RetroArch, iPhone and Android).

  GA_CART_PNG   the cartridge without a title (840 px): the real shell (original gce/Armor..Attack, the one the PiTrex's
                cartridge already ships with) + a label composed the label project's way - the exact silver grid, the
                shared furniture v6 WITHOUT the GCE logo, Last Human's legal box with one line - cut out as
                tools/ios_pack.py's shrink() / dark_edge(); GA_CART_X0 / Y0 / S: where the 3120 x 1700 label lies in it
                (the pill's frame); GA_CART_LY0 / LH: the label's top and height there exactly (the shrink's rows are
                rounded down: a little less than x k down) - the player's own labels go there (genart.c)
  GA_FURN_PNG   the furniture alone (the Vectrex logo + TM, ARCADE SYSTEM CARTRIDGE) at the box's scale
  GA_LEGAL_PNG  the box's legal line, as a mask
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


def furniture_body():
    furn = open(ART + '/print 4x/reconstructed2/shared-svg/shared furniture v6.svg', encoding='utf-8').read()
    fb = furn[furn.index('<g id="vectrex-logo">'):furn.index('</svg>')]
    fb = re.sub(r'<g id="gce-(?:symbol|tm)"[^>]*>.*?</g>', '', fb, flags=re.S)   # (no GCE: our own labels)
    assert 'gce-' not in fb
    return fb


def render_svg(body, w=W, h=H):
    svg = ('<svg xmlns="http://www.w3.org/2000/svg" width="%d" height="%d" viewBox="0 0 %d %d">\n' % (w, h, W, H) + body + '\n</svg>')
    return Image.open(io.BytesIO(bytes(resvg_py.svg_to_bytes(svg_string=svg)))).convert('RGBA')


def base_label():
    grid = open(ART + '/print 4x/reconstructed/shared-svg/silver-grid.svg', encoding='utf-8').read()
    gb = grid[grid.index('<g id="silver-grid">'):grid.index('</svg>')]
    cx = BOX[0] + BOX[2] / 2; cy = BOX[1] + BOX[3] / 2
    legal = ('<rect x="%.2f" y="%.2f" width="%.2f" height="%.2f" fill="%s"/>' % BOX
             + '<path d="%s" fill="black"/>' % text_path(LEGAL, REG, 47.0, cx, cy + 23.5))
    return render_svg(gb + furniture_body() + legal).convert('RGB')


def dark_edge(a):
    """the outline in the shell's dark plastic (tools/ios_pack.py's)"""
    al = a[:, :, 3].astype(np.float32); rgb = a[:, :, :3].astype(np.float32)
    inside = al >= 128; pad = 5
    ring = np.full(al.shape, 99, np.int32); ring[(al > 0) & ~inside] = 0
    cur = Image.fromarray(np.pad(inside, pad).astype(np.uint8) * 255, 'L')
    for i in (1, 2, 3, 4):
        er = cur.filter(ImageFilter.MinFilter(3)); r = (np.asarray(cur) > 0) & ~(np.asarray(er) > 0)
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
    return Image.fromarray(dark_edge(a), 'RGBA'), left, top, k, a.shape[0] / im.height


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
    fu = render_svg(furniture_body(), int(W * 0.255 * BOX_K), int(H * 0.255 * BOX_K)); fu = fu.crop(fu.getchannel('A').getbbox())
    furn_png = png(fu)
    f = ImageFont.truetype(REG, int(round(13 * BOX_K))); l, t, r, b = f.getbbox(LEGAL)
    lg = Image.new('L', (r - l + 2, b - t + 2), 0); ImageDraw.Draw(lg).text((1 - l, 1 - t), LEGAL, font=f, fill=255)
    legal_png = png(lg)
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
         '#define GA_FONT_CAP %d' % cap, '#define GA_FONT_UPM %d' % upm]
    L += c_bytes('GA_CART_PNG', cart_png) + c_bytes('GA_FURN_PNG', furn_png) + c_bytes('GA_LEGAL_PNG', legal_png) + c_bytes('GA_FONT', font)
    L.append('static const char *const GA_TITLES[][2] = {')
    L += ['\t{ %s, %s },' % (c_str(a), c_str(v)) for a, v in titles] + ['\t{ 0, 0 }', '};', '']
    io.open(os.path.join(root, 'libretro', 'genart_data.h'), 'w', encoding='utf-8', newline='\n').write('\n'.join(L))
    print('cartridge %d x %d (%d B), label at %.2f, %.2f x %.5f; furniture %s (%d B); legal %s (%d B); font %d B; %d titles'
          % (cart.width, cart.height, len(cart_png), cx0, cy0, cs, fu.size, len(furn_png), lg.size, len(legal_png), len(font), len(titles)))
