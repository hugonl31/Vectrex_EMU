"""the Full Grid Test side by side: the owner's photo (RAW as shot), the emulator with the measured tube, a perfect grid
(Tube geometry off, glass curve 0) - each cut to its lit area, the same height. Then the photo laid on the measured
picture, registered through the emulator's own grid drawn in red on the photo (docs/images/page/fullgrid.jpg, the fit's
overlay) - OVERLAY=1, experimental: ECC does not converge yet. argv: out_dir"""
import sys, os, tempfile, numpy as np, rawpy, cv2
from PIL import Image, ImageDraw, ImageFont
sys.path.insert(0, r'G:\AI\Vectrex_EMU\tools'); from lrhost import Host
out = sys.argv[1]
ROM = r'G:\AI\Vectrex_EMU\build\Vectrex_EMU\roms\Tests\Full Grid Test 50 fps.bin'
DNG = r'G:\Google Drive\#Sharing\Vectrex\Testcarts\Full Test Grid 50.dng'
def render(geom, curve, edge):
    h = Host(r'G:\AI\Vectrex_EMU\libretro\vectrex_emu_libretro.dll', 'G:/AI/Vectrex_EMU/libretro/dist/pkg/system', tempfile.mkdtemp(),
             options={'vectrex_emu_deck': 'off', 'vectrex_emu_rotate': 'off', 'vectrex_emu_skip_intro': 'on', 'vectrex_emu_wobble': 'off',
                      'vectrex_emu_bending': 'on', 'vectrex_emu_overlay': 'off', 'vectrex_emu_look': 'vectrex', 'vectrex_emu_case': 'off',
                      'vectrex_emu_glass_edge': edge, 'vectrex_emu_room': 'off (dark room)', 'vectrex_emu_geometry': geom,
                      'vectrex_emu_glass_curve': curve, 'vectrex_emu_afterglow': 'off', 'vectrex_emu_rate': '50 Hz (game)'}, size=(1200, 1500))
    h.load(ROM)
    for i in range(200): h.run()
    fr = []
    for i in range(3): h.run(); fr.append(h.grab().astype(np.float32)[..., :3])   # (a third of the grid a frame: the three together - at the cart's own 50 Hz, or the thirds land in the wrong places)
    h.close(); return np.max(fr, 0)
meas, ideal, meas_raw = render('on', '1.00', 'on'), render('off', '1.00', 'on'), render('on', '1.00', 'off')   # (perfect: the tube map off; the glass curvature stays 1 - at 0 it takes the glass's curve out of a picture that has none and stretches the edges)
lin = rawpy.imread(DNG).postprocess(gamma=(1, 1), no_auto_bright=True, output_bps=16, use_camera_wb=True, half_size=True).astype(np.float32) / 65535
lin = cv2.resize(lin, (1512, 2016), interpolation=cv2.INTER_AREA)   # (the fit's overlay picture's size)
L = lin.max(2); p = np.percentile(L[L > np.percentile(L, 98)], 95); ph = np.clip(lin / p * 0.9, 0, 1) ** (1 / 2.2) * 255
def box(a, thr, frac=0.03, pad=0.03):
    g = a.max(2) if a.ndim == 3 else a; g = np.clip(g - cv2.GaussianBlur(g, (0, 0), 12), 0, None); m = g > thr
    cols = np.nonzero(m.mean(0) > frac)[0]; rows = np.nonzero(m.mean(1) > frac)[0]
    x0, x1, y0, y1 = cols.min(), cols.max(), rows.min(), rows.max(); px, py = int((x1 - x0) * pad), int((y1 - y0) * pad)
    return max(0, y0 - py), min(g.shape[0], y1 + py), max(0, x0 - px), min(g.shape[1], x1 + px)
