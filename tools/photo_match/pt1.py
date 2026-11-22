"""Photo Test pages 1-2: the light a unit of line against the beam's dwell (cycles a unit), four intensities"""
import numpy as np
from scipy import optimize as op
SP = np.array([0x7F, 0x60, 0x40, 0x30, 0x20, 0x18, 0x10, 0x08, 0x04]); DW = 133.1 / SP
D = {0x7F: ('f/4', DW, [0.49, 0.59, 0.78, 0.94, 1.21, 1.43, 1.87, 3.24, 5.75]), 0x5F: ('f/4', DW, [0.24, 0.30, 0.41, 0.50, 0.66, 0.79, 1.05, 1.79, 3.20]),
     0x40: ('f/1.48', DW, [0.21, 0.27, 0.39, 0.51, 0.72, 0.90, 1.25, 2.21, 3.70]), 0x30: ('f/1.48', DW[3:], [0.08, 0.11, 0.14, 0.21, 0.40, 0.73])}
print('dwell (cycles a unit):      %s' % ' '.join('%6.2f' % d for d in DW))
for z, (ps, d, l) in D.items():
    l = np.array(l); print('intensity $%02X (%s): light / the light at the fastest: %s' % (z, ps, ' '.join('%6.2f' % v for v in l / l[0])))
    fits = {}
    f1 = lambda q: (q[0] + q[1] * d - l) / l; r = op.least_squares(f1, [l[0] / 2, l[0] / 2]); fits['a + b d'] = (r.x, np.sqrt((r.fun ** 2).mean()))
    f2 = lambda q: (q[0] * d ** q[1] - l) / l; r = op.least_squares(f2, [l[0], 0.7]); fits['c d^p'] = (r.x, np.sqrt((r.fun ** 2).mean()))
    f3 = lambda q: (q[0] + q[1] * q[2] * np.log1p(d / q[2]) - l) / l; r = op.least_squares(f3, [l[0] / 2, l[0] / 2, 30.0], bounds=([0, 0, 1], [10, 10, 1e4])); fits['a + b D ln(1 + d / D)'] = (r.x, np.sqrt((r.fun ** 2).mean()))
    f4 = lambda q: (q[0] + q[1] * d ** q[2] - l) / l; r = op.least_squares(f4, [l[0] / 2, l[0] / 2, 0.8], bounds=([0, 0, 0.2], [10, 10, 1.5])); fits['a + b d^p'] = (r.x, np.sqrt((r.fun ** 2).mean()))
    for k, (x, e) in fits.items(): print('      %-24s %s   rms %.1f %%   fixed part / the part by dwell at 1 cycle a unit: %s' % (k, ' '.join('%.3f' % v for v in x), 100 * e, '%.2f' % (x[0] / x[1]) if k != 'c d^p' else '-'))
print('by intensity, light at the same speed / at $7F (f/4):  $5F: %s' % ' '.join('%.2f' % (a / b) for a, b in zip(D[0x5F][2], D[0x7F][2])))
