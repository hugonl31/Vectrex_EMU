"""page 1: the photo's light along the line (by the picture's response) against the emulator's (dwell -> light by the law, blurred by the spot)"""
import numpy as np, sys
from scipy import ndimage as nd
import matplotlib; matplotlib.use('Agg'); import matplotlib.pyplot as plt
from swp1 import emu_rows, dwell
from swresp import resp
A_, B_, D0 = 0.70, 0.29, 150.0
KU = 0.9627            # (a unit of page 4's grid, Moveto at scale $FF, in the emulator's units of 134: measured on the core)
VR = [80, 56, 16, -8]; U0 = [-113.4, -113.4, -113.4, -113.4]
def emu_light(r, sig=0.45, du=0.02):
    ug, d = dwell(r, du); pad = int(4 / du); d = np.concatenate([np.zeros(pad), d, np.zeros(pad)]); ug = np.arange(len(d)) * du - 4
    lit = nd.gaussian_filter1d((d > 0).astype(float), sig / du); ds = nd.gaussian_filter1d(d, sig / du)
    return ug, A_ * lit + B_ * D0 * np.log1p(ds / D0)
def photo(ph, i):
    u, e = np.load('sw/u_%s_r%d.npy' % (ph, i)); e = e / resp(u + U0[i], VR[i]); u = u * KU; return u, e / np.median(e[(u > 3) & (u < 8)])
if __name__ == '__main__':
    ph = sys.argv[1]; tags = sys.argv[2:]
    fig, ax = plt.subplots(4, 1, figsize=(24, 14))
    for i in range(4):
        u, e = photo(ph, i); m = (u > -3) & (u < 205)
        ax[i].plot(u[m], e[m], 'k', lw=0.8, label='photo')
        for tag in tags:
            ug, l = emu_light(emu_rows(tag)[i]); r2 = np.median(l[(ug > 3) & (ug < 8)])
            ax[i].plot(ug, l / r2, lw=0.8, label=tag)
        ax[i].set_xlim(-3, 205); ax[i].set_ylim(0, 4); ax[i].grid(alpha=0.3); ax[i].set_xticks(range(0, 205, 5)); ax[i].tick_params(labelsize=6); ax[i].legend(fontsize=7)
    plt.tight_layout(); plt.savefig('sw/p1cmp_%s.png' % '_'.join(tags), dpi=65)
