"""Timer Test photos: the line ends per page, as st2 for the Settle Test"""
import numpy as np, st1, st2
from scipy import ndimage as nd
OFF_K = [-12, -8, -5, 5, 8, 12, 16, 20, 24, 32, 48]; ON_K_T1 = [-12, -8, 4, 8, 12, 16, 20, 24, 32]; ON_K = [-12, -8, -4, 5, 8, 12, 16, 20, 24, 32]
st1.load = lambda n: np.rot90(np.load('tt/%s_red.npy' % n).astype(np.float32), -1)
def cols(n):
    g = st1.load(n); s = nd.gaussian_filter(g, 2); m = s[1420:2950] > 0.3 * np.percentile(s, 99.9)
    e = np.nonzero(np.diff((m.sum(0) > 3).astype(int)))[0]; return e
if __name__ == '__main__':
    for n, name, K, C, on in (('32880', 'page 1: timer RAMP, beam OFF (PCR)', OFF_K, ((0x50, 40), (0x7F, 24)), False), ('33920', 'page 2: timer RAMP, beam ON (PCR)', ON_K_T1, ((0x50, 48), (0x7F, 40)), True),
                              ('34956', 'page 3: hand RAMP, beam OFF (shift register)', OFF_K, ((0x50, 40), (0x7F, 24)), False), ('35994', 'page 4: hand RAMP, beam ON (shift register)', ON_K, ((0x50, 48), (0x7F, 40)), True)):
        e = cols(n); print(name, ' columns x', e.tolist())
        mid = (e[1] + e[2]) // 2
        st2.page(n, K, C, ((1420, 2950, max(0, e[0] - 60), mid), (1420, 2950, mid, e[-1] + 60)), on)
