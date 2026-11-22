import numpy as np, sys
from PIL import Image
from scipy import ndimage as nd
def width(lin, units_w, name, frac=0.5):
    """mean line width at `frac` of the line's own level (area / length), in units; the logo is units_w wide"""
    sm = nd.gaussian_filter(lin, 0.7); mx = nd.maximum_filter(sm, 7)
    lvl = np.median(sm[(sm == mx) & (sm > 0.3 * np.percentile(sm, 99.5))])
    big = sm > 0.25 * lvl; ys, xs = np.nonzero(big); pxu = max(xs.max() - xs.min(), ys.max() - ys.min()) / units_w
    out = []
    for f in (0.5, 0.25, 0.1):
        m = sm > f * lvl; edge = m & ~nd.binary_erosion(m)
        out.append(m.sum() / (edge.sum() / 2.0) / pxu)
    bgm = ~nd.binary_dilation(big, iterations=int(3 * pxu)); haze = np.median(sm[nd.binary_dilation(big, iterations=int(2.5 * pxu)) & ~nd.binary_dilation(big, iterations=int(1.5 * pxu))]) - np.median(sm[bgm])
    print('%-30s %.1f px/unit  line level %.3f  width at 50%% %.2f u, 25%% %.2f u, 10%% %.2f u;  haze 1.5-2.5 u from a line: %.4f of the line' % (name, pxu, lvl, out[0], out[1], out[2], haze / lvl))
g = np.load('oct/Vectorblade1.npy').astype(np.float32); width(g, 236.0, 'photo Vectorblade1 (linear)')
g2 = np.load('oct/Vectorblade2.npy').astype(np.float32); width(g2, 236.0, 'photo Vectorblade2 (linear)')
for f in sys.argv[1:]:
    e = np.array(Image.open(f).convert('RGB')).astype(np.float32).max(2) / 255.0; H = e.shape[0]
    width((e ** 2.2)[int(H * 0.05):int(H * 0.5)], 236.0, f)
