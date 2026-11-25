"""the camera's blur out of the RAW: the photo's lines are 9.5 px wide (FWHM, mid intensity); the eye's spot is 1.25/1.85 of
what matched the photos (BT_SPOT), so the lens adds a Gaussian of sqrt(1 - (1.25/1.85)^2) x the line's sigma ~ 2.9 px -
Richardson-Lucy in linear light, per channel"""
import numpy as np, sys
from PIL import Image
from skimage.restoration import richardson_lucy
lin = np.load(sys.argv[2] if len(sys.argv) > 2 else 'dng/asshot.npy').astype(np.float64)
sp = 9.5 / 2.355; sc = sp * np.sqrt(1 - (1.25 / 1.85) ** 2); print('camera sigma %.2f px' % sc)
L = lin.max(2); ys, xs = np.nonzero(L > 0.02); y0, y1, x0, x1 = max(0, ys.min() - 60), ys.max() + 60, max(0, xs.min() - 60), xs.max() + 60
c = lin[y0:y1, x0:x1]; p = np.percentile(c.max(2)[c.max(2) > np.percentile(c.max(2), 98)], 95)
c = np.clip(c / p, 0, None)
r = int(4 * sc) | 1; yy, xx = np.mgrid[-r:r + 1, -r:r + 1]; psf = np.exp(-(xx ** 2 + yy ** 2) / (2 * sc * sc)); psf /= psf.sum()
it = int(sys.argv[1]) if len(sys.argv) > 1 else 30
out = np.stack([richardson_lucy(c[..., k], psf, num_iter=it, clip=False) for k in range(3)], 2)
for nm, a in (('soft', c), ('sharp', out)):
    d = np.clip(a * 0.9, 0, 1) ** (1 / 2.2)
    Image.fromarray((d * 255).astype(np.uint8)).save('day/corner_real_%s.png' % nm)
# the line width after
def fw(a, row):
    pass
print('done', c.shape)
