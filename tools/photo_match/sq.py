import numpy as np, sys
from scipy import ndimage as nd
from PIL import Image
def squares(d, thr):
    m = nd.gaussian_filter(d, 1.5) > thr
    big = nd.binary_dilation(m, iterations=max(3, int(d.shape[1] * 0.006)))          # (open corners, gaps: joined)
    lab, n = nd.label(big); out = []
    for i, s in enumerate(nd.find_objects(lab)):
        h, w = s[0].stop - s[0].start, s[1].stop - s[1].start
        if min(h, w) < 0.03 * d.shape[1] or abs(h - w) > 0.4 * max(h, w): continue
        sub = d[s] * (lab[s] == i + 1) * m[s]
        if sub.sum() == 0: continue
        px = sub.sum(0); py = sub.sum(1)
        def edges(p):
            t = 0.5 * p.max(); idx = np.nonzero(p > t)[0]
            lo = idx[idx < len(p) / 2]; hi = idx[idx >= len(p) / 2]
            return np.average(lo, weights=p[lo]), np.average(hi, weights=p[hi])
        try:
            x0, x1 = edges(px); y0, y1 = edges(py)
        except Exception: continue
        out.append((s[0].start + (s[0].stop - s[0].start) // 2, s[1].start, x1 - x0, y1 - y0))
    out.sort(key=lambda t: -(t[2] * t[3])); out = out[:8]
    ys = sorted(o[0] for o in out); mid = (ys[3] + ys[4]) / 2 if len(out) == 8 else 0
    return sorted(out, key=lambda t: (t[0] > mid, t[1]))
def table(sq, name):
    flat = sq
    w = np.array([q[2] for q in flat]); h = np.array([q[3] for q in flat])
    print('%-22s widths  /13: %s' % (name, ' '.join('%.3f' % v for v in w / w[-1])))
    print('%-22s heights /13: %s' % ('', ' '.join('%.3f' % v for v in h / h[-1])))
    return w / w[-1], h / h[-1]
g = np.load('oct/gaps3.npy').astype(np.float32); g = np.rot90(g, -1)
p = np.percentile(g[g > np.percentile(g, 98)], 95)
PW, PH = table(squares(np.clip(g / p, 0, 1), 0.25), 'photo gaps3')
for f in sys.argv[1:]:
    e = np.load(f).max(2) / 255.0; table(squares(e, 0.3), f)
N = np.array([6, 7, 8, 9, 16, 17, 18, 19.])
for nm, r in (('photo width', PW), ('photo height', PH)):
    # side = k (n + D): r = (n + D) / (19 + D)  ->  D from a least-squares fit
    Ds = np.linspace(-4, 30, 3401); err = [((r - (N + D) / (19 + D)) ** 2).sum() for D in Ds]; print('%s: extra cycles a line D = %.1f' % (nm, Ds[int(np.argmin(err))]))
