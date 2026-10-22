"""The room's light (Light menu): an office ceiling light - a long fluorescent fixture, a grid of white louvre cells
(5600 K), lying flat on the ceiling above and in front of the player, turned a little - reflected in the tube's glass
and on an overlay. Designed with the owner from previews and his photos (2026-10-22/23).

The geometry (shared with the shaders in vecrender, LIGHT_GLSL): a point on the screen (u, v: units of half the
picture height, centred, v down) sees, in a mirror, the ceiling point reflp(u, v): the viewer's angle across the glass
plus the surface's tilt (the glass a gentle dome, curvature GLASS_CURV; the phosphor layer behind it more curved,
INNER_CURV; an overlay flat). ceiling() is the fixture seen in perspective: a plane above, its horizon below the
fixture's middle (HZ, CH), the fixture turned ANG, its far end a little dimmer (the inverse square law, gentled).

Here, in pure Python and at low resolution (MAP_W x MAP_H over the glass), the blurry parts the glass shader samples:
 R: the glass's own soft light around the reflection (the glass not perfectly clean: 2 % spread wide, a slight bloom)
 G: the light on the phosphor layer behind the thick glass - the fixture's image on the inner surface, enlarged 3x
    (the owner's photo), offset inwards by the glass's thickness (thin in the middle, thick at the edges), very blurry
Made again only when the light is moved."""
import math

HZ, CH = 0.80, 2.4                      # the ceiling's horizon and height (image units)
ANG = math.radians(20)                  # the fixture turned on the ceiling
FIX_W, FIX_L = 0.30, 1.2                # its half width and half length; 2 x 9 cells
GLASS_SIZE, GLASS_CURV, INNER_CURV = 0.48, 0.2, 1.1
FLAT_SIZE = 0.42                        # (on an overlay)
LIGHT_RGB = (1.0, 0.94, 0.894)          # 5600 K
MAP_U, MAP_V = (-0.80, 0.80), (-1.02, 1.02)
MAP_W, MAP_H = 96, 122
TEXEL_PX = (MAP_V[1] - MAP_V[0]) / MAP_H * 917.0     # (one texel in the previews' 1834 px picture)
INNER_LONG, INNER_WIDE = 1.15, 2.0     # the phosphor's blob: its length / width against the glass reflection
INNER_PINCH = 0.42                       # (its length x this: the inner curvature makes it squarer)
INNER_ENL = 2.1                         # the phosphor layer's image: enlarged (the owner's photo: 3-4x)
INNER_OFF = 0.21                        # its offset at the glass's edge (half picture heights; less inwards: r^2)

def place(x, y):
    """Light position (0..1, 0..1: left .. right, highest - 5 % of it visible - .. lowest - its middle at a
    quarter of the screen) -> the fixture's point for the glass (lx, ly) and for a flat overlay"""
    dx, dy = 1.48 * x, -0.21 + 0.77 * y
    return (-0.74 + dx, -1.12 + dy), (-0.44 + 0.6 * dx, -0.72 + 0.6 * dy)

def ceiling(px, py):
    """the fixture on the ceiling at (px, py) (fixture units): its light, 0 outside"""
    den = HZ - py
    if den < 0.05:
        return 0.0
    D = CH / den; D0 = CH / HZ
    X = px * D / 2.0; Zr = D0 - D
    ca, sa = math.cos(ANG), math.sin(ANG)
    Xr, Z = ca * X - sa * Zr, sa * X + ca * Zr
    if abs(Xr) >= FIX_W or abs(Z) >= FIX_L:
        return 0.0
    fall = min(1.25, max(0.75, (D0 / D) ** 0.7)) / 1.25
    if abs(Xr) > FIX_W - 0.05 or abs(Z) > FIX_L - 0.05:
        return 0.12 * fall
    cx = (Xr + FIX_W) / (2 * FIX_W) * 2; cz = (Z + FIX_L) / (2 * FIX_L) * 9
    fx, fz = cx - math.floor(cx), cz - math.floor(cz)
    if abs(fx - 0.5) > 0.44 or abs(fz - 0.5) > 0.42:
        return 0.18 * fall
    return (0.85 + 0.15 * max(0.0, 1 - abs(fx - 0.5) * 2) ** 0.6) * fall

