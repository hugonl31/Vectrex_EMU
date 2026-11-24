"""legal_lines.py - a cartridge label's small print as the real GCE labels have it (the owner, 2026-11-24): TeX Gyre Heros
Bold (art/fonts/texgyreheros-bold.otf, GUST Font License) set with the parameters fitted on his photographs of real
labels (legal_fit: cap height, tracking, word spacing, the "1"'s narrower advance, the (c)'s size and place, the
baselines below the GCE logo), straight on the silver paper, on a plain area exactly two grid rows tall (its top and
bottom grid lines kept, whole cells wide round the text, no lines inside, the cells' own tone).

  one line   layout A ("(c) GCE 1982. All Rights Reserved. MADE IN HONG KONG", or a homebrew's line)
  two lines  layout B (the licensed games: "*Trademark of ... (c) 1980" over "(c) GCE 1982. All Rights Reserved. MADE IN
             HONG KONG.")
In the 3120 x 1700 label frame (print 4x's): the GCE logo's ink y 1283..1400, the grid's lines every 94.936 px across
(from x 82.960) and 96.316 px down (from y 77.985), 8.5 px wide.
    legal(lines) -> (svg_body, box)   box: (x, y, w, h) of the plain area, for make_genart_h.silver_paper(box)"""
import os
from fontTools.ttLib import TTFont
from fontTools.pens.svgPathPen import SVGPathPen
from fontTools.pens.transformPen import TransformPen
from fontTools.pens.boundsPen import BoundsPen

FONT = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'art', 'fonts', 'texgyreheros-bold.otf')
W = 3120.0
GCE_BOT, GCE_H = 1401.0, 118.0                       # the GCE logo's ink: rows 1283..1400
GX0, GPX, GY0, GPY, GLW = 82.96023, 94.93603, 77.98529, 96.31618, 8.5
AREA_Y = (1426.41176 + GLW, 1619.04412)              # the two rows between the grid lines at y 1426.4 and 1619.0
# the fit (scratchpad legal_fit/legal_fit_params.json): cap / GCE logo height, tracking, word space, (c), baseline
# below the GCE logo in GCE logo heights, x stretch, kerning
STYLE = {
    'A':  dict(cap=0.41201, track=0.015865, wsp=-0.047258, one=-0.164, cs=1.025384, craise=0.068116, cpre=0.020871, cpost=0.004912, base=1.202278, xs=1.003441, kern=False),
    'B1': dict(cap=0.40859, track=0.016127, wsp=-0.001007, one=-0.164, cs=1.037959, craise=0.028859, cpre=0.037454, cpost=0.072395, base=0.783756, xs=0.999864, kern=True),
    'B2': dict(cap=0.41205, track=0.013499, wsp=-0.042709, one=-0.164, cs=1.053163, craise=0.030909, cpre=0.007319, cpost=0.030910, base=1.443818, xs=1.006639, kern=False),
}
CAP_EM = 0.729
_F = {}


def _font():
    if not _F:
        f = TTFont(FONT); _F.update(f=f, gs=f.getGlyphSet(), cmap=f.getBestCmap(), upm=f['head'].unitsPerEm, kern=_pairs(f))
    return _F


def _pairs(tt):
    """GPOS pair kerning: (glyph, glyph) -> x advance, and the class-based subtables"""
    kern = {'__class__': []}
    if 'GPOS' not in tt or not tt['GPOS'].table.LookupList: return kern
    for lk in tt['GPOS'].table.LookupList.Lookup:
        for st in lk.SubTable:
            if lk.LookupType == 9: st = st.ExtSubTable
            if getattr(st, 'LookupType', 2) != 2 or not hasattr(st, 'Coverage'): continue
            if st.Format == 1:
                for g1, ps in zip(st.Coverage.glyphs, st.PairSet):
                    for r in ps.PairValueRecord:
                        v = getattr(r.Value1, 'XAdvance', 0) if r.Value1 else 0
                        if v: kern.setdefault((g1, r.SecondGlyph), v)
            elif st.Format == 2:
                kern['__class__'].append((set(st.Coverage.glyphs), st.ClassDef1.classDefs if st.ClassDef1 else {}, st.ClassDef2.classDefs if st.ClassDef2 else {}, st))
    return kern


