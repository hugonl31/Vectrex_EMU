"""docs/images/page/corner_diagram.svg: why a Vectrex corner is round - the integrators' path (sharp) and the spot's
(3 equal lag stages, 13.3 cycles in all, as the core), a corner of the Corner Test (X speed flips, Y stays), legs of 25
cycles, the beam lit throughout, rests of 0 / 8 / 32 cycles; light = time, one dot per 1/4 cycle"""
import math
TAU, N, LEG, DT = 13.3 / 3, 3, 25, 0.25
def run(rest):
    t, ix, iy, s = 0.0, 0.0, 0.0, [[0.0, 0.0] for _ in range(N)]
    path, ipath, xs = [], [], []
    T = 2 * LEG + rest + 40
    while t < T:
        if t < LEG: vx, vy = -1.0, 1.0
        elif t < LEG + rest: vx, vy = 0.0, 0.0
        elif t < 2 * LEG + rest: vx, vy = 1.0, 1.0
        else: vx, vy = 0.0, 0.0
        ix += vx * DT; iy += vy * DT
        px, py = ix, iy
        for k in range(N):
            a = 1 - math.exp(-DT / TAU); s[k][0] += (px - s[k][0]) * a; s[k][1] += (py - s[k][1]) * a; px, py = s[k]
        if t < 2 * LEG + rest + 14: path.append((px, py)); ipath.append((ix, iy)); xs.append((t, ix, px))   # (the beam goes dark ~ when the spot ends its leg)
        t += DT
    return path, ipath, xs
W, H = 960, 560
o = ['<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 %d %d" width="%d" height="%d" font-family="-apple-system,Segoe UI,Helvetica,Arial,sans-serif">' % (W, H, W, H),
     '<defs><filter id="g" x="-50%" y="-50%" width="200%" height="200%"><feGaussianBlur stdDeviation="2.2"/></filter></defs>',
     '<rect width="%d" height="%d" rx="14" fill="#0b0f14"/>' % (W, H)]
TXT, DIM, ORA, BEAM = '#dce6f0', '#8a98a8', '#f0a040', '#cfe6ff'
o.append('<text x="24" y="38" fill="%s" font-size="20" font-weight="600">Why a Vectrex draws round corners</text>' % TXT)
o.append('<text x="24" y="62" fill="%s" font-size="14">The game stops the integrators at the corner, waits, then sends them the new way. The spot follows them about 13 cycles late.</text>' % DIM)
panels = [(0, 'rest 0 cycles', 'still on its way: cuts the corner'), (8, 'rest 8 cycles', 'not there yet: round'), (32, 'rest 32 cycles', 'arrives and waits: sharp, a bright dot')]
for i, (rest, ttl, sub) in enumerate(panels):
    x0, y0, pw = 24 + i * 310, 84, 290
    o.append('<rect x="%d" y="%d" width="%d" height="300" rx="10" fill="#05080b" stroke="#223040"/>' % (x0, y0, pw))
    o.append('<text x="%d" y="%d" fill="%s" font-size="16" font-weight="600">%s</text>' % (x0 + 14, y0 + 26, TXT, ttl))
    o.append('<text x="%d" y="%d" fill="%s" font-size="13">%s</text>' % (x0 + 14, y0 + 46, DIM, sub))
    path, ipath, _ = run(rest)
    sc = 4.0; cx, cy = x0 + pw / 2, y0 + 72
    P = lambda p: (cx + (p[0] + 12.5) * sc, cy + p[1] * sc)
    a, b = P((0, 0)), P((-LEG, LEG)); c = P((0, 2 * LEG))
    o.append('<polyline points="%.1f,%.1f %.1f,%.1f %.1f,%.1f" fill="none" stroke="%s" stroke-width="1.6" stroke-dasharray="6 5"/>' % (*a, *b, *c, ORA))
    dots = ''.join('<circle cx="%.1f" cy="%.1f" r="2.1"/>' % P(p) for p in path)
    o.append('<g fill="%s" fill-opacity="0.16" filter="url(#g)">%s</g>' % (BEAM, dots))
    o.append('<g fill="#ffffff" fill-opacity="0.22">%s</g>' % dots.replace('r="2.1"', 'r="1.0"'))
# legend
ly = 410
o.append('<line x1="34" y1="%d" x2="74" y2="%d" stroke="%s" stroke-width="1.6" stroke-dasharray="6 5"/>' % (ly, ly, ORA))
o.append('<text x="84" y="%d" fill="%s" font-size="14">where the integrators are: the path an ordinary emulator draws - always a sharp corner</text>' % (ly + 5, TXT))
o.append('<circle cx="54" cy="%d" r="5" fill="%s" filter="url(#g)"/><circle cx="54" cy="%d" r="2" fill="#fff"/>' % (ly + 30, BEAM, ly + 30))
o.append('<text x="84" y="%d" fill="%s" font-size="14">where the spot really is (the beam lit all the way): brighter where it lingers</text>' % (ly + 35, TXT))
o.append('<text x="24" y="%d" fill="%s" font-size="13">Legs of 25 cycles at speed $50, X turning round, Y going on (the Corner Test). The spot: three equal lag stages, 13.3 cycles in all,</text>' % (ly + 78, DIM))
o.append('<text x="24" y="%d" fill="%s" font-size="13">as measured on a real Vectrex and used by the emulator (simplified: the RAMP delays left out). One cycle = 2/3 microsecond.</text>' % (ly + 98, DIM))
o.append('</svg>')
open(r'G:\AI\Vectrex_EMU\docs\images\page\corner_diagram.svg', 'w', encoding='utf-8').write('\n'.join(o))
for r in (0, 8, 32):
    p, ip, _ = run(r); tip = min(p, key=lambda q: q[0]); print(r, 'spot reaches x', round(tip[0], 2), 'of -25')
