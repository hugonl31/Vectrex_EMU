"""Switch Test page 1: the rows' profiles along the line"""
import numpy as np, sys
from scipy import ndimage as nd
def load(n): return np.load('sw/112120261828%s_red.npy' % n).astype(np.float32)[::-1, ::-1]
def rows(g):
    """the four long lines: y of each, by the row sums over the lines' x range"""
    s = nd.gaussian_filter1d(g[:, 1000:2400].sum(1), 3)
    pk = [y for y in range(50, len(s) - 50) if s[y] == s[y - 40:y + 41].max() and s[y] > 0.3 * s.max()]
    return pk
def prof(g, y, hw=14):
    """light along the row (the line may tilt: follow its centre), and its centre y by x"""
    H, W = g.shape; e = np.zeros(W); cy = np.zeros(W)
    yc = float(y)
    xs = np.arange(W)
    band = g[y - 40:y + 41]
    m = band.sum(0)
    c = (band * np.arange(-40, 41)[:, None]).sum(0) / np.maximum(m, 1e-6)
    ok = m > 0.2 * np.percentile(m, 95)
    p = np.polyfit(xs[ok], c[ok], 1)
    for x in range(W):
        yy = y + p[0] * x + p[1]; i = int(round(yy))
        e[x] = g[i - hw:i + hw + 1, x].sum()
    return e, p
if __name__ == '__main__':
    g = load(sys.argv[1]); ys = rows(g); print('rows', ys)
    out = {}
    for i, y in enumerate(ys):
        e, p = prof(g, y); out['r%d' % i] = e
        on = np.nonzero(e > 0.25 * np.percentile(e, 90))[0]
        print(i, y, 'tilt %.4f' % p[0], 'x %d..%d' % (on[0], on[-1]), 'median %.2f' % np.median(e[on]))
    np.savez('sw/p1_%s.npz' % sys.argv[1], **out)
