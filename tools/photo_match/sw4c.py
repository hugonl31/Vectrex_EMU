"""page 4 grid -> the 2-D map: units (u, v) <-> px (x, y) (flipped picture: y down = v down)"""
import numpy as np, json
rows = json.load(open('sw/p4_grid.json'))
SP = {705: (8, 30, 0x58), 889: (16, 15, 0x44), 1077: (24, 10, 0x30), 1279: (16, 15, 0x1C), 1484: (16, 15, 0x08), 1687: (16, 15, -0x0C), 2018: (16, 15, -0x30), 2281: (16, 15, -0x4C)}
EX = [945, 1180, 1420, 1550, 1690, 1840, 1990, 2140, 2340, 2680]; EV = [105, 119, 130, 135, 147, 150, 150, 147, 133, 123]
U = []; Vv = []; X = []; Y = []
for r in rows:
    r = np.array(r); y = r[:, 0].mean(); key = min(SP, key=lambda k: abs(k - y))
    if abs(key - y) > 20: continue
    S, P, v = SP[key]; r = r[np.argsort(r[:, 1])]
    # merge near points
    grp = [[r[0]]]
    for p in r[1:]:
        if p[1] - grp[-1][-1][1] < 12: grp[-1].append(p)
        else: grp.append([p])
    pts = [np.mean(np.array(g_), 0) for g_ in grp]; k = 0
    for i, p in enumerate(pts):
        if i: k += int(round((p[1] - pts[i - 1][1]) / (np.interp((p[1] + pts[i - 1][1]) / 2, EX, EV) * S / 16.0)))
        if k > P - 1: k = P - 1
        # the lines (rows 7, 8) are 8 units tall: their centre 4 units above the start
        U.append(-120 + k * S); Vv.append(v + (4 if key > 1900 else 0)); X.append(p[1]); Y.append(p[0])
U = np.array(U) / 100.; Vv = np.array(Vv) / 100.; X = np.array(X); Y = np.array(Y)
IJ = [(i, j) for i in range(6) for j in range(4) if i + j <= 5]
A = np.stack([U ** i * Vv ** j for i, j in IJ], 1); keep = np.ones(len(X), bool)
for it in range(3):
    cx = np.linalg.lstsq(A[keep], X[keep], rcond=None)[0]; cy = np.linalg.lstsq(A[keep], Y[keep], rcond=None)[0]
    rx = A @ cx - X; ry = A @ cy - Y; print('n', keep.sum(), 'rms x %.2f y %.2f' % (rx[keep].std(), ry[keep].std()))
    keep = (abs(rx) < 6) & (abs(ry) < 6)
np.savez('sw/map2.npz', cx=cx, cy=cy, IJ=np.array(IJ))
def fwd(u, v):
    u = np.asarray(u, float) / 100.; v = np.asarray(v, float) / 100.
    return sum(c * u ** i * v ** j for c, (i, j) in zip(cx, IJ)), sum(c * u ** i * v ** j for c, (i, j) in zip(cy, IJ))
for v in (88, 40, 0, -40, -76):
    for u in (-110, -60, 0, 60, 105):
        x0, y0 = fwd(u, v); x1, y1 = fwd(u + 1, v); x2, y2 = fwd(u, v - 1)
        print('u %4d v %4d -> px %6.0f %6.0f   px/unit x %.2f  y %.2f' % (u, v, x0, y0, x1 - x0, y2 - y0))
