"""SR Test photos: the dash by the gap G (pages 1-2), the BIOS's single vector by scale (pages 3-4)"""
import numpy as np, pt0, st1
from scipy import ndimage as nd
load = lambda p: np.rot90(np.load('srt/p%d_red.npy' % p).astype(np.float32), -1)
def rows(p, n=15):
    g = load(p); H = g.shape[0]; s = nd.gaussian_filter(g, 2); ys, xs = np.nonzero(s > 0.3 * np.percentile(s, 99.9)); y0, y1 = ys.min(), ys.max()
    L = [d for d in pt0.hlines(g, thr=0.08, minw=20, maxh=30) if y0 + 0.035 * (y1 - y0) < d["y"] < y1 - 0.09 * (y1 - y0) and d["x0"] < g.shape[1] * 0.5 and d["x0"] < xs.min() + 120]
    L.sort(key=lambda d: d['y']); R = []
    for d in L:
        if R and abs(d['y'] - R[-1]['y']) < 25:
            if d['x1'] - d['x0'] > R[-1]['x1'] - R[-1]['x0']: R[-1] = d
        else: R.append(d)
    lev = np.median([np.median(d['prof'][len(d['prof']) // 3:2 * len(d['prof']) // 3]) for d in sorted(R, key=lambda d: d['x0'] - d['x1'])[:5]])
    out = []
    for d in R[:n]:
        e = st1.ends(d['prof'], d['xa'], lev); out.append((e[0], e[1], e[1] - e[0], float(np.max(d['prof'][:max(4, len(d['prof']) // 3)]) / lev)))
    return out
if __name__ == '__main__':
    EVEN = [18, 20, 22, 24, 26, 28, 30, 32, 34, 36, 40, 44, 48, 54, 60]; ODD = list(range(19, 48, 2))
    r1, r2 = rows(1), rows(2); l1 = np.array([r[2] for r in r1]); l2 = np.array([r[2] for r in r2])
    k = np.polyfit(EVEN[6:], l1[6:], 1); print('page 1: %d rows; px a cycle %.2f (rows G >= 30)' % (len(r1), k[0]))
    print('   G    ', ' '.join('%5d' % g for g in EVEN)); print('   dash ', ' '.join('%5.1f' % (v / k[0]) for v in l1)); print('   - G  ', ' '.join('%+5.1f' % (v / k[0] - g) for v, g in zip(l1, EVEN)))
    k2 = np.polyfit(ODD[6:], l2[6:], 1); print('page 2: %d rows; px a cycle %.2f' % (len(r2), k2[0]))
    print('   G    ', ' '.join('%5d' % g for g in ODD)); print('   dash ', ' '.join('%5.1f' % (v / k2[0]) for v in l2)); print('   - G  ', ' '.join('%+5.1f' % (v / k2[0] - g) for v, g in zip(l2, ODD)))
    for p, n0 in ((3, 6), (4, 21)):
        r = rows(p); ppc = (k[0] + k2[0]) / 2
        print('page %d (Draw_VLp, one vector): N %s' % (p, ' '.join('%5d' % (n0 + i) for i in range(len(r)))))
        print('   lit length, cycles of travel: %s' % ' '.join('%5.1f' % (x[2] / ppc) for x in r)); print('   start / line level:           %s' % ' '.join('%5.2f' % x[3] for x in r))