def outline(px, py):
    """the fixture's outline only (1 inside, no grid): the phosphor layer shows a blob, not its cells"""
    den = HZ - py
    if den < 0.05:
        return 0.0
    D = CH / den; D0 = CH / HZ
    X = px * D / 2.0; Zr = D0 - D
    ca, sa = math.cos(ANG), math.sin(ANG)
    Xr, Z = ca * X - sa * Zr, sa * X + ca * Zr
    if abs(Xr) >= FIX_W or abs(Z) >= FIX_L:
        return 0.0
    return min(1.25, max(0.75, (D0 / D) ** 0.7)) / 1.25

def reflp(u, v, curv):
    nx, ny, nz = curv * u, curv * v, 1.0
    L = math.sqrt(nx * nx + ny * ny + nz * nz); nx, ny, nz = nx / L, ny / L, nz / L
    dn = -nz
    rx, ry, rz = -2 * dn * nx, -2 * dn * ny, -1 - 2 * dn * nz
    return u * 0.9 + rx / rz, v * 0.9 + ry / rz

def pattern(u, v, lx, ly, size, curv):
    px, py = reflp(u, v, curv)
    return ceiling((px - lx) / size, (py - ly) / size)

def centre(lx, ly, curv):
    """where on the screen the fixture's middle is seen (reflp(u, v) = (lx, ly))"""
    u, v = lx / 0.9, ly / 0.9
    for _ in range(12):
        px, py = reflp(u, v, curv); e = 1e-4
        ax, ay = reflp(u + e, v, curv); bx, by = reflp(u, v + e, curv)
        j11, j21, j12, j22 = (ax - px) / e, (ay - py) / e, (bx - px) / e, (by - py) / e
        det = j11 * j22 - j12 * j21
        rx, ry = px - lx, py - ly
        u -= (j22 * rx - j12 * ry) / det; v -= (-j21 * rx + j11 * ry) / det
    return u, v

def _box(a, w, h, r, horiz):
    """one box blur pass (zero beyond the edges), radius r texels"""
    out = [0.0] * (w * h)
    n = 2 * r + 1
    if horiz:
        for y in range(h):
            row = a[y * w:(y + 1) * w]; s = 0.0; acc = [0.0] * (w + 1)
            for x in range(w): s += row[x]; acc[x + 1] = s
            for x in range(w):
                out[y * w + x] = (acc[min(w, x + r + 1)] - acc[max(0, x - r)]) / n
    else:
        for x in range(w):
            s = 0.0; acc = [0.0] * (h + 1)
            for y in range(h): s += a[y * w + x]; acc[y + 1] = s
            for y in range(h):
                out[y * w + x] = (acc[min(h, y + r + 1)] - acc[max(0, y - r)]) / n
    return out

def blur(a, w, h, sigma):
    """a Gaussian of sigma texels, as three box blurs"""
    if sigma < 0.3:
        return list(a)
    r = max(1, int(round((math.sqrt(12 * sigma * sigma / 3 + 1) - 1) / 2)))
    for _ in range(3):
        a = _box(_box(a, w, h, r, True), w, h, r, False)
    return a

def flat_points(x, y):
    """the light's reflection on a flat overlay as six points spread over its area (along its length and across it):
    the scratches catch the light from all of it, not from one point (u, v: half picture heights)"""
    _, (fx, fy) = place(x, y)
    cu0, cv0 = fx / 0.9, fy / 0.9
    pts = []
    for j in range(-30, 31):
        for i in range(-30, 31):
            u, v = cu0 + i * 0.012, cv0 + j * 0.012
            if ceiling((u * 0.9 - fx) / FLAT_SIZE, (v * 0.9 - fy) / FLAT_SIZE) > 0.3:
                pts.append((u, v))
    if len(pts) < 3:
        return [(cu0, cv0)] * 6
    n = float(len(pts)); mu = sum(p[0] for p in pts) / n; mv = sum(p[1] for p in pts) / n
    cuu = sum((p[0] - mu) ** 2 for p in pts) / n; cvv = sum((p[1] - mv) ** 2 for p in pts) / n
    cuv = sum((p[0] - mu) * (p[1] - mv) for p in pts) / n
    th = 0.5 * math.atan2(2 * cuv, cuu - cvv); ca, sa = math.cos(th), math.sin(th)
    sl = math.sqrt(max(1e-8, ca * ca * cuu + 2 * ca * sa * cuv + sa * sa * cvv))
    ss = math.sqrt(max(1e-8, sa * sa * cuu - 2 * ca * sa * cuv + ca * ca * cvv))
    out = [(mu + ca * k * sl, mv + sa * k * sl) for k in (-1.5, -0.5, 0.5, 1.5)]
    out += [(mu - sa * k * ss, mv + ca * k * ss) for k in (-1.0, 1.0)]
    return out

