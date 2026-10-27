"""The plastic case round the tube (shown without an overlay): glossy black walls sloping steeply down to the
glass, a rounded lip at the glass and at the outer frame, smooth round corners (the owner's photos, 2026-10-24;
sizes measured 2026-10-27).

The shader (vecrender, CURVE_SHADER) draws it from these numbers; here only the outline and the scratch map.
Units: half picture heights, y down, like the glass (GLASS_LUT, GLASS_DX/DY)."""
import math

# the case's inner opening (where the glossy walls start): the matte frame's inner edge - the owner's measurements
# (2026-10-27): the frame's outer edge is the overlay sheet, 162 x 207 mm, corners r 8.63 mm (traced), the frame
# 3.5 mm wide, so the opening is 155 x 200 mm, corners r 5.13 mm. Its radius every degree from the glass's centre
# (units: half picture heights, 112.0 mm - vecrender.MM_UNIT)
CASE_LUT = (
    0.69188, 0.69199, 0.69231, 0.69283, 0.69357, 0.69453, 0.69570, 0.69708, 0.69868, 0.70051,
    0.70256, 0.70483, 0.70734, 0.71008, 0.71307, 0.71629, 0.71977, 0.72350, 0.72749, 0.73175,
    0.73629, 0.74111, 0.74622, 0.75164, 0.75736, 0.76341, 0.76979, 0.77652, 0.78361, 0.79107,
    0.79892, 0.80718, 0.81585, 0.82498, 0.83456, 0.84463, 0.85522, 0.86633, 0.87801, 0.89029,
    0.90319, 0.91676, 0.93102, 0.94603, 0.96183, 0.97847, 0.99601, 1.01450, 1.03400, 1.05461,
    1.07638, 1.09860, 1.10928, 1.11059, 1.10342, 1.08985, 1.07686, 1.06449, 1.05272, 1.04152,
    1.03086, 1.02073, 1.01111, 1.00196, 0.99328, 0.98504, 0.97724, 0.96985, 0.96287, 0.95627,
    0.95005, 0.94419, 0.93870, 0.93355, 0.92873, 0.92425, 0.92008, 0.91624, 0.91270, 0.90946,
    0.90653, 0.90388, 0.90153, 0.89946, 0.89767, 0.89616, 0.89493, 0.89398, 0.89330, 0.89289,
    0.89275)

FRONT_W = 0.03125           # the flat matte front round the walls (3.5 mm; vecrender's shader has the same)
CM_PER_UNIT = 11.0          # half the picture is about 11 cm
LIGHT_DIST = 236.0          # the ceiling light's distance (cm): the glare's direction, at a real viewer's distance
LIGHT_TAN = 0.49            # its direction from roomlight's glass position (the emulator's tangents x this)
RES = 920                   # the scratch map: pixels per unit (the 1839 px picture's own scale)

def light_pos(lx, ly):
    """the ceiling light in cm (x right, y up, z to the viewer) from roomlight.place's glass point"""
    x, y, z = LIGHT_TAN * lx, -LIGHT_TAN * ly, 1.0
    n = math.sqrt(x * x + y * y + z * z)
    return (LIGHT_DIST * x / n, LIGHT_DIST * y / n, LIGHT_DIST * z / n)

def _radius(L, ang):
    """the outline's radius at these angles (degrees, 0..90; numpy), linear between the degrees"""
    import numpy as np
    return np.interp(ang, np.arange(91), L)

def scratch_map(glass_lut, seed=11):
    """the case's scratches (the overlay sheet's recipe: counts per area per depth step, 5-80 mm long, mostly
    short): (w, h, RGBA8 bytes - bottom row first, bounds x0, x1, y0, y1 in units round the glass's centre).
    R: how much a scratch catches the tube's light (it lies across the direction to the glass), G: the room's light
    from above (lying level). None without numpy."""
    try:
        import numpy as np
    except ImportError:
        return None
    from vecrender import SCR_COUNTS, SCR_STEPS, ART_AW
    X0, X1 = -max(CASE_LUT[0], glass_lut[0]) - 0.02, max(CASE_LUT[0], glass_lut[0]) + 0.02
    Y0, Y1 = -CASE_LUT[90] - 0.02, CASE_LUT[90] + 0.02
    w, h = int((X1 - X0) * RES), int((Y1 - Y0) * RES)
    ys, xs = np.mgrid[0:h, 0:w].astype(np.float32)
    x = X0 + (xs + 0.5) / RES; y = Y0 + (ys + 0.5) / RES
    r = np.hypot(x, y); ang = np.degrees(np.arctan2(np.abs(y), np.abs(x)))
    wall = (r > _radius(glass_lut, ang)) & (r < _radius(CASE_LUT, ang))
    mm = ART_AW * 2.0 / 162.0 * RES                         # pixels per mm (the overlay sheet's scale)
    area_mm = wall.sum() / mm ** 2
    wy, wx = np.nonzero(wall)
    rng = np.random.RandomState(seed)
    Z = 2
    R = np.zeros((h * Z, w * Z), np.uint8); G = np.zeros_like(R)
    steps = [(SCR_COUNTS[0], 0.3)] + [(c, d) for c, _, _, d, _ in SCR_STEPS]
    for count, depth in steps:
        for _ in range(int(count / (162 * 207.0) * area_mm * 0.28)):
            i = rng.randint(len(wx)); x0, y0 = float(wx[i]), float(wy[i])
            u = rng.rand()
            if u > 0.012:
                L = math.exp(rng.uniform(math.log(5), math.log(15)))
            elif u > 0.0015:
                L = math.exp(rng.uniform(math.log(15), math.log(35)))
            else:
                L = math.exp(rng.uniform(math.log(35), math.log(80)))
            L *= mm
            a = rng.rand() * math.pi; ca, sa = math.cos(a), math.sin(a)
            px, py = X0 + x0 / RES, Y0 + y0 / RES               # (the outward direction: from the centre)
            ln = math.hypot(px, py) or 1.0; nx, ny = px / ln, py / ln
            k = min(1.0, math.sqrt(depth)) * 0.9
            rv = int(255 * k * abs(ca * ny - sa * nx) ** 4)     # (across the direction to the glass)
            gv = int(255 * k * abs(ca) ** 4)                    # (level: the light from above)
            if rv < 3 and gv < 3:
                continue
            n = max(2, int(L * Z * 2))
            tt = np.linspace(-0.5, 0.5, n)
            lx_ = (x0 + ca * L * tt) * Z; ly_ = (y0 + sa * L * tt) * Z
            wid = max(1, int(round(Z * max(0.6, min(depth, 1.1)))))
            for o in range(wid):
                off = o - (wid - 1) / 2.0
                qx = np.round(lx_ - sa * off).astype(int); qy = np.round(ly_ + ca * off).astype(int)
                ok = (qx >= 0) & (qx < w * Z) & (qy >= 0) & (qy < h * Z)
                np.maximum.at(R, (qy[ok], qx[ok]), rv); np.maximum.at(G, (qy[ok], qx[ok]), gv)
    R = R.reshape(h, Z, w, Z).mean(axis=(1, 3)); G = G.reshape(h, Z, w, Z).mean(axis=(1, 3))
    R *= wall; G *= wall
    img = np.zeros((h, w, 4), np.uint8)
    img[..., 0] = R.astype(np.uint8); img[..., 1] = G.astype(np.uint8); img[..., 3] = 255
    return w, h, img[::-1].tobytes(), (X0, X1, Y0, Y1)       # (GL: the bottom row first; y down -> flipped)
