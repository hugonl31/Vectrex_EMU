"""make_art_templates.py - the README's templates for the games list's own pictures (README: "Your own pictures for the
games list"): docs/images/label_template.png (1000 x 545, a cartridge label, 1.835:1), docs/images/box_template.png
(800 x 1024, a box front, 25:32) - the edge, the safe area, the sizes - and docs/images/label_on_cart.png, the label
template on the cartridge the emulator puts it on (libretro/genart.c through src/frontend/native's vxbuild library:
build_win.bat first). Our own drawing; the font Archivo Black (SIL OFL 1.1, libretro/fonts).
    python tools/make_art_templates.py
Copyright (c) 2026 Hugo Bokhorst. Part of Vectrex_EMU; see LICENSE.md."""
import os, sys
from PIL import Image, ImageDraw, ImageFont

R = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(R, 'docs', 'images')
FONT = os.path.join(R, 'libretro', 'fonts', 'ArchivoBlack-Regular.ttf')
PAPER, GRID, EDGE, SAFE, INK, SOFT = (236, 238, 240), (214, 219, 225), (40, 44, 52), (230, 70, 40), (30, 34, 40), (110, 118, 130)


def f(px):
    return ImageFont.truetype(FONT, px)


def dashed(d, box, col, dash=14, gap=9, w=3):
    x0, y0, x1, y1 = box
    for a, b, horiz, c in ((x0, x1, True, y0), (x0, x1, True, y1), (y0, y1, False, x0), (y0, y1, False, x1)):
        p = a
        while p < b:
            q = min(p + dash, b)
            d.line((p, c, q, c) if horiz else (c, p, c, q), fill=col, width=w); p = q + gap


def centred(d, cx, y, s, font, fill):
    l, t, r, b = d.textbbox((0, 0), s, font=font); d.text((cx - (r - l) / 2 - l, y - t), s, font=font, fill=fill)


def template(w, h, safe, title, lines, name):
    im = Image.new('RGB', (w, h), PAPER); d = ImageDraw.Draw(im)
    for x in range(0, w, 25): d.line((x, 0, x, h), fill=GRID, width=1)
    for y in range(0, h, 25): d.line((0, y, w, y), fill=GRID, width=1)
    d.line((w / 2, 0, w / 2, h), fill=GRID, width=3); d.line((0, h / 2, w, h / 2), fill=GRID, width=3)
    d.rectangle((0, 0, w - 1, h - 1), outline=EDGE, width=4)
    dashed(d, (safe, safe, w - 1 - safe, h - 1 - safe), SAFE)
    fs = f(max(18, w // 26)); fb = f(max(24, w // 15))
    y = h / 2 - (fb.size * 1.4 + len(lines) * fs.size * 1.45) / 2
    centred(d, w / 2, y, title, fb, INK); y += fb.size * 1.4
    for s in lines:
        centred(d, w / 2, y, s, fs, SOFT); y += fs.size * 1.45
    fl = f(max(14, w // 50))
    d.text((safe + 8, safe + 6), 'SAFE AREA (%d PX IN)' % safe, font=fl, fill=SAFE)
    im.save(os.path.join(OUT, name), optimize=True); print(name, im.size)
    return im


os.makedirs(OUT, exist_ok=True)
template(1000, 545, 30, 'YOUR LABEL', ['1000 x 545 PX  -  1.835 : 1', 'THE LABEL ONLY, NOT THE CARTRIDGE', 'KEEP TEXT INSIDE THE RED LINE'], 'label_template.png')
template(800, 1024, 24, 'YOUR BOX FRONT', ['800 x 1024 PX  -  25 : 32', 'THE FRONT OF THE BOX ONLY', 'KEEP TEXT INSIDE THE RED LINE'], 'box_template.png')
# the label template on the emulator's cartridge
sys.path.insert(0, os.path.join(R, 'src', 'frontend'))
import genart                                                   # noqa: E402
r = genart.label(os.path.join(OUT, 'label_template.png'))
if r:
    c = Image.frombytes('RGBA', (r[0], r[1]), r[2]); c = c.resize((420, round(420 * c.height / c.width)), Image.LANCZOS)
    c.save(os.path.join(OUT, 'label_on_cart.png'), optimize=True); print('label_on_cart.png', c.size)
else:
    print('label_on_cart.png: not made (no native library - src/frontend/native/build_win.bat)')
