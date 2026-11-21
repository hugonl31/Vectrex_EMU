"""pages 2 and 3: every figure of the photo resampled in the emulator's units, its ridge; the emulator's path over it"""
import numpy as np, sys
from scipy import ndimage as nd
from sw1 import load
from swmap2 import fwd
from swresp import resp
KU = 0.9627; OX, OY = 130.4, 152.4; PPU = 12
def rect_emu(g, X0, X1, Y0, Y1):
    xs = np.arange(X0, X1, 1.0 / PPU); ys = np.arange(Y0, Y1, 1.0 / PPU); X, Y = np.meshgrid(xs, ys)
    u = (X - OX) / KU; v = (OY - Y) / KU; px, py = fwd(u, v)
    return nd.map_coordinates(g, [py, px], order=1) / resp(u, v), xs, ys
# the figures: (page, name, start u, v) -> window in emulator units around the start
FIGS = []
for row, flip in ((0, False), (1, True)):
    for i, vv in enumerate((0x20, 0x40, 0x7F)):
        FIGS.append((2, '%s$%02X' % ('-V>+V ' if flip else '0>+V ', vv), -0x58 + i * 0x48 + (0x18 if flip else 0), 0x58 - row * 0x50))
for i, k in enumerate([-12, -8, -4, 0, 4, 8, 12, 16]): FIGS.append((3, 'off%+d' % k, -0x60 + i * 0x1B, 0x58))
for i, k in enumerate([-12, -8, -5, 0, 5, 8, 12]): FIGS.append((3, 'on%+d' % k, -0x60 + i * 0x1F, 0))
def emu_fig(A, u, v, win=(-24, 34, -6, 46)):
    """the emulator's lit pieces within the figure's window (emulator units), in time order"""
    X0 = u * KU + OX; Y0 = OY - v * KU
    m = (A[:, 4] > 100) & (A[:, 0] > X0 + win[0]) & (A[:, 0] < X0 + win[1]) & (A[:, 1] > Y0 + win[2]) & (A[:, 1] < Y0 + win[3])
    return A[m], (X0 + win[0], X0 + win[1], Y0 + win[2], Y0 + win[3])
def ridge(img, xs, ys, thr=0.12):
    """the figure's centre line: the pixels that are the maximum across the line (either axis) above thr x the peak"""
    s = nd.gaussian_filter(img, 1.5); m = s > thr * s.max()
    hx = (s >= np.roll(s, 1, 1)) & (s >= np.roll(s, -1, 1)); hy = (s >= np.roll(s, 1, 0)) & (s >= np.roll(s, -1, 0))
    gy, gx = np.gradient(nd.gaussian_filter(img, 3.0))
    # across-line direction from the structure tensor
    Jxx = nd.gaussian_filter(gx * gx, 4); Jyy = nd.gaussian_filter(gy * gy, 4); Jxy = nd.gaussian_filter(gx * gy, 4)
    ang = 0.5 * np.arctan2(2 * Jxy, Jxx - Jyy)                       # the direction of the strongest gradient = across the line
    horiz = abs(np.cos(ang)) > abs(np.sin(ang))                       # across = x
    r = m & np.where(horiz, hx, hy)
    yy, xx = np.nonzero(r); return xs[xx], ys[yy], s[yy, xx]
if __name__ == '__main__':
    import matplotlib; matplotlib.use('Agg'); import matplotlib.pyplot as plt
    tags = sys.argv[1:] or ['base']
    for page, ph in ((2, '37685'), (3, '39767')):
        g = load(ph); figs = [f for f in FIGS if f[0] == page]
        E = {t: np.load('sw/emu_%s_p%d.npy' % (t, page)) for t in tags}
        n = len(figs); cols = 3 if page == 2 else 8; rows_ = (n + cols - 1) // cols
        fig, ax = plt.subplots(rows_, cols, figsize=(cols * 4.2, rows_ * 5.2)); ax = np.atleast_2d(ax)
        for j, (_, name, u, v) in enumerate(figs):
            a = ax[j // cols, j % cols]
            e0, w = emu_fig(E[tags[0]], u, v)
            img, xs, ys = rect_emu(g, *w)
            a.imshow(np.clip(img / np.percentile(img, 99.7), 0, 1) ** 0.5, extent=(w[0], w[1], w[3], w[2]), cmap='gray')
            for t, c in zip(tags, ('red', 'cyan', 'lime', 'yellow')):
                e, _ = emu_fig(E[t], u, v)
                for x0, y0, x1, y1, z, d, tt in e:
                    if abs(x1 - x0) + abs(y1 - y0) < 0.02 and d >= 3: a.plot([x0], [y0], 'o', ms=3 + d / 6.0, mfc='none', mec=c, mew=0.7)
                    else: a.plot([x0, x1], [y0, y1], '-', color=c, lw=0.6)
            a.set_title(name, fontsize=9); a.tick_params(labelsize=6)
        for j in range(n, rows_ * cols): ax[j // cols, j % cols].axis('off')
        plt.tight_layout(); plt.savefig('sw/p%d_overlay_%s.png' % (page, '_'.join(tags)), dpi=60); plt.close()
