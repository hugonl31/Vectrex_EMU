"""grey pictures of the tube (photos without a RAW, old comparisons) in the emulator's 12000 K: each grey level takes the
colour the emulator gives that level of light (measured on a core render: the faint glow bluer, the bright core whiter,
blue at full). argv: emulator_render.png in.png out.png [...]"""
import sys, numpy as np
from PIL import Image
e = np.asarray(Image.open(sys.argv[1]).convert('RGB')).astype(np.float32); L = e.max(2)
lut = np.zeros((256, 3), np.float32); ok = []
for v in range(256):
    m = (L >= v - 4) & (L <= v + 4) & (L > 6)
    if m.sum() > 30: lut[v] = (e[m] / e[m].max(1, keepdims=True)).mean(0); ok.append(v)
ok = np.array(ok)
for ch in range(3): lut[:, ch] = np.interp(np.arange(256), ok, lut[ok, ch])
lut[:ok.min()] = lut[ok.min()]
print('12000 K by level: 32 %s 96 %s 160 %s 240 %s' % tuple(lut[v].round(2) for v in (32, 96, 160, 240)))
a = sys.argv[2:]
for src, dst in zip(a[0::2], a[1::2]):
    g = np.asarray(Image.open(src).convert('RGB')).astype(np.float32); v = g.max(2)
    grey = (g.max(2) - g.min(2)) < 12                                   # (only the grey parts: coloured marks keep theirs)
    out = np.where(grey[..., None], v[..., None] * lut[np.clip(v, 0, 255).astype(int)], g)
    Image.fromarray(np.clip(out, 0, 255).astype(np.uint8)).save(dst, quality=92) if dst.endswith('.jpg') else Image.fromarray(np.clip(out, 0, 255).astype(np.uint8)).save(dst)
    print(dst)
