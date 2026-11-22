import sys, os, numpy as np
os.environ.setdefault('VT_BEAMMODE', '1')
sys.path.insert(0, r'G:\AI\Vectrex_EMU\src\frontend'); os.chdir(r'G:\AI\Vectrex_EMU')
import vectrex_emu as V
c = V.Core(r'G:\AI\Vectrex_EMU\testcart\corner_test.bin', False, 'auto', 'off', None, None, None, False)
for i in range(960): L = c.frame(0, 0, 0, 0)[0]
L = sorted(list(L), key=lambda l: l[6])
A = np.array([(l[0] / 134.0, l[1] / 134.0, l[2] / 134.0, l[3] / 134.0, int(l[4]) & 255, l[5] / 8.0, l[6]) for l in L])
R = A[A[:, 4] == 127]; g = np.nonzero(np.diff(R[:, 6]) > 40)[0]; segs = [q for q in np.split(R, g + 1) if np.ptp(q[:, 1]) > 8 and np.ptp(q[:, 0]) > 3]
if len(sys.argv) < 2:
    for i, q in enumerate(segs): print(i, len(q), 'x %.0f..%.0f y %.0f..%.0f  T %.0f' % (q[:, 0].min(), q[:, 0].max(), q[:, 1].min(), q[:, 1].max(), q[-1, 6] - q[0, 6]))
else:
    s = segs[int(sys.argv[1])]; t0 = s[0, 6]
    for x0, y0, x1, y1, z, d, t in s: print('%5.0f %4.1f  vx %+.3f vy %+.3f   x %.2f y %.2f' % (t - t0, d, (x1 - x0) / max(d, 1e-9), (y1 - y0) / max(d, 1e-9), x1, y1))
