import numpy as np, os
from PIL import Image, ImageDraw
from scipy import ndimage as nd
PAIRS = [('intensity', 'Brightness', 'Test Cartridge Rev. 4, INTENSITY (Brightness.dng, 2026-11-20)'),
         ('intensity', '1', 'Test Cartridge Rev. 4, INTENSITY (1.dng, 2026-10-25)'),
         ('axis', '10252026220143323', 'Axis Test (2026-10-25)'),
         ('beam_p1', '10252026234453844', 'Beam Test page 1 (2026-10-25)'), ('beam_p2', '10252026234518728', 'Beam Test page 2'),
         ('beam_p3', '10252026234543842', 'Beam Test page 3'), ('beam_p4', '10252026234613703', 'Beam Test page 4'),
         ('corner', 'Cornertest', 'Corner Test (2026-10-25)'),
         ('dac_p1', 'Dactest1', 'DAC Test page 1 (2026-10-26)'), ('dac_p2', 'Dactest2', 'DAC Test page 2'),
         ('gap_p1', 'Gaps', 'Gap Test page 1 (2026-10-26)'), ('gap_p2', 'gaps2', 'Gap Test page 2'), ('gap_p3', 'gaps3', 'Gap Test page 3'),
         ('lag', 'Lagtest', 'Lag Test (2026-10-25)'), ('vectorblade', 'Vectorblade1', 'Vectorblade title (2026-10-25)')]
def crop(d, thr, m=0.04):
    sm = nd.gaussian_filter(d, 2) > thr; ys, xs = np.nonzero(sm)
    if len(ys) == 0: return d
    H, W = d.shape[:2]; y0, y1, x0, x1 = ys.min(), ys.max(), xs.min(), xs.max(); my, mx = int((y1 - y0) * m) + 10, int((x1 - x0) * m) + 10
    return d[max(0, y0 - my):min(H, y1 + my), max(0, x0 - mx):min(W, x1 + mx)]
def fit(im, h):
    return im.resize((max(1, int(im.size[0] * h / im.size[1])), h), Image.LANCZOS)
for name, ph, title in PAIRS:
    g0 = np.load('oct/%s.npy' % ph).astype(np.float32)
    p = np.percentile(g0[g0 > np.percentile(g0, 98)], 95)
    d0 = np.clip(g0 / p * 0.9, 0, 1) ** (1 / 2.2)
    ref = crop(np.load('all_%s_vectrex.npy' % name).max(2) / 255.0, 0.35)
    rs = np.array(Image.fromarray((ref * 255).astype(np.uint8)).resize((64, 80), Image.BILINEAR), np.float32)
    best = None
    for k in range(4):
        c = crop(np.rot90(d0, k), 0.35)
        cs = np.array(Image.fromarray((c * 255).astype(np.uint8)).resize((64, 80), Image.BILINEAR), np.float32)
        r = np.corrcoef(cs.ravel(), rs.ravel())[0, 1]
        if best is None or r > best[0]: best = (r, k, c)
    P = Image.fromarray((best[2] * 255).astype(np.uint8)).convert('RGB')
    tiles = [fit(P, 900)]
    for look in ('vectrex', 'bright'):
        e = np.load('all_%s_%s.npy' % (name, look)); em = e.max(2) / 255.0
        sm = nd.gaussian_filter(em, 2) > 0.35; ys, xs = np.nonzero(sm)
        H, W = em.shape; y0, y1, x0, x1 = ys.min(), ys.max(), xs.min(), xs.max(); my, mx = int((y1 - y0) * 0.04) + 10, int((x1 - x0) * 0.04) + 10
        tiles.append(fit(Image.fromarray(e[max(0, y0 - my):min(H, y1 + my), max(0, x0 - mx):min(W, x1 + mx)]), 900))
    for t, n in zip(tiles, ['real Vectrex: ' + title, 'emulator: Vectrex look', 'emulator: Bright look']): ImageDraw.Draw(t).text((10, 8), n, fill=(255, 140, 0))
    im = np.concatenate([np.array(t) for t in tiles], 1)
    Image.fromarray(im).save(r'G:\AI\Vectrex_EMU\testcart\compare\%s_%s.png' % (name, ph))
    print(name, ph, im.shape)
