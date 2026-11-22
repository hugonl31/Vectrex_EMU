"""the core on the Switch Test: one frame of each page as pieces (units; x0 y0 x1 y1 intensity cycles t) -> sw/emu_<tag>_p<n>.npy"""
import sys, os, numpy as np
os.environ.setdefault('VT_BEAMMODE', '1')
sys.path.insert(0, r'G:\AI\Vectrex_EMU\src\frontend'); S = os.path.dirname(os.path.abspath(__file__)); os.chdir(r'G:\AI\Vectrex_EMU')
import vectrex_emu as V
tag = sys.argv[1]; pages = [int(c) for c in (sys.argv[2] if len(sys.argv) > 2 else '123')]
c = V.Core(r'G:\AI\Vectrex_EMU\testcart\switch_test.bin', False, 'auto', 'off', None, None, None, False)
def run(n, b=0):
    for i in range(n): L = c.frame(0, 0, b, 0)[0]
    return list(L)
run(900)
for p in pages:
    run(4, 1 << (p - 1)); run(30); L = sorted(run(1), key=lambda l: l[6])
    A = np.array([(l[0] / 134.0, l[1] / 134.0, l[2] / 134.0, l[3] / 134.0, int(l[4]) & 255, l[5] / 8.0, l[6]) for l in L])
    np.save(os.path.join(S, 'sw', 'emu_%s_p%d.npy' % (tag, p)), A); print(p, len(A))
