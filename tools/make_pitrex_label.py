"""make_pitrex_label.py - the PiTrex cartridge label, composed as the owner's label project does it
(G:/AI/Vectrex_Artwork/print 4x/reconstructed2/compose_label.py, README there):

  the drawn labels' silver paper (tools/make_genart_h.py silver_paper: the exact silver grid in white, grain, light, the
  highlight streak) under
  + the artwork (icons/pitrex_logo.png) in the 3120 x 1700 label frame, centred between the bottom of ARCADE SYSTEM
    CARTRIDGE (y 495) and the small print's area
  + the shared furniture v6 (the Vectrex logo with its TM, ARCADE SYSTEM CARTRIDGE) - no GCE: a homebrew label
  + the small print as the real labels' (tools/legal_lines.py: TeX Gyre Heros Bold on a plain two-row area)
  then gloss c (genart: the paper's shine over the print)

    python tools/make_pitrex_label.py ["line 1" "line 2"]
-> icons/pitrex_label.svg, icons/pitrex_label_4x.png (3120 x 1700), icons/pitrex_label.png (780 x 425), icons/pitrex_cart.png
(the tile: the label on the cartridge, genart)"""
import sys, os, io, re, base64
import numpy as np
from PIL import Image
import resvg_py

here = os.path.dirname(os.path.abspath(__file__)); root = os.path.dirname(here); sys.path.insert(0, here)
ART = 'G:/AI/Vectrex_Artwork/print 4x'
W, H = 3120, 1700
LINE1 = sys.argv[1] if len(sys.argv) > 1 else 'Raspberry Pi cartridge for the Vectrex.'
LINE2 = sys.argv[2] if len(sys.argv) > 2 else 'FOR USE WITH THE VECTREX ARCADE SYSTEM'
ASC_BOTTOM = 495.0
logo = Image.open(os.path.join(root, 'icons', 'pitrex_logo.png')).convert('RGBA')
lw = 1900; lh = int(round(lw * logo.size[1] / logo.size[0])); lg = logo.resize((lw, lh), Image.LANCZOS)
frame = Image.new('RGBA', (W, H), (0, 0, 0, 0))
A = np.asarray(lg); ys = np.nonzero((A[..., 3] > 200).any(1))[0]; at, ab = ys.min(), ys.max()
y = int(round((ASC_BOTTOM + 1434.9) / 2 - (at + ab) / 2))   # (the small print's plain area: its top)
frame.paste(lg, ((W - lw) // 2, y), lg)
buf = io.BytesIO(); frame.save(buf, 'PNG'); b64 = base64.b64encode(buf.getvalue()).decode()

furn = open(ART + '/reconstructed2/shared-svg/shared furniture v6.svg', encoding='utf-8').read()
fbody = furn[furn.index('<g id="vectrex-logo">'):furn.index('</svg>')]
fbody = re.sub(r'<g id="gce-(?:symbol|tm)"[^>]*>.*?</g>', '', fbody, flags=re.S)       # (homebrew: no GCE)
assert 'gce-' not in fbody
import legal_lines                                                    # (the real labels' small print, tools/legal_lines.py)
legal, box = legal_lines.legal([LINE1, LINE2])
svg = ('<svg xmlns="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink" width="66.04mm" height="35.983333333mm" viewBox="0 0 %d %d">\n' % (W, H)
       + '\n<g id="artwork"><image x="0" y="0" width="%d" height="%d" xlink:href="data:image/png;base64,%s"/></g>\n' % (W, H, b64)
       + fbody + '\n<g id="legal">' + legal + '</g>\n</svg>')
open(os.path.join(root, 'icons', 'pitrex_label.svg'), 'w', encoding='utf-8').write(svg)
png = bytes(resvg_py.svg_to_bytes(svg_string=re.sub(r'width="[^"]*mm" height="[^"]*mm"', 'width="%d" height="%d"' % (W, H), svg, count=1)))
import make_genart_h as G   # (the paper: the drawn labels' silver, its grid white, the legal box plain)
im = G.silver_paper(box).convert('RGBA'); im.alpha_composite(Image.open(io.BytesIO(png)).convert('RGBA'))
sys.path.insert(0, os.path.join(root, 'src', 'frontend')); import genart   # (gloss c: the paper's shine over the print)
im = Image.frombytes('RGBA', im.size, genart.gloss(im.width, im.height, im.tobytes(), False)).convert('RGB')
im.save(os.path.join(root, 'icons', 'pitrex_label_4x.png'), optimize=True)
im.resize((780, 425), Image.LANCZOS).save(os.path.join(root, 'icons', 'pitrex_label.png'), optimize=True)
w, h, b = genart.label(os.path.join(root, 'icons', 'pitrex_label_4x.png'), 840)   # (the cartridge tile: the label on the shell, as every label)
Image.frombytes('RGBA', (w, h), b).save(os.path.join(root, 'icons', 'pitrex_cart.png'), optimize=True)
print('artwork rows %d..%d of the logo at y %d; wrote icons/pitrex_label.svg, _4x.png, .png' % (at, ab, y))
