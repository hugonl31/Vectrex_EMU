"""make_pitrex_box.py - the PiTrex's box for the game library: the owner's box artwork on the drawn boxes' own paper.

The owner's picture (icons/pitrex_box_src.png) is a narrower format than the pack's boxes; widening it showed a seam. So
(the owner, 2026-11-22): its dinosaur with the frame is cut out and put, under the cleaned PiTrex logo, on the drawn
boxes' paper with its furniture and foot (libretro/genart.c vx_genart_boxpaper_w: the silver, the real box's grid, his
small print and GCE), then the drawn boxes' wear for "PiTrex" (vx_genart_wear). Needs src/frontend/native's library.
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
r = genart.boxpaper(800, well=False)
if not r: raise SystemExit('genart: no native library (src/frontend/native: build_win.bat)')
box = Image.frombytes('RGBA', (r[0], r[1]), r[2]).convert('RGB')
TOP, BOT = 132, 872                                                  # (the room between the furniture and the small print)
logo = Image.open(I('pitrex_logo.png')).convert('RGBA')              # the title: the cleaned PiTrex logo
lw = 400; lg = logo.resize((lw, int(round(lw * logo.size[1] / logo.size[0]))), Image.LANCZOS)
s = 0.72                                                             # (the paper's own scale would be 18 / 24.1 = 0.747: a little room round it)
s = min(s, (BOT - TOP - 10 - lg.size[1] - 8) / art.size[1])         # (all of it between the furniture and the legal line)
aw, ah = int(round(art.size[0] * s)), int(round(art.size[1] * s)); ar = art.resize((aw, ah), Image.LANCZOS)
gap = 10; tot = lg.size[1] + gap + ah; y0 = TOP + (BOT - TOP - tot) // 2
box.paste(lg, ((800 - lw) // 2, y0), lg)
x0 = int(round(400 - (W / 2.0 - bb[0]) * s)); y1 = y0 + lg.size[1] + gap
box.paste(ar, (x0, y1), ar)
rgba = box.convert('RGBA'); rgba = Image.frombytes('RGBA', rgba.size, genart.wear(rgba.width, rgba.height, rgba.tobytes(), 'PiTrex'))   # (its wear, as every drawn box's)
rgba.save(I('pitrex_box_full.png')); rgba.resize((840, 1075), Image.LANCZOS).save(I('pitrex_box.png'), optimize=True)   # (840: the tiles' width)
print('artwork', bb, '-> %d x %d at %d, %d; the logo at y %d' % (aw, ah, x0, y1, y0))
