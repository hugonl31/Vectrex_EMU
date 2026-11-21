import numpy as np, os
from PIL import Image, ImageDraw
from scipy import ndimage as nd
PAIRS = [('intensity', 'Brightness', 'Test Cartridge Rev. 4, INTENSITY (2026-11-20)'), ('axis', '10252026220143323', 'Axis Test'),
         ('beam_p1', '10252026234453844', 'Beam Test page 1'), ('beam_p2', '10252026234518728', 'Beam Test page 2'),
         ('beam_p3', '10252026234543842', 'Beam Test page 3'), ('beam_p4', '10252026234613703', 'Beam Test page 4'),
         ('corner', 'Cornertest', 'Corner Test'), ('dac_p1', 'Dactest1', 'DAC Test page 1'), ('dac_p2', 'Dactest2', 'DAC Test page 2'),
         ('gap_p1', 'Gaps', 'Gap Test page 1'), ('gap_p2', 'gaps2', 'Gap Test page 2'), ('gap_p3', 'gaps3', 'Gap Test page 3'), ('lag', 'Lagtest', 'Lag Test')]
def box(d, thr, m=0.04):
    sm = nd.gaussian_filter(d, 2) > thr; ys, xs = np.nonzero(sm)
    H, W = d.shape[:2]; y0, y1, x0, x1 = ys.min(), ys.max(), xs.min(), xs.max(); my, mx = int((y1 - y0) * m) + 10, int((x1 - x0) * m) + 10
    return max(0, y0 - my), min(H, y1 + my), max(0, x0 - mx), min(W, x1 + mx)
fit = lambda im, h: im.resize((max(1, int(im.size[0] * h / im.size[1])), h), Image.LANCZOS)
for name, ph, title in PAIRS:
    bm = np.array(Image.open('bm_%s.png' % name).convert('RGB')); bm = bm[:int(bm.shape[0] * 0.93)]      # (without the panel below the screen)
    a, b, c, e = box(bm.max(2) / 255.0, 0.3); bmc = bm[a:b, c:e]
    g0 = np.load('oct/%s.npy' % ph).astype(np.float32); p = np.percentile(g0[g0 > np.percentile(g0, 98)], 95)
    d0 = np.clip(g0 / p * 0.9, 0, 1) ** (1 / 2.2)
    rs = np.array(Image.fromarray(bmc.max(2)).resize((64, 80), Image.BILINEAR), np.float32); best = None
    for k in range(4):
        r = np.rot90(d0, k); y0, y1, x0, x1 = box(r, 0.35); cc = r[y0:y1, x0:x1]
        cs = np.array(Image.fromarray((cc * 255).astype(np.uint8)).resize((64, 80), Image.BILINEAR), np.float32)
        q = np.corrcoef(cs.ravel(), rs.ravel())[0, 1]
        if best is None or q > best[0]: best = (q, cc)
    old = np.load('all_%s_vectrex.npy' % name); a, b, c, e = box(old.max(2) / 255.0, 0.35)
    tiles = [fit(Image.fromarray((best[1] * 255).astype(np.uint8)).convert('RGB'), 900), fit(Image.fromarray(bmc), 900), fit(Image.fromarray(old[a:b, c:e]), 900)]
    for t, n in zip(tiles, ['real Vectrex: ' + title, 'new: beam on phosphor (PC)', 'current default renderer']): ImageDraw.Draw(t).text((10, 8), n, fill=(255, 140, 0))
    im = np.concatenate([np.array(t) for t in tiles], 1)
    Image.fromarray(im).save(r'G:\AI\Vectrex_EMU\testcart\compare\beam_%s.png' % name); print(name, im.shape)
