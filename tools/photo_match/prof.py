"""the mean cross-section of the logo's lines: for ridge pixels on straight pieces, the profile along the normal"""
import numpy as np, sys
from PIL import Image
from scipy import ndimage as nd
def profile(lin, units_w, name):
    sm = nd.gaussian_filter(lin, 0.6)
    big = sm > 0.25 * np.percentile(sm, 99.5); ys, xs = np.nonzero(big); pxu = max(np.ptp(xs), np.ptp(ys)) / units_w
    s2 = nd.gaussian_filter(lin, 0.25 * pxu)
    gy, gx = np.gradient(s2); gyy, gyx = np.gradient(gy); gxy, gxx = np.gradient(gx)
    # ridge: the Hessian's strongest negative curvature direction = the normal; the gradient along it ~ 0
    tr = gxx + gyy; det = gxx * gyy - gxy * gyx; disc = np.sqrt(np.maximum(tr * tr / 4 - det, 0)); l1 = tr / 2 - disc
    nx = gxy; ny = l1 - gxx; nn = np.hypot(nx, ny) + 1e-9; nx /= nn; ny /= nn
    lvl = np.median(s2[(s2 == nd.maximum_filter(s2, 5)) & (s2 > 0.3 * np.percentile(s2, 99.5))])
    cand = np.argwhere((s2 > 0.7 * lvl) & (s2 < 1.25 * lvl) & (np.abs(gx * nx + gy * ny) < 0.02 * lvl) & (l1 < 0))
    rng = np.random.default_rng(1); cand = cand[rng.choice(len(cand), min(4000, len(cand)), replace=False)]
    R = 4.0; t = np.linspace(-R, R, 161); acc = np.zeros_like(t); n = 0
    H, W = lin.shape
    for y, x in cand:
        px = x + t * pxu * nx[y, x]; py = y + t * pxu * ny[y, x]
        if px.min() < 1 or py.min() < 1 or px.max() > W - 2 or py.max() > H - 2: continue
        p = nd.map_coordinates(lin, [py, px], order=1)
        if p[70:91].max() < 0.6 * lvl or p[:20].max() > 0.3 * lvl or p[-20:].max() > 0.3 * lvl: continue     # (one line, nothing beside it)
        c = t[np.argmax(nd.gaussian_filter1d(p, 3))]
        acc += np.interp(t, t - c, p); n += 1
    pr = acc / max(n, 1); pr -= np.median(np.r_[pr[:10], pr[-10:]]); pr /= pr.max()
    w = lambda f: (np.nonzero(pr > f)[0].ptp() + 1) * (t[1] - t[0]) if False else (lambda idx: (idx[-1] - idx[0]) * (t[1] - t[0]))(np.nonzero(pr > f)[0])
    print('%-26s %4d cuts; width (units) at 90%% %.2f  75%% %.2f  50%% %.2f  25%% %.2f  10%% %.2f  5%% %.2f  2%% %.2f  1%% %.2f' % ((name, n) + tuple(w(f) for f in (0.9, 0.75, 0.5, 0.25, 0.1, 0.05, 0.02, 0.01))))
    return t, pr
g = np.load('oct/Vectorblade1.npy').astype(np.float32); tp, pp = profile(g, 236.0, 'photo Vectorblade1')
for f in sys.argv[1:]:
    e = np.array(Image.open(f).convert('RGB')).astype(np.float32).max(2) / 255.0; H = e.shape[0]
    te, pe = profile((e ** 2.2)[int(H * 0.05):int(H * 0.5)], 236.0, f)
np.save('prof_photo.npy', np.stack([tp, pp]))
# a Gaussian and a flatter shape for comparison: exp(-|x/a|^p)
from scipy.optimize import least_squares
m = np.abs(tp) < 2.5
for nm, fn, q0 in (('Gaussian', lambda q, x: np.exp(-0.5 * (x / q[0]) ** 2), [0.4]), ('exp(-|x/a|^p)', lambda q, x: np.exp(-np.abs(x / q[0]) ** q[1]), [0.5, 2.5])):
    s = least_squares(lambda q: fn(q, tp[m]) - pp[m], q0); print('photo fit %-14s %s  rms %.4f' % (nm, np.round(s.x, 3), np.sqrt((s.fun ** 2).mean())))
