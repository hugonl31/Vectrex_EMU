"""page 1, the emulator's pieces: per row the lit length, the gaps, the dwell peaks (as the photo's sw1c.py)"""
import numpy as np, sys
from scipy import ndimage as nd
V = [0.5, 0.5, 127 / 128., 127 / 128.]; L = [24, 24, 12, 12]
def emu_rows(tag):
    A = np.load('sw/emu_%s_p1.npy' % tag); A = A[A[:, 4] > 100]
    ys = np.round(A[:, 1]); out = []
    for y in sorted(set(ys)):
        r = A[ys == y]
        if np.ptp(r[:, [0, 2]]) < 100: continue
        out.append(r)
    return out
def dwell(r, du=0.02):
    """cycles per unit along x, on a grid from the row's start"""
    u0 = r[:, [0, 2]].min(); u1 = r[:, [0, 2]].max(); n = int((u1 - u0) / du) + 2; d = np.zeros(n)
    for x0, _, x1, _, _, t, _ in r:
        a, b = sorted((x0 - u0, x1 - u0)); i0, i1 = int(a / du), int(b / du)
        d[i0:i1 + 1] += t / (i1 - i0 + 1) / du
    return np.arange(n) * du, d
if __name__ == '__main__':
    for tag in sys.argv[1:]:
        print('emulator', tag)
        for i, r in enumerate(emu_rows(tag)):
            ug, d = dwell(r); lit = d > 0
            ed = np.nonzero(np.diff(lit.astype(int)))[0]
            print(' row %d: length %.2f units = %.1f cycles' % (i, ug[-1], ug[-1] / V[i]))
            if i in (1, 3):
                gaps = [(ug[a + 1], ug[b + 1]) for a, b in zip(ed[0::2], ed[1::2])]
                for a, b in gaps: print('    gap %.2f .. %.2f (%.2f units = %.1f cycles)' % (a, b, b - a, (b - a) / V[i]))
            else:
                es = nd.gaussian_filter1d(d, 25)       # (the spot: ~0.5 unit sigma)
                pk = [k for k in range(160, len(es) - 160) if es[k] == es[max(0, k - 150):k + 151].max() and es[k] > 1.04 * np.median(es[max(0, k - 400):k + 400])]
                print('    peaks:', ' '.join('%.2f' % ug[k] for k in pk))
                print('    spacing (cycles):', ' '.join('%.1f' % ((ug[b] - ug[a]) / V[i]) for a, b in zip(pk, pk[1:])))