def maps(x, y, aw, gn):
    """the R / G map (MAP_W x MAP_H floats each, row 0 = the top) for Light position (x, y); aw, gn: the glass's
    width / height and superellipse power"""
    (lx, ly), _ = place(x, y)
    w, h = MAP_W, MAP_H
    us = [MAP_U[0] + (i + 0.5) * (MAP_U[1] - MAP_U[0]) / w for i in range(w)]
    vs = [MAP_V[0] + (j + 0.5) * (MAP_V[1] - MAP_V[0]) / h for j in range(h)]
    G = [pattern(u, v, lx, ly, GLASS_SIZE, GLASS_CURV) for v in vs for u in us]
    px = lambda p: p / TEXEL_PX
    b1, b2, b3 = blur(G, w, h, px(8)), blur(G, w, h, px(25)), blur(G, w, h, px(40))
    R = [0.12 * a + 0.06 * b + 0.02 * c for a, b, c in zip(b1, b2, b3)]
    # the phosphor layer (as the approved preview): the fixture seen in the more curved inner surface, enlarged,
    # placed at the glass reflection's middle + the glass's thickness inwards, very blurry
    ug, vg = centre(lx, ly, GLASS_CURV); ui, vi = centre(lx, ly, INNER_CURV)
    rg = (abs(ug / aw) ** gn + abs(vg) ** gn) ** (1.0 / gn)
    o0 = INNER_OFF * min(1.0, rg) ** 2; nn = max(1e-6, math.hypot(ug, vg))
    tu, tv = ug - o0 * ug / nn, vg - o0 * vg / nn
    # the glow: the glass reflection itself, spread around its own centre of light (its length x INNER_PINCH - the
    # inner surface's stronger curvature makes it squarer - and both ways x INNER_ENL), every point moved inwards by
    # the glass's thickness there (thin in the middle, thick at the edges)
    tot = sum(G) or 1.0
    cu = sum(G[k] * us[k % w] for k in range(w * h)) / tot; cv = sum(G[k] * vs[k // w] for k in range(w * h)) / tot
    cuu = sum(G[k] * (us[k % w] - cu) ** 2 for k in range(w * h)); cvv = sum(G[k] * (vs[k // w] - cv) ** 2 for k in range(w * h))
    cuv = sum(G[k] * (us[k % w] - cu) * (vs[k // w] - cv) for k in range(w * h))
    th = 0.5 * math.atan2(2 * cuv, cuu - cvv)                      # (its long axis)
    ca, sa = math.cos(th), math.sin(th)
    def src(u, v):
        r_ = (abs(u / aw) ** gn + abs(v) ** gn) ** (1.0 / gn); n_ = max(1e-6, math.hypot(u, v))
        o_ = INNER_OFF * min(1.0, r_) ** 2
        u, v = u + o_ * u / n_, v + o_ * v / n_                   # (lit by the light seen further out)
        du, dv = u - cu, v - cv
        a_, b_ = (ca * du + sa * dv) / (INNER_ENL * INNER_PINCH), (-sa * du + ca * dv) / INNER_ENL
        return cu + ca * a_ - sa * b_, cv + sa * a_ + ca * b_
    I = [pattern(*src(u, v), lx, ly, GLASS_SIZE, GLASS_CURV) for v in vs for u in us]
    i1, i2, i3 = blur(I, w, h, px(80)), blur(I, w, h, px(150)), blur(I, w, h, px(260))   # (a shorter reach)
    Gm = [0.286 * (1.1 * a + 0.6 * b + 0.2 * c) for a, b, c in zip(i1, i2, i3)]   # (as bright as the preview)
    return R, Gm
