"""SR Test pages in the core (headless): each row's lit stretch in cycles of travel at speed $7F, and its dots"""
import numpy as np, sys, os
def core_rows(page, cart=r'G:\AI\Vectrex_EMU\testcart\sr_test.bin'):
    sys.path.insert(0, r'G:\AI\Vectrex_EMU\src\frontend'); os.environ.setdefault('VT_BEAMMODE', '1'); cwd = os.getcwd(); os.chdir(r'G:\AI\Vectrex_EMU')
    import vectrex_emu as V
    c = V.Core(cart, False, 'auto', 'off', None, None, None, False)
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
    return rows
if __name__ == '__main__':
    V7 = 127 / 133.1
    for p in (1, 2, 3, 4):
        R = core_rows(p); R = R[:15]
        print('page %d: %d rows; lit, cycles: %s' % (p, len(R), ' '.join('%5.1f' % ((r[:, [0, 2]].max() - r[:, [0, 2]].min()) / V7) for r in R)))
        if p > 2: print('   x0 %s' % ' '.join('%5.1f' % r[:, [0, 2]].min() for r in R)); print('   segs/dwell first: %s' % ' '.join('%d/%.1f' % (len(r), r[0, 5]) for r in R))
