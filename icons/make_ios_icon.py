"""make_ios_icon.py - the iOS app's icon (1024 x 1024, no transparency - App Store Connect takes none): the desktop's
icon (make_icon.py, option A - the grey screen) with the screen a true 3:4, on a slate grey ground shading darker
downwards, a soft shadow under the console, drawn a little smaller so iOS's rounded corners cut nothing.
  python make_ios_icon.py  ->  ios_icon_1024.png
Copyright (c) 2026 Hugo Bokhorst. Part of Vectrex_EMU; see LICENSE.md."""
import os
from PIL import Image, ImageDraw, ImageFilter

S = 1024


def poly(d, p, fill):
    d.polygon(p, fill=fill)


def console():
    """make_icon.py's option A, the screen 3:4 (its glass narrower and shorter, centred where it was)"""
    im = Image.new('RGBA', (S, S), (0, 0, 0, 0)); d = ImageDraw.Draw(im)
    fx0, fy0, fx1, fy1 = 200, 130, 660, 770; dx, dy = 150, -90
    fb = fy1 + 150
    poly(d, [(fx1, fy0), (fx1 + dx, fy0 + dy), (fx1 + dx, fb + dy), (fx1, fb)], (38, 38, 41, 255))
    poly(d, [(fx0, fy0), (fx0 + dx, fy0 + dy), (fx1 + dx, fy0 + dy), (fx1, fy0)], (66, 66, 70, 255))
    d.rounded_rectangle((fx0, fy0, fx1, fb), 18, fill=(50, 50, 54, 255))
    d.rectangle((fx0 + 30, fy1 + 10, fx1 - 30, fb), fill=(20, 20, 22, 255))
    d.rectangle((fx0 + 30, fy1 + 10, fx1 - 30, fy1 + 26), fill=(12, 12, 13, 255))
    for i in range(6):
        y = fy1 + 45 + i * 17; d.line([(fx0 + 150, y), (fx0 + 330, y)], fill=(42, 42, 45, 255), width=7)
    d.line([(fx0 + 20, fy0 + 30), (fx1 - 20, fy0 + 30)], fill=(110, 110, 115, 255), width=6)
    ox0, oy0, ox1, oy1 = fx0 + 35, fy0 + 70, fx1 - 35, fy1 - 30
    # the glass: 3:4 (width : height), as the Vectrex's - the desktop's was 280 x 415, taller; centred where it was
    cx, cy, gh = (ox0 + 70 + ox1 - 40) / 2, (oy0 + 55 + oy1 - 70) / 2, 400
    gw = gh * 3 / 4
    gx0, gy0, gx1, gy1 = cx - gw / 2, cy - gh / 2, cx + gw / 2, cy + gh / 2
    poly(d, [(ox0, oy0), (ox1, oy0), (gx1, gy0), (gx0, gy0)], (20, 20, 22, 255))
    poly(d, [(ox0, oy0), (gx0, gy0), (gx0, gy1), (ox0, oy1)], (34, 34, 37, 255))
    poly(d, [(ox1, oy0), (ox1, oy1), (gx1, gy1), (gx1, gy0)], (28, 28, 30, 255))
    poly(d, [(ox0, oy1), (gx0, gy1), (gx1, gy1), (ox1, oy1)], (58, 58, 62, 255))
    d.rounded_rectangle((gx0 - 6, gy0 - 6, gx1 + 6, gy1 + 6), 50, fill=(12, 12, 13, 255))
    d.rounded_rectangle((gx0, gy0, gx1, gy1), 46, fill=(150, 152, 148, 255))
    cx0, cy0, cx1, cy1 = 60, 860, 470, 960; cdx, cdy = 35, -35
    poly(d, [(cx0, cy0), (cx0 + cdx, cy0 + cdy), (cx1 + cdx, cy0 + cdy), (cx1, cy0)], (70, 70, 74, 255))
    poly(d, [(cx1, cy0), (cx1 + cdx, cy0 + cdy), (cx1 + cdx, cy1 + cdy), (cx1, cy1)], (30, 30, 32, 255))
    d.rectangle((cx0, cy0, cx1, cy1), fill=(44, 44, 48, 255))
    d.ellipse((cx0 + 55, cy0 - 40, cx0 + 125, cy0 + 0), fill=(20, 20, 22, 255))
    d.line([(cx0 + 90, cy0 - 20), (cx0 + 90, cy0 - 120)], fill=(25, 25, 27, 255), width=22)
    d.ellipse((cx0 + 70, cy0 - 150, cx0 + 110, cy0 - 105), fill=(225, 205, 180, 255))
    for i in range(4):
        bx = cx0 + 160 + i * 62; d.ellipse((bx, cy0 - 24, bx + 46, cy0 + 0), fill=(18, 18, 20, 255))
    d.line([(cx0 + 20, cy0 + 45), (cx1 - 20, cy0 + 45)], fill=(95, 95, 100, 255), width=5)
    return im


def icon():
    # the ground: slate grey, lighter at the top
    g = Image.new('RGB', (1, S))
    for y in range(S):
        t = y / (S - 1); g.putpixel((0, y), tuple(int(a + (b - a) * t) for a, b in zip((76, 86, 102), (40, 46, 57))))
    out = g.resize((S, S)).convert('RGBA')
    c = console()
    k = 0.80; c = c.resize((int(S * k), int(S * k)), Image.LANCZOS)   # (smaller: inside iOS's rounded corners)
    ox, oy = (S - c.width) // 2 + 14, (S - c.height) // 2 + 6
    # its shadow: the console's shape, dark and soft, a little down and right
    sh = Image.new('RGBA', (S, S), (0, 0, 0, 0)); a = c.split()[3].point(lambda v: v * 0.55)
    sh.paste((0, 0, 0, 255), (ox + 16, oy + 22), a); sh = sh.filter(ImageFilter.GaussianBlur(22))
    out = Image.alpha_composite(out, sh)
    out.alpha_composite(c, (ox, oy))
    return out.convert('RGB')


if __name__ == '__main__':
    p = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'ios_icon_1024.png')
    icon().save(p); print(p)
