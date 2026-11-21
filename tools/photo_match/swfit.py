import numpy as np
from scipy.optimize import least_squares
RS = np.array([5, 8, 11, 16, 24, 32.]); M = np.array([1.5, 4.9, 7.4, 5.9, 2.1, 0.1])
def travel(R, tf, tr, t0, t1, dt=0.05, L=60.0):
    # the control: 1 = RAMP on. Off for R cycles, then on again; the integrators' rate g(v) between t0 (stopped) and t1 (full)
    v = 1.0; lost = 0.0; kf = dt / tf; kr = dt / tr
    for i in range(int((R + L) / dt)):
        on = i * dt >= R
        v += ((1.0 - v) * kr) if on else (-v * kf)
        g = min(1.0, max(0.0, (v - t0) / (t1 - t0)))
        lost += (1.0 - g) * dt
    return R - lost
def res(q):
    tf, tr, t0, w = q
    return [travel(R, tf, tr, t0, t0 + w) - m for R, m in zip(RS, M)]
best = None
for tf0 in (6, 12, 20):
    for tr0 in (2, 6, 12):
        for t00 in (0.2, 0.5):
            s = least_squares(res, [tf0, tr0, t00, 0.2], bounds=([1, 0.3, 0.02, 0.02], [80, 60, 0.9, 0.9]))
            if best is None or s.cost < best.cost: best = s
tf, tr, t0, w = best.x
print('fall tau %.1f, rise tau %.1f, the rate from 0 at control %.2f to full at %.2f; rms %.2f cycles' % (tf, tr, t0, t0 + w, np.sqrt(2 * best.cost / len(RS))))
print('pulse:   ', ' '.join('%5d' % r for r in (3, 5, 8, 11, 16, 24, 32, 48, 100)))
print('model:   ', ' '.join('%5.1f' % travel(r, tf, tr, t0, t0 + w) for r in (3, 5, 8, 11, 16, 24, 32, 48, 100)))
print('measured:', '      ' + ' '.join('%5.1f' % m for m in M))
# short RAMP-on pulses (a line of n cycles from rest): the cycles really integrated
def line(n, dt=0.05):
    v = 0.0; got = 0.0
    for i in range(int((n + 80) / dt)):
        on = i * dt < n
        v += ((1.0 - v) * dt / tr) if on else (-v * dt / tf)
        got += min(1.0, max(0.0, (v - t0) / w)) * dt
    return got
print('a line of n cycles integrates:', ' '.join('%d: %.1f' % (n, line(n)) for n in (4, 6, 8, 12, 16, 24, 40, 100)))
