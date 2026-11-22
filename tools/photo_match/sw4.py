"""Switch Test page 4 stills: the dots' positions = a grid of known places (8 / 16 / 24 units apart) -> px per unit by place"""
import numpy as np, sys, json
from scipy import ndimage as nd
from sw1 import load
g = load('41850') + load('42884')
s = nd.gaussian_filter(g, 3)
mx = (s == nd.maximum_filter(s, 31)) & (s > 0.004)
ys, xs = np.nonzero(mx)
# sub-pixel centroids
P = []
for y, x in zip(ys, xs):
    if y < 30 or x < 30 or y > g.shape[0] - 30 or x > g.shape[1] - 30: continue
    w = s[y - 8:y + 9, x - 8:x + 9]; w = np.clip(w - w.min(), 0, None)
    P.append((y - 8 + (w.sum(1) * np.arange(17)).sum() / w.sum(), x - 8 + (w.sum(0) * np.arange(17)).sum() / w.sum(), float(s[y, x])))
P = np.array(P); print(len(P))
# rows: cluster by y
o = np.argsort(P[:, 0]); P = P[o]
rows = []; cur = [P[0]]
for p in P[1:]:
    if p[0] - cur[-1][0] < 25: cur.append(p)
    else: rows.append(np.array(cur)); cur = [p]
rows.append(np.array(cur))
out = []
for r in rows:
    if len(r) < 8: continue
    r = r[np.argsort(r[:, 1])]; d = np.diff(r[:, 1])
    print('y %.0f n %d x %.0f..%.0f  steps median %.1f  : %s' % (r[:, 0].mean(), len(r), r[0, 1], r[-1, 1], np.median(d), ' '.join('%.0f' % v for v in d)))
    out.append(r.tolist())
json.dump(out, open('sw/p4_grid.json', 'w'))
