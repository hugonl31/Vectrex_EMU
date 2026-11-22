"""the BIOS line's start / end dot in the core as the photo measures it: dwell density along x, smoothed by the spot,
^p (the light law), the first 20 px window against the middle's"""
import numpy as np, srt3, sys
from scipy import ndimage as nd
PXU = 7.14 / (127 / 133.1); BW = 0.05
def prof(r, p=0.7, fwhm=0.9):
    x0 = r[:, [0, 2]].min() - 4; n = int((r[:, [0, 2]].max() + 4 - x0) / BW) + 1; d = np.zeros(n)
    for a in r:
        xa, xb, dw = a[0], a[2], a[5]
        if abs(xb - xa) < BW: d[int(((xa + xb) / 2 - x0) / BW)] += dw
        else:
            i0, i1 = sorted((int((xa - x0) / BW), int((xb - x0) / BW))); d[i0:i1 + 1] += dw / (i1 - i0 + 1)
    s = nd.gaussian_filter1d(d / BW, fwhm / 2.355 / BW); return x0, s ** p
def meas(r, p=0.7):
    x0, L = prof(r, p); cam = nd.gaussian_filter1d(L, 6.0 / PXU / 2.355 / BW)        # (the lens: ~6 px more)
    mid = len(cam) // 2; lev = np.median(cam[mid - 20:mid + 20]); on = np.nonzero(cam > 0.5 * lev)[0]; a, b = on[0], on[-1]; w = lambda px: int(px / PXU / BW)
    m = (a + b) // 2; midsum = L[m - w(10):m + w(10)].sum()
    return (b - a) * BW / (127 / 133.1), L[max(0, a - w(6)):a + w(14)].sum() / midsum, L[b - w(14):b + w(6)].sum() / midsum, cam[a:a + w(12)].max() / lev, cam[b - w(12):b + 1].max() / lev
if __name__ == '__main__':
    for p in (1, 3, 4):
        R = srt3.core_rows(p)[:15]; M = [meas(r) for r in R]
        print('page %d' % p); 
        for k, nm in enumerate(('len cyc ', 'start/mid', 'end/mid ', 'start pk', 'end pk  ')): print('  ', nm, ' '.join('%5.2f' % m[k] if k else '%5.1f' % m[k] for m in M))
