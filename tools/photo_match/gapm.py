"""Gap Test page 1 measured on a picture (photo or rendered frame): per row (scales 6 - 13) the dark share of the line
between its first and last lit place, and the line's length relative to the longest row"""
import numpy as np, sys
from scipy import ndimage as nd
def rows(img, n=8):
    """the n long horizontal lines: (y, x0, x1)"""
    s = nd.gaussian_filter(img, 2); m = s > 0.3 * np.percentile(s, 99.7)
    prof = nd.gaussian_filter1d(m.sum(1).astype(float), 4); pk = [y for y in range(31, len(prof) - 31) if prof[y] == prof[y - 30:y + 31].max() and prof[y] > 0.3 * prof.max()]
    pk = sorted(sorted(pk, key=lambda y: -prof[y])[:n]); return pk
def measure(img, blur=0.0):
    if blur > 0: img = nd.gaussian_filter(img, blur)
    out = []
    for y in rows(img):
        band = img[max(0, y - 25):y + 26]; cols = band.max(0)
        # follow a tilted line: the band's max per column is enough
        e = nd.gaussian_filter1d(cols, 1.0); lev = np.percentile(e, 92); lit = e > 0.3 * lev
        xs = np.nonzero(lit)[0]
        # drop the scale's label at the left: the line = the longest run group; take from the first lit column after the widest dark stretch in the first 20 %
        x0, x1 = xs[0], xs[-1]; seg = lit[x0:x1 + 1]
        d = np.diff(np.concatenate([[1], seg.astype(int), [1]])); st = np.nonzero(d == -1)[0]; en = np.nonzero(d == 1)[0]
        gaps = [(a, b - a) for a, b in zip(st, en)]
        big = [g for g in gaps if g[1] > 0.03 * len(seg) and g[0] < 0.25 * len(seg)]
        if big: off = max(big, key=lambda g: g[1]); x0 = x0 + off[0] + off[1]; seg = lit[x0:x1 + 1]
        q = np.clip(e[x0:x1 + 1] / np.median(e[x0:x1 + 1]), 0, 1); deep = q < 0.6
        q2 = e[x0:x1 + 1] / np.median(e[x0:x1 + 1]); hi = nd.gaussian_filter1d(q2, 1.0) > 1.10
        out.append((y, x1 - x0, float((1 - q).mean()), int((np.diff(deep.astype(int)) == 1).sum()), int((np.diff(hi.astype(int)) == 1).sum())))
    L = max(o[1] for o in out)
    return [(o[1] / L, o[2], o[3], o[4]) for o in out]
def photo():
    g = np.load('oct/Gaps.npy').astype(np.float32); best = None
    for k in (1, 3):
        r = np.rot90(g, k)
        try: m = measure(r)
        except Exception: continue
        lens = [x[0] for x in m]
        sc = np.sum(np.diff(lens) > 0) + (len(m) == 8) * 3
        if best is None or sc > best[0]: best = (sc, k, m)
    return best
if __name__ == '__main__':
    sc, k, m = photo(); print('photo (rotation %d, ordered %d)' % (k, sc))
    print('%-28s length: %s | dark share: %s | gaps: %s' % ('photo', ' '.join('%.2f' % x[0] for x in m), ' '.join('%.3f' % x[1] for x in m), ' '.join('%2d' % x[2] for x in m)) + ' | dots: ' + ' '.join('%2d' % x[3] for x in m))
    for t in sys.argv[1:]:
        a = np.load('sl_gap_p1_%s.npy' % t).max(2).astype(np.float32) / 255; m = measure(a, blur=2.0)
        print('%-28s length: %s | dark share: %s | gaps: %s' % (t, ' '.join('%.2f' % x[0] for x in m), ' '.join('%.3f' % x[1] for x in m), ' '.join('%2d' % x[2] for x in m)) + ' | dots: ' + ' '.join('%2d' % x[3] for x in m))
