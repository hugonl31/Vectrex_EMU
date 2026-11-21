"""the core's own Short Test lines (headless; settings from the environment): the travel still to come at the switch-off"""
import sys, os, numpy as np
os.environ.setdefault('VT_BEAMMODE', '1')
sys.path.insert(0, r'G:\AI\Vectrex_EMU\src\frontend'); os.chdir(r'G:\AI\Vectrex_EMU')
import vectrex_emu as V
def figs(rom, p):
    c = V.Core(rom, False, 'auto', 'off', None, None, None, False)
    def run(n, b=0):
        for i in range(n): L = c.frame(0, 0, b, 0)[0]
        return list(L)
    run(900); run(4, 1 << (p - 1)); run(30); L = sorted(run(1), key=lambda l: l[6])
    A = np.array([(l[0] / 134.0, l[1] / 134.0, l[2] / 134.0, l[3] / 134.0, int(l[4]) & 255, l[5] / 8.0, l[6]) for l in L]); A = A[A[:, 4] == 127]
    g = np.nonzero(np.diff(A[:, 6]) > 150)[0]; return [q for q in np.split(A, g + 1) if len(q)]
def table(rom, p, ncol=4, nrow=8):
    F = figs(rom, p); F = [f for f in F if abs(f[:, 3] - f[:, 1]).max() < 0.5][:ncol * nrow]
    ln = [f[:, [0, 2]].max() - f[:, [0, 2]].min() for f in F]
    return [ln[i * nrow:(i + 1) * nrow] for i in range(ncol)]
if __name__ == '__main__':
    K = [5, 8, 12, 16, 20, 24, 32, 48]; rom = r'G:\AI\Vectrex_EMU\testcart\short_test.bin'
    for p, name, Ls, extra in ((1, 'page 1: hand RAMP L', [6, 8, 10, 13], 2.0), (3, 'page 3: timer N', [6, 8, 10, 13], None)):
        T = table(rom, p); print(name)
        for ln, L in zip(T, Ls):
            full = max(ln); v = 127 / 133.1
            cyc = full / v if extra is None else L + extra; ppc = full / cyc
            print('   %2d: whole line %.2f cycles of travel; to come at K 5..48: %s' % (L, full / v, ' '.join('%6.2f' % ((full - x) / (v if extra is None else ppc)) for x in ln)))
