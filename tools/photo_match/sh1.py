"""Short Test photos: page 1 (short ramps, beam off at K), 3 (timer), 4 (speeds) - the travel still to come at the switch-off"""
import numpy as np, st1, st5, json
from scipy import ndimage as nd
load = lambda n: np.rot90(np.load('sh/%s_red.npy' % n).astype(np.float32), -1)
K = [5, 8, 12, 16, 20, 24, 32, 48]
def cols_x(g, y0=1560, y1=2800):
    s = nd.gaussian_filter(g, 2); m = s[y0:y1] > 0.25 * np.percentile(s, 99.9); e = np.nonzero(np.diff((nd.binary_dilation(m.sum(0) > 2, iterations=25)).astype(int)))[0]
    return [(int(a) - 20, int(b) + 20) for a, b in zip(e[0::2], e[1::2])]
def page(n, nrows=8):
    g = load(n); C = cols_x(g); out = []
    # the line's level: from the last column's longest lines
    lev = None
    for (xa, xb) in C[::-1]:
        roi = (1560, 2800, xa, xb); L = sorted(st1.lines(g, roi), key=lambda d: d['pos'])
        if lev is None: lev = np.median([np.median(d['prof'][len(d['prof']) // 3:2 * len(d['prof']) // 3]) for d in sorted(L, key=lambda d: -len(d['prof']))[:3]])
        E = [st1.ends(d['prof'], d['xa'], lev) for d in L]; out.append([e[1] - e[0] if e else np.nan for e in E])
    return out[::-1], C
if __name__ == '__main__':
    for n, name, Ls, key in (('58983', 'page 1: hand RAMP of L cycles, speed $7F', [6, 8, 10, 13], 'L'), ('01056', 'page 3: timer 1, N cycles', [6, 8, 10, 13], 'N'), ('02094', 'page 4: L = 12 at speeds', [0x20, 0x40, 0x60, 0x7F], 'v')):
        P, C = page(n); print(name, ' columns', C)
        print('   K              %s' % ' '.join('%6d' % k for k in K))
        for ln, L in zip(P, Ls):
            full = np.nanmax(ln); cyc = (12 if key == 'v' else L) + (2.0 if key != 'N' else 4.8); ppc = full / cyc
            print('   %s %-4s (%2d rows, whole %5.1f px, %.2f px a cycle): %s' % (key, ('$%02X' % L) if key == 'v' else L, len(ln), full, ppc, ' '.join('%6.2f' % ((full - x) / ppc) for x in ln)))
    print('   long lines (Settle Test, L 24, $7F), K 5 8 12 16 20 24 32:   9.36   6.82   4.07   2.20   1.13   0.51   0.12')
