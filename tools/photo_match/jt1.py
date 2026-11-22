"""Join Test photos, pages 1 and 2 (the BIOS's line by hand): lit length per row -> travel in cycles; and the core's own"""
import numpy as np, st1, sys, os
from scipy import ndimage as nd
load = lambda n: np.rot90(np.load('jt/%s_red.npy' % n).astype(np.float32), -1)
N = [6, 8, 11, 13]; K1 = [2, 5, 8, 11, 14, 17, 20, 24, 32]; K2 = [-12, -8, -4, 4, 8, 12, 16, 20]
def page(n, nrows, y0=1500, y1=2900):
    g = load(n); s = nd.gaussian_filter(g, 2); m = s[y0:y1] > 0.25 * np.percentile(s, 99.9)
    e = np.nonzero(np.diff((nd.binary_dilation(m.sum(0) > 2, iterations=25)).astype(int)))[0]; C = [(int(a) - 20, int(b) + 20) for a, b in zip(e[0::2], e[1::2])]
    out = []; lev = None
    for (xa, xb) in C[::-1]:
        roi = (y0, y1, xa, xb); L = sorted(st1.lines(g, roi), key=lambda d: d['pos'])
        if lev is None: lev = np.median([np.median(d['prof'][len(d['prof']) // 3:2 * len(d['prof']) // 3]) for d in sorted(L, key=lambda d: -len(d['prof']))[:3]])
        # one line per row: rows by position
        ys = np.array([d['pos'] for d in L]); rows = np.linspace(ys.min(), ys.max(), nrows); E = []
        for r in rows:
            d = L[int(np.argmin(abs(ys - r)))]; e2 = st1.ends(d['prof'], d['xa'], lev); E.append(e2[1] - e2[0] if e2 else np.nan)
        out.append(E)
    return out[::-1]
if __name__ == '__main__':
    P1 = page('39401', 9); P2 = page('40441', 8)
    print('the tube, page 1 (shift register off K after t0 + N): the travel still to come, cycles (the whole line = N + 4.8)')
    print('     K   %s' % ' '.join('%6d' % k for k in K1))
    for n_, ln in zip(N, P1):
        ln = np.array(ln); full = np.nanmedian(ln[-2:]); ppc = full / (n_ + 4.8); print('   N %2d  %s   (%.2f px a cycle)' % (n_, ' '.join('%6.2f' % ((full - x) / ppc) for x in ln), ppc))
    print('the tube, page 2 (shift register on K after t0): the travel done before the beam came on')
    print('     K   %s' % ' '.join('%6d' % k for k in K2))
    for n_, ln in zip(N, P2):
        ln = np.array(ln); full = np.nanmax(ln[:3]); ppc = full / (n_ + 4.8); print('   N %2d  %s' % (n_, ' '.join('%6.2f' % ((full - x) / ppc) for x in ln)))
