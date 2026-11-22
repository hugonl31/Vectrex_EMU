"""clean_pitrex_logo.py - the owner's cut-out of the PiTrex logo (icons/pitrex_logo_owner.png) with its edges cleaned:
the small white cuts along the outline and the badge's leftover arc gone -> icons/pitrex_logo.png

The panel's shape is measured (its black inside, the grey edge round it); in the edge's ring every pixel that is not the
raspberry's becomes the edge's grey, inside it at the top left what is not the raspberry's becomes black; the outline:
the panel's own shape and the raspberry with its dark line, whitish fringe pixels outside them cut away."""
import os
import numpy as np
from PIL import Image, ImageDraw, ImageFilter
from scipy.ndimage import binary_dilation, binary_closing, binary_fill_holes, binary_erosion, label
here = os.path.dirname(os.path.abspath(__file__)); root = os.path.dirname(here)
im = Image.open(os.path.join(root, 'icons', 'pitrex_logo_owner.png')).convert('RGBA')
a = np.asarray(im).astype(np.float32); H, W = a.shape[:2]
rgb, al = a[:, :, :3].copy(), a[:, :, 3]
mx = rgb.max(2); mn = rgb.min(2); sat = (mx - mn) / np.maximum(mx, 1)
dark = (mx < 30) & (al > 128)
ys = np.where(dark[:, W * 2 // 3])[0]; iy0, iy1 = int(ys.min()), int(ys.max())
xs = np.where(dark[H * 2 // 3])[0]; ix0, ix1 = int(xs.min()), int(xs.max())
GREY = np.median(rgb[(np.abs(mx - 104) < 16) & (sat < 0.08) & (al > 200)], 0)
c_ = W * 2 // 3; up = [y for y in range(max(0, iy0 - 60), iy0) if al[y, c_] > 128]          # (the edge above the black: down to the outline)
bw = iy0 - min(up) if up else 20
r_in = 45
for dy in range(1, 120):
    row = np.where(dark[iy0 + dy, W // 2:])[0]
    if len(row) and W // 2 + row.max() >= ix1 - 1:
        r_in = dy; break
print('inside', (ix0, iy0, ix1, iy1), 'edge', bw, 'radius', r_in, 'grey', GREY.astype(int))
def rr(box, r):
    m = Image.new('L', (W, H), 0); ImageDraw.Draw(m).rounded_rectangle(box, r, fill=255); return np.asarray(m) > 127
outer, inner = rr((ix0 - bw, iy0 - bw, ix1 + bw, iy1 + bw), r_in + bw), rr((ix0, iy0, ix1, iy1), r_in)
# the raspberry: red and green, and the dark line round them
colr = (sat > 0.36) & (mx > 80) & (al > 128)
rasp = binary_fill_holes(binary_closing(colr, iterations=3))
lab, n = label(rasp); sz = np.bincount(lab.ravel()); rasp = np.isin(lab, np.where(sz > 400)[0]) & rasp
line = binary_dilation(rasp, iterations=10) & (mx < 70) & (al > 128)            # its black outline (where it is drawn)
rasp_all = binary_fill_holes(rasp | line)
ring = outer & ~inner
fix = ring & ~binary_dilation(rasp_all, iterations=1)
rgb[fix] = GREY                                                                   # the edge: whole, no cuts
wcols = np.where(((mx > 200) & (sat < 0.1) & inner & (al > 128))[int(0.45 * H):int(0.75 * H)].any(0))[0]
xP = int(wcols.min()) if len(wcols) else int(0.12 * W)                              # (the letters begin here: the P's white rim)
tl = np.zeros((H, W), bool); tl[:int(0.52 * H), :max(0, xP - 3)] = True          # (where the badge was, left of the letters - the P's grey
                                                                                  #  body was painted black with it, the owner 2026-11-22)
stray = inner & tl & ~binary_dilation(rasp_all, iterations=2) & (mx > 6) & (mx < 150) & ~binary_dilation(mx > 200, iterations=4)   # (not the letters' white and its soft rim)
rgb[stray] = (1, 1, 1)
obj = outer | rasp_all
obj = binary_fill_holes(binary_closing(obj, iterations=2))
alpha = Image.fromarray((obj * 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(0.9))
# whitish fringe just inside the outline, outside the panel: the dark line's colour instead
edge_band = obj & ~binary_erosion(obj, iterations=4)
fr = edge_band & ~outer & (mx > 140) & (sat < 0.30)
rgb[fr] = (4, 4, 4)
fr2 = edge_band & outer & ~inner & ((mx > 150) | (al < 200))
rgb[fr2] = GREY
out = Image.fromarray(np.clip(rgb, 0, 255).astype(np.uint8)).convert('RGBA'); out.putalpha(alpha)
bb = Image.fromarray((obj * 255).astype(np.uint8)).getbbox(); out = out.crop(bb)
out.save(os.path.join(root, 'icons', 'pitrex_logo.png'), optimize=True); print('wrote icons/pitrex_logo.png', out.size)
