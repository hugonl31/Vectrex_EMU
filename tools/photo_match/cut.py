import numpy as np
from scipy import ndimage as nd
exec(open('dots.py').read())
SP = 7.79
a = np.load('22828_raw.npy')[..., :3].astype(np.float32) / 65535; g = lum(a[::-1, ::-1])
m = nd.gaussian_filter(g, 1.5) > 0.06
lab, n = nd.label(m); objs = nd.find_objects(lab)
shapes = []
for i, s in enumerate(objs):
    h, w = s[0].stop - s[0].start, s[1].stop - s[1].start
    if 60 < max(h, w) < 600 and s[0].start < 1900: shapes.append((s[0].start, s[1].start, i + 1, s))
shapes.sort()
rows = []
for sh in shapes:
    if rows and abs(rows[-1][-1][0] - sh[0]) < 150: rows[-1].append(sh)
    else: rows.append([sh])
def cutdist(k, s):
    ys, xs = np.nonzero(lab[s] == k); ys = ys + s[0].start; xs = xs + s[1].start
    w = g[ys, xs]
    # ridge: per row of pixels (y), the brightness-weighted x
    yy = np.unique(ys); rx = np.array([np.average(xs[ys == y], weights=w[ys == y]) for y in yy])
    tip = np.argmin(rx); vy, vx = yy[tip], rx[tip]
    up = (yy < vy - 3 * SP); dn = (yy > vy + 3 * SP)
    if up.sum() < 5 or dn.sum() < 5: return None
    pu = np.polyfit(yy[up], rx[up], 1); pd = np.polyfit(yy[dn], rx[dn], 1)
    yi = (pd[1] - pu[1]) / (pu[0] - pd[0]); xi = pu[0] * yi + pu[1]
    return (vx - xi) / SP                      # units the tip lies inside the ideal corner
for r in rows[:4]:
    r.sort(key=lambda t: t[1])
    print('row y %4d:' % r[0][0], ' '.join('%.2f' % (cutdist(k, s) or -1) for _, _, k, s in r))
