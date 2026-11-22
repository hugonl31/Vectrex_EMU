"""make_pitrex_logo.py - the PiTrex logo (icons/pitrex_logo_src.png, the owner's file) made ready for the library's tiles:
the USB badge at the top left and the maker's name taken out, the grid paper round it made transparent.

    python tools/make_pitrex_logo.py   ->  icons/pitrex_logo.png (RGBA, cut to the logo)

What the badge covered is drawn again: the panel's corner (its black and its grey edge, from the panel's measured shape) and
the left leaf's tip and the raspberry's edge (the right half's, mirrored - only its red, green and dark outline)."""
import os
import numpy as np
from PIL import Image, ImageDraw, ImageFilter
from scipy.ndimage import binary_dilation, binary_closing, binary_fill_holes, label

here = os.path.dirname(os.path.abspath(__file__)); root = os.path.dirname(here)
src = Image.open(os.path.join(root, 'icons', 'pitrex_logo_src.png')).convert('RGB')
a = np.asarray(src).astype(np.float32); H, W = a.shape[:2]
mx = a.max(2); mn = a.min(2); sat = (mx - mn) / np.maximum(mx, 1)

# the panel: its black inside, the grey edge round it
dark = mx < 30
ys = np.where(dark[:, W * 2 // 3])[0]; iy0, iy1 = int(ys.min()), int(ys.max())
xs = np.where(dark[H * 2 // 3])[0]; ix0, ix1 = int(xs.min()), int(xs.max())
grey = (np.abs(mx - 104) < 14) & (sat < 0.08)
gy = np.where(grey[:iy0, W * 2 // 3])[0]; bw = int(iy0 - gy.min()) if len(gy) else 18          # the edge's width
ox0, oy0, ox1, oy1 = ix0 - bw, iy0 - bw, ix1 + bw, iy1 + bw
# the inside's corner radius: how far in the black starts along the top row's end, at the right (the badge is at the left)
r_in = 0
for dy in range(1, 120):
    row = np.where(dark[iy0 + dy, W // 2:])[0]
    if len(row) and W // 2 + row.max() >= ix1 - 1:
        r_in = dy; break
r_out = r_in + bw
print('panel inside', (ix0, iy0, ix1, iy1), 'edge', bw, 'radius', r_in)

def rr(box, r):
    m = Image.new('L', (W, H), 0); ImageDraw.Draw(m).rounded_rectangle(box, r, fill=255); return np.asarray(m) > 127
outer, inner = rr((ox0, oy0, ox1, oy1), r_out), rr((ix0, iy0, ix1, iy1), r_in)

# the badge: a dark grey disc at the top left
reg = np.zeros((H, W), bool); reg[:int(0.50 * H), :int(0.20 * W)] = True
disc = reg & (mx > 14) & (mx < 62) & (sat < 0.25)
lab, n = label(disc); big = 1 + int(np.argmax(np.bincount(lab.ravel())[1:])); disc = lab == big
yy, xx = np.where(disc); cx, cy = xx.mean(), yy.mean(); rad = float(np.percentile(np.hypot(xx - cx, yy - cy), 99.5)) + 6
Y, X = np.mgrid[0:H, 0:W]; badge = np.hypot(X - cx, Y - cy) <= rad
print('badge centre %.0f, %.0f radius %.0f' % (cx, cy, rad))

# the raspberry: its red and green, with the dark outline round them
col = (sat > 0.38) & (mx > 80) & ~badge
leaf = col & (a[:, :, 1] > a[:, :, 0] * 1.15)
axis = float(np.where(leaf.any(0))[0].mean())                                   # the two leaves' middle
rasp = binary_fill_holes(binary_closing(col, iterations=3))
rasp_out = binary_dilation(rasp, iterations=9)                                  # (with its black outline)

out = a.copy()
# under the badge: the panel as its shape says, else nothing
out[badge & outer & ~inner] = (104, 102, 103)
out[badge & inner] = (1, 1, 1)
# and the raspberry's mirrored half over it
mxs = np.clip(np.round(2 * axis - X).astype(int), 0, W - 1)
mir_col = rasp_out[Y, mxs] & (X < axis)
take = badge & mir_col
out[take] = a[Y[take], mxs[take]]
# the maker's name: the white letters in the panel's upper right part, above the big ones
name = np.zeros((H, W), bool); name[int(0.33 * H):int(0.48 * H), int(0.60 * W):int(0.93 * W)] = True
white = (mx > 150) & (sat < 0.15) & name
lab, n = label(binary_dilation(white, iterations=6))
for i in range(1, n + 1):
    m = lab == i; ys_, xs_ = np.where(m)
    if ys_.max() < int(0.47 * H) and (ys_.max() - ys_.min()) < 0.16 * H:          # (a low line of letters: not the big ones' tops)
        out[m] = (1, 1, 1)

obj = outer | (rasp_out & ~badge) | take
obj = binary_fill_holes(obj)
al = Image.fromarray((obj * 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(1.0))
im = Image.fromarray(np.clip(out, 0, 255).astype(np.uint8)).convert('RGBA'); im.putalpha(al)
bb = Image.fromarray((obj * 255).astype(np.uint8)).getbbox()
im = im.crop((max(0, bb[0] - 4), max(0, bb[1] - 4), min(W, bb[2] + 4), min(H, bb[3] + 4)))
im.save(os.path.join(root, 'icons', 'pitrex_logo.png'), optimize=True)
print('wrote icons/pitrex_logo.png', im.size)
