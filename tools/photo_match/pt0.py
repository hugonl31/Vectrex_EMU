"""Photo Test photos: loading by pass and page; the horizontal lines of a page (ends, light a px along, width)"""
import numpy as np, glob, os
from scipy import ndimage as nd
F = sorted(glob.glob('pt/*_red.npy'))
PASS = {'f1': F[0:12], 'f4': F[12:24], 'rate': F[24:28]}
def load(p, page): return np.rot90(np.load(PASS[p][page - 1 if p != 'rate' else page - 13]).astype(np.float32), -1)
def hlines(g, roi=None, thr=0.2, minw=25, maxh=40):
    """blobs wider than tall: dict(x0, x1, y, light (sum across, per px along, the middle half), width FWHM, peak)"""
    y0, y1, x0, x1 = roi or (0, g.shape[0], 0, g.shape[1]); c = g[y0:y1, x0:x1]
    s = nd.gaussian_filter(c, 1.0); m = s > thr * np.percentile(s, 99.9); lab, k = nd.label(nd.binary_dilation(m, iterations=2)); out = []
    bg = np.median(c)
    for i, sl in enumerate(nd.find_objects(lab)):
        a, b = sl; h, w = a.stop - a.start, b.stop - b.start
        if w < minw or h > maxh or w < 2.5 * h: continue
        ym = (a.start + a.stop) // 2; ya, yb = max(0, ym - 22), min(c.shape[0], ym + 23)
        prof = (c[ya:yb, b.start:b.stop] - bg).sum(0)
        q = prof[w // 4:3 * w // 4] if w >= 8 else prof
        col = (c[ya:yb, b.start + w // 4:b.start + 3 * w // 4] - bg).sum(1); pk = col.max(); on = np.nonzero(col > 0.5 * pk)[0]
        out.append(dict(x0=b.start + x0, x1=b.stop + x0, y=ym + y0, light=float(np.median(q)), width=float(len(on)), peak=float(c[a, b].max()), prof=prof, xa=b.start + x0))
    return out
def rows_cols(L, ncol):
    """sort the blobs into columns (by x0) and rows (by y)"""
    xs = np.array([d['x0'] for d in L]); o = np.argsort(xs); gaps = np.argsort(np.diff(xs[o]))[::-1][:ncol - 1]; cuts = sorted(xs[o][g_ + 1] for g_ in gaps)
    cols = [[] for _ in range(ncol)]
    for d in L: cols[int(np.searchsorted(cuts, d['x0'], side='right'))].append(d)
    return [sorted(c, key=lambda d: d['y']) for c in cols]