def _kern(F, a, b):
    if (a, b) in F['kern']: return F['kern'][(a, b)]
    for cov, c1, c2, st in F['kern']['__class__']:
        if a in cov:
            r = st.Class1Record[c1.get(a, 0)].Class2Record[c2.get(b, 0)]; v = getattr(r.Value1, 'XAdvance', 0) if r.Value1 else 0
            if v: return v
    return 0


def line(text, style, cx=W / 2):
    """one line of small print, centred on cx (its ink), on its style's baseline -> (svg path d, ink x0, x1, y0, y1)"""
    F = _font(); P = STYLE[style]; cap = P['cap'] * GCE_H; k = cap / (CAP_EM * F['upm']); base = GCE_BOT + P['base'] * GCE_H
    glyphs = []; x = 0.0
    for i, ch in enumerate(text):
        g = F['cmap'].get(ord(ch))
        if g is None: continue
        if ch == '©': x += P['cpre'] * cap
        glyphs.append((g, x, ch))
        x += F['f']['hmtx'][g][0] * k + P['track'] * cap
        if ch == ' ': x += P['wsp'] * cap
        if ch == '1': x += P['one'] * cap
        if ch == '©': x += P['cpost'] * cap
        if P['kern'] and i + 1 < len(text):
            g2 = F['cmap'].get(ord(text[i + 1]))
            if g2: x += _kern(F, g, g2) * k
    def tr(g, gx, ch, ox):
        if ch == '©':                               # (scaled about its middle, raised)
            aw = F['f']['hmtx'][g][0] * k; s = P['cs']; mx, my = gx + aw / 2, cap / 2
            return (k * s * P['xs'], 0, 0, -k * s, ox + (mx * (1 - s) + gx * s) * P['xs'], base - my * (1 - s) - P['craise'] * cap)
        return (k * P['xs'], 0, 0, -k, ox + gx * P['xs'], base)
    bp = BoundsPen(F['gs'])
    for g, gx, ch in glyphs: F['gs'][g].draw(TransformPen(bp, tr(g, gx, ch, 0)))
    x0, y0, x1, y1 = bp.bounds                         # (font y up: the transform flips it)
    ox = cx - (x0 + x1) / 2; d = []
    for g, gx, ch in glyphs:
        pen = SVGPathPen(F['gs']); F['gs'][g].draw(TransformPen(pen, tr(g, gx, ch, ox))); d.append(pen.getCommands())
    return ' '.join(p for p in d if p), x0 + ox, x1 + ox, y0, y1


PAD_MIN = 0.5                                        # (the least room beside the text, in cells - the owner, 2026-11-24)


def area(x0, x1, cx=W / 2, pad=PAD_MIN):
    """the plain area round ink x0..x1: whole cells (inside the grid lines), the two rows, the same number of cells each
    side of the cell under the label's middle - the text centred in it, as the real GCE labels' room beside it (0.6-1.35
    cells measured on Blitz and Berzerk) but even: at least pad cells, so 0.5..1.5 cells, a cell on average"""
    import math
    c = math.floor((cx - GX0) / GPX); m = pad * GPX; k = 0
    while GX0 + (c - k) * GPX + GLW > x0 - m or GX0 + (c + 1 + k) * GPX < x1 + m: k += 1
    L = GX0 + (c - k) * GPX + GLW; R = GX0 + (c + 1 + k) * GPX
    return (L, AREA_Y[0], R - L, AREA_Y[1] - AREA_Y[0])


def legal(lines, cx=W / 2):
    """the small print (1 line: A; 2: B1 over B2) -> (svg body, the plain area's box)"""
    lines = [s for s in lines if s]
    styles = ['A'] if len(lines) == 1 else ['B1', 'B2']
    body, xs = [], []
    for s, st in zip(lines, styles):
        d, a, b, _, _ = line(s, st, cx); body.append('<path d="%s" fill="black"/>' % d); xs += [a, b]
    return '<g id="legal">%s</g>' % ''.join(body), area(min(xs), max(xs), cx)


if __name__ == '__main__':
    import sys
    print(legal(sys.argv[1:] or ['© GCE 1982. All Rights Reserved. MADE IN HONG KONG'])[1])
