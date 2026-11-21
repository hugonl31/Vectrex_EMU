"""Vectorblade's logo: the emulator's lines laid on the owner's photo (a similarity transform, fitted) - the mean
distance between the two line sets, in units (1 unit = 1/236 of the old logo's length)."""
import numpy as np, sys, os, subprocess, json
from scipy import ndimage as nd
from scipy.spatial import cKDTree
from scipy.optimize import minimize
CODE = r'''
import sys, os, json
sys.path.insert(0, r'G:\AI\Vectrex_EMU\src\frontend'); os.chdir(r'G:\AI\Vectrex_EMU')
import vectrex_emu as V
c = V.Core(os.path.abspath(r'build\Vectrex_EMU\roms\Vectorblade.bin'), False, 'auto', 'off', None, None, None, False)
for i in range(760): L = c.frame(0, 0, 0, 0)[0]
print(json.dumps([[l[0], l[1], l[2], l[3]] for l in L if int(l[4]) & 127 and not int(l[4]) & 128]))
'''
def photo_pts(f='oct/Vectorblade1.npy'):
    g = np.load(f).astype(np.float32); sm = nd.gaussian_filter(g, 2.0); mx = nd.maximum_filter(sm, 5)
    lvl = np.percentile(sm, 99.5); ys, xs = np.nonzero((sm > 0.35 * lvl))
    # thin: keep ridge-ish pixels (the local maximum along x or y)
    r = (sm >= np.roll(sm, 1, 0)) & (sm >= np.roll(sm, -1, 0)) | (sm >= np.roll(sm, 1, 1)) & (sm >= np.roll(sm, -1, 1))
    ys, xs = np.nonzero((sm > 0.35 * lvl) & r); P = np.stack([xs, ys], 1).astype(float)
    return P[::3]
def emu_pts(env):
    e = dict(os.environ); e.update(env); r = subprocess.run([sys.executable, '-c', CODE], env=e, capture_output=True, text=True)
    L = np.array(json.loads(r.stdout.strip().splitlines()[-1])); out = []
    for x0, y0, x1, y1 in L:
        n = max(1, int(np.hypot(x1 - x0, y1 - y0) / 40)); t = (np.arange(n) + 0.5) / n
        out.append(np.stack([x0 + (x1 - x0) * t, -(y0 + (y1 - y0) * t)], 1))
    return np.concatenate(out) / 134.0          # units
def norm(P):
    c = P.mean(0); q = P - c; ev, evec = np.linalg.eigh(q.T @ q); a = q @ evec[:, 1]; b = q @ evec[:, 0]
    return np.stack([a, b], 1)
PH = norm(photo_pts()); PH /= (np.percentile(PH[:, 0], 99.5) - np.percentile(PH[:, 0], 0.5)) / 236.0     # photo in "units" (old logo length 236)
TP = cKDTree(PH)
def score(E):
    E = norm(E); best = None
    for sx in (1, -1):
        for sy in (1, -1):
            Q = E * [sx, sy]
            def f(q):
                s, th, tx, ty, ay = q; c, sn = np.cos(th), np.sin(th); T = (Q * [1.0, ay]) @ np.array([[c, -sn], [sn, c]]).T * s + [tx, ty]   # (ay: the tube's own height / width)
                d1 = TP.query(T)[0]; d2 = cKDTree(T).query(PH)[0]
                return np.mean(np.minimum(d1, 6.0)) + np.mean(np.minimum(d2, 6.0))
            r = minimize(f, [1.0, 0.0, 0.0, 0.0, 1.0], method='Nelder-Mead', options={'xatol': 1e-3, 'fatol': 1e-4, 'maxiter': 700})
            if best is None or r.fun < best[0]: best = (r.fun, r.x)
    return best[0] / 2, best[1][0], best[1][4]
if __name__ == '__main__':
    for name, env in [(a.split('=', 1)[0], dict(kv.split('=') for kv in a.split('=', 1)[1].split(';') if kv)) for a in sys.argv[1:]]:
        E = emu_pts(env); d, s, ay = score(E); n = norm(E)
        print('%-28s mean distance %.3f units   (fitted scale %.3f, height x %.3f; logo length %.0f units)' % (name, d, s, ay, np.ptp(n[:, 0])))
