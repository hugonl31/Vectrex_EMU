"""Settle Test pages 1 and 2: the spot's response fitted - the speed after RAMP on = V S(t - A), after RAMP off V (1 - S(t - B)),
S = the step response of n equal lag stages (tau in all) or of two unequal ones; A, B = the RAMP delays less the beam switch's.
The data: travel still to come at the switch-off (page 1), travel done at the switch-on (page 2), cycles."""
import numpy as np
from scipy import optimize as op
from scipy.signal import lfilter
OFF = {0x50: (40, [(-12, 26.28), (-8, 22.26), (-5, 19.23), (5, 9.90), (8, 7.29), (12, 4.34), (16, 2.64), (20, 1.37), (24, 0.78), (32, 0.20), (48, 0.0)]),
       0x7F: (24, [(-12, 22.79), (-8, 20.57), (-5, 18.33), (5, 9.39), (8, 6.77), (12, 4.03), (16, 2.24), (20, 1.13), (24, 0.49), (32, 0.11), (48, 0.0)])}
ON = {0x50: (48, [(-12, 0.14), (-8, 0.0), (-4, 0.0), (5, 0.67), (8, 1.28), (12, 2.84), (16, 5.17), (20, 8.22), (24, 11.64), (32, 19.25)]),
      0x7F: (40, [(-12, 0.0), (-8, 0.31), (-4, 0.36), (5, 0.81), (8, 1.41), (12, 3.00), (16, 5.42), (20, 8.51), (24, 12.00), (32, 19.92)])}
DT = 0.05
def pos(L, A, B, taus):
    """the spot's travel (cycles of full speed) by time, t = 0 at the RAMP-on write; t grid -40 .. L + 120"""
    t = np.arange(-40, L + 120, DT); v = ((t >= A) & (t < L + B)).astype(float)
    for tau in taus:
        if tau > 0.02: k = 1 - np.exp(-DT / tau); v = lfilter([k], [1, -(1 - k)], v)
    return t, np.cumsum(v) * DT
def model(q, kind):
    A, B = q[0], q[1]
    taus = [q[2] / kind] * kind if kind > 0 else [q[2], q[3]]
    out = []
    for v in (0x50, 0x7F):
        L, d = OFF[v]; t, p = pos(L, A, B, taus); tot = p[-1]
        out += [tot - np.interp(L + k, t, p) for k, _ in d]
        L, d = ON[v]; t, p = pos(L, A, B, taus)
        out += [np.interp(k, t, p) + q[-1] for k, _ in d]
    return np.array(out)
DATA = np.array([x for v in (0x50, 0x7F) for x in [r for _, r in OFF[v][1]] + [r for _, r in ON[v][1]]])
W = np.array([x for v in (0x50, 0x7F) for x in [1.0 if k > 0 else 0.3 for k, _ in OFF[v][1]] + [1.0] * len(ON[v][1])])
if __name__ == '__main__':
    print('the emulator today (start 5, stop 7, lag 6, beam-off 0.5): rms %.3f cycles' % np.sqrt((((model([5, 6.5, 6, 0], 1) - DATA) * W) ** 2).mean()))
    for name, kind, q0, lo, hi in (('1 stage', 1, [5, 7, 6, 0.2], [0, 0, 1, 0], [12, 14, 20, 0.6]), ('2 equal stages', 2, [4, 6, 8, 0.2], [-4, -4, 1, 0], [12, 14, 20, 0.6]),
                                   ('3 equal stages', 3, [3, 5, 9, 0.2], [-4, -4, 1, 0], [12, 14, 25, 0.6]), ('4 equal stages', 4, [2, 4, 10, 0.2], [-6, -6, 1, 0], [12, 14, 25, 0.6]),
                                   ('2 unequal stages', 0, [4, 6, 6, 2, 0.2], [-4, -4, 0.5, 0.1, 0], [12, 14, 20, 20, 0.6])):
        r = op.least_squares(lambda q: (model(q, kind) - DATA) * W, q0, bounds=(lo, hi))
        q = r.x; m = model(q, kind)
        print('%-17s rms %.3f cycles: start %.2f  stop %.2f (stop - start %.2f)  lag %s  (baseline %.2f)' % (name, np.sqrt((r.fun ** 2).mean()), q[0], q[1], q[1] - q[0], ('%.2f in all' % q[2]) if kind else '%.2f + %.2f' % (q[2], q[3]), q[-1]))
        if name in ('1 stage', '2 unequal stages', '4 equal stages'):
            n1 = len(OFF[0x50][1]); n2 = len(ON[0x50][1])
            print('      $7F OFF  K>0: data %s' % ' '.join('%5.2f' % r_ for k, r_ in OFF[0x7F][1] if k > 0)); print('                    fit  %s' % ' '.join('%5.2f' % x for (k, _), x in zip(OFF[0x7F][1], m[n1 + n2:n1 + n2 + n1]) if k > 0))
            print('      $7F ON   K>0: data %s' % ' '.join('%5.2f' % r_ for k, r_ in ON[0x7F][1] if k > 0)); print('                    fit  %s' % ' '.join('%5.2f' % x for (k, _), x in zip(ON[0x7F][1], m[n1 + n2 + n1:]) if k > 0))
