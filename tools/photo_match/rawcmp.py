"""the owner's RAW photos of test carts next to the core's picture, the photo as the eye sees the tube: as shot (6400 K, the
screen's white), the lens's blur taken out lightly (Richardson-Lucy, 8 rounds, the blur from the photo's own line width and
BT_SPOT's 1.25 / 1.85), the same total light as the emulator's picture. argv: out_dir [job ...]"""
import sys, os, tempfile, numpy as np, rawpy
from PIL import Image, ImageDraw, ImageFont
from scipy import ndimage as nd
from skimage.restoration import richardson_lucy
sys.path.insert(0, r'G:\AI\Vectrex_EMU\tools'); from lrhost import Host
P = r'G:\Google Drive\#Sharing\Vectrex\Testcarts'; T = r'G:\AI\Vectrex_EMU\build\Vectrex_EMU\roms\Tests'
JOBS = {'dot_p1': (P + r'\Dot test\11212026132219710.dng', 'Dot Test.bin', [('b1', 1)], 'Dot Test, page 1'),
        'dot_p4': (P + r'\Dot test\11212026132222828.dng', 'Dot Test.bin', [('b4', 1)], 'Dot Test, page 4'),
        'gap_p1': (P + r'\Gaps.dng', 'Gap Test.bin', [('b1', 1)], 'Gap Test, page 1'),
        'lag': (P + r'\Lagtest.dng', 'Lag Test.bin', [], 'Lag Test'),
        'dac_p1': (P + r'\Dactest1.dng', 'DAC Test.bin', [('b1', 1)], 'DAC Test, page 1'),
        'intensity': (P + r'\Brightness.dng', 'Test Cartridge Rev. 4 (Proto).bin', [('b4', 6)], 'Test Cartridge, INTENSITY')}
out = sys.argv[1]; names = sys.argv[2:] or list(JOBS)
def fwhm(col):
    m = col.max(); bg = np.percentile(col, 5); h = bg + (m - bg) / 2; i = col.argmax(); a = b = i
    while a > 0 and col[a] > h: a -= 1
    while b < len(col) - 1 and col[b] > h: b += 1
    if col[a + 1] == col[a] or col[b - 1] == col[b]: return np.nan
    return (b - (h - col[b]) / (col[b - 1] - col[b])) - (a + (h - col[a]) / (col[a + 1] - col[a]))
def line_sigma(L):
    """the lines' width (sigma, px): horizontal and vertical lines' profiles at mid brightness"""
    ws = []
    for A in (L, L.T):
        prof = np.percentile(A, 90, axis=1)
        for r in np.nonzero((prof > np.roll(prof, 1)) & (prof >= np.roll(prof, -1)) & (prof > prof.max() * 0.1))[0]:
            if r < 25 or r > A.shape[0] - 26: continue
            seg = A[r - 25:r + 26]; top = np.percentile(seg.max(0), 95)
            for c in range(0, A.shape[1], 2):
                col = seg[:, c]
                if 0.3 * top < col.max() < 0.8 and 18 < col.argmax() < 32: ws.append(fwhm(col))
    ws = np.array([w for w in ws if np.isfinite(w) and 1 < w < 40])
    return (np.median(ws) / 2.355, len(ws)) if len(ws) > 50 else (None, len(ws))
def render(rom, presses):
    h = Host(r'G:\AI\Vectrex_EMU\libretro\vectrex_emu_libretro.dll', 'G:/AI/Vectrex_EMU/libretro/dist/pkg/system', tempfile.mkdtemp(),
             options={'vectrex_emu_deck': 'off', 'vectrex_emu_rotate': 'off', 'vectrex_emu_skip_intro': 'on', 'vectrex_emu_wobble': 'off',
                      'vectrex_emu_bending': 'off', 'vectrex_emu_overlay': 'off', 'vectrex_emu_look': 'vectrex', 'vectrex_emu_case': 'off',
                      'vectrex_emu_glass_edge': 'off', 'vectrex_emu_room': 'off (dark room)'}, size=(1200, 1500))
    h.load(os.path.join(T, rom))
    for i in range(60): h.run()
    for b, n in presses:
        for k in range(n):
            for i in range(3): h.run(**{b: 1})
            for i in range(10 if n > 1 else 3): h.run()
    for i in range(150): h.run()
    g = h.grab().astype(np.float32)[..., :3]; h.close(); return g
