"""make_hud_font.py - the desktop's stroke font (vecrender.py HUD_FONT: 4 x 6 grid, y down) and its load button
(LOAD_ICON: the slot, the arrow, the cartridge) as C tables for the core's ROM browser -> hud_font.h. Run when
vecrender.py's font changes. Copyright (c) 2026 Hugo Bokhorst. Part of Vectrex_EMU; see LICENSE.md."""
import os
here = os.path.dirname(os.path.abspath(__file__))
src = open(os.path.join(here, '..', 'src', 'frontend', 'vecrender.py'), encoding='utf-8').read()
a = src.index('_A, _B, _C, _D = (0, 0, 4, 0)')
b = src.index('# the volume knob and the RESET button')
c = src.index('LOGO_ICON = [')
d = src.index('METER_K = ')
g = {}
exec(src[a:b], g)
blk = src[c:d]
keep = []
for name in ('LOGO_ICON', '_TICKS', 'CART_ICON', 'LOAD_ICON', 'JOY_ICON', 'VECLINK_ICON', 'PORT2_ICON', 'VOICE_ICON', 'IMAGER_ICON', 'LPEN_ICON'):   # (the deck's and the load button's)
    i = blk.index(name + ' = ')
    j = len(blk)
    for other in ('JOY_ICON', 'VECLINK_ICON', 'PORT2_ICON', 'VOICE_ICON', 'IMAGER_ICON', 'LPEN_ICON', 'CART_ICON', 'LOAD_ICON', '_TICKS', 'LOGO_ICON'):
        k = blk.find('\n' + other + ' = ', i + 1)
        if k > 0: j = min(j, k)
    keep.append(blk[i:j])
exec('\n'.join(keep), g)
font, icon = g['HUD_FONT'], g['LOAD_ICON']
out = ['/* hud_font.h - made by make_hud_font.py from vecrender.py: the stroke font (HUD_FONT) and LOAD_ICON. Do not edit. */']
segs, idx = [], [[0, 0] for _ in range(128)]
for ch in sorted(font):
    o = ord(ch)
    if o >= 128: continue
    idx[o] = [len(segs), len(font[ch])]
    segs += font[ch]
out.append('static const float HUD_SEG[%d][4] = {' % max(1, len(segs)))
F = lambda v: repr(float(v)) + 'f'
out.append(',\n'.join('\t{ %s, %s, %s, %s }' % tuple(F(x) for x in s) for s in segs) + ' };')
out.append('static const unsigned short HUD_IDX[128][2] = {')
out.append(',\n'.join('\t{ %d, %d }' % tuple(x) for x in idx) + ' };')
pts, lines = [], []
for line in icon:
    lines.append((len(pts), len(line))); pts += line
out.append('static const float LOAD_PTS[%d][2] = {' % len(pts))
out.append(',\n'.join('\t{ %s, %s }' % (F(u), F(v)) for u, v in pts) + ' };')
out.append('static const unsigned short LOAD_LINES[%d][2] = {' % len(lines))
out.append(',\n'.join('\t{ %d, %d }' % x for x in lines) + ' };')
# the control deck's pictograms (vecrender.py _port2, _cart): each a list of polylines, 100 x 100 grid, y down
for name in ('CART_ICON', 'JOY_ICON', 'PORT2_ICON', 'VOICE_ICON', 'IMAGER_ICON', 'LPEN_ICON', 'VECLINK_ICON'):
    pts_, lines_ = [], []
    for line in g[name]:
        lines_.append((len(pts_), len(line))); pts_ += line
    out.append('static const float %s_P[%d][2] = {' % (name, len(pts_)))
    out.append(',\n'.join('\t{ %s, %s }' % (F(u), F(v)) for u, v in pts_) + ' };')
    out.append('static const unsigned short %s_L[%d][2] = {' % (name, len(lines_)))
    out.append(',\n'.join('\t{ %d, %d }' % x for x in lines_) + ' };')
open(os.path.join(here, 'hud_font.h'), 'w', newline='\n').write('\n'.join(out) + '\n')
print('hud_font.h: %d glyphs, %d strokes, load icon %d lines' % (sum(1 for x in idx if x[1]), len(segs), len(lines)))
