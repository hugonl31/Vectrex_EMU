"""the logo's shape, scale-free: the lit pixels' distribution along the logo's own long axis (PCA), as a signature"""
import numpy as np, sys, os
from scipy import ndimage as nd
def sig(pts, wts):
    c = np.average(pts, axis=0, weights=wts); q = pts - c
    cov = (q * wts[:, None]).T @ q / wts.sum(); ev, evec = np.linalg.eigh(cov)
    ax = q @ evec[:, 1]; pr = q @ evec[:, 0]
    if np.average(ax ** 3, weights=wts) < 0: ax = -ax
    L = np.percentile(ax, 99.8) - np.percentile(ax, 0.2); Hh = np.percentile(pr, 99.8) - np.percentile(pr, 0.2)
    h, _ = np.histogram((ax - np.percentile(ax, 0.2)) / L, bins=60, range=(0, 1), weights=wts); h = h / h.sum()
    return L / Hh, h
def photo(f):
    g = np.load(f).astype(np.float32); sm = nd.gaussian_filter(g, 1.5); m = sm > 0.3 * np.percentile(sm, 99.5)
    ys, xs = np.nonzero(m); return sig(np.stack([xs, ys], 1).astype(float), np.ones(len(xs)))
def emu(env):
    import subprocess, json
    code = r'''
import sys, os, json
sys.path.insert(0, r'G:\AI\Vectrex_EMU\src\frontend'); os.chdir(r'G:\AI\Vectrex_EMU')
import vectrex_emu as V
c = V.Core(os.path.abspath(r'build\Vectrex_EMU\roms\Vectorblade.bin'), False, 'auto', 'off', None, None, None, False)
for i in range(760): L = c.frame(0, 0, 0, 0)[0]
print(json.dumps([[l[0], l[1], l[2], l[3]] for l in L if int(l[4]) & 127 and not int(l[4]) & 128]))
'''
    e = dict(os.environ); e.update(env); r = subprocess.run([sys.executable, '-c', code], env=e, capture_output=True, text=True)
    L = np.array(json.loads(r.stdout.strip().splitlines()[-1])); mid = (L[:, :2] + L[:, 2:]) / 2; ln = np.hypot(L[:, 2] - L[:, 0], L[:, 3] - L[:, 1]) + 1.0
    mid[:, 1] = -mid[:, 1]
    return sig(mid, ln)
ap, hp = photo('oct/Vectorblade1.npy'); ap2, hp2 = photo('oct/Vectorblade2.npy')
print('photo 1: length / height %.3f;  photo 2: %.3f;  the two photos differ by %.4f' % (ap, ap2, np.abs(hp - hp2).sum()))
for name, env in (('emulator, as fitted in October', {'VT_RAMPSW': '0'}), ('emulator, short-pause rule', {'VT_RAMPSW': '1'})):
    a, h = emu(env); d = min(np.abs(h - hp).sum(), np.abs(h[::-1] - hp).sum())
    print('%-34s length / height %.3f;  shape difference to photo 1: %.4f' % (name, a, d))