f = ImageFont.truetype('segoeui.ttf', 26)
Hh = 900
def fit(a): im = Image.fromarray(np.clip(a, 0, 255).astype(np.uint8)); return im.resize((round(im.width * Hh / im.height), Hh), Image.LANCZOS)
from scipy.signal import find_peaks
def lines(a):
    """the grid's line positions across the middle (columns) and down the middle (rows)"""
    g = a.max(2) if a.ndim == 3 else a; H, W = g.shape
    def pk(prof): prof = prof - np.median(prof); q, _ = find_peaks(prof, height=prof.max() * 0.3, distance=10); return q
    return pk(g[H // 2 - H // 8:H // 2 + H // 8].mean(0)), pk(g[:, W // 2 - W // 8:W // 2 + W // 8].mean(1))
def centre(xs, ys, W, H):
    """the middle crossing and the spacing there (the four cells round the middle)"""
    i = int(np.argmin(abs(xs - W / 2))); j = int(np.argmin(abs(ys - H / 2)))
    return xs[i], ys[j], np.mean(np.diff(xs[max(0, i - 2):i + 3])), np.mean(np.diff(ys[max(0, j - 2):j + 3]))
pxs, pys = lines(ph); pcx, pcy, pdx, pdy = centre(pxs, pys, ph.shape[1], ph.shape[0])
exs, eys = lines(meas); ecx, ecy, edx, edy = centre(exs, eys, meas.shape[1], meas.shape[0])
print('photo: %d cols, %d rows, middle cell %.1f x %.1f px; emulator: %d cols, %d rows, middle cell %.1f x %.1f px' % (len(pxs), len(pys), pdx, pdy, len(exs), len(eys), edx, edy), flush=True)
def span(ls, c, n):
    """the distance from the n-th line left (above) of the middle one to the n-th right (below)"""
    i = int(np.argmin(abs(ls - c))); n = min(n, i, len(ls) - 1 - i); return ls[i + n] - ls[i - n], n
sxp, nx = span(pxs, pcx, 9); sxe, _ = span(exs, ecx, nx); syp, ny = span(pys, pcy, 9); sye, _ = span(eys, ecy, ny)
kx, ky = sxp / sxe, syp / sye        # (the same grid lines the same distance apart: the whole grid the photo's size)
print('emulator scaled by %.3f across (%d lines each side), %.3f down (%d)' % (kx, nx, ky, ny), flush=True)
def scaled(a): return cv2.resize(a, None, fx=kx, fy=ky, interpolation=cv2.INTER_AREA)
ms, ids = scaled(meas), scaled(ideal)
def shift(p, e):
    """the offset that lays the emulator's lines (scaled) on the photo's: every pairing of the two line sets a few lines
    apart, the one with the smallest spread wins - the same grid line on both, not just the nearest one"""
    best = None
    for o in range(-3, 4):
        pairs = [(p[j + o], e[j]) for j in range(len(e)) if 0 <= j + o < len(p)]
        if len(pairs) < 12: continue
        d = np.array([a - b for a, b in pairs]); t = np.median(d); r = np.sqrt(np.mean((d - t) ** 2))
        if best is None or r < best[0]: best = (r, t, o)
    return best
rx_, tx, ox = shift(pxs, exs * kx); ry_, ty, oy = shift(pys, eys * ky)
print('lines paired: across offset %d (rms %.1f px), down offset %d (rms %.1f px)' % (ox, rx_, oy, ry_), flush=True)
mcx, mcy = pcx - tx, pcy - ty        # (the emulator's point under the photo's middle crossing)
# one window for all three: the photo's lit area, around the middle crossing
y0, y1, x0, x1 = box(ph, 25); hw0, hw1, hh0, hh1 = pcx - x0, x1 - pcx, pcy - y0, y1 - pcy
def cut(a, cx, cy):
    X0, Y0 = int(round(cx - hw0)), int(round(cy - hh0)); w, h = int(hw0 + hw1), int(hh0 + hh1)
    o = np.zeros((h, w, 3), np.float32); sx0, sy0 = max(0, X0), max(0, Y0); sx1, sy1 = min(a.shape[1], X0 + w), min(a.shape[0], Y0 + h)
    o[sy0 - Y0:sy1 - Y0, sx0 - X0:sx1 - X0] = a[sy0:sy1, sx0:sx1]; return o
P, M, I = fit(cut(ph, pcx, pcy)), fit(cut(ms, mcx, mcy)), fit(cut(ids, mcx, mcy))
# (the photo at the emulator's total light, as rawcmp: exposed for the detail, the camera shows the lines brighter)
light = lambda a: ((np.asarray(a, np.float32) / 255.0) ** 2.2)[np.asarray(a).max(2) > 12].sum()
kp = (light(M) / light(P)) ** (1 / 2.2); print('photo scaled by %.3f' % kp, flush=True)
P = Image.fromarray(np.clip(np.asarray(P, np.float32) * kp, 0, 255).astype(np.uint8))
panels = [(P, 'Real Vectrex (photo)'), (M, 'Vectrex_EMU: the measured tube'), (I, 'A perfect grid (an ideal tube)')]
W = sum(p_.width for p_, _ in panels) + 20 * 4; im = Image.new('RGB', (W, Hh + 50), (6, 6, 6)); d = ImageDraw.Draw(im); x = 20
for p_, t in panels: im.paste(p_, (x, 44)); d.text((x, 8), t, fill=(235, 160, 60), font=f); x += p_.width + 20
im.save(os.path.join(out, 'fullgrid_side.jpg'), quality=90); print('side', im.size, flush=True)
if os.environ.get('OVERLAY'):   # (the overlay: experimental - ECC does not lock on the repeating grid yet)
    # the overlay: the emulator's grid (full raster) registered on the red lines of the fit's overlay picture (photo space)
    ov = np.asarray(Image.open(r'G:\AI\Vectrex_EMU\docs\images\page\fullgrid.jpg').convert('RGB')).astype(np.float32)
    red = np.clip((ov[..., 0] - np.maximum(ov[..., 1], ov[..., 2])) / 120.0, 0, 1)
    assert red.shape == ph.shape[:2], (red.shape, ph.shape)
    g = meas_raw.max(2) / 255.0
    def bb(m, t): ys, xs = np.nonzero(m > t); return xs.min(), ys.min(), xs.max(), ys.max()
    ax0, ay0, ax1, ay1 = bb(g, 0.25); bx0, by0, bx1, by1 = bb(red, 0.3)
    s_ = 0.4; sx, sy = (bx1 - bx0) / (ax1 - ax0), (by1 - by0) / (ay1 - ay0)
    Hm = np.array([[sx, 0, bx0 - ax0 * sx], [0, sy, by0 - ay0 * sy], [0, 0, 1]], np.float32)   # (emulator px -> photo px)
    Sd = np.diag([s_, s_, 1.0]).astype(np.float32)
    tmpl = cv2.resize(g, None, fx=s_, fy=s_, interpolation=cv2.INTER_AREA); inp = cv2.resize(red, None, fx=s_, fy=s_, interpolation=cv2.INTER_AREA)
    Hs = (Sd @ Hm @ np.linalg.inv(Sd)).astype(np.float32)
    crit = (cv2.TERM_CRITERIA_EPS | cv2.TERM_CRITERIA_COUNT, 300, 1e-6)
    for blur in (5.0, 2.5, 1.2):
        cc, Hs = cv2.findTransformECC(cv2.GaussianBlur(tmpl, (0, 0), blur), cv2.GaussianBlur(inp, (0, 0), blur), Hs, cv2.MOTION_HOMOGRAPHY, crit, None, 5)
        print('ECC (emulator grid on the red lines) blur %.1f: %.3f' % (blur, cc), flush=True)
    Hm = np.linalg.inv(Sd) @ Hs.astype(np.float64) @ Sd
    warp = cv2.warpPerspective(ph, Hm, (meas.shape[1], meas.shape[0]), flags=cv2.INTER_LINEAR | cv2.WARP_INVERSE_MAP)
    def over(base):
        b = base[ey0:ey1, ex0:ex1] / 255.0; r = warp[ey0:ey1, ex0:ex1].max(2) / 255.0
        return np.clip(b * 0.9 + r[..., None] * np.array([1.0, 0.45, 0.08]), 0, 1) * 255
    O1, O2 = fit(over(meas)), fit(over(ideal))
    panels = [(O1, 'Photo (orange) on Vectrex_EMU (blue)'), (O2, 'Photo (orange) on a perfect grid (blue)')]
    W = sum(p_.width for p_, _ in panels) + 20 * 3; im = Image.new('RGB', (W, Hh + 50), (6, 6, 6)); d = ImageDraw.Draw(im); x = 20
    for p_, t in panels: im.paste(p_, (x, 44)); d.text((x, 8), t, fill=(235, 160, 60), font=f); x += p_.width + 20
    im.save(os.path.join(out, 'fullgrid_overlay.jpg'), quality=90); print('overlay', im.size)
