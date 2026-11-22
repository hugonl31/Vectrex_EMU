import numpy as np, sys
from scipy import ndimage as nd
exec(open('dots.py').read()); exec(open('cmp.py').read().split('for f, flip')[0])
TH = [0.9, 0.5, 0.25, 0.12]
def disp_photo(f, LP=0.428):
    a = np.load(f)[..., :3].astype(np.float32) / 65535; g = lum(a[::-1, ::-1]); return np.clip(g / LP, 0, None) ** (1 / 2.2)
def disp_emu(f): return np.load(f)[..., :3].astype(np.float32).max(2) / 255.0
def radii(d, rows, sp):
    out = np.zeros((4, 10, len(TH)))
    for ri, r in enumerate(rows[:4]):
        for ci, (y, x) in enumerate(r):
            prof, E, bg = profile(d, y, x, int(5 * sp) + 8); u = np.arange(len(prof)) / sp
            for ti, t in enumerate(TH):
                i = np.argmax(prof < t); out[ri, ci, ti] = 0 if i == 0 else u[i-1] + (u[i]-u[i-1]) * (prof[i-1]-t) / (prof[i-1]-prof[i])
    return out
def line_hw(d, rows, sp):
    y4 = rows[3][0][0]; x = int(np.mean([p[1] for p in rows[0][3:7]])); col = d[y4 + int(5*sp): y4 + int(60*sp), x-5:x+5].mean(1)
    yl = y4 + int(5*sp) + int(np.argmax(col)); c = d[yl-int(4*sp): yl+int(4*sp)+1, x-int(8*sp): x+int(8*sp)].mean(1); u = (np.arange(len(c)) - len(c)//2) / sp
    return [float(np.abs(u[c > t]).max()) if (c > t).any() else 0 for t in TH], c.max()
EROWS = [[[y, x] for x in [206, 276, 351, 431, 515, 602, 690, 772, 851, 925]] for y in [403, 496, 591, 688]]
def analyse(d, thr, rows=None):
    rows = rows or rows10(d, thr); sp = np.mean([np.mean(np.diff([p[1] for p in r])) for r in rows]) / 23.0
    return radii(d, rows, sp), line_hw(d, rows, sp)
if __name__ == '__main__':
    res = {}
    P = disp_photo('19710_raw.npy'); res['photo'] = analyse(P[680:2200], 0.15)
    for f in sys.argv[1:]: res[f] = analyse(disp_emu(f), 0, EROWS)
    for k, (R, (lh, lp)) in res.items():
        print('== %s   line peak %.2f, line half-width at %s: %s' % (k, lp, TH, ' '.join('%.2f' % v for v in lh)))
        for ti, t in enumerate(TH):
            print('  r@%.2f' % t, ' | '.join(' '.join('%4.2f' % v for v in R[ri, :, ti]) for ri in range(4)))
