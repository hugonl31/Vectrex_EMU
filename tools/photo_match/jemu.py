"""the emulator's own beam path, Dot Test page 3: the joints' dwell points -> travel during each RAMP-off pulse"""
import sys, os, numpy as np
sys.path.insert(0, r'G:\AI\Vectrex_EMU\src\frontend'); os.chdir(r'G:\AI\Vectrex_EMU')
import vectrex_emu as V
def joints():
    c = V.Core(r'G:\AI\Vectrex_EMU\testcart\dot_test.bin', False, 'auto', 'off', None, None, None, False)
    def run(n, b=0):
        for i in range(n): L = c.frame(0, 0, b, 0)[0]
        return list(L)
    run(900); run(4, 4); run(60); L = sorted(run(1), key=lambda l: l[6])
    out = {}
    for y, v, Lc in ((10100, 0x28, 70), (14259, 0x50, 35), (18419, 0x7F, 22), (22579, 0x50, 35)):
        row = [l for l in L if abs(l[1] - y) < 200 and abs(l[3] - y) < 200 and (int(l[4]) & 255) in (127, 95)]
        if not row: continue
        t = np.array([l[5] / 8.0 for l in row]); x = np.array([(l[0] + l[2]) / 2 for l in row]) / 134.0
        ln = np.array([abs(l[2] - l[0]) for l in row]) / 134.0; sp = ln / np.maximum(t, 1e-6); vv = v / 128.0
        slow = sp < 0.5 * vv                                   # the beam's time below half the line's speed
        # clusters of slow time along x: the dwell points
        xs = x[slow]; ts = t[slow]; o = np.argsort(xs); xs, ts = xs[o], ts[o]
        cl = []
        for xi, ti in zip(xs, ts):
            if cl and xi - cl[-1][0][-1] < 3.0: cl[-1][0].append(xi); cl[-1][1].append(ti)
            else: cl.append(([xi], [ti]))
        cen = np.array([np.average(a, weights=b) for a, b in cl if sum(b) > 1.5]); tt = [sum(b) for a, b in cl if sum(b) > 1.5]
        cen = cen[1:-1] if len(cen) > 8 else cen               # (the line's own start and end)
        d = np.diff(cen) / vv - Lc
        out[(v, Lc, y)] = (len(cen), d, tt)
    return out
if __name__ == '__main__':
    for k, (n, d, tt) in joints().items():
        print('speed $%02X %d cycles: dwell points %d; travel between them less the piece: %s' % (k[0], k[1], n, ' '.join('%5.1f' % v for v in d)))
