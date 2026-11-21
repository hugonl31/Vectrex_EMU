"""energy ratios, page 1: each dot's light / the 7F reference line's light per unit (linear, exposure- and blur-free)"""
import numpy as np, sys
from PIL import Image
from scipy import ndimage as nd
src = open('cmpd.py').read(); exec(src[:src.index("if __name__")])
ROWS = [None]
def ratios(lin, y0f, y1f, thr, R_u=3.0, keep=False):
    H, W = lin.shape; sub = lin[int(H * y0f):int(H * y1f)]
    rows = ROWS[0] if (keep and ROWS[0]) else rows10(sub, thr)
    if keep: ROWS[0] = rows
    sp = np.mean([np.mean(np.diff([p[1] for p in r])) for r in rows]) / 23.0
    E = np.zeros((4, 10)); PK = np.zeros((4, 10))
    for ri, r in enumerate(rows[:4]):
        for ci, (y, x) in enumerate(r):
            R = int(R_u * sp); yy, xx = np.mgrid[-R:R + 1, -R:R + 1]; p = sub[y - R:y + R + 1, x - R:x + R + 1]
            rr = np.hypot(yy, xx); bg = np.median(p[rr > R - 3]); E[ri, ci] = ((p - bg) * (rr <= R - 3)).sum() / sp ** 2; PK[ri, ci] = p.max() - bg
    # the 7F reference line: the first long line below the dots
    y4 = rows[3][0][0] + int(H * y0f); x = int(np.mean([p[1] for p in rows[0][3:7]]))
    col = lin[y4 + int(5 * sp): y4 + int(60 * sp), x - 5:x + 5].mean(1); yl = y4 + int(5 * sp) + int(np.argmax(col))
    c = lin[yl - int(3 * sp): yl + int(3 * sp) + 1, x - int(8 * sp): x + int(8 * sp)].mean(1); c = c - np.median(np.r_[c[:3], c[-3:]])
    return E / (c.sum() / sp), PK / c.max(), sp, c.max()
if __name__ == '__main__':
    a = np.load('19710_raw.npy')[..., :3].astype(np.float32) / 65535; g = lum(a[::-1, ::-1])
    EP, PP, sp, lp = ratios(g[680:2200], 0, 1, 0.004)
    print('photo: dot light / line light a unit (7F 5F 40 30 x 4..128 cycles); line peak %.3f' % lp)
    for r in EP: print('   ', ' '.join('%6.2f' % v for v in r))
    for f in sys.argv[1:]:
        d = np.array(Image.open(f).convert('RGB')).astype(np.float32).max(2) / 255.0; lin = d ** 2.2
        if ROWS[0] is None:
            dr = np.array(Image.open('bt2_p1.png').convert('RGB')).astype(np.float32).max(2) / 255.0; ratios(dr ** 2.2, 0.26, 0.50, 0.0005, keep=True)
        EE, PE, sp, lp = ratios(lin, 0.26, 0.50, 0, keep=True)
        print('%s: line peak (display) %.2f' % (f, lp ** (1 / 2.2)))
        for r in EE: print('   ', ' '.join('%6.2f' % v for v in r))
        print('    emu / photo:'); [print('   ', ' '.join('%6.2f' % v for v in r)) for r in EE / EP]
