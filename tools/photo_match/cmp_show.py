"""the Corner Test's corners and Vectorblade's logo: the photo | today | beam trace + the proposed timing -> testcart/compare/now_*.png"""
import numpy as np
from PIL import Image, ImageDraw, ImageFont
from scipy import ndimage as nd
OUT = r'G:\AI\Vectrex_EMU\testcart\compare'
def disp(g, q=99.9):
    p = np.percentile(g, q); return np.clip(g / p, 0, 1) ** (1 / 2.2)
def box(d, thr, m=12):
    ys, xs = np.nonzero(nd.gaussian_filter(d, 2) > thr); return max(0, ys.min() - m), min(d.shape[0], ys.max() + m), max(0, xs.min() - m), min(d.shape[1], xs.max() + m)
def tile(a, w, label):
    im = Image.fromarray((np.clip(a, 0, 1) * 255).astype(np.uint8)); im = im.resize((w, max(1, int(im.height * w / im.width))), Image.LANCZOS).convert('RGB')
    o = Image.new('RGB', (w, im.height + 34), (0, 0, 0)); o.paste(im, (0, 34)); ImageDraw.Draw(o).text((8, 6), label, fill=(255, 150, 0), font=FONT); return o
try: FONT = ImageFont.truetype('arial.ttf', 22)
except Exception: FONT = ImageFont.load_default()
def stack(tiles, f):
    W = max(t.width for t in tiles); H = sum(t.height for t in tiles) + 8 * (len(tiles) - 1); o = Image.new('RGB', (W, H), (0, 0, 0)); y = 0
    for t in tiles: o.paste(t, (0, y)); y += t.height + 8
    o.save(f); print(f, o.size)
ren = lambda n, t: (np.load('sl_%s_%s.npy' % (n, t)).max(2).astype(np.float32) / 255) ** 2.2          # (as light, each tile then to its own white)
# 1. the corners (the $7F row; the photo's figures run bottom to top: turned)
g = np.load('oct/Cornertest.npy').astype(np.float32); ph = np.rot90(g[380:2920, 2500:2960], -1)
T = [tile(disp(ph, 99.7), 1800, 'REAL VECTREX  -  Corner Test, rest 0 5 8 12 16 24 32 48 64 96 cycles at the corner')]
for t, nm in (('today', 'EMULATOR TODAY'), ('btnew', 'NEW: beam trace + measured timing (test, not default)')):
    T.append(tile(disp(ren('corner', t)[760:915, 140:1070], 99.7), 1800, nm))
stack(T, OUT + r'\now_corners.png')
# 2. Vectorblade's logo
g = np.load('oct/Vectorblade1.npy').astype(np.float32); r0 = ren('vectorblade', 'today'); a, b, c, e = box(r0 ** (1 / 2.2), 0.3); rs = np.array(Image.fromarray((r0[a:b, c:e] ** (1 / 2.2) * 255).astype(np.uint8)).resize((96, 40)), np.float32); best = None
for k in range(4):
    r = np.rot90(g, k); d = disp(r); y0, y1, x0, x1 = box(d, 0.35); cc = r[y0:y1, x0:x1]
    if cc.shape[1] < cc.shape[0]: continue
    cs = np.array(Image.fromarray((disp(cc) * 255).astype(np.uint8)).resize((96, 40)), np.float32); q = np.corrcoef(cs.ravel(), rs.ravel())[0, 1]
    if best is None or q > best[0]: best = (q, cc)
print('photo logo', best[0], best[1].shape)
T = [tile(disp(best[1], 98.5), 1800, 'REAL VECTREX  -  Vectorblade title')]
for t, nm in (('today', 'EMULATOR TODAY'), ('btnew', 'NEW: beam trace + measured timing (test, not default)')):
    r = ren('vectorblade', t); a, b, c, e = (239, 570, 167, 975) if t == 'today' else (219, 583, 134, 1010)   # (each logo's own extent: the new one is 8 % larger)
    T.append(tile(disp(r[a - 14:b + 14, c - 14:e + 14], 98.5), 1800, nm))
stack(T, OUT + r'\now_vectorblade.png')
