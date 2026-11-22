"""the PC's beam-trace pictures (low exposure: light = value^2.2) measured as the photos: page 10's square side, page 4's lines and dots, page 1's rows"""
import numpy as np, sys
from PIL import Image
from scipy import ndimage as nd
def lin(f):
    a = np.asarray(Image.open(f).convert('RGB')).astype(np.float32) / 255; return (a ** 2.2).max(2)
def fwhm(p):
    pk = p.max(); ab = np.nonzero(p > 0.5 * pk)[0]; i, j = ab[0], ab[-1]
    return (j + (p[j] - 0.5 * pk) / (p[j] - p[min(j + 1, len(p) - 1)] + 1e-9)) - (i - (p[i] - 0.5 * pk) / (p[i] - p[max(i - 1, 0)] + 1e-9))
tag = sys.argv[1] if len(sys.argv) > 1 else 'pc2'
g = lin('%s_p10_lo.png' % tag); bg = np.median(g); print('p10 max %.3f' % g.max())
# the square at scale $20: in the 1384-wide picture x ~445..560, y ~1010..1140
c = g[985:1160, 425:585] - bg; cm = nd.gaussian_filter1d(c.mean(0), 1); rm = nd.gaussian_filter1d(c.mean(1), 1)
xs = [i for i in range(2, len(cm) - 2) if cm[i] == cm[max(0, i - 8):i + 9].max() and cm[i] > 0.4 * cm.max()]; ys = [i for i in range(2, len(rm) - 2) if rm[i] == rm[max(0, i - 8):i + 9].max() and rm[i] > 0.4 * rm.max()]
xl, xr, yt, yb = xs[0] + 425, xs[-1] + 425, ys[0] + 985, ys[-1] + 985; pxu = (xr - xl) / 25.79; print('square $20: x %d..%d y %d..%d, %.2f px a unit' % (xl, xr, yt, yb, pxu))
for nm, prof in (('top side (left to right)', (g[yt - 6:yt + 7, xl - 14:xr + 15] - bg).sum(0)), ('bottom (right to left)', (g[yb - 6:yb + 7, xl - 14:xr + 15] - bg).sum(0)[::-1]),
                 ('left (bottom to top)', (g[yt - 14:yb + 15, xl - 6:xl + 7] - bg).sum(1)[::-1]), ('right (top to bottom)', (g[yt - 14:yb + 15, xr - 6:xr + 7] - bg).sum(1))):
    n = len(prof); lev = np.median(prof[n // 3:2 * n // 3]); print('  %-26s / the line: %s' % (nm, ' '.join('%.1f' % v for v in (prof / lev)[::2])))
g = lin('%s_p4_lo.png' % tag); bg = np.median(g); print('p4 max %.3f' % g.max())
s = nd.gaussian_filter(g - bg, 1.0); lab, k = nd.label(nd.binary_dilation(s > 0.004, iterations=2))
B = []
for sl in nd.find_objects(lab):
    a, b = sl; h, w = a.stop - a.start, b.stop - b.start
    if h > 40 or w > 150 or h < 3 or a.start < 560 or a.stop > 1400: continue
    cc = g[a.start - 5:a.stop + 5, b.start - 5:b.stop + 5] - bg; B.append((a.start, b.start, w, h, cc))
B.sort(key=lambda d: (round(d[0] / 40), d[1])); last = None
for y, x, w, h, cc in B:
    if w > 2.2 * h:
        mid = cc[:, cc.shape[1] // 3:2 * cc.shape[1] // 3].mean(1); last = mid.sum(); print('  y %4d x %4d LINE len %3d  peak %.3f  FWHM %.1f  light a px %.4f' % (y, x, w, mid.max(), fwhm(mid), mid.sum()))
    else:
        iy, ix = np.unravel_index(np.argmax(nd.gaussian_filter(cc, 1)), cc.shape); print('  y %4d x %4d DOT  peak %.3f  FWHM %.1f x %.1f  light %.3f%s' % (y, x, cc.max(), fwhm(cc[iy]), fwhm(cc[:, ix]), cc.sum(), '  = %.1f px of its line' % (cc.sum() / last) if last else ''))
