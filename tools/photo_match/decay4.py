import numpy as np
from scipy.optimize import least_squares
from decay3 import curves, NAMES
C = curves('C009')
def series(ri, nmax):
    f, _ = C[ri]['first']; s, _ = C[ri]['second']
    t = []; v = []
    for n in range(nmax):
        if n >= 1: t.append((20.0 * n, 0)); v.append(f[n])
        t.append((20.0 * n + 10.0, 0)); v.append(s[n])
    return np.array([a for a, _ in t]), np.array(v), f[0]
res = {}
for ri, nmax in ((6, 14), (7, 14), (0, 6), (1, 5), (3, 5), (4, 5), (5, 5)):
    t, v, fresh = series(ri, nmax); ok = v > 0
    f = lambda q: np.log(q[0] * (t[ok] + q[2]) ** -q[1]) - np.log(v[ok])
    s = least_squares(f, [v[0] * 20, 1.3, 2.0], bounds=([0, 0.3, -8], [1e9, 4, 15])).x
    res[ri] = s
    print('%-12s fresh frame %7.0f | light = A (t + %.1f ms)^-%.2f, rms %.0f %% | at 10 20 40 80 160 ms: %s' % (NAMES[ri], fresh, s[2], s[1], 100 * np.sqrt(np.mean(f(s) ** 2)), ' '.join('%.3g' % (s[0] * (x + s[2]) ** -s[1]) for x in (10, 20, 40, 80, 160))))
# the trail against a steadily redrawn line: the light of one whole Vectrex frame (four 5 ms windows), frame by frame after the last redraw
ri = 6; A, p, t0 = res[ri]; fresh = C[ri]['first'][0][0]
L = lambda t: A * (t + t0) ** -p
live = fresh + sum(L(x) for x in (5, 10, 15))
print('7F line, a frame\'s light after the last redraw, as a share of a line redrawn every frame (linear / as a display value):')
for n in (1, 2, 3, 5, 8, 12, 20, 30, 50):
    fr = sum(L(20 * n + x) for x in (0, 5, 10, 15)) / live
    print('   %2d frames (%4d ms): %.2f %%  / %.0f %% grey' % (n, 20 * n, 100 * fr, 100 * fr ** (1 / 2.2)))
print('ratios 5F / 7F line, same age:', ' '.join('%.2f' % (C[7][ph][0][n] / C[6][ph][0][n]) for n in (1, 2, 4, 8) for ph in ('first',)), '| fresh %.2f' % (C[7]['first'][0][0] / C[6]['first'][0][0]))
print('dots 7F 64cy / 16cy, age 1-3:', ' '.join('%.2f' % (C[3]['first'][0][n] / C[1]['first'][0][n]) for n in (1, 2, 3)), '| 5F / 7F dots:', ' '.join('%.2f' % (C[4]['first'][0][n] / C[1]['first'][0][n]) for n in (1, 2, 3)), '| 40 / 7F:', ' '.join('%.2f' % (C[5]['first'][0][n] / C[1]['first'][0][n]) for n in (1, 2, 3)))
