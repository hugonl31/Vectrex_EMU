import numpy as np, sys, json
from scipy import ndimage as nd
from decay1 import frames, applelog
NPOS = [30, 15, 10, 15, 15, 15, 15, 15]
STEP = [1, 2, 3, 2, 2, 2, 2, 2]                          # a row's step, in row 1's steps (8 units)
def lattice(mx):
    sm = nd.gaussian_filter(mx, 4); H, W = sm.shape
    rows = []; fits = []
    for y0, n, st in zip((256, 390, 524, 670, 810, 940, 1180, 1360), NPOS, STEP):
        pr = sm[y0 - 30:y0 + 31, 150:].max(1); y = y0 - 30 + int(np.argmax(pr))
        band = nd.gaussian_filter1d(sm[y - 25:y + 26].max(0), 3)
        pk = [x for x in range(20, W - 20) if band[x] == band[max(0, x - 18):x + 19].max() and band[x] > 0.2 * band.max()]
        fits.append((y, np.array(pk)))
    # the lattice: x = x0 + k dx (row 1: every k, others every STEP), from row 1's 30 peaks
    y1, p1 = fits[0]; p1 = np.sort(p1)[:30]; dx = np.median(np.diff(p1)); x0 = p1[0]
    for (y, pk), n, st in zip(fits, NPOS, STEP):
        k = np.round((pk - x0) / (dx * st)); ok = (k >= 0) & (k < n) & (np.abs(pk - x0 - k * dx * st) < 0.25 * dx * st)
        c = np.polyfit(k[ok], pk[ok], 1) if ok.sum() >= 3 else (dx * st, x0)
        ks = np.arange(n); xs = np.polyval(c, ks).round().astype(int)
        yy = [int(y - 20 + np.argmax(sm[y - 20:y + 21, max(0, min(W - 1, x))])) for x in xs]
        yfit = np.polyval(np.polyfit(ks, yy, 1), ks).round().astype(int)
        rows.append(list(zip(yfit.tolist(), xs.tolist())))
    return rows
if __name__ == '__main__':
    tag = sys.argv[1]; clip = [c for c in ('A001_11211859_C007.mov', 'A001_11211901_C009.mov') if tag in c][0]
    mx = np.load('dec_max_C009.npy'); rows = lattice(mx)
    print('rows:', [(r[0][0], r[0][1], r[-1][1], len(r)) for r in rows])
    out = []
    BX = [(14, 14)] * 6 + [(40, 14)] * 2                 # half box (y, x): dots, lines
    for fr in frames(clip):
        lin = applelog(fr); v = []
        for ri, r in enumerate(rows):
            hy, hx = BX[ri]; vr = []
            for (y, x) in r:
                b = lin[y - hy:y + hy + 1, x - hx:x + hx + 1]
                ring = np.concatenate([lin[y - hy - 6:y - hy, x - hx:x + hx + 1].ravel(), lin[y + hy + 1:y + hy + 7, x - hx:x + hx + 1].ravel()])
                vr.append(float(b.sum() - np.median(ring) * b.size))
            v.append(vr)
        out.append(v)
    json.dump(dict(rows=[[list(map(int, p)) for p in r] for r in rows], data=out), open('dec_%s.json' % tag, 'w')); print(tag, len(out), 'frames')
