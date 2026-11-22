"""pages 2 and 3, each corner: the two legs' straight parts meet at a point I; the cut = the curve's nearest distance to I
(0 = sharp), the dot's place relative to I. The photo's ridge and the emulator's path alike (emulator units)."""
import numpy as np, sys, json
from scipy import ndimage as nd
import swfig2
from swfig import FIGS, emu_fig
def legs(P, w=None, far=5.5, top=None):
    """P: points (x, y); corner = the leftmost; the legs' straight parts = points more than `far` (y) from it"""
    c = P[np.argmin(P[:, 0])]; up = P[P[:, 1] < c[1] - far]; dn = P[P[:, 1] > c[1] + far]
    if len(up) < 8 or len(dn) < 8: return None
    pu = np.polyfit(up[:, 1], up[:, 0], 1); pd = np.polyfit(dn[:, 1], dn[:, 0], 1)
    yi = (pd[1] - pu[1]) / (pu[0] - pd[0]); xi = pu[0] * yi + pu[1]
    d = np.hypot(P[:, 0] - xi, P[:, 1] - yi)
    return (xi, yi), d.min(), pu[0], pd[0]
def photo(page, name, u, v, box):
    (rx, ry, rw), img, _ = swfig2.photo_fig(page, u, v, box); P = np.stack([rx, ry], 1)
    # only this figure: the ridge points connected to the brightest region within the window's middle
    r = legs(P)
    if r is None: return None
    I, cut, s1, s2 = r
    # the dot: the brightest blob (smoothed image), sub-pixel
    s = nd.gaussian_filter(img, 4); iy, ix = np.unravel_index(np.argmax(s), s.shape)
    xs = np.linspace(box[0], box[1], img.shape[1], endpoint=False); ys = np.linspace(box[2], box[3], img.shape[0], endpoint=False)
    line = np.median(rw); peak = s[iy, ix]
    return dict(cut=cut, dot=(xs[ix] - I[0], ys[iy] - I[1]), s1=s1, s2=s2, dotrel=float(peak / max(line, 1e-9)))
def emu(E, page, name, u, v):
    win = (-34, 34, -6, 46) if name.startswith('-V') else (-24, 34, -6, 46) if page == 2 else (-19, 5, -6, 46)
    e, _ = emu_fig(E[page], u, v, win); P = swfig2.path_pts(e, 0.02); r = legs(P)
    if r is None: return None
    I, cut, s1, s2 = r
    rest = e[(abs(e[:, 2] - e[:, 0]) + abs(e[:, 3] - e[:, 1]) < 0.05)]
    # the dwell point: the time-weighted mean place of the slowest 20 cycles' worth
    sp = np.hypot(e[:, 2] - e[:, 0], e[:, 3] - e[:, 1]) / np.maximum(e[:, 5], 1e-9); o = np.argsort(sp); t = np.cumsum(e[o, 5]); k = o[:max(1, np.searchsorted(t, 20.0))]
    dx = np.average((e[k, 0] + e[k, 2]) / 2, weights=e[k, 5]) - I[0]; dy = np.average((e[k, 1] + e[k, 3]) / 2, weights=e[k, 5]) - I[1]
    return dict(cut=cut, dot=(dx, dy), s1=s1, s2=s2)
def table(tags):
    B = {p: np.load('sw/emu_base_p%d.npy' % p) for p in (2, 3)}
    rows = []
    for page, name, u, v in FIGS:
        if page == 2 and not name.startswith('-V'): continue
        win = (-34, 34, -6, 46) if name.startswith('-V') else (-19, 5, -6, 46)
        eb, _ = emu_fig(B[page], u, v, win); box = (eb[:, [0, 2]].min() - 4, eb[:, [0, 2]].max() + 4, eb[:, [1, 3]].min() - 4, eb[:, [1, 3]].max() + 4)
        ph = photo(page, name, u, v, box); r = [(page, name), ph]
        for t in tags:
            E = {p: np.load('sw/emu_%s_p%d.npy' % (t, p)) for p in (2, 3)}; r.append(emu(E, page, name, u, v))
        rows.append(r)
    return rows
if __name__ == '__main__':
    tags = sys.argv[1:]
    rows = table([t for t in tags if t != '--short'])
    short = '--short' in sys.argv; tags = [t for t in tags if t != '--short']; rows = table(tags) if short else rows
    print('%-16s %-28s %s' % ('', 'photo: cut  dot dx dy', '   '.join('%-22s' % t[-22:] for t in tags)))
    err = {t: [] for t in tags}
    for r in rows:
        (page, name), ph = r[0], r[1]
        if ph is None: print('p%d %-12s photo: -' % (page, name)); continue
        s = 'p%d %-12s %5.2f  %+5.2f %+5.2f      ' % (page, name, ph['cut'], ph['dot'][0], ph['dot'][1])
        for t, e in zip(tags, r[2:]):
            if e is None: s += '   -                   '; continue
            s += '%5.2f  %+5.2f %+5.2f      ' % (e['cut'], e['dot'][0], e['dot'][1]); err[t].append((e['cut'] - ph['cut'], e['dot'][0] - ph['dot'][0], e['dot'][1] - ph['dot'][1]) if page == 3 else (e['cut'] - ph['cut'], 0, 0))
        if not short: print(s)
    print('%-28s                 cuts: %s' % ('photo', ' '.join('%5.2f' % r[1]['cut'] for r in rows if r[1] is not None)))
    for t in tags:
        a = np.array(err[t]); print('%-28s cut rms %.3f   cuts: %s' % (t, np.sqrt((a[:, 0] ** 2).mean()), ' '.join('%5.2f' % (r[2 + tags.index(t)]['cut'] if r[2 + tags.index(t)] else -1) for r in rows if r[1] is not None)))
