import numpy as np, sys
from PIL import Image, ImageDraw, ImageFont
from scipy import ndimage as nd
S = sys.argv[1]
def bbox(a, thr):
    m = nd.gaussian_filter(a.max(2), 2) > thr; ys, xs = np.nonzero(m); return ys.min(), ys.max(), xs.min(), xs.max()
r = np.asarray(Image.open(S + '/' + (sys.argv[2] if len(sys.argv) > 2 else 'corner_real_wb.png')).convert('RGB')).astype(np.float32)   # (the RAW as shot: white balance 6400 K, the screen's own white)
e = np.asarray(Image.open(S + '/corner_now_v.png').convert('RGB')).astype(np.float32)
def content_box(a):   # (the drawn content's own extent: bright lines, high-passed - as rawcmp.py; not the photo's glare)
    g = a.max(2); g = np.clip(g - nd.gaussian_filter(g, 12), 0, None); ys, xs = np.nonzero(g > 0.3 * np.percentile(g, 99.9))
    x0, x1 = np.percentile(xs, [0.2, 99.8]); y0, y1 = np.percentile(ys, [0.2, 99.8]); p_ = 0.03 * max(x1 - x0, y1 - y0)
    return max(0, int(y0 - p_)), min(a.shape[0] - 1, int(y1 + p_)), max(0, int(x0 - p_)), min(a.shape[1] - 1, int(x1 + p_))
ry0, ry1, rx0, rx1 = content_box(r); ey0, ey1, ex0, ex1 = content_box(e)
print('content: photo %.3f, emulator %.3f wide to tall' % ((rx1 - rx0) / (ry1 - ry0), (ex1 - ex0) / (ey1 - ey0)))
W = 760
def fit(a): im = Image.fromarray(np.clip(a, 0, 255).astype(np.uint8)); return np.asarray(im.resize((W, round(im.height * W / im.width)), Image.LANCZOS)).astype(np.float32)
rc, ec = fit(r[ry0:ry1 + 1, rx0:rx1 + 1]), fit(e[ey0:ey1 + 1, ex0:ex1 + 1])
def norm(a): l = a.max(2); p = np.percentile(l[l > 20], 99.7); return np.clip(a * 235.0 / p, 0, 255)
# (brightness: the emulator as rendered; the photo brought to it on the lines' own level - the photo was exposed for the
#  fine detail, so the brightest places clip and its lines are wider and brighter than the tube looks in the room)
def light(a): l = a.max(2); return ((a / 255.0) ** 2.2)[l > 12].sum()   # (the light in linear terms, the background left out)
k = (light(ec) / light(rc)) ** (1 / 2.2)                                    # (the same light in all: the camera spreads it wider)
rc = np.clip(rc * k, 0, 255); print('photo scaled by %.3f' % k)
# the emulator's white, level by level (its 12000 K: the faint glow bluer, the bright core whiter) -> the photo, which is grey
L = ec.max(2); lut = np.zeros((256, 3), np.float32); cnt = np.zeros(256)
for v in range(256):
    m = (L >= v - 6) & (L <= v + 6) & (L > 8)
    if m.sum() > 30: lut[v] = (ec[m] / ec[m].max(1, keepdims=True)).mean(0); cnt[v] = 1
ok = np.nonzero(cnt)[0]
for ch in range(3): lut[:, ch] = np.interp(np.arange(256), ok, lut[ok, ch])
def part(a):
    H = a.shape[0]; return np.concatenate([a[:int(H * 0.10)], np.zeros((14, W, 3), np.float32), a[int(H * 0.57):]], 0)
rp, ep = part(rc), part(ec)
pad = 18; H = max(rp.shape[0], ep.shape[0]); out = np.zeros((H + 56, 2 * W + 3 * pad, 3), np.float32) + 6
out[44:44 + rp.shape[0], pad:pad + W] = rp; out[44:44 + ep.shape[0], 2 * pad + W:2 * pad + 2 * W] = ep
im = Image.fromarray(np.clip(out, 0, 255).astype(np.uint8)); d = ImageDraw.Draw(im)
f = ImageFont.truetype('segoeui.ttf', 22)
d.text((pad, 10), 'Real Vectrex (photo)', fill=(235, 160, 60), font=f); d.text((2 * pad + W, 10), 'Vectrex_EMU', fill=(235, 160, 60), font=f)
im.save(r'G:\AI\Vectrex_EMU\docs\images\page\corners_compare.jpg', quality=92); print(im.size)
