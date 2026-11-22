import numpy as np, sys
from scipy import ndimage as nd
import cornerm as C
def row_table(img, roi, n, order, name, blur=0.0):
    if blur > 0: img = nd.gaussian_filter(img, blur)
    F = C.figures(img, roi); F = [f for f in F]
    key = (lambda f: f[0]) if order in ('y', '-y') else (lambda f: f[1])
    F = sorted(F, key=key, reverse=order.startswith('-'))
    if len(F) != n: print('  %s: %d figures found, %d expected' % (name, len(F), n))
    R = [C.measure(f[2]) for f in F[:n]]
    return R
def ratio(r, photo):
    # second leg / first leg: the first leg's end = the smaller x in the photo, the smaller y in the render
    k = 0 if photo else 1; a, b = (r['la'], r['lb']) if r['A'][k] < r['B'][k] else (r['lb'], r['la']); return b / a
def show(name, R):
    print('%-34s second / first leg: %s' % ('', ' '.join('%5.2f' % ratio(r, name.startswith('photo')) if r and 'A' in r else '  -  ' for r in R)))
    print('%-34s %s   width %.1f px' % (name, ' '.join('%5.3f' % (r['cut'] / r['leg']) if r else '  -  ' for r in R), np.median([r['width'] for r in R if r])))
if __name__ == '__main__':
    g = np.load('oct/Cornertest.npy').astype(np.float32)
    # the photo: figures run bottom (rest 0) to top (rest 96); two columns of figures (x ~ 2560-2920 and 2980-3320)
    for nm, roi in (('photo row $7F', (400, 2900, 2520, 2950)), ('photo row $5F', (400, 2900, 2960, 3340))):
        show(nm, row_table(g, roi, 10, '-y', nm))
    for tag in sys.argv[1:]:
        a = np.load('sl_corner_%s.npy' % tag).max(2).astype(np.float32) / 255
        for nm, roi in (('%s row $7F' % tag, (770, 905, 150, 1060)), ('%s row $5F' % tag, (905, 1010, 150, 1060))):
            show(nm, row_table(a, roi, 10, 'x', nm))
