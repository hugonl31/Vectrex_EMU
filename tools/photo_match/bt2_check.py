import sys; sys.path.insert(0, r'G:\AI\Vectrex_EMU\src\frontend')
import numpy as np, vecrender as V
d = np.array([1.048, 1.59, 8.319, 16.64, 33.28, 20.0, 100.0])
print('dwell     ', ' '.join('%7.2f' % v for v in d))
for z in (31, 48, 63, 64, 95, 111, 127): print('z %3d     ' % z, ' '.join('%7.4f' % v for v in V.bt2_light(np.full(len(d), float(z)), d)))
U = 134.0
L = np.array([[0, 0, 0, 0, 127, 20 * 8, 100], [0, 0, 30 * U, 0, 127, 30 * 1.59 * 8, 120], [0, 5000, 0, 5000, 127, 20 * 8, 5000]], float)
R, bl, _ = V.bt2_prepare(L)
for r, v in zip(R, bl): print('  x %6.2f..%6.2f y %5.0f  t %5.1f  light %.3f' % (r[0] / U, r[2] / U, r[1], r[5] / 8, v))
print('a pure rest of 20 cycles = %.2f units of the 8.3 line (the tube: 2.0)' % (bl[-1] + bl[-3] if False else sum(v for r, v in zip(R, bl) if r[1] == 5000) / V.bt2_light(np.array([127.0]), np.array([8.319]))[0]))
