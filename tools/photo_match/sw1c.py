import numpy as np, sys
from scipy import ndimage as nd
from swmap import *
YS = [780, 1004, 1411, 1659]; V = [0.5, 0.5, 127 / 128., 127 / 128.]; L = [24, 24, 12, 12]
R = [5, 7, 8, 9, 10, 11, 12, 13, 14, 16, 20, 24, 32, 48]
for tag in sys.argv[1:]:
    z = np.load('sw/p1_%s.npz' % tag); print('photo', tag)
    for i in range(4):
        ug, e = unitprof(z['r%d' % i], YS[i]); es = nd.gaussian_filter1d(e, 10)
        ref = np.percentile(e, 60); on = np.nonzero(e > 0.5 * ref)[0]
        # edges at half the local level
        def edges(lo, hi):
            m = e[lo:hi] > 0.5 * np.median(e[lo:hi][e[lo:hi] > 0.3 * ref]); d = np.diff(m.astype(int)); return [(ug[lo + k + 1], int(s)) for k, s in zip(np.nonzero(d)[0], d[np.nonzero(d)[0]])]
        ed = edges(0, len(e)); u0, u1 = ed[0][0], ed[-1][0]
        n = L[i] * 15 * V[i]
        print(' row %d: start %.2f end %.2f  length %.2f units = %.1f cycles (15 pieces = %d cycles)' % (i, u0, u1, u1 - u0, (u1 - u0) / V[i], L[i] * 15))
        if i in (1, 3):
            g = [(a, b) for (a, sa), (b, sb) in zip(ed[1:-1:2], ed[2:-1:2])]
            # nominal place of RAMP off for each joint if every piece ran L cycles: start + V * (L * k - 5) (the beam on 5 after RAMP on)
            for (a, b), k in zip(g, range(11, 15)):
                print('    gap R=%d: %.2f .. %.2f  (width %.2f units = %.1f cycles)' % (R[k - 1], a - u0, b - u0, b - a, (b - a) / V[i]))
        else:
            pk = [k for k in range(200, len(es) - 200) if es[k] == es[k - 150:k + 151].max() and es[k] > 1.04 * np.median(es[max(0, k - 400):k + 400]) and u0 + 1 < ug[k] < u1 - 1]
            print('    peaks (units from start):', ' '.join('%.2f' % (ug[k] - u0) for k in pk))
            print('    spacing (cycles):', ' '.join('%.1f' % ((ug[b] - ug[a]) / V[i]) for a, b in zip(pk, pk[1:])))
            print('    peak / local floor:', ' '.join('%.2f' % (es[k] / es[max(0, k - 300):k + 300].min()) for k in pk))
        np.save('sw/u_%s_r%d.npy' % (tag, i), np.stack([ug - u0, e]))
