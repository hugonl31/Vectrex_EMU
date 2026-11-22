import numpy as np
from scipy import ndimage as nd
_m = np.load('sw/map2.npz'); _cx, _cy, _IJ = _m['cx'], _m['cy'], [tuple(v) for v in _m['IJ']]
def fwd(u, v):
    u = np.asarray(u, float) / 100.; v = np.asarray(v, float) / 100.
    return sum(c * u ** i * v ** j for c, (i, j) in zip(_cx, _IJ)), sum(c * u ** i * v ** j for c, (i, j) in zip(_cy, _IJ))
def rect(g, u0, u1, v0, v1, ppu=10):
    """the photo resampled on a grid of units (ppu px a unit; row 0 = v1, the top); light per unit^2"""
    us = np.arange(u0, u1, 1.0 / ppu); vs = np.arange(v1, v0, -1.0 / ppu); U, Vv = np.meshgrid(us, vs)
    X, Y = fwd(U, Vv)
    X1, Y1 = fwd(U + 0.5, Vv); X2, Y2 = fwd(U, Vv - 0.5)
    jac = abs((X1 - X) * (Y2 - Y) - (X2 - X) * (Y1 - Y)) * 4          # px^2 per unit^2
    return nd.map_coordinates(g, [Y, X], order=1) * jac, us, vs
