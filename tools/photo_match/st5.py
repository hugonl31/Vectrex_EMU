"""Settle Test, the second set of photos (2026-11-21 21:41): the spot's response in X (pages 1, 2) and in Y (page 4) fitted"""
import numpy as np
from scipy import optimize as op
from scipy.signal import lfilter
OFF_K = [-12, -8, -5, 5, 8, 12, 16, 20, 24, 32, 48]; ON_K = [-12, -8, -4, 5, 8, 12, 16, 20, 24, 32]
X = dict(off=[(40, [25.56, 21.72, 19.04, 9.51, 6.75, 4.22, 2.27, 1.16, 0.36, 0.09, 0.0]), (24, [22.75, 20.55, 18.36, 9.36, 6.82, 4.07, 2.20, 1.13, 0.51, 0.12, 0.0])],
         on=[(48, [0.10, 0.0, 0.09, 0.84, 1.47, 3.18, 5.56, 8.77, 12.27, 19.93]), (40, [0.0, 0.34, 0.43, 0.88, 1.51, 3.09, 5.50, 8.60, 12.12, 20.02])])
Y = dict(off=[(40, [24.46, 20.36, 17.52, 8.08, 5.77, 3.15, 1.66, 0.61, 0.27, 0.0, 0.07])], on=[(40, [0.0, 0.06, 0.13, 0.89, 1.86, 3.91, 6.29, 9.55, 13.11, 20.76])])
DT = 0.05
def pos(L, A, B, taus):
    t = np.arange(-40, L + 120, DT); v = np.clip((t - A) / DT + 1, 0, 1) - np.clip((t - L - B) / DT + 1, 0, 1)
    for tau in taus:
        if tau > 0.02: k = 1 - np.exp(-DT / tau); v = lfilter([k], [1, -(1 - k)], v)
    return t, np.cumsum(v) * DT
def model(q, n, D):
    A, B, tau, base = q; taus = [tau / n] * n; out = []
    for L, d in D['off']:
        t, p = pos(L, A, B, taus); out += [p[-1] - np.interp(L + k, t, p) for k in OFF_K]
    for L, d in D['on']:
        t, p = pos(L, A, B, taus); out += [np.interp(k, t, p) + base for k in ON_K]
    return np.array(out)
def data(D): return np.array([x for L, d in D['off'] for x in d] + [x for L, d in D['on'] for x in d])
def wts(D): return np.array([1.0 if k > 0 else 0.3 for L, d in D['off'] for k in OFF_K] + [1.0 for L, d in D['on'] for k in ON_K])
if __name__ == '__main__':
    for name, D in (('X', X), ('Y', Y)):
        dd, w = data(D), wts(D)
        print('%s: the core today (start 5, stop 6.5 with the beam-off half cycle, lag 6): rms %.3f cycles' % (name, np.sqrt((((model([5, 6.5, 6, 0.2], 1, D) - dd) * w) ** 2).mean())))
        for n in (1, 2, 3, 4):
            best = None
            for A0 in (5, 2, 0):
                r = op.least_squares(lambda q: (model(q, n, D) - dd) * w, [A0, A0 + 2, 6 + 2.5 * n, 0.2], bounds=([-4, -4, 1, 0], [12, 14, 25, 0.6]), diff_step=0.02)
                if best is None or r.cost < best.cost: best = r
            q = best.x; print('   %d stage%s: rms %.3f  start %5.2f  stop %5.2f  (stop - start %.2f)  lag %5.2f   total after a start %.2f, a stop %.2f' % (n, 's' if n > 1 else ' ', np.sqrt((best.fun ** 2).mean()), q[0], q[1], q[1] - q[0], q[2], q[0] + q[2], q[1] + q[2]))
