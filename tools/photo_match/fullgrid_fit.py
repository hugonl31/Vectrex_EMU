"""the owner's photo of the Full Grid Test (RAW as shot) laid on the emulator's own Full Grid - with the measured tube
map and with a perfect grid (Tube geometry off): the photo is registered on the measured picture (ECC, a homography),
the same transform then put on the perfect one. argv: out_dir"""
import sys, os, tempfile, numpy as np, rawpy, cv2
from PIL import Image, ImageDraw, ImageFont
sys.path.insert(0, r'G:\AI\Vectrex_EMU\tools'); from lrhost import Host
out = sys.argv[1]
ROM = r'G:\AI\Vectrex_EMU\build\Vectrex_EMU\roms\Tests\Full Grid Test 50 fps.bin'
DNG = r'G:\Google Drive\#Sharing\Vectrex\Testcarts\Full Test Grid 50.dng'
def render(geom, curve='1.00'):
    h = Host(r'G:\AI\Vectrex_EMU\libretro\vectrex_emu_libretro.dll', 'G:/AI/Vectrex_EMU/libretro/dist/pkg/system', tempfile.mkdtemp(),
             options={'vectrex_emu_deck': 'off', 'vectrex_emu_rotate': 'off', 'vectrex_emu_skip_intro': 'on', 'vectrex_emu_wobble': 'off',
                      'vectrex_emu_bending': 'off', 'vectrex_emu_overlay': 'off', 'vectrex_emu_look': 'vectrex', 'vectrex_emu_case': 'off',
                      'vectrex_emu_glass_edge': 'on', 'vectrex_emu_room': 'off (dark room)', 'vectrex_emu_geometry': geom, 'vectrex_emu_glass_curve': curve, 'vectrex_emu_afterglow': 'off'}, size=(1200, 1500))
    h.load(ROM)
    for i in range(200): h.run()
    fr = []
    for i in range(3):                                  # (a third of the grid a frame)
        h.run(); fr.append(h.grab().astype(np.float32)[..., :3])
    h.close(); return fr
measF, idealF = render('on'), render('off', '0.00')   # (perfect: no tube map, no glass curve)
meas, ideal = np.max(measF, 0), np.max(idealF, 0)   # (the photo holds the whole grid: the three thirds together, no afterglow)
lin = rawpy.imread(DNG).postprocess(gamma=(1, 1), no_auto_bright=True, output_bps=16, use_camera_wb=True, half_size=True).astype(np.float32) / 65535
L = lin.max(2); p = np.percentile(L[L > np.percentile(L, 98)], 95); ph = np.clip(lin / p * 0.9, 0, 1) ** (1 / 2.2)
def prep(a, w): g = a.max(2) if a.ndim == 3 else a; g = np.clip(g - cv2.GaussianBlur(g, (0, 0), 12), 0, None); g = g / max(1e-6, np.percentile(g, 99.8)); s = w / g.shape[1]; g = cv2.resize(g, (w, round(g.shape[0] * s)), interpolation=cv2.INTER_AREA); return cv2.GaussianBlur(g, (0, 0), 2.0), s
W0 = 600
E, se = prep(meas / 255.0, W0); Pp, sp = prep(ph, W0)
# start: the lit areas' boxes
def box(g):
    m = g > 0.15; cols = np.nonzero(m.mean(0) > 0.03)[0]; rows = np.nonzero(m.mean(1) > 0.03)[0]; return cols.min(), rows.min(), cols.max(), rows.max()
ex0, ey0, ex1, ey1 = box(E); px0, py0, px1, py1 = box(Pp); print('boxes', (ex0, ey0, ex1, ey1), (px0, py0, px1, py1), flush=True)
cv2.imwrite(os.path.join(out, 'dbgE.png'), (np.clip(E, 0, 1) * 255).astype(np.uint8)); cv2.imwrite(os.path.join(out, 'dbgP.png'), (np.clip(Pp, 0, 1) * 255).astype(np.uint8))
sx, sy = (ex1 - ex0) / (px1 - px0), (ey1 - ey0) / (py1 - py0)
H = np.array([[sx, 0, ex0 - px0 * sx], [0, sy, ey0 - py0 * sy], [0, 0, 1]], np.float32)
Hi = np.linalg.inv(H).astype(np.float32)   # (ECC maps the template's points into the input: template = emulator, input = photo)
crit = (cv2.TERM_CRITERIA_EPS | cv2.TERM_CRITERIA_COUNT, 400, 1e-6)
for blur in (6.0, 3.0, 1.5):
    e_ = cv2.GaussianBlur(E, (0, 0), blur); p_ = cv2.GaussianBlur(Pp, (0, 0), blur)
    cc, Hi = cv2.findTransformECC(e_.astype(np.float32), p_.astype(np.float32), Hi, cv2.MOTION_HOMOGRAPHY, crit, None, 5) if True else None
    print('ECC blur %.1f: %.3f' % (blur, cc), flush=True)
# to full sizes: emulator px -> photo px
S_e = np.diag([se, se, 1.0]); S_p = np.diag([1 / sp, 1 / sp, 1.0])
Hfull = S_p @ Hi.astype(np.float64) @ S_e
warp = cv2.warpPerspective(ph, Hfull, (meas.shape[1], meas.shape[0]), flags=cv2.INTER_LINEAR | cv2.WARP_INVERSE_MAP)
np.save(os.path.join(out, 'fg_H.npy'), Hfull)
f = ImageFont.truetype('segoeui.ttf', 30)
def compose(base, title):
    b = base / 255.0; r = warp.max(2)                    # (the photo's lines in orange over the emulator's blue)
    o = np.clip(b * 0.85 + r[..., None] * np.array([1.0, 0.45, 0.08]) * 0.9, 0, 1)
    im = Image.fromarray((o * 255).astype(np.uint8)); d = ImageDraw.Draw(im); d.text((24, 16), title, fill=(255, 255, 255), font=f); return im
a = compose(meas, 'Vectrex_EMU, the measured tube'); b = compose(ideal, 'A perfect grid (an ideal tube)')
both = Image.new('RGB', (a.width * 2 + 20, a.height), (0, 0, 0)); both.paste(a, (0, 0)); both.paste(b, (a.width + 20, 0))
both.save(os.path.join(out, 'fullgrid_fit.jpg'), quality=90); print(both.size)
