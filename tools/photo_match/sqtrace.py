import numpy as np, sys, os
sys.path.insert(0, r'G:\AI\Vectrex_EMU\src\frontend'); os.environ.setdefault('VT_BEAMMODE', '1'); os.chdir(r'G:\AI\Vectrex_EMU')
import vectrex_emu as V
c = V.Core(r'G:\AI\Vectrex_EMU\testcart\photo_test.bin', False, 'auto', 'off', None, None, None, False)
def fr(b=0): return list(c.frame(0, 0, b, 0)[0])
for i in range(900): fr()
for p in range(9):
    for i in range(3): fr(8)
    for i in range(20): fr()
for i in range(30): L = fr()
A = np.array([(l[0] / 134.0, l[1] / 134.0, l[2] / 134.0, l[3] / 134.0, int(l[4]) & 255, l[5] / 8.0, l[6]) for l in sorted(L, key=lambda l: l[6])])
print(len(A), 'x', A[:, 0].min(), A[:, 0].max(), 'y', A[:, 1].min(), A[:, 1].max())
# the square at scale $20: sides 0x60*0x20/... find segments forming long axis-aligned runs; print groups by time gaps
t = A[:, 6]; g = np.nonzero(np.diff(t) > 40)[0] + 1; G = np.split(A, g)
for k, s in enumerate(G):
    dx = s[-1, 2] - s[0, 0]; dy = s[-1, 3] - s[0, 1]
    print('%3d t %7.0f n %2d  from (%7.2f %7.2f) to (%7.2f %7.2f)  d (%6.2f %6.2f) dwell first %.1f last %.1f total %.1f' % (k, s[0, 6], len(s), s[0, 0], s[0, 1], s[-1, 2], s[-1, 3], dx, dy, s[0, 5], s[-1, 5], s[:, 5].sum()))
print('GAPS', ' '.join('%s %.2f/%.1f' % (nm, np.hypot(G[k + 1][0, 0] - G[k][-1, 2], G[k + 1][0, 1] - G[k][-1, 3]), G[k + 1][0, 5]) for nm, k in (('$08', 48), ('$10', 52), ('$20', 56), ('$30', 60))))
