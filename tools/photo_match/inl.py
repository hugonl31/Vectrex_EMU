import numpy as np, sys
from scipy import ndimage as nd
from PIL import Image
exec(open('dots.py').read())
TH = [0.5, 0.25, 0.12]
def photo_d(f):
    a = np.load(f)[..., :3].astype(np.float32) / 65535; g = lum(a[::-1, ::-1]); return np.clip(g / 0.428, 0, None) ** (1 / 2.2)
def emu_d(f):
    a = np.load(f) if f.endswith('.npy') else np.array(Image.open(f).convert('RGB')); return a.astype(np.float32).max(2) / 255.0
def joints(d, sp, nrows=4, x0f=0.15, x1f=0.85):
    H, W = d.shape; prj = nd.uniform_filter1d(d[:, int(W * 0.35):int(W * 0.65)].mean(1), 3)
    ys = [y for y in range(5, H - 5) if prj[y] == prj[max(0, y - int(8 * sp)):y + int(8 * sp)].max() and prj[y] > 0.15]
    ys = [y for y in ys if (d[y - 2:y + 3].max(0) > 0.3).sum() > 0.45 * W][:nrows]; out = []
    for y in ys:
        band = d[y - int(4 * sp): y + int(4 * sp) + 1]
        on = np.nonzero(band.max(0) > 0.3)[0]; xa, xb = on[0] + int(2 * sp), on[-1] - int(2 * sp)
        res = []
        for t in TH:
            w = (band > t).sum(0).astype(float) / sp / 2; w = nd.uniform_filter1d(w, max(1, int(0.6 * sp)))
            base = np.median(w[xa:xb]); seg = w[xa:xb]
            pk = [i for i in range(len(seg)) if seg[i] == seg[max(0, i - int(4 * sp)):i + int(4 * sp)].max() and seg[i] > base + 0.08]
            res.append((base, [(xa + i, seg[i] - base) for i in pk]))
        out.append((y, res))
    return out
if __name__ == '__main__':
    for f, kind, sp in [(a.split(':')[0], a.split(':')[1], float(a.split(':')[2])) for a in sys.argv[1:]]:
        d = photo_d(f) if kind == 'photo' else emu_d(f)
        print('==', f)
        for y, res in joints(d, sp):
            print('  y %d  ' % y + ' || '.join('@%.2f base %.2f: %s' % (t, b, ' '.join('%.2f' % e for _, e in pk[-7:])) for t, (b, pk) in zip(TH, res)))
