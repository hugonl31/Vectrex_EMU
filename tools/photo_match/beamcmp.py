import numpy as np
from PIL import Image, ImageDraw
from scipy import ndimage as nd
src = open('cmpd.py').read(); exec(src[:src.index("if __name__")])
d = np.array(Image.open('pc_vectrex_p1.png').convert('RGB')).astype(np.float32).max(2) / 255
H, W = d.shape; y0 = int(H * 0.26); rows = rows10(d[y0:int(H * 0.5)], 0.08)
fy, fx = rows[0][0][0] + y0, rows[0][0][1]; sp = np.mean([np.mean(np.diff([p[1] for p in r])) for r in rows]); s = sp / 179.0
def photo(f):
    a = np.load(f)[..., :3].astype(np.float32) / 65535; a = a[::-1, ::-1]
    g = 0.2126 * a[..., 0] + 0.7152 * a[..., 1] + 0.0722 * a[..., 2]
    dd = (np.clip(g / 0.428 * 0.85, 0, 1) ** (1 / 2.2) * 255).astype(np.uint8)
    return Image.fromarray(dd).convert('RGB').transform((W, H), Image.AFFINE, (1 / s, 0, 1128 - fx / s, 0, 1 / s, 766 - fy / s), Image.BILINEAR)
box = (int(W * 0.06), int(H * 0.08), int(W * 0.94), int(H * 0.86))
names = ['real Vectrex (photo 1/50 s)', 'beam on phosphor: photo-wide spot', 'beam on phosphor: thin spot', 'rule renderer (current exe)']
for p, ph in ((1, '19710'), (2, '20751'), (3, '21790'), (4, '22828')):
    t = [photo(ph + '_raw.npy')] + [Image.open('%s_p%d.png' % (k, p)).convert('RGB') for k in ('btw', 'btv', 'pc_vectrex')]
    t = [x.crop(box) for x in t]
    for x, n in zip(t, names): ImageDraw.Draw(x).text((12, 12), 'page %d - %s' % (p, n), fill=(255, 140, 0))
    Image.fromarray(np.concatenate([np.array(x) for x in t], 1)).save(r'G:\AI\Vectrex_EMU\testcart\compare\beam_dot_p%d.png' % p)
def crop(dd, thr, m=0.04):
    sm = nd.gaussian_filter(dd, 2) > thr; ys, xs = np.nonzero(sm)
    Hh, Ww = dd.shape[:2]; a, b, c, e = ys.min(), ys.max(), xs.min(), xs.max(); my, mx = int((b - a) * m) + 10, int((e - c) * m) + 10
    return (max(0, a - my), min(Hh, b + my), max(0, c - mx), min(Ww, e + mx))
fit = lambda im, w: im.resize((w, max(1, int(im.size[1] * w / im.size[0]))), Image.LANCZOS)
g0 = np.load('oct/Vectorblade1.npy').astype(np.float32); p_ = np.percentile(g0[g0 > np.percentile(g0, 98)], 95)
d0 = np.rot90(np.clip(g0 / p_ * 0.9, 0, 1) ** (1 / 2.2), 3); a, b, c, e = crop(d0, 0.35)
tiles = [Image.fromarray((d0[a:b, c:e] * 255).astype(np.uint8)).convert('RGB')]
for k in ('btwvb', 'btvb', 'vb_vectrex'):
    im = np.array(Image.open('%s_p1.png' % k).convert('RGB')); a, b, c, e = crop(im.max(2) / 255.0, 0.3); tiles.append(Image.fromarray(im[a:b, c:e]))
tiles = [fit(t, 1000) for t in tiles]
for t, n in zip(tiles, names): ImageDraw.Draw(t).text((10, 8), n, fill=(255, 140, 0))
out = np.concatenate([np.pad(np.array(t), ((0, 12), (0, 0), (0, 0))) for t in tiles], 0)
Image.fromarray(out).save(r'G:\AI\Vectrex_EMU\testcart\compare\beam_vectorblade.png')
Image.fromarray(out).resize((500, out.shape[0] // 2)).save('bvb_small.png')
