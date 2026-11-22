"""Photo Test pages 5 and 7 on the tube (row lengths by a fixed row grid) and in the core (headless)"""
import numpy as np, pt0, sys, os
from scipy import ndimage as nd
def tube_rows(page, n, x_lo, x_hi, pas='f1'):
    g = pt0.load(pas, page); bg = np.median(g); H = g.shape[0]
    band = g[int(0.08 * H):int(0.9 * H), x_lo:x_hi] - bg; prof = nd.gaussian_filter1d(band.sum(1), 3)
    pk = [i for i in range(20, len(prof) - 20) if prof[i] == prof[i - 18:i + 19].max() and prof[i] > 0.15 * np.percentile(prof, 99)]
    pk = pk[:n] if len(pk) >= n else pk
    out = []
    for y in pk:
        yy = y + int(0.08 * H); r = (g[yy - 8:yy + 9] - bg).sum(0); lev = np.percentile(r, 99.5) * 0.5; on = np.nonzero(nd.gaussian_filter1d(r, 1) > 0.5 * lev)[0]
        # runs
        runs = np.split(on, np.nonzero(np.diff(on) > 3)[0] + 1); out.append([(int(a[0]), int(a[-1])) for a in runs if len(a) > 3])
    return out
def core_rows(page, n):
    sys.path.insert(0, r'G:\AI\Vectrex_EMU\src\frontend'); os.environ.setdefault('VT_BEAMMODE', '1'); cwd = os.getcwd(); os.chdir(r'G:\AI\Vectrex_EMU')
    import vectrex_emu as V
    c = V.Core(r'G:\AI\Vectrex_EMU\testcart\photo_test.bin', False, 'auto', 'off', None, None, None, False)
    def fr(b=0): return list(c.frame(0, 0, b, 0)[0])
    for i in range(900): fr()
    for p in range(page - 1):
        for i in range(3): fr(8)
        for i in range(20): fr()
    for i in range(30): L = fr()
    os.chdir(cwd)
    A = np.array([(l[0] / 134.0, l[1] / 134.0, l[2] / 134.0, l[3] / 134.0, int(l[4]) & 255, l[5] / 8.0, l[6]) for l in sorted(L, key=lambda l: l[6])]); A = A[(A[:, 4] == 127) & (abs(A[:, 3] - A[:, 1]) < 0.3)]
    ys = np.round(A[:, 1] / 3.0) * 3; rows = []
    for y in sorted(set(ys)):
        r = A[ys == y]
        if r[:, [0, 2]].max() - r[:, [0, 2]].min() < 1.0 and r[:, 5].sum() < 3: continue
        rows.append(r)
    return rows[:n]
if __name__ == '__main__':
    V7 = 127 / 133.1
    T = tube_rows(7, 15, 700, 1200); N = list(range(6, 21))
    ln = [r[0][1] - r[0][0] for r in T]; print('page 7, the tube: lit length px by N 6..20:', ' '.join('%d' % v for v in ln))
    C = core_rows(7, 15); lc = [r[:, [0, 2]].max() - r[:, [0, 2]].min() for r in C]; print('page 7, the core today: lit length, cycles of travel:', ' '.join('%.1f' % (v / V7) for v in lc))
    ppc = np.polyfit([lc[i] / V7 for i in (5, 6, 7, 8, 9)], [ln[i] for i in (5, 6, 7, 8, 9)], 1)
    print('   the tube in cycles (scaled on rows N 11-15): ', ' '.join('%.1f' % ((v - ppc[1]) / ppc[0]) for v in ln))
    T = tube_rows(5, 15, 700, 1400); print('page 5, the tube: lit px by G 10..24:', ' '.join('+'.join('%d' % (b - a) for a, b in r) for r in T))
    C = core_rows(5, 15); print('page 5, the core today: lit cycles by G 10..24:', ' '.join('%.1f' % ((r[:, [0, 2]].max() - r[:, [0, 2]].min()) / V7) for r in C))
