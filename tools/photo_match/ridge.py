import numpy as np, sys
from PIL import Image
from scipy import ndimage as nd
def stats(lin, name):
    sm = nd.gaussian_filter(lin, 1.0); mx = nd.maximum_filter(sm, 5)
    lvl = np.percentile(sm[sm > np.percentile(sm, 97)], 50)            # a typical line pixel
    ridge = (sm == mx) & (sm > 0.35 * lvl)
    v = sm[ridge]; med = np.median(v)
    print('%-34s ridge px %6d  line level %.4f  brightest ridge points / line: p90 %.2f  p99 %.2f  p99.9 %.2f  max %.2f' % (name, len(v), med, np.percentile(v, 90) / med, np.percentile(v, 99) / med, np.percentile(v, 99.9) / med, v.max() / med))
g = np.load('oct/Vectorblade1.npy').astype(np.float32)
stats(g, 'photo Vectorblade1 (raw, linear)')
g2 = np.load('oct/Vectorblade2.npy').astype(np.float32); stats(g2, 'photo Vectorblade2')
for f in sys.argv[1:]:
    d = np.array(Image.open(f).convert('RGB')).astype(np.float32).max(2) / 255.0
    H, W = d.shape; stats((d ** 2.2)[int(H * 0.1):int(H * 0.5)], f)
