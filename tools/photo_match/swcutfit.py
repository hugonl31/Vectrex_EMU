"""the corner cuts (and the dot's place) of pages 2 and 3: the model (swmodel.simulate) against the photo's"""
import numpy as np, sys, itertools
import swmodel as M
from swcut import legs
PH = {(2, '-V>+V $20'): (1.23, None), (2, '-V>+V $40'): (2.43, None), (2, '-V>+V $7F'): (5.09, None),
      (3, 'off-12'): (3.19, (7.33, 7.08)), (3, 'off-8'): (2.82, (4.47, 4.70)), (3, 'off-4'): (1.70, (1.75, 2.34)), (3, 'off+0'): (0.07, (-0.30, 0.04)),
      (3, 'off+4'): (0.20, (-0.21, 0.13)), (3, 'on-5'): (0.30, (-0.40, 0.27)), (3, 'on+0'): (1.27, (0.92, -1.08)), (3, 'on+5'): (1.85, (3.16, -3.14))}
D0 = (-0.27, 0.13)                       # (the photo's dot at a sharp corner: its bloom's bias)
def measure(page, name, p):
    t, x, y = M.simulate(page, name, p); P = np.stack([x, y], 1); r = legs(P[::2])
    if r is None: return None
    I, cut, s1, s2 = r
    sp = np.hypot(np.gradient(x), np.gradient(y)) / M.DT; d = np.hypot(x - I[0], y - I[1]); m = (d < 12) & (sp < 0.15 * np.percentile(sp, 90))
    dot = (x[m].mean() - I[0], y[m].mean() - I[1]) if m.sum() > 20 else None
    return cut, dot
def resid(p, detail=False):
    r = []; out = {}
    for k, (cut, dot) in PH.items():
        m = measure(k[0], k[1], p)
        if m is None: r += [2.0, 2.0, 2.0] if dot is not None else [2.0]; continue
        r.append(m[0] - cut)
        if dot is not None: r += [0.5 * (m[1][0] - (dot[0] - D0[0])), 0.5 * (m[1][1] - (dot[1] - D0[1]))] if m[1] is not None else [1.0, 1.0]
        out[k] = m
    return (np.array(r), out) if detail else np.array(r)
def show(label, p):
    r, out = resid(p, True)
    print('%-52s rms %.3f | cuts: %s' % (label, np.sqrt((r ** 2).mean()), ' '.join('%.2f' % out[k][0] for k in PH if k in out)))
    print('%-52s           | dots: %s' % ('', ' '.join('%+.1f,%+.1f' % out[k][1] if out[k][1] else '-' for k in PH if k in out and PH[k][1])))
    return r
if __name__ == '__main__':
    print('%-52s           | cuts: %s' % ('photo', ' '.join('%.2f' % v[0] for v in PH.values())))
    print('%-52s           | dots: %s' % ('', ' '.join('%+.1f,%+.1f' % (v[1][0] - D0[0], v[1][1] - D0[1]) for v in PH.values() if v[1])))
    show('emulator (5, 7, X 9, lag 6)', [5, 7, 9, 0, 0, 6, 1, 0, 0])
    from scipy import optimize as op
    for label, n, useS, useRC in (('delay + lag', 1, 0, 0), ('+ slew', 1, 1, 0), ('+ RC', 1, 0, 1), ('+ slew + RC', 1, 1, 1), ('2-stage lag + slew', 2, 1, 0)):
        best = None
        for q0 in ([5, 7, 8, 30, 2, 6], [5, 7, 5, 60, 4, 7], [5, 7, 3, 25, 1, 6]):
            f = lambda q: resid([q[0], q[1], q[2], q[3] if useS else 0, q[4] if useRC else 0, q[5], n, 0, 0])
            r = op.least_squares(f, q0, bounds=([3, 5, 0, 8, 0, 3], [8, 10, 12, 400, 10, 12]), diff_step=0.03)
            if best is None or r.cost < best.cost: best = r
        q = best.x; p = [q[0], q[1], q[2], q[3] if useS else 0, q[4] if useRC else 0, q[5], n, 0, 0]
        show('%s: on %.1f off %.1f X %.1f S %.0f RC %.1f lag %.1f' % (label, *p[:6]), p)
