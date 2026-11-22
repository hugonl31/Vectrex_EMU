"""Settle Test (second set), X and Y together, in px: one spot response (n stages, lag), RAMP start A / stop B (against the
beam's switch), the X speed's own delay xd (it is written one cycle after RAMP on: until it arrives the line does not move),
a px-a-cycle scale per column. No 'whole line = L + 2' assumed."""
import numpy as np, st1, st2
from scipy import optimize as op
from scipy.signal import lfilter
st1.load = lambda n: np.rot90(np.load('st/%s_red.npy' % n).astype(np.float32), -1)
OFF_K = st2.OFF_K; ON_K = st2.ON_K; DT = 0.05
def px(n, roi, nrows, horizontal=True):
    E, lev = st1.column(st1.load(n), roi, nrows, horizontal); return [b - a for a, b in E]
D = [('x', 'off', 40, px('36374', (1440, 2830, 700, 1100), 11)), ('x', 'off', 24, px('36374', (1440, 2830, 1400, 1900), 11)),
     ('x', 'on', 48, px('37415', (1440, 2760, 660, 1150), 10)), ('x', 'on', 40, px('37415', (1440, 2760, 1300, 1900), 10))]
yl = px('39502', (1570, 1900, 650, 2300), 21, False); D += [('y', 'off', 40, yl[:11]), ('y', 'on', 40, yl[11:])]
def pos(L, A, B, taus):
    t = np.arange(-40, L + 120, DT); v = np.clip((t - A) / DT + 1, 0, 1) - np.clip((t - L - B) / DT + 1, 0, 1)
    for tau in taus:
        k = 1 - np.exp(-DT / tau); v = lfilter([k], [1, -(1 - k)], v)
    return t, np.cumsum(v) * DT
def model(q, n):
    A, B, lag, xd = q[:4]; sc = q[4:]; out = []
    for (ax, kind, L, d), s in zip(D, sc):
        a = max(A, 1 + xd) if ax == 'x' else A
        t, p = pos(L, a, B, [lag / n] * n)
        if kind == 'off': out += [s * (np.interp(L + k, t, p) - np.interp(8, t, p)) for k in OFF_K]      # lit from the beam-on (8 before... t = -12) to the switch-off
        else: out += [s * (p[-1] - np.interp(k, t, p)) for k in ON_K]
    return np.array(out)
data = np.array([x for _, _, _, d in D for x in d])
for n in (1, 2, 3, 4):
    best = None
    for A0, xd0 in ((4, 4), (0, 1), (0, 3), (2, 5)):
        q0 = [A0, A0 + 2, 5 + 3 * n, xd0] + [d[-1] / (L + 2.0) if k == 'off' else d[0] / (L + 2.0) for _, k, L, d in D]
        r = op.least_squares(lambda q: model(q, n) - data, q0, bounds=([-6, -6, 1, -4] + [1] * 6, [12, 14, 30, 14] + [20] * 6), diff_step=0.01)
        if best is None or r.cost < best.cost: best = r
    q = best.x; res = best.fun
    print('%d stage%s: rms %.2f px (%.2f cycles)  RAMP start %5.2f stop %5.2f (stop - start %.2f)  lag %5.2f  X speed arrives %.2f after its write (%.2f after RAMP start)  px a cycle %s' % (
        n, 's' if n > 1 else ' ', np.sqrt((res ** 2).mean()), np.sqrt(((res / np.repeat(q[4:], [len(d) for *_, d in D])) ** 2).mean()), q[0], q[1], q[1] - q[0], q[2], q[3], 1 + q[3] - q[0], ' '.join('%.2f' % s for s in q[4:])))
