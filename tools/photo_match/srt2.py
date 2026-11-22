"""SR Test photos, rows by projection: the dash by the gap G (pages 1-2), the BIOS's one vector by scale (pages 3-4)"""
import numpy as np, srt1, st1
from scipy import ndimage as nd
from scipy.signal import find_peaks
def rows(p):
    g = srt1.load(p)[1380:2960, 1150:2800]; s = nd.gaussian_filter(g, 1.5)
    xl = np.nonzero(s.max(0) > 0.3 * s.max())[0].min()
    pr = s[:, xl + 10:xl + 70].mean(1); pk, _ = find_peaks(pr, height=0.25 * pr.max(), distance=60)
    out = []
    for y in pk:
        band = s[y - 14:y + 15]; prof = band.max(0); xa = np.arange(len(prof), dtype=float)
        hi = np.nonzero(prof > 0.5 * np.median(np.sort(prof)[-40:]))[0]; lev = np.median(prof[hi[0] + 8:hi[-1] - 8]) if hi[-1] - hi[0] > 30 else prof.max()
        a = hi[0] - 1 + (0.5 * lev - prof[hi[0] - 1]) / (prof[hi[0]] - prof[hi[0] - 1]); b = hi[-1] + (prof[hi[-1]] - 0.5 * lev) / (prof[hi[-1]] - prof[hi[-1] + 1])
        hi2 = np.nonzero(prof > 0.5 * lev)[0]; a = hi2[0]; b = hi2[-1]
        a = a - 1 + (0.5 * lev - prof[a - 1]) / (prof[a] - prof[a - 1]); b = b + (prof[b] - 0.5 * lev) / (prof[b] - prof[b + 1])
        n = int(b - a); out.append(dict(y=int(y) + 1380, a=a, b=b, len=b - a, lev=lev, start=band[:, int(a) - 6:int(a) + 14].sum() / (band[:, int(a + n // 2) - 10:int(a + n // 2) + 10].sum() + 1e-9),
                                        end=band[:, int(b) - 14:int(b) + 6].sum() / (band[:, int(a + n // 2) - 10:int(a + n // 2) + 10].sum() + 1e-9), pk0=prof[int(a):int(a) + 12].max() / lev, pk1=prof[int(b) - 12:int(b) + 1].max() / lev))
    return out
if __name__ == '__main__':
    for p in (1, 2, 3, 4):
        r = rows(p); print('page %d: %d rows' % (p, len(r)))
        print('  len px ', ' '.join('%5.0f' % d['len'] for d in r)); print('  x0     ', ' '.join('%5.0f' % d['a'] for d in r))
        print('  start/mid light', ' '.join('%5.2f' % d['start'] for d in r)); print('  end/mid light  ', ' '.join('%5.2f' % d['end'] for d in r))
        print('  start pk', ' '.join('%5.2f' % d['pk0'] for d in r)); print('  end pk  ', ' '.join('%5.2f' % d['pk1'] for d in r))
