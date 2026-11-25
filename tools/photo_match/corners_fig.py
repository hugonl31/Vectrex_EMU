import numpy as np, sys
from PIL import Image, ImageDraw, ImageFont
from scipy import ndimage as nd
S = sys.argv[1]
def bbox(a, thr):
    m = nd.gaussian_filter(a.max(2), 2) > thr; ys, xs = np.nonzero(m); return ys.min(), ys.max(), xs.min(), xs.max()
r = np.asarray(Image.open(S + '/corner_real_wb.png').convert('RGB')).astype(np.float32)   # (the RAW as shot: white balance 6400 K, the screen's own white)
e = np.asarray(Image.open(S + '/corner_now_v.png').convert('RGB')).astype(np.float32)
ry0, ry1, rx0, rx1 = bbox(r, 40); ey0, ey1, ex0, ex1 = bbox(e, 25)
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
