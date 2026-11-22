import numpy as np, sys, os
sys.path.insert(0, r'G:\AI\Vectrex_EMU\src\frontend'); os.environ.setdefault('VT_BEAMMODE', '1'); os.chdir(r'G:\AI\Vectrex_EMU')
import vectrex_emu as V
c = V.Core(r'G:\AI\Vectrex_EMU\testcart\gap_test.bin', False, 'auto', 'off', None, None, None, False)
def fr(b=0): return list(c.frame(0, 0, b, 0)[0])
for i in range(900): fr()
for i in range(3): fr(1)
for i in range(40): L = fr()
A = np.array([(l[0] / 134.0, l[1] / 134.0, l[2] / 134.0, l[3] / 134.0, int(l[4]) & 255, l[5] / 8.0, l[6]) for l in sorted(L, key=lambda l: l[6])]); A = A[A[:, 4] == 127]
t = A[:, 6]; G = np.split(A, np.nonzero(np.diff(t) > 25)[0] + 1); G = [s for s in G if abs(s[-1, 2] - s[0, 0]) > 3 and abs(s[-1, 3] - s[0, 1]) < 1]
rows = {}
for s in G: rows.setdefault(round(s[0, 1] / 4), []).append(s)
for y in sorted(rows):
    R = rows[y]
    if len(R) < 8: continue
    seg = np.mean([abs(s[-1, 2] - s[0, 0]) for s in R]); gap = np.mean([abs(R[k + 1][0, 0] - R[k][-1, 2]) for k in range(len(R) - 1)])
    print('row y %.0f: %d lines, lit %.2f units, gap to the next start %.2f units, pitch %.2f; first piece rests %.1f cycles; last pieces dwell %s' % (R[0][0, 1], len(R), seg, gap, seg + gap, R[1][0, 5], ' '.join('%.1f' % v for v in R[1][-3:, 5])))
