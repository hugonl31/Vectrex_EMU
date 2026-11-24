"""make_pitrex_box.py - the PiTrex's box for the game library: the owner's box artwork on the drawn boxes' own paper.

The owner's picture (icons/pitrex_box_src.png) is a narrower format than the pack's boxes; widening it showed a seam. So
(the owner, 2026-11-22): its dinosaur with the frame is cut out and put, under his PiTrex title logo
(icons/pitrex_title_logo.png, 2026-11-23), on the drawn
boxes' paper with its furniture and foot (libretro/genart.c vx_genart_boxpaper_w: the silver, the real box's grid, his
small print and GCE), then gloss c and the drawn boxes' wear for "PiTrex" (vx_genart_gloss, vx_genart_wear). Needs
src/frontend/native's library.
-> icons/pitrex_box_art.png (the cut-out), icons/pitrex_box_full.png (800 x 1024), icons/pitrex_box.png (840 x 1075)"""
import os
import numpy as np
from PIL import Image, ImageFilter
from scipy.ndimage import binary_opening, binary_dilation, binary_fill_holes, label
here = os.path.dirname(os.path.abspath(__file__)); root = os.path.dirname(here); I = lambda f: os.path.join(root, 'icons', f)
disk = lambda r: (np.hypot(*np.mgrid[-r:r + 1, -r:r + 1]) <= r)
src = Image.open(I('pitrex_box_src.png')).convert('RGB'); a = np.asarray(src).astype(np.float32); H, W = a.shape[:2]
top = a[6:24].reshape(-1, 3); lum = top.mean(1); bg = np.median(top[lum < np.percentile(lum, 60)], 0)
nonbg = np.abs(a - bg).max(2) > 26                                   # not the paper's squares
core = binary_opening(nonbg, structure=disk(4))                      # (the grid's thin lines go; what is thicker stays)
m = binary_fill_holes(binary_dilation(core, structure=disk(3)) & nonbg)
m[:int(0.135 * H)] = False; m[int(0.855 * H):] = False               # (the header and the small print: the other box's own)
lab, n = label(m); sz = np.bincount(lab.ravel()); keep = lab == int(np.argmax(sz[1:]) + 1)   # (the dinosaur with its frame: the largest; the
                                                                     #  picture's own title is not used - the cleaned logo instead, the owner)
al = Image.fromarray((keep * 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(0.7))
art = src.convert('RGBA'); art.putalpha(al)
ys, xs = np.where(keep); bb = (int(xs.min()), int(ys.min()), int(xs.max()) + 1, int(ys.max()) + 1)
art = art.crop(bb); art.save(I('pitrex_box_art.png'))
import sys; sys.path.insert(0, os.path.join(root, 'src', 'frontend'))
import genart                                                        # (the drawn boxes' own paper, furniture, foot and wear: libretro/genart.c)
r = genart.boxpaper(800, well=True)                                  # (with the drawn boxes' well: no grid round the picture)
if not r: raise SystemExit('genart: no native library (src/frontend/native: build_win.bat)')
box = Image.frombytes('RGBA', (r[0], r[1]), r[2]).convert('RGB')
# the picture's rectangle exactly the drawn boxes' frame (genart.c GA_FRM: the same on every box), its red edge on the
# frame's edge; the dinosaur breaking out of it as on his artwork (the cut-out's red rectangle: its outer edges)
S14 = 800 / 2022.0
FRM = (400 + (455 - 997) * S14 + 20.5 * S14, 780 * S14 - 6, 400 + (1498 - 997) * S14 + 20.5 * S14, 2150 * S14 - 6)
A = np.asarray(art).astype(int); red = (A[..., 0] > 170) & (A[..., 1] < 80) & (A[..., 2] < 80) & (A[..., 3] > 200)
cols = np.nonzero(red.sum(0) > 3)[0]; rows = np.nonzero(red.sum(1) > 3)[0]   # (the dinosaur hides parts of the edge)
rx0, rx1, ry0, ry1 = cols.min(), cols.max() + 1, rows.min(), rows.max() + 1        # (the red rectangle's outer edges)
sx, sy = (FRM[2] - FRM[0]) / (rx1 - rx0), (FRM[3] - FRM[1]) / (ry1 - ry0)
ar = art.resize((int(round(art.width * sx)), int(round(art.height * sy))), Image.LANCZOS)
x0, y1 = int(round(FRM[0] - rx0 * sx)), int(round(FRM[1] - ry0 * sy))
box.paste(ar, (x0, y1), ar); aw, ah = ar.size
# the title: his logo under the furniture, centred, its letters + outline 172.5 px high, their top at y 128 (the owner,
# 2026-11-24: B1v19 - 15 % over the GCE titles' weight; it reaches over the picture's top, in front of it)
logo = Image.open(I('pitrex_title_logo.png')).convert('RGBA')        # (the owner's PiTrex logo, 2026-11-23)
LA = np.asarray(logo)[..., 3]; ly = np.nonzero((LA > 200).any(1))[0]; oh = ly.max() + 1 - ly.min()
TH = float(os.environ.get('PX_TITLE_H', '172.5')); k = TH / oh
lg = logo.resize((int(round(logo.width * k)), int(round(logo.height * k))), Image.LANCZOS)
WELL_TOP = 745 * S14 - 6; HEAD_BOT = 122.0; cy = (HEAD_BOT + WELL_TOP) / 2
y0 = int(round(cy - (ly.min() + ly.max() + 1) / 2 * k))
y0 = int(round(float(os.environ.get('PX_TITLE_TOP', '128')) - ly.min() * k))   # (its letters' top)
box.paste(lg, ((800 - lg.width) // 2, y0), lg)
rgba = box.convert('RGBA'); b = genart.gloss(rgba.width, rgba.height, rgba.tobytes(), True)   # (gloss c, then its wear, as every drawn box's)
rgba = Image.frombytes('RGBA', rgba.size, genart.wear(rgba.width, rgba.height, b, 'PiTrex'))
OUT = os.environ.get('PX_OUT')                                       # (a preview: the box elsewhere, the icons untouched)
if OUT: rgba.save(OUT)
else: rgba.save(I('pitrex_box_full.png')); rgba.resize((840, 1075), Image.LANCZOS).save(I('pitrex_box.png'), optimize=True)   # (840: the tiles' width)
print('artwork: its red rectangle %d..%d x %d..%d -> the frame %.1f..%.1f x %.1f..%.1f (x %.4f, %.4f); the logo %d x %d at y %d' % (rx0, rx1, ry0, ry1, FRM[0], FRM[2], FRM[1], FRM[3], sx, sy, lg.width, lg.height, y0))
