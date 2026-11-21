"""page 4 grid -> the map units <-> px along a row: x_px = sum a_ij u^i y^j"""
import numpy as np, json
rows = json.load(open('sw/p4_grid.json'))
SP = {705: (8, 30), 889: (16, 15), 1077: (24, 10), 1279: (16, 15), 1484: (16, 15), 1687: (16, 15), 2018: (16, 15), 2281: (16, 15)}
def merge(xs):
    xs = sorted(xs); out = [[xs[0]]]
    for x in xs[1:]:
        if x - out[-1][-1] < 12: out[-1].append(x)
        else: out.append([x])
    return [float(np.mean(o)) for o in out]
EX = [945, 1180, 1420, 1550, 1690, 1840, 1990, 2140, 2340, 2680]; EV = [105, 119, 130, 135, 147, 150, 150, 147, 133, 123]   # (a 16-unit step by place, provisional)
U = []; X = []; Y = []
for r in rows:
    r = np.array(r); y = r[:, 0].mean(); key = min(SP, key=lambda k: abs(k - y))
    if abs(key - y) > 20: continue
    S, P = SP[key]; xs = merge(r[:, 1]); k = 0; ks = [0]
    for a, b in zip(xs, xs[1:]):
        n = (b - a) / (np.interp((a + b) / 2, EX, EV) * S / 16.0); k += int(round(n)); ks.append(k)
        if abs(n - round(n)) > 0.3: print('  ? row', key, 'step %.0f -> %.0f: %.2f steps' % (a, b, n))
    print(key, ks, '(last should be <= %d)' % (P - 1))
    for x, k in zip(xs, ks): U.append(-120 + k * S); X.append(x); Y.append(y)
U = np.array(U) / 100.0; X = np.array(X); Yn = (np.array(Y) - 1500) / 1000.0
IJ = [(i, j) for i in range(6) for j in range(3) if i + j <= 5 and (j < 2 or i < 3)]
keep = np.ones(len(X), bool)
for it in range(3):
    A = np.stack([U ** i * Yn ** j for i, j in IJ], 1)
    c = np.linalg.lstsq(A[keep], X[keep], rcond=None)[0]; r = A @ c - X
    print('n', keep.sum(), 'rms %.2f max %.2f' % (r[keep].std(), abs(r[keep]).max()))
    keep = abs(r) < max(4.0, 2.5 * r[keep].std())
def F(u, y):
    u = np.asarray(u, float) / 100.0; yn = (y - 1500) / 1000.0
    return sum(cc * u ** i * yn ** j for cc, (i, j) in zip(c, IJ))
for y0 in sorted(set(np.round(Y))): print(int(y0), ' '.join('%+.1f' % v for v in r[np.round(Y) == y0]))
np.save('sw/map_c.npy', c); json.dump(IJ, open('sw/map_ij.json', 'w'))
for y in (780, 1004, 1411, 1659):
    print(y, 'px/unit at u=-112,-60,0,60,100:', ' '.join('%.2f' % ((F(u + 1, y) - F(u - 1, y)) / 2) for u in (-112, -60, 0, 60, 100)), ' x(-112)=%.0f' % F(-112, y))