def bbox(a, thr):
    m = nd.gaussian_filter(a.max(2), 3) > thr; ys, xs = np.nonzero(m); H, W = m.shape
    my, mx = int((ys.max() - ys.min()) * 0.04) + 8, int((xs.max() - xs.min()) * 0.04) + 8
    return max(0, ys.min() - my), min(H, ys.max() + my), max(0, xs.min() - mx), min(W, xs.max() + mx)
f = ImageFont.truetype('segoeui.ttf', 20)
for nm in names:
    dng, rom, presses, title = JOBS[nm]
    lin = rawpy.imread(dng).postprocess(gamma=(1, 1), no_auto_bright=True, output_bps=16, use_camera_wb=True, half_size=True).astype(np.float64) / 65535
    L = lin.max(2); y0, y1, x0, x1 = bbox(lin.astype(np.float32), np.percentile(L, 99.5) * 0.15); c = lin[y0:y1, x0:x1]
    p = np.percentile(c.max(2)[c.max(2) > np.percentile(c.max(2), 98)], 95); c = c / p
    sp, n = line_sigma(c.mean(2)); sc = sp * np.sqrt(1 - (1.25 / 1.85) ** 2) if sp else None
    print(nm, 'line sigma', None if sp is None else round(sp, 2), 'from', n, 'camera sigma', None if sc is None else round(sc, 2), flush=True)
    if sc:
        r = int(4 * sc) | 1; yy, xx = np.mgrid[-r:r + 1, -r:r + 1]; psf = np.exp(-(xx ** 2 + yy ** 2) / (2 * sc * sc)); psf /= psf.sum()
        c = np.stack([richardson_lucy(c[..., k], psf, num_iter=8, clip=False) for k in range(3)], 2)
    ph = np.clip(c * 0.9, 0, 1) ** (1 / 2.2) * 255
    e = render(rom, presses); ey0, ey1, ex0, ex1 = bbox(e, 25); e = e[ey0:ey1, ex0:ex1]
    es = np.asarray(Image.fromarray(e.astype(np.uint8)).convert('L').resize((80, 100)), np.float32); best = None
    for k in range(4):
        q = np.rot90(ph, k); g = np.asarray(Image.fromarray(q.astype(np.uint8)).convert('L').resize((80, 100)), np.float32)
        s = np.corrcoef(g.ravel(), es.ravel())[0, 1]
        if best is None or s > best[0]: best = (s, k)
    ph = np.ascontiguousarray(np.rot90(ph, best[1]))
    Hh = 640
    fit = lambda a: np.asarray(Image.fromarray(np.clip(a, 0, 255).astype(np.uint8)).resize((max(1, round(a.shape[1] * Hh / a.shape[0])), Hh), Image.LANCZOS)).astype(np.float32)
    ph, e = fit(ph), fit(e)
    light = lambda a: ((a / 255.0) ** 2.2)[a.max(2) > 12].sum()
    ph = np.clip(ph * (light(e) / light(ph)) ** (1 / 2.2), 0, 255)
    W = ph.shape[1] + e.shape[1] + 30; im = Image.new('RGB', (W, Hh + 40), (6, 6, 6))
    im.paste(Image.fromarray(ph.astype(np.uint8)), (10, 34)); im.paste(Image.fromarray(e.astype(np.uint8)), (20 + ph.shape[1], 34))
    d = ImageDraw.Draw(im); d.text((10, 6), 'Real Vectrex', fill=(235, 160, 60), font=f); d.text((20 + ph.shape[1], 6), 'Vectrex_EMU', fill=(235, 160, 60), font=f)
    im.save(os.path.join(out, 'raw_%s.jpg' % nm), quality=90); print(nm, 'rot', best[1], 'corr %.2f' % best[0], im.size, flush=True)
