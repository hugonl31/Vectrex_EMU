"""make_pitrex_art.py - the PiTrex's tiles for the game library: a cartridge and a box in the look of the others.

    python tools/make_pitrex_art.py <photo of the PiTrex cartridge> <thumbs folder> <out folder>

The cartridge: another game's cartridge picture (thumbs/carts/Bedlam.png) with its label's title taken out (the label's
grid paper drawn over it) and the PiTrex's logo - cut from the photo - on a black panel in its place. The box: the same
with a box front (thumbs/boxes/Bedlam.png): the logo where the title is, the cartridge's photo where the picture is.
Writes pitrex_cart.png, pitrex_box.png (the owner's photo and logo, 2026-11-22)."""
import sys, os
import numpy as np
from PIL import Image, ImageDraw, ImageFilter

photo, thumbs, out = sys.argv[1], sys.argv[2], sys.argv[3]

def period(strip):
    """the grid's pitch along a 1-d brightness profile (px, a fraction)"""
    p = strip - strip.mean(); n = len(p)
    ac = np.correlate(p, p, 'full')[n - 1:]
    lo = 4; k = lo + int(np.argmax(ac[lo:min(n // 2, 40)]))
    # refine: the peak's parabola
    if 1 <= k < len(ac) - 1:
        d = (ac[k - 1] - ac[k + 1]) / (2 * (ac[k - 1] - 2 * ac[k] + ac[k + 1])); k = k + d
    return float(k)

def paper(img, box, ref):
    """the label's grid paper drawn over box (x0, y0, x1, y1), its pitch, phase and colours measured in ref (a clean part)"""
    a = np.asarray(img).astype(np.float32); rx0, ry0, rx1, ry1 = ref
    r = a[ry0:ry1, rx0:rx1].mean(2)
    px, py = period(r.mean(0)), period(r.mean(1))
    def phase(prof, p, o):
        best = max(np.arange(0, p, 0.25), key=lambda ph: prof[np.clip(np.round(np.arange(ph, len(prof), p)).astype(int), 0, len(prof) - 1)].mean())
        return (o + best) % p
    phx, phy = phase(r.mean(0), px, rx0), phase(r.mean(1), py, ry0)
    flat = a[ry0:ry1, rx0:rx1].reshape(-1, 3); lum = flat.mean(1)
    bg = np.median(flat, 0); ln = np.median(flat[lum > np.percentile(lum, 97)], 0)   # (the squares fill most of it: the median; the lines: its brightest)
    x0, y0, x1, y1 = box
    X, Y = np.meshgrid(np.arange(x0, x1), np.arange(y0, y1))
    dx = np.abs(((X - phx + px / 2) % px) - px / 2); dy = np.abs(((Y - phy + py / 2) % py) - py / 2)
    w = np.clip(1.0 - np.minimum(dx, dy) / 0.85, 0, 1)[..., None]
    a[y0:y1, x0:x1] = bg * (1 - w) + ln * w
    return Image.fromarray(np.clip(a, 0, 255).astype(np.uint8))

def cut(im, thr=232):
    """the photo without its white ground: RGBA"""
    a = np.asarray(im.convert('RGB')).astype(np.int32)
    white = (a.min(2) > thr)
    # only the white connected to the border goes
    from scipy.ndimage import label
    lab, n = label(white); edge = set(np.unique(np.concatenate([lab[0], lab[-1], lab[:, 0], lab[:, -1]]))) - {0}
    m = ~np.isin(lab, list(edge))
    al = Image.fromarray((m * 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(0.8))
    o = im.convert('RGBA'); o.putalpha(al); return o

def panel(size, logo, pad=0.06):
    """the logo on a black rounded panel with a grey edge"""
    w, h = size
    p = Image.new('RGBA', (w, h), (0, 0, 0, 0)); d = ImageDraw.Draw(p)
    d.rounded_rectangle((0, 0, w - 1, h - 1), int(0.14 * h), fill=(120, 120, 124, 255))
    d.rounded_rectangle((3, 3, w - 4, h - 4), int(0.13 * h), fill=(8, 8, 9, 255))
    lw, lh = logo.size; s = min((w * (1 - 2 * pad)) / lw, (h * (1 - 2 * pad)) / lh)
    lg = logo.resize((max(1, int(lw * s)), max(1, int(lh * s))), Image.LANCZOS)
    p.paste(lg, ((w - lg.size[0]) // 2, (h - lg.size[1]) // 2), lg)
    return p

LOGO = Image.open(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'icons', 'pitrex_logo.png')).convert('RGBA')
ph = Image.open(photo).convert('RGB')
W, H = ph.size
# the logo: the lit part of the cartridge's top face (the plastic's sparkle taken to black)
lg = ph.crop((int(0.13 * W), int(0.10 * H), int(0.87 * W), int(0.52 * H)))
a = np.asarray(lg).astype(np.float32); mx = a.max(2); sat = (mx - a.min(2)) / np.maximum(mx, 1)
keep = (mx > 120) | ((sat > 0.35) & (mx > 70))
al = Image.fromarray((keep * 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(0.7))
logo = lg.convert('RGBA'); logo.putalpha(al)
bb = Image.fromarray((keep * 255).astype(np.uint8)).getbbox(); logo = logo.crop(bb)

# ---- the cartridge: the label (tools/make_pitrex_label.py - the owner's label project's way: the exact grid, the shared
# furniture, the artwork, the legal box) put on the real shell as tools/ios_pack.py does (cart, shrink)
ART = 'G:/AI/Vectrex_Artwork'
lab = Image.open(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'icons', 'pitrex_label_4x.png')).convert('RGB')

def shrink(im, W=420):
    src = np.asarray(im.convert('RGBA')).copy()
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
    clear = Image.fromarray(((src[:, :, 3] < 128) * 255).astype(np.uint8), 'L').filter(ImageFilter.MaxFilter(21))
    ring = np.asarray(clear) > 0; lightpx = src[:, :, :3].mean(2) > 95
    src[ring & lightpx] = 0
    im = Image.fromarray(np.ascontiguousarray(src), 'RGBA')
    a = np.asarray(im.resize((W, int(W * im.height / im.width)), Image.LANCZOS)).copy()
    al = a[:, :, 3]; semi = (al > 0) & (al < 250)
    a[semi, :3] = np.minimum(a[semi, :3], (40, 42, 46)); a[al < 48] = 0
    return Image.fromarray(dark_edge(a), 'RGBA')
def dark_edge(a):
    """the outline in the shell's dark plastic (as ios_pack.py: half-clear pixels and four rings capped, premultiplied)"""
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
sh = Image.open(os.path.join(ART, 'original gce', 'Armor..Attack (World).png')).convert('RGBA'); w, h = sh.size; pxs = sh.load()
def light(x, y):
    r, g, b, a_ = pxs[x, y]; return a_ > 200 and r > 150 and g > 150 and b > 150
y0 = int(h * 0.35)
rows = [y for y in range(y0, h) if sum(light(x, y) for x in range(0, w, 3)) > w / 3 * 0.6]
yb = max(y for y in range(h) if (np.asarray(sh)[y, :, 3] > 128).mean() > 0.5)   # (not the shell's bottom scan line: as ios_pack.py)
runs = []
for y in rows:
    if runs and y == runs[-1][1] + 1: runs[-1][1] = y
    else: runs.append([y, y])
rows = [y for r0, r1 in runs if yb - r1 > 12 for y in range(r0, r1 + 1)]
cols = [x for x in range(w) if sum(light(x, y) for y in range(rows[0], rows[-1], 3)) > (rows[-1] - rows[0]) / 3 * 0.6]
x0, y0, x1, y1 = cols[0], rows[0], cols[-1], rows[-1]
lw = x1 - x0 + 1; lh = int(round(lw * lab.height / lab.width))   # (the label at its own aspect, top-aligned)
im = sh.copy(); im.paste(lab.convert('RGBA').resize((lw, lh), Image.LANCZOS), (x0, y0))
shrink(im, 840).save(os.path.join(out, 'pitrex_cart.png'), optimize=True)   # (840: the tiles' width, as the generated ones)

# ---- the box: the owner's box art (icons/pitrex_box_src.png -> icons/pitrex_box.png, 390 wide: made once by hand, kept)
print('ok')
