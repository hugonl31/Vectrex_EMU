"""pages 2 and 3: the photo's ridge against the emulator's path, figure by figure: the mean distance (units) after the best
shift; pictures with the path over the photo. usage: swfig2.py tag [tag...] [--pic]"""
import numpy as np, sys
from scipy import ndimage as nd
from scipy.spatial import cKDTree
from sw1 import load
from swfig import FIGS, emu_fig, rect_emu, ridge, KU, OX, OY
_G = {}
def photo_fig(page, u, v, box):
    """ridge points (x, y, light) of the photo within box (emulator units)"""
    key = (page, u, v)
    if key not in _G:
        if page not in _G: _G[page] = load({2: '37685', 3: '39767'}[page])
        img, xs, ys = rect_emu(_G[page], *box); _G[key] = (ridge(img, xs, ys), img, box)
    return _G[key]
def path_pts(e, step=0.05):
    P = []; T = []
    for x0, y0, x1, y1, z, d, t in e:
        n = max(1, int(np.hypot(x1 - x0, y1 - y0) / step)); f = (np.arange(n) + 0.5) / n
        P.append(np.stack([x0 + (x1 - x0) * f, y0 + (y1 - y0) * f], 1))
    return np.concatenate(P)
def score(tag, pic=False):
    out = {}
    E = {p: np.load('sw/emu_%s_p%d.npy' % (tag, p)) for p in (2, 3)}
    B = {p: np.load('sw/emu_base_p%d.npy' % p) for p in (2, 3)}
    for page, name, u, v in FIGS:
        win = (-34, 34, -6, 46) if name.startswith('-V') else (-24, 34, -6, 46) if page == 2 else (-19, 5, -6, 46)
        eb, _ = emu_fig(B[page], u, v, win); box = (eb[:, [0, 2]].min() - 4, eb[:, [0, 2]].max() + 4, eb[:, [1, 3]].min() - 4, eb[:, [1, 3]].max() + 4)
        (rx, ry, rw), img, _ = photo_fig(page, u, v, box)
        e, _ = emu_fig(E[page], u, v, win); P = path_pts(e); R = np.stack([rx, ry], 1)
        best = None
        for it, (rng, st) in enumerate(((2.0, 0.5), (0.5, 0.125), (0.125, 0.03))):
            c0 = best[1] if best else (0.0, 0.0)
            for dx in np.arange(c0[0] - rng, c0[0] + rng + 1e-9, st):
                for dy in np.arange(c0[1] - rng, c0[1] + rng + 1e-9, st):
                    Q = P + (dx, dy); d1 = cKDTree(Q).query(R)[0]; d2 = cKDTree(R).query(Q)[0]
                    s = 0.5 * (np.sqrt((d1 ** 2).mean()) + np.sqrt((d2 ** 2).mean()))
                    if best is None or s < best[0]: best = (s, (dx, dy), d1.max(), d2.max())
        out[(page, name)] = best + (img, box, e)
    return out
if __name__ == '__main__':
    pic = '--pic' in sys.argv; tags = [a for a in sys.argv[1:] if a != '--pic']
    S = {t: score(t) for t in tags}
    for key in S[tags[0]]:
        print('p%d %-10s' % key, '   '.join('%s rms %.3f (max %.2f / %.2f) shift %+.2f %+.2f' % (t, S[t][key][0], S[t][key][2], S[t][key][3], *S[t][key][1]) for t in tags))
    for t in tags: print(t, 'page 2 mean %.3f   page 3 mean %.3f' % (np.mean([S[t][k][0] for k in S[t] if k[0] == 2]), np.mean([S[t][k][0] for k in S[t] if k[0] == 3])))
    if pic:
        import matplotlib; matplotlib.use('Agg'); import matplotlib.pyplot as plt
        for page, cols in ((2, 3), (3, 8)):
            keys = [k for k in S[tags[0]] if k[0] == page]; rows_ = (len(keys) + cols - 1) // cols
            fig, ax = plt.subplots(rows_, cols, figsize=(cols * 5, rows_ * 6.5)); ax = np.atleast_2d(ax)
            for j, k in enumerate(keys):
                a = ax[j // cols, j % cols]; s, sh, m1, m2, img, w, e = S[tags[0]][k]
                a.imshow(np.clip(img / np.percentile(img, 99.8), 0, 1) ** 0.5, extent=(w[0], w[1], w[3], w[2]), cmap='gray')
                for t, c in zip(tags, ('red', 'cyan', 'lime', 'yellow')):
                    s, sh, m1, m2, _, _, e = S[t][k]
                    for x0, y0, x1, y1, z, d, tt in e:
                        if abs(x1 - x0) + abs(y1 - y0) < 0.02 and d >= 3: a.plot([x0 + sh[0]], [y0 + sh[1]], 'o', ms=4 + d / 4.0, mfc='none', mec=c, mew=0.8)
                        else: a.plot([x0 + sh[0], x1 + sh[0]], [y0 + sh[1], y1 + sh[1]], '-', color=c, lw=0.7)
                a.set_title('%s  %s' % (k[1], ' / '.join('%.2f' % S[t][k][0] for t in tags)), fontsize=10); a.tick_params(labelsize=6)
            for j in range(len(keys), rows_ * cols): ax[j // cols, j % cols].axis('off')
            plt.tight_layout(); plt.savefig('sw/p%d_ov_%s.png' % (page, '_'.join(tags)), dpi=55); plt.close()
