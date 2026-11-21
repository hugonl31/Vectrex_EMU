import numpy as np, json
_c = np.load('sw/map_c.npy'); _IJ = json.load(open('sw/map_ij.json'))
def F(u, y):
    u = np.asarray(u, float) / 100.0; yn = (y - 1500) / 1000.0
    return sum(cc * u ** i * yn ** j for cc, (i, j) in zip(_c, _IJ))
_ug = np.linspace(-135, 125, 2601)
def Finv(x, y):
    return np.interp(x, F(_ug, y), _ug)
def unitprof(e, y, du=0.02, u0=-125.0, u1=120.0):
    """a row's light per px -> light per unit on a regular grid of units"""
    ug = np.arange(u0, u1, du); xg = F(ug, y); dx = np.gradient(xg, du)        # px per unit
    return ug, np.interp(xg, np.arange(len(e)), e) * dx
