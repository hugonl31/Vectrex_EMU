"""The plastic case round the tube (shown without an overlay): glossy black walls sloping steeply down to the
glass, a rounded lip at the glass and at the outer frame, smooth round corners (the owner's photos, 2026-10-24).

The shader (vecrender, CURVE_SHADER) draws it from these numbers; here only the outline and the scratch map.
Units: half picture heights, y down, like the glass (GLASS_LUT, GLASS_DX/DY)."""
import math

# the case's inner opening, traced from the owner's photo (the green line): its radius every degree from the
# glass's centre, smoothed (its corners squarer than the glass's)
CASE_LUT = (
    0.7598, 0.7599, 0.7602, 0.7608, 0.7616, 0.7627, 0.7640, 0.7655, 0.7672, 0.7692, 0.7715, 0.7740, 0.7767,
    0.7798, 0.7830, 0.7866, 0.7904, 0.7945, 0.7989, 0.8036, 0.8085, 0.8138, 0.8194, 0.8254, 0.8317, 0.8383,
    0.8454, 0.8528, 0.8607, 0.8689, 0.8776, 0.8867, 0.8962, 0.9062, 0.9168, 0.9279, 0.9396, 0.9519, 0.9647,
    0.9782, 0.9924, 1.0074, 1.0231, 1.0395, 1.0570, 1.0754, 1.0947, 1.1151, 1.1367, 1.1600, 1.1852, 1.2112,
    1.2355, 1.2549, 1.2663, 1.2684, 1.2622, 1.2504, 1.2363, 1.2222, 1.2091, 1.1971, 1.1859, 1.1751, 1.1650,
    1.1553, 1.1461, 1.1374, 1.1292, 1.1214, 1.1140, 1.1070, 1.1005, 1.0944, 1.0887, 1.0835, 1.0786, 1.0741,
    1.0700, 1.0662, 1.0628, 1.0598, 1.0570, 1.0546, 1.0525, 1.0508, 1.0494, 1.0483, 1.0475, 1.0471, 1.0469)

FRONT_W = 0.047             # the flat matte front round the walls (5 mm; vecrender's shader has the same)
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
