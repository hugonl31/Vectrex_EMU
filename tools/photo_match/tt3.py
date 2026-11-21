"""the core's own Settle / Timer Test lines (headless): lit length per row -> the same lateness numbers as tt2 for the tube"""
import sys, os, numpy as np
os.environ.setdefault('VT_BEAMMODE', '1')
sys.path.insert(0, r'G:\AI\Vectrex_EMU\src\frontend'); S = os.path.dirname(os.path.abspath(__file__)); os.chdir(r'G:\AI\Vectrex_EMU')
import vectrex_emu as V
OFF_K = [-12, -8, -5, 5, 8, 12, 16, 20, 24, 32, 48]; ON_K_T1 = [-12, -8, 4, 8, 12, 16, 20, 24, 32]; ON_K = [-12, -8, -4, 5, 8, 12, 16, 20, 24, 32]
def pages(rom):
    c = V.Core(rom, False, 'auto', 'off', None, None, None, False); out = {}
    def run(n, b=0):
        for i in range(n): L = c.frame(0, 0, b, 0)[0]
        return list(L)
    run(900)
    for p in (1, 2, 3, 4):
        run(4, 1 << (p - 1)); run(30); L = run(1)
        A = np.array([(l[0] / 134.0, l[1] / 134.0, l[2] / 134.0, l[3] / 134.0, int(l[4]) & 255) for l in L]); A = A[A[:, 4] == 127]
        cols = []
        for x0, x1 in ((0, 100), (100, 300)):                       # left / right column
            m = (np.minimum(A[:, 0], A[:, 2]) >= x0) & (np.minimum(A[:, 0], A[:, 2]) < x1) & (abs(A[:, 3] - A[:, 1]) < 0.5); B = A[m]
            ys = np.round(B[:, 1] / 4.0) * 4; rows = []
            for y in sorted(set(ys)):
                r = B[ys == y]; rows.append(r[:, [0, 2]].max() - r[:, [0, 2]].min())
            cols.append(rows)
        out[p] = cols
    return out
def shift(K, val, Kref, ref, lo, hi):
    best = None
    for d in np.arange(-8, 8, 0.02):
        m = [(k, v) for k, v in zip(K, val) if lo <= np.interp(k - d, Kref, ref) <= hi or lo <= v <= hi]
        r = np.mean([(v - np.interp(k - d, Kref, ref)) ** 2 for k, v in m])
        if best is None or r < best[0]: best = (r, d)
    return best[1]
if __name__ == '__main__':
    st = pages(r'G:\AI\Vectrex_EMU\testcart\settle_test.bin'); tt = pages(r'G:\AI\Vectrex_EMU\testcart\timer_test2.bin')
    for ci, (name, Lo, Ln) in enumerate((('speed $50', 40, 48), ('speed $7F', 24, 40))):
        so, sn = st[1][ci], st[2][ci]; ppo = max(so) / (Lo + 2.0); ppn = max(sn) / (Ln + 2.0)
        if len(so) != 11 or len(sn) != 10: print(name, 'rows', len(so), len(sn), [len(tt[p][ci]) for p in (1, 2, 3, 4)])
        ro = [(max(so) - x) / ppo for x in so]; tn = [(max(sn) - x) / ppn for x in sn]
        print(name, ': the core, hand RAMP + PCR (Settle Test): to come %s | done %s' % (' '.join('%.2f' % v for v in ro[3:10]), ' '.join('%.2f' % v for v in tn[3:])))
        t1o, t1n, sro, srn = tt[1][ci], tt[2][ci], tt[3][ci], tt[4][ci]
        print('   timer 1: the whole line N + %.2f (page 1), N + %.2f (page 2)   [the tube: N + 4.8]' % (max(t1o) / ppo - Lo, max(t1n) / ppn - Ln))
        print('   shift register OFF %+.2f  ON %+.2f   [the tube: +2.2, +1.7]' % (-shift(OFF_K, [(max(sro) - x) / ppo for x in sro], OFF_K, ro, 0.5, 12), -shift(ON_K, [(max(srn) - x) / ppn for x in srn], ON_K, tn, 0.8, 14)))
        print('   timer 1 RAMP off %+.2f  on %+.2f   [the tube: +1.1, -1.3]' % (shift(OFF_K, [(max(t1o) - x) / ppo for x in t1o], OFF_K, ro, 0.5, 12), shift(ON_K_T1, [(max(t1n) - x) / ppn for x in t1n], ON_K, tn, 0.8, 14)))
