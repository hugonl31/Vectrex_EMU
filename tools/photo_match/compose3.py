"""the owner's photo | the core today | the candidate (X delay 6, slew 18, lag 7) -> testcart/compare/slew_<name>.png"""
import numpy as np, os
from PIL import Image, ImageDraw
from scipy import ndimage as nd
PAIRS = [('axis', 'oct/10252026220143323', 'Axis Test'), ('beam_p1', 'oct/10252026234453844', 'Beam Test page 1'), ('beam_p2', 'oct/10252026234518728', 'Beam Test page 2'),
         ('beam_p3', 'oct/10252026234543842', 'Beam Test page 3'), ('beam_p4', 'oct/10252026234613703', 'Beam Test page 4'),
         ('corner', 'oct/Cornertest', 'Corner Test'), ('dac_p1', 'oct/Dactest1', 'DAC Test page 1'), ('dac_p2', 'oct/Dactest2', 'DAC Test page 2'),
         ('gap_p1', 'oct/Gaps', 'Gap Test page 1'), ('gap_p2', 'oct/gaps2', 'Gap Test page 2'), ('gap_p3', 'oct/gaps3', 'Gap Test page 3'), ('lag', 'oct/Lagtest', 'Lag Test'),
         ('vectorblade', 'oct/Vectorblade1', 'Vectorblade title'), ('dot_p3', '21790_raw', 'Dot Test page 3'), ('dot_p4', '22828_raw', 'Dot Test page 4'),
         ('intensity', 'oct/Brightness', 'Test Cartridge Rev. 4, INTENSITY')]
def box(d, thr, m=0.04):
    sm = nd.gaussian_filter(d, 2) > thr; ys, xs = np.nonzero(sm)
    H, W = d.shape[:2]; y0, y1, x0, x1 = ys.min(), ys.max(), xs.min(), xs.max(); my, mx = int((y1 - y0) * m) + 10, int((x1 - x0) * m) + 10
    return max(0, y0 - my), min(H, y1 + my), max(0, x0 - mx), min(W, x1 + mx)
fit = lambda im, h: im.resize((max(1, int(im.size[0] * h / im.size[1])), h), Image.LANCZOS)
for name, ph, title in PAIRS:
    g0 = np.load(ph + '.npy').astype(np.float32)
    if g0.ndim == 3: g0 = g0[..., :3].max(2) / 65535.0
    p = np.percentile(g0[g0 > np.percentile(g0, 98)], 95); d0 = np.clip(g0 / p * 0.9, 0, 1) ** (1 / 2.2)
    now = np.load('sl_%s_now.npy' % name); cand = np.load('sl_%s_cand.npy' % name)
    a, b, c, e = box(now.max(2) / 255.0, 0.35); a2, b2, c2, e2 = box(cand.max(2) / 255.0, 0.35); a, b, c, e = min(a, a2), max(b, b2), min(c, c2), max(e, e2)
    rs = np.array(Image.fromarray(now[a:b, c:e].max(2)).resize((64, 80), Image.BILINEAR), np.float32); best = None
    for k in range(4):
        r = np.rot90(d0, k); y0, y1, x0, x1 = box(r, 0.35); cc = r[y0:y1, x0:x1]
        cs = np.array(Image.fromarray((cc * 255).astype(np.uint8)).resize((64, 80), Image.BILINEAR), np.float32)
        q = np.corrcoef(cs.ravel(), rs.ravel())[0, 1]
        if best is None or q > best[0]: best = (q, cc)
    tiles = [fit(Image.fromarray((best[1] * 255).astype(np.uint8)).convert('RGB'), 1000), fit(Image.fromarray(now[a:b, c:e]), 1000), fit(Image.fromarray(cand[a:b, c:e]), 1000)]
    for t, n in zip(tiles, ['real Vectrex: ' + title, 'emulator now', 'candidate: X delay 6, slew 18, lag 7']): ImageDraw.Draw(t).text((10, 8), n, fill=(255, 140, 0))
    im = np.concatenate([np.array(t) for t in tiles], 1)
    Image.fromarray(im).save(r'G:\AI\Vectrex_EMU\testcart\compare\slew_%s.png' % name); print(name, im.shape)
