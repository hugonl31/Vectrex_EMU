import sys, os, numpy as np
sys.path.insert(0, r'G:\AI\Vectrex_EMU\src\frontend'); os.chdir(r'G:\AI\Vectrex_EMU')
import vectrex_emu as V
c = V.Core(r'G:\AI\Vectrex_EMU\testcart\dot_test.bin', False, 'auto', 'off', None, None, None, False)
def run(n, b=0):
    for i in range(n): L = c.frame(0, 0, b, 0)[0]
    return list(L)
run(900); run(4, 8); run(60); L = sorted(run(1), key=lambda l: l[6])
U = 134.0
# shapes: the hand-drawn corners - records at intensity 127 with a sign flip in dx; group by time gaps
segs = [l for l in L if (int(l[4]) & 255) == 127]
groups, cur = [], []
for l in segs:
    if cur and l[6] - (cur[-1][6] + cur[-1][5] / 8.0) > 3: groups.append(cur); cur = []
    cur.append(l)
groups.append(cur)
res = []
for gp in groups:
    P = [(gp[0][0], gp[0][1])] + [(l[2], l[3]) for l in gp]
    P = np.array(P) / U
    if len(P) < 4: continue
    span = np.ptp(P, 0)
    if not (1.5 < span[1] < 40 and span[0] > 0.3): continue
    tip = np.argmin(P[:, 0])
    if tip < 1 or tip > len(P) - 2: continue
    # legs: points far (>3 units in y) from the tip, densified by interpolation
    t = np.linspace(0, 1, 2000); idx = np.interp(t, np.linspace(0, 1, len(P)), np.arange(len(P)))
    D = np.array([np.interp(idx, np.arange(len(P)), P[:, k]) for k in (0, 1)]).T
    ti = np.argmin(D[:, 0]); vy, vx = D[ti, 1], D[ti, 0]
    up = np.abs(D[:ti, 1] - vy) > 3; dn = np.abs(D[ti:, 1] - vy) > 3
    if up.sum() < 5 or dn.sum() < 5: continue
    A = D[:ti][up]; B = D[ti:][dn]
    pu = np.polyfit(A[:, 1], A[:, 0], 1); pd = np.polyfit(B[:, 1], B[:, 0], 1)
    yi = (pd[1] - pu[1]) / (pu[0] - pd[0]); xi = pu[0] * yi + pu[1]
    res.append((round(vy, 1), round(vx, 1), vx - xi))
rows = {}
for vy, vx, cut in res:
    k = min(rows, key=lambda q: abs(q - vy)) if rows and min(abs(q - vy) for q in rows) < 12 else vy
    rows.setdefault(k, []).append((vx, cut))
for k in sorted(rows): print('  row y %5.0f:' % k, ' '.join('%5.2f' % c for _, c in sorted(rows[k])))
