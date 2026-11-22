"""one curve for the phosphor: light a unit = F(i(z) x dwell); the Photo Test's pages 1-2 (dwell 1..33 at four
intensities), page 4 (the line at 8.3 cycles a unit by intensity, a 20-cycle dot)"""
import numpy as np
from scipy import optimize as op
SP = np.array([0x7F, 0x60, 0x40, 0x30, 0x20, 0x18, 0x10, 0x08, 0x04]); DW = 133.1 / SP
L7 = np.array([0.49, 0.59, 0.78, 0.94, 1.21, 1.43, 1.87, 3.24, 5.75]); L5 = np.array([0.24, 0.30, 0.41, 0.50, 0.66, 0.79, 1.05, 1.79, 3.20])
L4 = np.array([0.21, 0.27, 0.39, 0.51, 0.72, 0.90, 1.25, 2.21, 3.70]); L3 = np.array([0.08, 0.11, 0.14, 0.21, 0.40, 0.73])
def F(D, q): D0, pi = q; return D * (1.0 + D / D0) ** (pi - 1.0)
def res(x):
    D0, pi, i5, i4, i3, s4, s3, i3f, i6f = x; q = (D0, pi); a = L7[0] / F(DW[0], q)
    r = [(a * F(DW, q) - L7) / L7, (a * F(i5 * DW, q) - L5) / L5, (s4 * F(i4 * DW, q) - L4) / L4, (s3 * F(i3 * DW[3:], q) - L3) / L3]
    f8 = F(8.3, q); r.append(np.array([F(i3f * 8.3, q) / f8 / 0.12 - 1, F(i5 * 8.3, q) / f8 / 0.51 - 1, F(i6f * 8.3, q) / f8 / 0.76 - 1]) * 2)
    return np.concatenate(r)
best = None
for D0 in (0.05, 0.3, 1, 3):
    r = op.least_squares(res, [D0, 0.65, 0.4, 0.2, 0.1, 1, 1, 0.08, 0.7], bounds=([1e-3, 0.3, 0.01, 0.005, 0.002, 0.01, 0.01, 0.001, 0.1], [50, 1.0, 1, 1, 1, 100, 100, 1, 1]))
    if best is None or r.cost < best.cost: best = r
x = best.x; print('D0 %.3f  p_inf %.3f  i($5F) %.3f  i($40) %.3f  i($30) %.3f  i($3F) %.3f  i($6F) %.3f  rms %.1f %%' % (x[0], x[1], x[2], x[3], x[4], x[7], x[8], 100 * np.sqrt((best.fun ** 2).mean())))
q = (x[0], x[1]); a = L7[0] / F(DW[0], q)
for nm, i, s, L, d in (('$7F', 1, a, L7, DW), ('$5F', x[2], a, L5, DW), ('$40', x[3], x[5], L4, DW), ('$30', x[4], x[6], L3, DW[3:])):
    print(nm, 'fit/meas:', ' '.join('%.2f' % (s * F(i * d, q) / l) for d, l in zip(d, L)))
# the local slope
for D in (0.1, 0.3, 1, 3, 10, 30): print('dose %5.1f: slope %.2f' % (D, 1 + (x[1] - 1) * (D / x[0]) / (1 + D / x[0])))
# the 20-cycle dot: light = W F(20 / W) against 2.0 units of the 8.3 line (15 px at 7.6 px a unit)
for W in (0.5, 0.8, 1.0, 1.3, 1.6): print('W %.1f: a 20-cycle dot = %.2f units of the 8.3 line (the tube 2.0); at $3F %.2f' % (W, W * F(20 / W, q) / F(8.3, q), W * F(x[7] * 20 / W, q) / F(x[7] * 8.3, q)))
