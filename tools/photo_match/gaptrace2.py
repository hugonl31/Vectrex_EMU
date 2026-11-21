import numpy as np, sys, os, json
sys.path.insert(0, r'G:\AI\Vectrex_EMU\src\frontend'); os.environ.setdefault('VT_BEAMMODE', '1'); os.chdir(r'G:\AI\Vectrex_EMU')
import vectrex_emu as V
unit = json.load(open(r'G:\AI\Vectrex_EMU\system\settings.json'))['unit']
drop = sys.argv[1].split(',') if len(sys.argv) > 1 else []
if drop == ['none']: unit = None
elif drop != ['all']: unit = {k: v for k, v in unit.items() if k in drop}
c = V.Core(r'G:\AI\Vectrex_EMU\testcart\gap_test.bin', False, 'auto', 'off', None, None, unit, False)
if len(sys.argv) > 2: c.set_gap(float(sys.argv[2]))
def fr(b=0): return list(c.frame(0, 0, b, 0)[0])
for i in range(900): fr()
for i in range(3): fr(1)
for i in range(40): L = fr()
A = np.array([(l[0] / 134.0, l[1] / 134.0, l[2] / 134.0, l[3] / 134.0, int(l[4]) & 255, l[5] / 8.0, l[6]) for l in sorted(L, key=lambda l: l[6])]); A = A[A[:, 4] == 127]
t = A[:, 6]; G = np.split(A, np.nonzero(np.diff(t) > 25)[0] + 1); G = [s for s in G if abs(s[-1, 2] - s[0, 0]) > 3 and abs(s[-1, 3] - s[0, 1]) < 1]
rows = {}
for s in G: rows.setdefault(round(s[0, 1] / 4), []).append(s)
print(sys.argv[1:], 'gaps:', ' '.join('%.2f' % np.mean([abs(R[k + 1][0, 0] - R[k][-1, 2]) for k in range(len(R) - 1)]) for y, R in sorted(rows.items()) if len(R) >= 8))
