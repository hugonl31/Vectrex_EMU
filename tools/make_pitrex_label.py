"""make_pitrex_label.py - the PiTrex cartridge label, composed as the owner's label project does it
(G:/AI/Vectrex_Artwork/print 4x/reconstructed2/compose_label.py, README there):

  the drawn labels' silver paper (tools/make_genart_h.py silver_paper: the exact silver grid in white, grain, light, the
  highlight streak; the legal box plain) under
  + the artwork (icons/pitrex_logo.png) in the 3120 x 1700 label frame, centred between the bottom of ARCADE SYSTEM
    CARTRIDGE (y 495) and the legal box's top
  + the shared furniture v6 (the Vectrex logo with its TM, ARCADE SYSTEM CARTRIDGE) - no GCE: a homebrew label
  + the legal box (the grid-free rectangle of the GCE labels, as Last Human's) with its two lines as outlines

    python tools/make_pitrex_label.py ["line 1" "line 2"]
-> icons/pitrex_label.svg, icons/pitrex_label_4x.png (3120 x 1700), icons/pitrex_label.png (780 x 425)"""
import sys, os, io, re, base64
import numpy as np
from PIL import Image
import resvg_py
from fontTools.ttLib import TTFont
from fontTools.pens.svgPathPen import SVGPathPen
from fontTools.pens.transformPen import TransformPen

here = os.path.dirname(os.path.abspath(__file__)); root = os.path.dirname(here)
ART = 'G:/AI/Vectrex_Artwork/print 4x'
W, H = 3120, 1700
LINE1 = sys.argv[1] if len(sys.argv) > 1 else 'Raspberry Pi cartridge for the Vectrex.'
LINE2 = sys.argv[2] if len(sys.argv) > 2 else 'FOR USE WITH THE VECTREX ARCADE SYSTEM'
ASC_BOTTOM = 495.0
BOX = (376.62, 1435.72, 2269.94, 184.28, '#aeaeae')                  # (x, y, w, h, tone: Last Human's, the GCE labels')
CAP = 47.0                                                           # the legal lines' capital height, px (Last Human's)
BASE1, BASE2 = 1507.0, 1583.0                                        # their baselines

def text_path(s, font_file, cap, cx, base):
    f = TTFont(font_file); gs = f.getGlyphSet(); cmap = f.getBestCmap(); upm = f['head'].unitsPerEm
    caph = getattr(f['OS/2'], 'sCapHeight', 0) or 0.716 * upm; k = cap / caph
    adv = sum(f['hmtx'][cmap[ord(c)]][0] for c in s) * k; x = cx - adv / 2; d = []
    for c in s:
        g = cmap[ord(c)]; pen = SVGPathPen(gs); gs[g].draw(TransformPen(pen, (k, 0, 0, -k, x, base)))
        d.append(pen.getCommands()); x += f['hmtx'][g][0] * k
    return ' '.join(p for p in d if p)

logo = Image.open(os.path.join(root, 'icons', 'pitrex_logo.png')).convert('RGBA')
lw = 1900; lh = int(round(lw * logo.size[1] / logo.size[0])); lg = logo.resize((lw, lh), Image.LANCZOS)
frame = Image.new('RGBA', (W, H), (0, 0, 0, 0))
A = np.asarray(lg); ys = np.nonzero((A[..., 3] > 200).any(1))[0]; at, ab = ys.min(), ys.max()
y = int(round((ASC_BOTTOM + BOX[1]) / 2 - (at + ab) / 2))
frame.paste(lg, ((W - lw) // 2, y), lg)
buf = io.BytesIO(); frame.save(buf, 'PNG'); b64 = base64.b64encode(buf.getvalue()).decode()

furn = open(ART + '/reconstructed2/shared-svg/shared furniture v6.svg', encoding='utf-8').read()
fbody = furn[furn.index('<g id="vectrex-logo">'):furn.index('</svg>')]
fbody = re.sub(r'<g id="gce-(?:symbol|tm)"[^>]*>.*?</g>', '', fbody, flags=re.S)       # (homebrew: no GCE)
assert 'gce-' not in fbody
cx = BOX[0] + BOX[2] / 2
legal = ('<path d="%s" fill="black"/>' % text_path(LINE1, 'C:/Windows/Fonts/arialbd.ttf', CAP, cx, BASE1)
         + '<path d="%s" fill="black"/>' % text_path(LINE2, 'C:/Windows/Fonts/arial.ttf', CAP, cx, BASE2))
svg = ('<svg xmlns="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink" width="66.04mm" height="35.983333333mm" viewBox="0 0 %d %d">\n' % (W, H)
       + '\n<g id="artwork"><image x="0" y="0" width="%d" height="%d" xlink:href="data:image/png;base64,%s"/></g>\n' % (W, H, b64)
       + fbody + '\n<g id="legal">' + legal + '</g>\n</svg>')
open(os.path.join(root, 'icons', 'pitrex_label.svg'), 'w', encoding='utf-8').write(svg)
png = bytes(resvg_py.svg_to_bytes(svg_string=re.sub(r'width="[^"]*mm" height="[^"]*mm"', 'width="%d" height="%d"' % (W, H), svg, count=1)))
import sys; sys.path.insert(0, here); import make_genart_h as G   # (the paper: the drawn labels' silver, its grid white, the legal box plain)
im = G.silver_paper(BOX).convert('RGBA'); im.alpha_composite(Image.open(io.BytesIO(png)).convert('RGBA')); im = im.convert('RGB')
im.save(os.path.join(root, 'icons', 'pitrex_label_4x.png'), optimize=True)
im.resize((780, 425), Image.LANCZOS).save(os.path.join(root, 'icons', 'pitrex_label.png'), optimize=True)
print('artwork rows %d..%d of the logo at y %d; wrote icons/pitrex_label.svg, _4x.png, .png' % (at, ab, y))
