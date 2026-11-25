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
                      'vectrex_emu_bending': 'off', 'vectrex_emu_overlay': 'off', 'vectrex_emu_look': 'vectrex', 'vectrex_emu_case': 'off',
                      'vectrex_emu_glass_edge': edge, 'vectrex_emu_room': 'off (dark room)', 'vectrex_emu_geometry': geom,
                      'vectrex_emu_glass_curve': curve, 'vectrex_emu_afterglow': 'off'}, size=(1200, 1500))
    h.load(ROM)
    for i in range(200): h.run()
    fr = []
    for i in range(3): h.run(); fr.append(h.grab().astype(np.float32)[..., :3])   # (a third of the grid a frame: the three together)
    h.close(); return np.max(fr, 0)
meas, ideal, meas_raw = render('on', '1.00', 'on'), render('off', '0.00', 'on'), render('on', '1.00', 'off')
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
y0, y1, x0, x1 = box(ph, 60, 0.08); P = fit(ph[y0:y1, x0:x1])
ey0, ey1, ex0, ex1 = box(meas, 25); M = fit(meas[ey0:ey1, ex0:ex1]); I = fit(ideal[ey0:ey1, ex0:ex1])   # (the same cut for both: the ideal grid in the same place)
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
