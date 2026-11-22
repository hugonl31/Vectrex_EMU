"""Timer Test against the Settle Test: timer 1's and the shift register's own lateness (cycles)"""
import numpy as np, st1, st2, tt1, st5
from scipy import ndimage as nd
def page(n, K, C):
    e = tt1.cols(n); mid = (e[1] + e[2]) // 2; g = st1.load(n); out = []
    for roi in ((1420, 2950, max(0, e[0] - 60), mid), (1420, 2950, mid, e[-1] + 60)):
        E, lev = st1.column(g, roi, len(K)); out.append([b - a for a, b in E])
    return out
p1 = page('32880', tt1.OFF_K, 2 * [0]); p2 = page('33920', tt1.ON_K_T1, 2 * [0]); p3 = page('34956', tt1.OFF_K, 2 * [0]); p4 = page('35994', tt1.ON_K, 2 * [0])
OFFC = ((0x50, 40), (0x7F, 24)); ONC = ((0x50, 48), (0x7F, 40))
def shift(K, val, Kref, ref, lo, hi):
    """the delay d with val(K) = ref(K - d), least squares over the rows where the curve moves"""
    best = None
    for d in np.arange(-6, 8, 0.02):
        m = [(k, v) for k, v in zip(K, val) if lo <= np.interp(k - d, Kref, ref) <= hi or lo <= v <= hi]
        r = np.mean([(v - np.interp(k - d, Kref, ref)) ** 2 for k, v in m])
        if best is None or r < best[0]: best = (r, d)
    return best[1], np.sqrt(best[0])
for ci, name in ((0, 'speed $50'), (1, 'speed $7F')):
    # px a cycle from the hand-RAMP pages (the whole line = L + 2 cycles)
    L3 = OFFC[ci][1]; L4 = ONC[ci][1]; ppc3 = max(p3[ci]) / (L3 + 2.0); ppc4 = max(p4[ci]) / (L4 + 2.0)
    print(name, ': px a cycle %.2f (page 3) %.2f (page 4)' % (ppc3, ppc4))
    print('   timer 1: the whole line %.2f cycles of travel for N = %d (page 1), %.2f for N = %d (page 2): N + %.2f / N + %.2f (a hand RAMP of L: L + 2)' % (max(p1[ci]) / ppc3, L3, max(p2[ci]) / ppc4, L4, max(p1[ci]) / ppc3 - L3, max(p2[ci]) / ppc4 - L4))
    r_sr = [(max(p3[ci]) - x) / ppc3 for x in p3[ci]]; t_sr = [(max(p4[ci]) - x) / ppc4 for x in p4[ci]]
    r_t1 = [(max(p1[ci]) - x) / ppc3 for x in p1[ci]]; t_t1 = [(max(p2[ci]) - x) / ppc4 for x in p2[ci]]
    href_off = st5.X['off'][ci][1]; href_on = st5.X['on'][ci][1]
    print('   to come at beam-off, K %s' % ' '.join('%6d' % k for k in tt1.OFF_K)); print('      Settle (hand, PCR) %s' % ' '.join('%6.2f' % v for v in href_off)); print('      shift register     %s' % ' '.join('%6.2f' % v for v in r_sr)); print('      timer RAMP         %s' % ' '.join('%6.2f' % v for v in r_t1))
    print('   done at beam-on,     K %s' % ' '.join('%6d' % k for k in tt1.ON_K)); print('      Settle (hand, PCR) %s' % ' '.join('%6.2f' % v for v in href_on)); print('      shift register     %s' % ' '.join('%6.2f' % v for v in t_sr)); print('      timer RAMP (K %s) %s' % (tt1.ON_K_T1, ' '.join('%6.2f' % v for v in t_t1)))
    d, e = shift(tt1.OFF_K, r_sr, st5.OFF_K, href_off, 0.5, 12); print('   the shift register switches OFF %+.2f cycles later than a PCR write (fit %.2f)' % (-d * -1 if False else d * -1 * -1, e) if False else '   shift register OFF: the beam goes off %+.2f cycles after a PCR write would (fit %.2f)' % (-d, e))
    d, e = shift(tt1.ON_K, t_sr, st5.ON_K, href_on, 0.8, 14); print('   shift register ON : the beam comes on %+.2f cycles after a PCR write would (fit %.2f)' % (-d, e))
    d, e = shift(tt1.OFF_K, r_t1, st5.OFF_K, href_off, 0.5, 12); print('   timer 1 RAMP off  : %+.2f cycles after a port write at t0 + N would (fit %.2f)' % (d, e))
    d, e = shift(tt1.ON_K_T1, t_t1, st5.ON_K, href_on, 0.8, 14); print('   timer 1 RAMP on   : %+.2f cycles after a port write at t0 would (fit %.2f)' % (d, e))
