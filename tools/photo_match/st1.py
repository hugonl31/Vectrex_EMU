"""Settle Test photos: every line's lit ends (px along the line), pages 1, 2 and 4"""
import numpy as np, sys
from scipy import ndimage as nd
def load(n): return np.rot90(np.load('st/%s_red.npy' % n).astype(np.float32), -1)      # (the screen upright)
def lines(img, roi, horizontal=True, minlen=6):
    """the line blobs in roi -> list of dict(pos (across), a, b (lit ends along, half level), level, prof)"""
    y0, y1, x0, x1 = roi; g = img[y0:y1, x0:x1]
    if not horizontal: g = g.T
    s = nd.gaussian_filter(g, 1.0); m = s > 0.25 * np.percentile(s, 99.8)
    lab, n = nd.label(nd.binary_dilation(m, iterations=2)); out = []
    for i, sl in enumerate(nd.find_objects(lab)):
        a, b = sl
        if (lab[sl] == i + 1).sum() < 40: continue
        ya, yb = max(0, a.start - 3), min(g.shape[0], a.stop + 3); xa, xb = max(0, b.start - 12), min(g.shape[1], b.stop + 12)
        prof = g[ya:yb, xa:xb].sum(0); out.append(dict(pos=(ya + yb) / 2.0, xa=xa, prof=prof))
    return out
def ends(prof, xa, level):
    """half-level edges (sub-pixel) of the lit stretch"""
    p = nd.gaussian_filter1d(prof, 0.8); on = np.nonzero(p > 0.5 * level)[0]
    if len(on) == 0: return None
    i, j = on[0], on[-1]
    def cross(k0, k1):
        if not (0 <= k0 < len(p) and 0 <= k1 < len(p)): return float(k1)
        return k0 + (0.5 * level - p[k0]) / (p[k1] - p[k0]) * (k1 - k0)
    return xa + cross(i - 1, i), xa + cross(j + 1, j)
def column(img, roi, n, horizontal=True):
    L = sorted(lines(img, roi, horizontal), key=lambda d: d['pos'])
    if len(L) != n: print('  found %d lines, expected %d' % (len(L), n))
    # the line's level: the median of the long lines' middles
    lev = np.median([np.median(d['prof'][len(d['prof']) // 3:2 * len(d['prof']) // 3]) for d in sorted(L, key=lambda d: -len(d['prof']))[:4]])
    return [ends(d['prof'], d['xa'], lev) for d in L], lev
if __name__ == '__main__':
    g = load(sys.argv[1]); print(g.shape)
    s = nd.gaussian_filter(g, 2); ys, xs = np.nonzero(s > 0.3 * np.percentile(s, 99.9)); print('lit box y %d..%d x %d..%d' % (ys.min(), ys.max(), xs.min(), xs.max()))
